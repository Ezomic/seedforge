#include "ichor/world/phase_pipeline.h"
#include "ichor/world/validation.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

using namespace ichor;

namespace {

template <class V>
concept Appendable = requires(V v) { v.push_back(1); };

struct Counts {
    int a = 0;
    int b = 0;
    int c = 0;
};

std::uint64_t digestOf(const std::vector<int>& v)
{
    std::uint64_t h = 1469598103934665603ull;
    for (int x : v) {
        h = (h ^ static_cast<std::uint64_t>(x)) * 1099511628211ull;
    }
    return h;
}

// a makes {seedA}, b doubles a and adds seedB, c sums b. Counts say which phases actually ran.
PhasePipeline numbers(int seedA, int seedB, Counts& counts)
{
    return PhasePipeline({
        makePhase<std::vector<int>>(
            "a", [seedA, &counts](const PhaseView&) { ++counts.a; return std::vector<int>{seedA, 7}; }, digestOf),
        makePhase<std::vector<int>>(
            "b",
            [seedB, &counts](const PhaseView& earlier) {
                ++counts.b;
                std::vector<int> out = earlier.get<std::vector<int>>("a");
                for (int& x : out) {
                    x = x * 2 + seedB;
                }
                return out;
            },
            digestOf),
        makePhase<std::vector<int>>(
            "c",
            [&counts](const PhaseView& earlier) {
                ++counts.c;
                int sum = 0;
                for (int x : earlier.get<std::vector<int>>("b")) {
                    sum += x;
                }
                return std::vector<int>{sum};
            },
            digestOf),
    });
}

ForgeRequest smallRequest(std::uint64_t seed)
{
    ForgeRequest request;
    request.desc.seed = seed;
    request.desc.sizeMetres = 1024;
    request.desc.metresPerSample = 16;
    request.desc.cellMetres = 256;
    request.desc.waterHeight = 20.0f;
    request.carveRivers = true;
    request.rivers.riverThreshold = 40;
    return request;
}

}

TEST_CASE("an artifact can only be read, never changed, through the API", "[phase]")
{
    using Read = decltype(std::declval<const PhaseArtifact&>().get<std::vector<int>>());
    using ViewRead = decltype(std::declval<const PhaseView&>().get<std::vector<int>>(std::string()));
    using RunRead = decltype(std::declval<const PhaseRun&>().get<std::vector<int>>(std::string()));
    static_assert(std::is_const_v<std::remove_reference_t<Read>>);
    static_assert(std::is_const_v<std::remove_reference_t<ViewRead>>);
    static_assert(std::is_const_v<std::remove_reference_t<RunRead>>);
    static_assert(!std::is_assignable_v<Read, std::vector<int>>);
    static_assert(!Appendable<Read>);

    Counts counts;
    const PhaseRun run = numbers(1, 3, counts).run();
    const std::vector<int>& a = run.get<std::vector<int>>("a");
    const std::uint64_t digestBefore = run.artifact("a").digest();

    SECTION("a re-run from a later phase leaves the earlier artifact in place")
    {
        const PhaseRun again = numbers(1, 3, counts).runFrom(run, 1);
        CHECK(&again.get<std::vector<int>>("a") == &a);
        CHECK(again.artifact("a").digest() == digestBefore);
    }

    SECTION("copies of an artifact are the same value, not a way to a second one")
    {
        const PhaseRun copy = run;
        CHECK(&copy.get<std::vector<int>>("a") == &a);
    }

    SECTION("asking for the wrong type or a missing phase throws instead of guessing")
    {
        CHECK_THROWS_AS(run.get<std::vector<float>>("a"), std::logic_error);
        CHECK_THROWS_AS(run.artifact("nope"), std::out_of_range);
    }
}

TEST_CASE("a phase sees only the phases before it", "[phase]")
{
    PhasePipeline pipeline({
        makePhase<int>("first", [](const PhaseView&) { return 1; }, [](const int& v) { return std::uint64_t(v); }),
        makePhase<int>(
            "second", [](const PhaseView& earlier) { return earlier.get<int>("third"); }, [](const int& v) { return std::uint64_t(v); }),
        makePhase<int>("third", [](const PhaseView&) { return 3; }, [](const int& v) { return std::uint64_t(v); }),
    });
    CHECK_THROWS_AS(pipeline.run(), std::out_of_range);
}

TEST_CASE("a pipeline refuses unnamed, repeated and mislabelled phases", "[phase]")
{
    const auto one = [](const char* name) { return makePhase<int>(name, [](const PhaseView&) { return 1; }, [](const int&) { return std::uint64_t(1); }); };
    CHECK_THROWS_AS(PhasePipeline({one("x"), one("x")}), std::invalid_argument);
    CHECK_THROWS_AS(PhasePipeline({one("")}), std::invalid_argument);
    CHECK_THROWS_AS(PhasePipeline({Phase{"x", nullptr}}), std::invalid_argument);

    PhasePipeline liar({Phase{"honest", [](const PhaseView&) { return PhaseArtifact("other", 1, 1); }}});
    CHECK_THROWS_AS(liar.run(), std::logic_error);
}

TEST_CASE("running from phase N reuses the earlier artifacts and matches a full run", "[phase]")
{
    Counts counts;
    const PhasePipeline pipeline = numbers(1, 3, counts);
    const PhaseRun full = pipeline.run();
    counts = {};

    const PhaseRun again = pipeline.runFrom(full, 1);
    CHECK(counts.a == 0);
    CHECK(counts.b == 1);
    CHECK(counts.c == 1);
    CHECK(&again.get<std::vector<int>>("a") == &full.get<std::vector<int>>("a"));
    REQUIRE(again.size() == full.size());
    for (std::size_t i = 0; i < full.size(); ++i) {
        CHECK(again.artifacts()[i].digest() == full.artifacts()[i].digest());
        CHECK(again.chainDigest(i) == full.chainDigest(i));
    }
    CHECK_FALSE(firstDivergence(full, again));

    SECTION("a changed later input gives what a full run with it gives")
    {
        Counts other;
        const PhaseRun changed = numbers(1, 9, other).runFrom(full, "b");
        CHECK(other.a == 0);
        CHECK(changed.chainDigest() == numbers(1, 9, other).run().chainDigest());
        CHECK(changed.chainDigest() != full.chainDigest());
    }

    SECTION("running from the start reuses nothing")
    {
        counts = {};
        pipeline.runFrom(full, 0);
        CHECK(counts.a == 1);
    }

    SECTION("a run that does not match the pipeline is refused")
    {
        const PhaseRun empty;
        CHECK_THROWS_AS(pipeline.runFrom(empty, 1), std::invalid_argument);
        CHECK_THROWS_AS(pipeline.runFrom(full, 3), std::out_of_range);
        CHECK_THROWS_AS(pipeline.runFrom(full, "nope"), std::out_of_range);

        PhasePipeline renamed({makePhase<int>("z", [](const PhaseView&) { return 1; }, [](const int&) { return std::uint64_t(1); }),
            makePhase<int>("y", [](const PhaseView&) { return 1; }, [](const int&) { return std::uint64_t(1); })});
        CHECK_THROWS_AS(renamed.runFrom(full, 1), std::invalid_argument);
    }
}

TEST_CASE("a diff of two runs names the first phase that differs", "[phase]")
{
    Counts counts;
    const PhaseRun base = numbers(1, 3, counts).run();

    SECTION("identical inputs do not differ")
    {
        CHECK_FALSE(firstDivergence(base, numbers(1, 3, counts).run()));
    }

    SECTION("a change in the first phase names the first phase, though every later one changes too")
    {
        const auto diff = firstDivergence(base, numbers(2, 3, counts).run());
        REQUIRE(diff);
        CHECK(diff->phase == "a");
        CHECK(diff->index == 0);
        CHECK(diff->digestA != diff->digestB);
    }

    SECTION("a change in the middle phase names the middle phase")
    {
        const auto diff = firstDivergence(base, numbers(1, 4, counts).run());
        REQUIRE(diff);
        CHECK(diff->phase == "b");
        CHECK(diff->index == 1);
    }

    SECTION("a phase that hides an earlier difference is still blamed on the earlier phase")
    {
        const auto constant = [](int first) {
            return PhasePipeline({makePhase<int>("a", [first](const PhaseView&) { return first; }, [](const int& v) { return std::uint64_t(v); }),
                makePhase<int>("b", [](const PhaseView&) { return 5; }, [](const int& v) { return std::uint64_t(v); })});
        };
        const auto diff = firstDivergence(constant(1).run(), constant(2).run());
        REQUIRE(diff);
        CHECK(diff->phase == "a");
    }

    SECTION("a run that stops early differs where the shorter one stops")
    {
        const PhaseRun shorter = PhasePipeline({numbers(1, 3, counts).phases()[0], numbers(1, 3, counts).phases()[1]}).run();
        const auto diff = firstDivergence(base, shorter);
        REQUIRE(diff);
        CHECK(diff->phase == "c");
        CHECK(diff->digestB == 0);
        CHECK(firstDivergence(shorter, base)->digestA == 0);
    }
}

TEST_CASE("the forge's phases give the world forgeWorld gives", "[phase][forge]")
{
    const ForgeRequest request = smallRequest(11);
    const PhaseRun run = forgePipeline(request).run();
    REQUIRE(run.size() == 3);
    CHECK(run.artifacts()[0].phase() == kPhaseTerrain);
    CHECK(run.artifacts()[1].phase() == kPhaseRivers);
    CHECK(run.artifacts()[2].phase() == kPhaseHydrology);

    const ForgedWorld world = forgeWorld(request);
    CHECK(world.heightfield.digest() == run.get<Heightfield>(kPhaseRivers).digest());
    CHECK(world.heightfield.samples() == run.get<Heightfield>(kPhaseRivers).samples());
    CHECK(world.hydrology.accumulation == run.get<Hydrology>(kPhaseHydrology).accumulation);
    CHECK(run.get<Heightfield>(kPhaseTerrain).samples() != run.get<Heightfield>(kPhaseRivers).samples());
}

TEST_CASE("re-running the forge from hydrology matches a full run and leaves the land alone", "[phase][forge]")
{
    const ForgeRequest request = smallRequest(23);
    const PhasePipeline pipeline = forgePipeline(request);
    const PhaseRun full = pipeline.run();
    const PhaseRun rerun = pipeline.runFrom(full, kPhaseHydrology);

    CHECK(&rerun.get<Heightfield>(kPhaseTerrain) == &full.get<Heightfield>(kPhaseTerrain));
    CHECK(&rerun.get<Heightfield>(kPhaseRivers) == &full.get<Heightfield>(kPhaseRivers));
    CHECK(rerun.chainDigest() == full.chainDigest());
    CHECK_FALSE(firstDivergence(full, rerun));
}

TEST_CASE("the forge diff names the phase a request change first touches", "[phase][forge]")
{
    const PhaseRun base = forgePipeline(smallRequest(5)).run();

    SECTION("another seed changes the terrain")
    {
        const auto diff = firstDivergence(base, forgePipeline(smallRequest(6)).run());
        REQUIRE(diff);
        CHECK(diff->phase == kPhaseTerrain);
    }

    SECTION("not carving leaves the terrain and changes the rivers")
    {
        ForgeRequest dry = smallRequest(5);
        dry.carveRivers = false;
        const auto diff = firstDivergence(base, forgePipeline(dry).run());
        REQUIRE(diff);
        CHECK(diff->phase == kPhaseRivers);
    }

    SECTION("a change that leaves every sample alone but moves the water is still a difference")
    {
        ForgeRequest wetter = smallRequest(5);
        wetter.desc.waterHeight = 40.0f;
        const PhaseRun other = forgePipeline(wetter).run();
        REQUIRE(other.get<Heightfield>(kPhaseTerrain).samples() == base.get<Heightfield>(kPhaseTerrain).samples());
        const auto diff = firstDivergence(base, other);
        REQUIRE(diff);
        CHECK(diff->phase == kPhaseTerrain);
    }

    SECTION("the same request does not differ")
    {
        CHECK_FALSE(firstDivergence(base, forgePipeline(smallRequest(5)).run()));
    }
}
