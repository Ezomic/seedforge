#include <catch2/catch_test_macros.hpp>

#include <forgecli/gallery.h>
#include <forgecli/forge.h>

#include <algorithm>
#include <cstddef>
#include <string>

namespace {

forgecli::GalleryOptions options(std::uint64_t a, std::uint64_t b, int size = 64)
{
    forgecli::GalleryOptions o;
    o.seedBegin = a;
    o.seedEnd = b;
    o.size = size;
    return o;
}

std::size_t differingBytes(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b)
{
    REQUIRE(a.size() == b.size());
    std::size_t n = 0;
    for (std::size_t i = 0; i < a.size(); ++i) n += a[i] != b[i];
    return n;
}

}

TEST_CASE("the gallery is byte-identical across two runs", "[forge_cli][gallery]")
{
    const auto first = forgecli::makeGallery(options(1, 4));
    const auto second = forgecli::makeGallery(options(1, 4));
    REQUIRE(first.error.empty());
    REQUIRE(first.failedSeeds.empty());
    CHECK(first.png == second.png);
    CHECK(first.png.size() > 1000);
    CHECK(first.png[1] == 'P');
}

TEST_CASE("the gallery lays seeds out in a square-ish grid", "[forge_cli][gallery]")
{
    const auto one = forgecli::makeGallery(options(5, 5));
    const auto sixteen = forgecli::makeGallery(options(1, 16));
    const auto seventeen = forgecli::makeGallery(options(1, 17));
    REQUIRE(sixteen.error.empty());
    CHECK(sixteen.width == 2 + 4 * (64 + 2));
    CHECK(sixteen.height == 2 + 4 * (64 + 14 + 8 + 2));
    CHECK(seventeen.width == 2 + 5 * (64 + 2));
    CHECK(seventeen.height == 2 + 4 * (64 + 14 + 8 + 2));
    CHECK(one.width == 2 + 64 + 2);
}

TEST_CASE("the gallery changes when a phase output changes", "[forge_cli][gallery]")
{
    const auto base = forgecli::makeGallery(options(1, 4));

    auto flatter = options(1, 4);
    flatter.config = forgecli::defaultForgeConfig(64);
    flatter.config->erosion.iterations = 0;
    const auto uneroded = forgecli::makeGallery(flatter);

    auto dry = options(1, 4);
    dry.config = forgecli::defaultForgeConfig(64);
    dry.config->hydrology.riverThreshold = worldforge::kHydrologyMaxRiverThreshold;
    const auto noRivers = forgecli::makeGallery(dry);

    REQUIRE(uneroded.error.empty());
    REQUIRE(noRivers.error.empty());
    // Rivers are thin at this size, so dropping them moves tens of pixels; a gallery that ignored the river layer would differ by zero.
    CHECK(differingBytes(base.png, uneroded.png) > 500);
    CHECK(differingBytes(base.png, noRivers.png) > 100);
}

TEST_CASE("different seeds draw different cells", "[forge_cli][gallery]")
{
    const auto a = forgecli::makeGallery(options(1, 1));
    const auto b = forgecli::makeGallery(options(2, 2));
    CHECK(differingBytes(a.png, b.png) > 1000);
}

TEST_CASE("the gallery rejects hostile options", "[forge_cli][gallery]")
{
    CHECK_FALSE(forgecli::makeGallery(options(5, 4)).error.empty());
    CHECK_FALSE(forgecli::makeGallery(options(0, forgecli::kMaxGallerySeeds)).error.empty());
    CHECK_FALSE(forgecli::makeGallery(options(0, ~std::uint64_t{0})).error.empty());
    CHECK_FALSE(forgecli::makeGallery(options(1, 2, 0)).error.empty());
    CHECK_FALSE(forgecli::makeGallery(options(1, 2, -5)).error.empty());
    CHECK_FALSE(forgecli::makeGallery(options(1, 2, 1 << 20)).error.empty());
    CHECK_FALSE(forgecli::makeGallery(options(1, 256, 1024)).error.empty());
    CHECK(forgecli::makeGallery(options(~std::uint64_t{0}, ~std::uint64_t{0})).error.empty());
}

namespace {

worldforge::GateThresholds permissive()
{
    worldforge::GateThresholds t = forgecli::sanityThresholds();
    t.minSettlementsPerKm2Milli = 0;
    t.maxSettlementsPerKm2Milli = 2000000000;
    t.maxRoadComponents = 256;
    t.minSettlementReachPermille = 0;
    t.minPoiReachPermille = 0;
    t.minLandmarkCoveragePermille = 0;
    return t;
}

worldforge::GateResult gate(const char* id, worldforge::GateStatus status)
{
    worldforge::GateResult g;
    g.id = id;
    g.status = status;
    return g;
}

}

TEST_CASE("the sanity row counts evaluated gates and numbers the failed ones", "[forge_cli][gallery][sanity]")
{
    worldforge::ValidationReport report;
    report.seed = 9;
    report.gates = { gate("a", worldforge::GateStatus::Pass), gate("b", worldforge::GateStatus::Fail), gate("c", worldforge::GateStatus::Pass),
        gate("d", worldforge::GateStatus::Fail), gate("e", worldforge::GateStatus::Skipped) };
    const forgecli::SanityRow row = forgecli::sanityRow(report);
    CHECK(row.passed == 2);
    CHECK(row.evaluated == 4);
    CHECK(row.failedGates == std::vector<std::string>{ "b", "d" });
    CHECK(forgecli::sanityCaption(row) == "2/4 2 4");

    report.gates[1].status = worldforge::GateStatus::Pass;
    report.gates[3].status = worldforge::GateStatus::Pass;
    CHECK(forgecli::sanityCaption(forgecli::sanityRow(report)) == "4/4");

    report.error = "gate thresholds are out of range";
    CHECK(forgecli::sanityCaption(forgecli::sanityRow(report)) == "X");
}

TEST_CASE("the gallery caption changes when a check fails", "[forge_cli][gallery][sanity]")
{
    auto good = options(6, 8);
    good.thresholds = permissive();
    auto broken = good;
    broken.thresholds->minLandmarkCoveragePermille = 1000;
    broken.thresholds->minSettlementsPerKm2Milli = 2000000000;

    const auto passing = forgecli::makeGallery(good);
    const auto failing = forgecli::makeGallery(broken);
    REQUIRE(passing.error.empty());
    REQUIRE(failing.error.empty());
    REQUIRE(passing.sanity.size() == 3);
    REQUIRE(failing.sanity.size() == 3);
    for (std::size_t i = 0; i < 3; ++i) {
        CHECK(passing.sanity[i].failedGates.empty());
        CHECK(passing.sanity[i].passed == passing.sanity[i].evaluated);
        CHECK(forgecli::sanityCaption(failing.sanity[i]) != forgecli::sanityCaption(passing.sanity[i]));
        CHECK(failing.sanity[i].failedGates.size() >= 1);
        CHECK(failing.sanity[i].passed < failing.sanity[i].evaluated);
    }
    CHECK(failing.sanity[0].failedGates.front() == "settlement_density");
    CHECK(forgecli::sanitySummary(failing.sanity).find("FAIL settlement_density") != std::string::npos);
    CHECK(forgecli::sanitySummary(passing.sanity).find("FAIL") == std::string::npos);

    REQUIRE(passing.png.size() == failing.png.size());
    // Only the caption pixels move: the worlds are the same, so a gallery that ignored the report would differ by zero.
    const std::size_t moved = differingBytes(passing.png, failing.png);
    CHECK(moved > 50);
    CHECK(moved < passing.png.size() / 20);
}

TEST_CASE("the gallery without sanity has no caption row and no rows", "[forge_cli][gallery][sanity]")
{
    auto plain = options(1, 4);
    plain.sanity = false;
    const auto without = forgecli::makeGallery(plain);
    const auto with = forgecli::makeGallery(options(1, 4));
    REQUIRE(without.error.empty());
    CHECK(without.sanity.empty());
    CHECK(with.sanity.size() == 4);
    CHECK(with.height == without.height + 2 * 8);
}

TEST_CASE("invalid thresholds draw an X caption instead of crashing", "[forge_cli][gallery][sanity]")
{
    auto bad = options(1, 1);
    bad.thresholds = forgecli::sanityThresholds();
    bad.thresholds->maxRoadComponents = 0;
    const auto result = forgecli::makeGallery(bad);
    REQUIRE(result.error.empty());
    REQUIRE(result.sanity.size() == 1);
    CHECK(forgecli::sanityCaption(result.sanity[0]) == "X");
    CHECK(forgecli::sanitySummary(result.sanity).find("invalid thresholds") != std::string::npos);
}
