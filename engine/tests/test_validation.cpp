#include "ichor/core/hash.h"
#include "ichor/world/forge.h"
#include "ichor/world/validation.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <functional>
#include <limits>
#include <stdexcept>

using namespace ichor;

namespace {

ForgeRequest smallRequest(std::uint64_t seed, bool rivers = true)
{
    ForgeRequest request;
    request.desc.seed = seed;
    request.desc.sizeMetres = 1024;
    request.desc.metresPerSample = 16;
    request.desc.cellMetres = 256;
    request.desc.waterHeight = 20.0f;
    request.carveRivers = rivers;
    request.rivers.riverThreshold = 40;
    return request;
}

Gate failsUnless(const char* name, std::function<bool(std::uint64_t)> passesForSeed)
{
    return {name, [passesForSeed](const ForgedWorld& world, const ForgeRequest&) -> std::optional<std::string> {
                if (passesForSeed(world.heightfield.desc().seed)) {
                    return std::nullopt;
                }
                return std::string("not this seed");
            }};
}

const Gate kAlwaysFails{"always", [](const ForgedWorld&, const ForgeRequest&) -> std::optional<std::string> { return std::string("never"); }};

}

TEST_CASE("the first attempt uses the seed as given and later ones a derived seed", "[validation]")
{
    CHECK(attemptSeed(77, 0) == 77);
    CHECK(attemptSeed(77, 1) != 77);
    CHECK(attemptSeed(77, 1) != attemptSeed(77, 2));
    CHECK(attemptSeed(77, 3) == hashCombine(77, 3));
}

TEST_CASE("a world that passes is exactly the world the forge makes without gates", "[validation]")
{
    for (std::uint64_t seed = 1; seed <= 6; ++seed) {
        const ForgeRequest request = smallRequest(seed);
        const ValidatedWorld validated = forgeValidated(request, defaultGates());
        INFO("seed " << seed);
        REQUIRE(validated.passed);
        CHECK(validated.attempt == 0);
        CHECK(validated.rejected.empty());
        const Heightfield raw = forgeHeightfield(request.desc);
        const Heightfield oldPipeline = carveRivers(raw, computeHydrology(raw), request.rivers, CarveParams{});
        CHECK(validated.world.heightfield.digest() == oldPipeline.digest());
    }
}

TEST_CASE("without rivers the forged land is untouched", "[validation]")
{
    const ForgeRequest request = smallRequest(4, false);
    const ValidatedWorld validated = forgeValidated(request, defaultGates());
    REQUIRE(validated.passed);
    CHECK(validated.world.heightfield.digest() == forgeHeightfield(request.desc).digest());
}

TEST_CASE("a gate that always fails ends in the error flag with the first world", "[validation]")
{
    const ForgeRequest request = smallRequest(5);
    const ValidatedWorld validated = forgeValidated(request, {kAlwaysFails}, 3);
    CHECK_FALSE(validated.passed);
    CHECK(validated.attempt == 0);
    REQUIRE(validated.rejected.size() == 4);
    for (std::uint32_t i = 0; i < 4; ++i) {
        CHECK(validated.rejected[i].attempt == i);
        CHECK(validated.rejected[i].seed == attemptSeed(5, i));
    }
    CHECK(validated.world.heightfield.digest() == forgeWorld(request).heightfield.digest());
}

TEST_CASE("the number of rerolls is bounded, even when asked for the most", "[validation]")
{
    ForgeRequest request = smallRequest(5, false);
    request.desc.sizeMetres = 128;
    request.desc.metresPerSample = 16;
    request.desc.cellMetres = 64;
    const ValidatedWorld validated = forgeValidated(request, {kAlwaysFails}, std::numeric_limits<std::uint32_t>::max());
    CHECK_FALSE(validated.passed);
    CHECK(validated.rejected.size() == 65);
}

TEST_CASE("no rerolls allowed means one attempt", "[validation]")
{
    const ValidatedWorld validated = forgeValidated(smallRequest(5), {kAlwaysFails}, 0);
    CHECK_FALSE(validated.passed);
    CHECK(validated.rejected.size() == 1);
}

TEST_CASE("a gate that throws is a failure with the message", "[validation]")
{
    const Gate throws{"throws", [](const ForgedWorld&, const ForgeRequest&) -> std::optional<std::string> { throw std::runtime_error("boom"); }};
    const ValidatedWorld validated = forgeValidated(smallRequest(5), {throws}, 1);
    CHECK_FALSE(validated.passed);
    REQUIRE(validated.rejected.size() == 2);
    REQUIRE(validated.rejected[0].failures.size() == 1);
    CHECK(validated.rejected[0].failures[0].gate == "throws");
    CHECK(validated.rejected[0].failures[0].reason.find("boom") != std::string::npos);
}

TEST_CASE("every failing gate is listed, in gate order", "[validation]")
{
    const Gate passes{"passes", [](const ForgedWorld&, const ForgeRequest&) -> std::optional<std::string> { return std::nullopt; }};
    const Gate second{"second", [](const ForgedWorld&, const ForgeRequest&) -> std::optional<std::string> { return std::string("two"); }};
    const ForgeRequest request = smallRequest(5);
    const auto failures = runGates({kAlwaysFails, passes, second}, forgeWorld(request), request);
    REQUIRE(failures.size() == 2);
    CHECK(failures[0].gate == "always");
    CHECK(failures[1].gate == "second");
    CHECK(failures[1].reason == "two");
}

TEST_CASE("a world that passes only on a later attempt is the same attempt every run", "[validation]")
{
    const std::uint64_t seed = 9;
    const std::uint64_t wanted = attemptSeed(seed, 3);
    const Gate onlyThird = failsUnless("third", [wanted](std::uint64_t s) { return s == wanted; });
    const ForgeRequest request = smallRequest(seed);

    const ValidatedWorld first = forgeValidated(request, {onlyThird});
    const ValidatedWorld second = forgeValidated(request, {onlyThird});
    REQUIRE(first.passed);
    CHECK(first.attempt == 3);
    CHECK(second.attempt == 3);
    CHECK(first.rejected.size() == 3);
    CHECK(first.world.heightfield.digest() == second.world.heightfield.digest());

    ForgeRequest derived = request;
    derived.desc.seed = wanted;
    CHECK(first.world.heightfield.digest() == forgeWorld(derived).heightfield.digest());
    CHECK(first.seed == seed);
    CHECK(first.world.heightfield.desc().seed == seed);
}

TEST_CASE("the largest seed rerolls without trouble", "[validation]")
{
    const std::uint64_t seed = std::numeric_limits<std::uint64_t>::max();
    const std::uint64_t wanted = attemptSeed(seed, 1);
    const Gate onlySecond = failsUnless("second", [wanted](std::uint64_t s) { return s == wanted; });
    const ValidatedWorld validated = forgeValidated(smallRequest(seed), {onlySecond});
    REQUIRE(validated.passed);
    CHECK(validated.attempt == 1);
    CHECK(validated.seed == seed);
}

TEST_CASE("the default gates reject a flat world and a world drowned in water", "[validation]")
{
    ForgeRequest drowned = smallRequest(3, false);
    drowned.desc.waterHeight = 400.0f;
    ForgedWorld world = forgeWorld(drowned);
    auto failures = runGates(defaultGates(), world, drowned);
    REQUIRE_FALSE(failures.empty());
    CHECK(failures[0].gate == "land-above-water");

    ForgeRequest flat = smallRequest(3, false);
    flat.desc.heightRange = 0.001f;
    ForgedWorld flatWorld;
    flatWorld.heightfield = Heightfield(flat.desc, std::vector<std::uint16_t>(static_cast<std::size_t>(flat.desc.samplesPerSide()) * flat.desc.samplesPerSide(), 1000));
    flatWorld.hydrology = computeHydrology(flatWorld.heightfield);
    bool flagged = false;
    for (const GateFailure& failure : runGates(defaultGates(), flatWorld, flat)) {
        flagged = flagged || failure.gate == "not-flat";
    }
    CHECK(flagged);
}

TEST_CASE("a requested river must exist", "[validation]")
{
    ForgeRequest request = smallRequest(3);
    request.rivers.riverThreshold = std::numeric_limits<std::uint32_t>::max();
    const ForgedWorld world = forgeWorld(request);
    bool flagged = false;
    for (const GateFailure& failure : runGates(defaultGates(), world, request)) {
        flagged = flagged || failure.gate == "has-river";
    }
    CHECK(flagged);
}

TEST_CASE("hydrology that does not agree with itself is flagged", "[validation]")
{
    const ForgeRequest request = smallRequest(3);
    const Gate agrees = defaultGates().back();
    REQUIRE(agrees.name == "hydrology-agrees");
    ForgedWorld world = forgeWorld(request);
    CHECK_FALSE(agrees.check(world, request).has_value());

    ForgedWorld wrongSize = world;
    wrongSize.hydrology.accumulation.pop_back();
    CHECK(agrees.check(wrongSize, request).has_value());

    ForgedWorld badLake = world;
    badLake.hydrology.lake[0] = static_cast<std::uint32_t>(badLake.hydrology.lakeDeepest.size()) + 1;
    CHECK(agrees.check(badLake, request).has_value());
}
