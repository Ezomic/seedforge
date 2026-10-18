#include <worldforge/forge_world.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

using namespace worldforge;

namespace {

constexpr int kSmall = 64;

ForgeConfig smallConfig()
{
    ForgeConfig c;
    c.settlements.minSpacing = 8;
    c.settlements.maxSettlements = 12;
    c.settlements.minScore = 24;
    c.pois.minSpacing = 8;
    return c;
}

ForgeConfig richConfig()
{
    ForgeConfig c = smallConfig();
    c.itemIds = { "sword", "shield", "ring", "lantern" };
    c.npcArchetypes = { "smith", "healer" };
    c.dungeonArchetypes = { "crypt", "mine" };
    FactionSpec a;
    a.strategy = ClaimStrategy::Contiguous;
    FactionSpec b;
    b.strategy = ClaimStrategy::ResourceFollowing;
    b.oreWeight = 200;
    c.factions = { a, b };
    return c;
}

void requireComplete(const ForgedWorld& w)
{
    INFO(w.error());
    REQUIRE(w.complete());
}

std::vector<ForgePhaseHash> completedHashes(const ForgedWorld& w)
{
    std::vector<ForgePhaseHash> out;
    for (int i = 0; i < w.completedPhases(); ++i) out.push_back({ static_cast<ForgePhase>(i + 1), w.phaseHashes()[static_cast<std::size_t>(i)] });
    return out;
}

}

TEST_CASE("forgeWorld runs every phase and fills every field", "[worldforge][forge]")
{
    const ForgedWorld w = forgeWorld(7, kSmall, richConfig());
    REQUIRE(w.ok());
    CHECK(w.complete());
    CHECK(w.completedPhases() == kForgePhaseCount);
    CHECK(w.tectonics().has_value());
    CHECK(w.eroded().has_value());
    CHECK(w.hydrology().has_value());
    CHECK(w.climate().has_value());
    CHECK(w.biomes().has_value());
    CHECK(w.resources().has_value());
    CHECK(w.settlements().has_value());
    CHECK(w.roads().has_value());
    CHECK(w.politics().has_value());
    CHECK(w.pois().has_value());
    CHECK(w.landmarkCoverage().has_value());
    REQUIRE(w.manifest().has_value());
    CHECK(w.manifest()->genVersion == kGenVersion);
    CHECK(w.manifest()->worldSeed == 7);
    CHECK(w.worldHash() != 0);
    CHECK(w.genVersion() == kGenVersion);
    CHECK(w.politics()->factionCount == 2);
}

TEST_CASE("forgeWorld is repeatable and seed sensitive", "[worldforge][forge]")
{
    const ForgeConfig cfg = richConfig();
    const ForgedWorld a = forgeWorld(11, kSmall, cfg);
    const ForgedWorld b = forgeWorld(11, kSmall, cfg);
    const ForgedWorld other = forgeWorld(12, kSmall, cfg);
    REQUIRE(a.ok());
    REQUIRE(other.ok());
    CHECK(a.worldHash() == b.worldHash());
    CHECK(a.phaseHashes() == b.phaseHashes());
    CHECK(a.chain() == b.chain());
    CHECK(a.worldHash() != other.worldHash());
    CHECK(a.worldHash() != forgeWorld(11, kSmall + 1, cfg).worldHash());
}

TEST_CASE("stop-after N equals the first N phases of a full run", "[worldforge][forge]")
{
    const ForgeConfig cfg = richConfig();
    const ForgedWorld full = forgeWorld(21, kSmall, cfg);
    requireComplete(full);
    for (int n = 1; n <= kForgePhaseCount; ++n) {
        INFO("stopAfter " << n);
        const ForgedWorld part = forgeWorld(21, kSmall, cfg, static_cast<ForgePhase>(n));
        REQUIRE(part.ok());
        CHECK(part.complete());
        CHECK(part.completedPhases() == n);
        CHECK(part.worldHash() == full.chain()[static_cast<std::size_t>(n - 1)]);
        for (int i = 0; i < kForgePhaseCount; ++i) {
            const auto idx = static_cast<std::size_t>(i);
            CHECK(part.phaseHashes()[idx] == (i < n ? full.phaseHashes()[idx] : 0));
            CHECK(part.chain()[idx] == (i < n ? full.chain()[idx] : 0));
        }
        CHECK(part.tectonics().has_value() == (n >= 1));
        CHECK(part.eroded().has_value() == (n >= 2));
        CHECK(part.hydrology().has_value() == (n >= 3));
        CHECK(part.climate().has_value() == (n >= 4));
        CHECK(part.biomes().has_value() == (n >= 5));
        CHECK(part.resources().has_value() == (n >= 6));
        CHECK(part.settlements().has_value() == (n >= 7));
        CHECK(part.roads().has_value() == (n >= 8));
        CHECK(part.politics().has_value() == (n >= 9));
        CHECK(part.pois().has_value() == (n >= 10));
        CHECK(part.landmarkCoverage().has_value() == (n >= 10));
        CHECK(part.manifest().has_value() == (n >= 11));
    }
    const ForgedWorld none = forgeWorld(21, kSmall, cfg, static_cast<ForgePhase>(0));
    CHECK(none.ok());
    CHECK(none.completedPhases() == 0);
    CHECK_FALSE(none.tectonics().has_value());
    CHECK(forgeWorld(21, kSmall, cfg, static_cast<ForgePhase>(200)).worldHash() == full.worldHash());
}

TEST_CASE("a failing phase stops the run and keeps earlier artifacts", "[worldforge][forge]")
{
    SECTION("hydrology rejects its params")
    {
        ForgeConfig cfg = smallConfig();
        cfg.hydrology.riverThreshold = 0;
        std::vector<ForgeProgress> events;
        const ForgedWorld w = forgeWorld(3, kSmall, cfg, ForgePhase::Content, [&](const ForgeProgress& p) { events.push_back(p); });
        CHECK_FALSE(w.ok());
        CHECK_FALSE(w.complete());
        REQUIRE(w.failedPhase().has_value());
        CHECK(*w.failedPhase() == ForgePhase::Hydrology);
        CHECK(w.error().rfind("hydrology", 0) == 0);
        CHECK(w.completedPhases() == 2);
        CHECK(w.tectonics().has_value());
        CHECK(w.eroded().has_value());
        CHECK_FALSE(w.hydrology().has_value());
        CHECK_FALSE(w.climate().has_value());
        CHECK_FALSE(w.manifest().has_value());
        CHECK(w.worldHash() == w.chain()[1]);
        REQUIRE_FALSE(events.empty());
        CHECK(events.back().phase == ForgePhase::Hydrology);
        CHECK(events.back().finished);
        CHECK_FALSE(events.back().ok);
        CHECK(std::none_of(events.begin(), events.end(), [](const ForgeProgress& p) { return p.phase == ForgePhase::Climate; }));
    }
    SECTION("roads fail when there is nothing to connect")
    {
        ForgeConfig cfg = smallConfig();
        cfg.settlements.maxSettlements = 0;
        const ForgedWorld w = forgeWorld(3, kSmall, cfg);
        REQUIRE(w.failedPhase().has_value());
        CHECK(*w.failedPhase() == ForgePhase::Roads);
        CHECK(w.completedPhases() == 7);
        REQUIRE(w.settlements().has_value());
        CHECK(w.settlements()->sites.empty());
        CHECK_FALSE(w.roads().has_value());
        CHECK_FALSE(w.pois().has_value());
    }
    SECTION("the failed run's completed prefix matches a clean stop")
    {
        ForgeConfig cfg = smallConfig();
        cfg.settlements.maxSettlements = 0;
        const ForgedWorld failed = forgeWorld(3, kSmall, cfg);
        const ForgedWorld clean = forgeWorld(3, kSmall, cfg, ForgePhase::Settlements);
        CHECK(failed.worldHash() == clean.worldHash());
    }
    SECTION("content rejects duplicate item ids after the POIs were kept")
    {
        ForgeConfig cfg = richConfig();
        cfg.itemIds = { "sword", "sword" };
        const ForgedWorld w = forgeWorld(3, kSmall, cfg);
        REQUIRE(w.failedPhase().has_value());
        CHECK(*w.failedPhase() == ForgePhase::Content);
        CHECK(w.pois().has_value());
        CHECK(w.landmarkCoverage().has_value());
        CHECK_FALSE(w.manifest().has_value());
    }
}

TEST_CASE("the chained hash goes red when a phase is dropped", "[worldforge][forge]")
{
    const ForgedWorld w = forgeWorld(5, kSmall, richConfig());
    requireComplete(w);
    const std::vector<ForgePhaseHash> all = completedHashes(w);
    REQUIRE(all.size() == static_cast<std::size_t>(kForgePhaseCount));
    CHECK(chainForgeHash(5, kSmall, all) == w.worldHash());

    for (std::size_t drop = 0; drop < all.size(); ++drop) {
        INFO("dropped phase " << drop + 1);
        std::vector<ForgePhaseHash> fewer = all;
        fewer.erase(fewer.begin() + static_cast<std::ptrdiff_t>(drop));
        CHECK(chainForgeHash(5, kSmall, fewer) != w.worldHash());
    }
    std::vector<ForgePhaseHash> swapped = all;
    std::swap(swapped[3], swapped[4]);
    CHECK(chainForgeHash(5, kSmall, swapped) != w.worldHash());
    std::vector<ForgePhaseHash> changed = all;
    changed[0].hash ^= 1;
    CHECK(chainForgeHash(5, kSmall, changed) != w.worldHash());
}

TEST_CASE("a late-phase param changes the world hash but not the chain before it", "[worldforge][forge]")
{
    const ForgedWorld base = forgeWorld(5, kSmall, richConfig());
    ForgeConfig tweaked = richConfig();
    tweaked.binding.tensionCount = 1;
    tweaked.binding.castPerArchetype = 5;
    const ForgedWorld other = forgeWorld(5, kSmall, tweaked);
    requireComplete(base);
    requireComplete(other);
    for (std::size_t i = 0; i + 1 < static_cast<std::size_t>(kForgePhaseCount); ++i) CHECK(base.chain()[i] == other.chain()[i]);
    CHECK(base.worldHash() != other.worldHash());
}

TEST_CASE("each optional input is really wired through", "[worldforge][forge]")
{
    constexpr int size = 96;
    const std::uint64_t seed = 9;
    const ForgedWorld base = forgeWorld(seed, size, smallConfig());
    requireComplete(base);

    SECTION("climate moisture reaches resources")
    {
        ForgeConfig cfg = smallConfig();
        cfg.moistureScale = 1024;
        const ForgedWorld w = forgeWorld(seed, size, cfg, ForgePhase::Resources);
        REQUIRE(w.ok());
        CHECK(w.phaseHashes()[5] != base.phaseHashes()[5]);
        CHECK(w.phaseHashes()[3] == base.phaseHashes()[3]);
    }
    SECTION("biome suitability reaches settlements")
    {
        ForgeConfig cfg = smallConfig();
        cfg.biomeSuitability.fill(0);
        const ForgedWorld w = forgeWorld(seed, size, cfg, ForgePhase::Settlements);
        REQUIRE(w.ok());
        CHECK(w.phaseHashes()[6] != base.phaseHashes()[6]);
        CHECK(w.phaseHashes()[5] == base.phaseHashes()[5]);
    }
    SECTION("biome cost reaches roads")
    {
        ForgeConfig cfg = smallConfig();
        cfg.biomeRoadCost.fill(0);
        cfg.biomeRoadCost[static_cast<std::size_t>(BiomeId::Ocean)] = kRoadsImpassable;
        cfg.biomeRoadCost[static_cast<std::size_t>(BiomeId::Lake)] = kRoadsImpassable;
        cfg.roads.biomeWeight = 256;
        ForgeConfig heavy = cfg;
        heavy.biomeRoadCost.fill(200);
        heavy.biomeRoadCost[static_cast<std::size_t>(BiomeId::Ocean)] = kRoadsImpassable;
        heavy.biomeRoadCost[static_cast<std::size_t>(BiomeId::Lake)] = kRoadsImpassable;
        heavy.biomeRoadCost[static_cast<std::size_t>(BiomeId::Grassland)] = 0;
        heavy.biomeRoadCost[static_cast<std::size_t>(BiomeId::LowlandFarm)] = 0;
        const ForgedWorld free = forgeWorld(seed, size, cfg, ForgePhase::Roads);
        const ForgedWorld costly = forgeWorld(seed, size, heavy, ForgePhase::Roads);
        REQUIRE(free.ok());
        REQUIRE(costly.ok());
        CHECK(free.phaseHashes()[7] != costly.phaseHashes()[7]);
    }
    SECTION("landmark height reaches the coverage report")
    {
        ForgeConfig cfg = smallConfig();
        cfg.landmarkHeightM = 1;
        cfg.landmarks.nearRangeCells = 0;
        ForgeConfig tall = cfg;
        tall.landmarkHeightM = 400;
        const ForgedWorld low = forgeWorld(seed, size, cfg, ForgePhase::Pois);
        const ForgedWorld high = forgeWorld(seed, size, tall, ForgePhase::Pois);
        REQUIRE(low.ok());
        REQUIRE(high.ok());
        REQUIRE(low.landmarkCoverage().has_value());
        CHECK(low.landmarkCoverage()->coveragePermille <= high.landmarkCoverage()->coveragePermille);
        CHECK(low.phaseHashes()[9] != high.phaseHashes()[9]);
    }
    SECTION("roads own the deformed terrain")
    {
        REQUIRE(base.roads().has_value());
        REQUIRE(base.hydrology().has_value());
        CHECK(base.roads()->heightM != base.hydrology()->heightM);
    }
}

TEST_CASE("the manifest carries genVersion and old manifests stay readable", "[worldforge][forge]")
{
    const ForgedWorld w = forgeWorld(13, kSmall, richConfig());
    requireComplete(w);
    const std::vector<std::uint8_t> bytes = encodeManifest(*w.manifest());
    const auto decoded = decodeManifest(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->genVersion == kGenVersion);
    CHECK(decoded->version == kManifestVersionGen);
    CHECK(*decoded == *w.manifest());

    ContentManifest legacy = *w.manifest();
    legacy.version = kManifestVersion;
    legacy.genVersion = 0;
    const auto old = decodeManifest(encodeManifest(legacy));
    REQUIRE(old.has_value());
    CHECK(old->genVersion == 0);
    CHECK(old->version == kManifestVersion);

    std::vector<std::uint8_t> truncated(bytes.begin(), bytes.begin() + 14);
    CHECK_FALSE(decodeManifest(truncated).has_value());
    std::vector<std::uint8_t> future = bytes;
    future[4] = 3;
    CHECK_FALSE(decodeManifest(future).has_value());
}

TEST_CASE("progress reports every phase once, in order", "[worldforge][forge]")
{
    std::vector<ForgeProgress> events;
    const ForgedWorld w = forgeWorld(2, kSmall, richConfig(), ForgePhase::Content, [&](const ForgeProgress& p) { events.push_back(p); });
    requireComplete(w);
    REQUIRE(events.size() == static_cast<std::size_t>(kForgePhaseCount) * 2);
    for (int i = 0; i < kForgePhaseCount; ++i) {
        const ForgeProgress& begin = events[static_cast<std::size_t>(i) * 2];
        const ForgeProgress& end = events[static_cast<std::size_t>(i) * 2 + 1];
        CHECK(static_cast<int>(begin.phase) == i + 1);
        CHECK_FALSE(begin.finished);
        CHECK(end.phase == begin.phase);
        CHECK(end.finished);
        CHECK(end.ok);
        CHECK(end.phaseHash == w.phaseHashes()[static_cast<std::size_t>(i)]);
    }
}

TEST_CASE("forgeWorld survives adversarial sizes and configs", "[worldforge][forge]")
{
    SECTION("sizes outside the tectonics limits fail at tectonics")
    {
        for (const int size : { std::numeric_limits<int>::min(), -1, 0, 1, kTectonicsMinSize - 1, kTectonicsMaxSize + 1, 100000, std::numeric_limits<int>::max() }) {
            INFO("size " << size);
            const ForgedWorld w = forgeWorld(1, size);
            REQUIRE(w.failedPhase().has_value());
            CHECK(*w.failedPhase() == ForgePhase::Tectonics);
            CHECK(w.completedPhases() == 0);
            CHECK_FALSE(w.tectonics().has_value());
        }
    }
    SECTION("the smallest size runs end to end or stops cleanly")
    {
        const ForgedWorld w = forgeWorld(1, kTectonicsMinSize, richConfig());
        CHECK(w.complete() != w.failedPhase().has_value());
    }
    SECTION("a size over the erosion budget is refused at erosion and keeps tectonics")
    {
        ForgeConfig cfg = smallConfig();
        cfg.erosionBudgetCellPasses = 1000;
        const ForgedWorld w = forgeWorld(1, kSmall, cfg);
        REQUIRE(w.failedPhase().has_value());
        CHECK(*w.failedPhase() == ForgePhase::Erosion);
        CHECK(w.tectonics().has_value());
        CHECK_FALSE(w.eroded().has_value());
        CHECK(w.error().find("budget") != std::string::npos);
    }
    SECTION("budget at the exact cost passes, one below fails")
    {
        ForgeConfig cfg = smallConfig();
        const std::int64_t cost = erosionCellPasses(kSmall, kSmall, cfg.erosion);
        cfg.erosionBudgetCellPasses = cost;
        CHECK(forgeWorld(1, kSmall, cfg, ForgePhase::Erosion).ok());
        cfg.erosionBudgetCellPasses = cost - 1;
        CHECK_FALSE(forgeWorld(1, kSmall, cfg, ForgePhase::Erosion).ok());
    }
    SECTION("hostile budgets")
    {
        for (const std::int64_t budget : { std::int64_t{ 0 }, std::int64_t{ -1 }, std::numeric_limits<std::int64_t>::min() }) {
            ForgeConfig cfg = smallConfig();
            cfg.erosionBudgetCellPasses = budget;
            const ForgedWorld w = forgeWorld(1, kSmall, cfg);
            REQUIRE(w.failedPhase().has_value());
            CHECK(*w.failedPhase() == ForgePhase::Erosion);
        }
        ForgeConfig huge = smallConfig();
        huge.erosionBudgetCellPasses = std::numeric_limits<std::int64_t>::max();
        CHECK(forgeWorld(1, kSmall, huge, ForgePhase::Erosion).ok());
    }
    SECTION("moisture scale out of range fails at resources")
    {
        for (const int scale : { std::numeric_limits<int>::min(), -1, 0, kClimateMaxMoisture + 1, std::numeric_limits<int>::max() }) {
            ForgeConfig cfg = smallConfig();
            cfg.moistureScale = scale;
            const ForgedWorld w = forgeWorld(1, kSmall, cfg);
            REQUIRE(w.failedPhase().has_value());
            CHECK(*w.failedPhase() == ForgePhase::Resources);
            CHECK(w.biomes().has_value());
        }
    }
    SECTION("extreme but valid params do not crash")
    {
        ForgeConfig cfg = richConfig();
        cfg.tectonics.plateCount = kTectonicsMaxPlates;
        cfg.erosion.iterations = 4;
        cfg.erosion.thermalPasses = kErosionMaxIterations;
        cfg.erosion.strength = kErosionMaxRate;
        cfg.erosion.talusM = 0;
        cfg.hydrology.riverThreshold = kHydrologyMaxRiverThreshold;
        cfg.climate.evaporation = kClimateMaxMoisture;
        cfg.climate.inflow = kClimateMaxMoisture;
        cfg.settlements.maxSettlements = kSettlementMaxSites;
        cfg.settlements.minSpacing = 1;
        cfg.settlements.densityMult256 = kSettlementMaxDensityMult256;
        cfg.pois.maxPois = kPoiMaxPois;
        cfg.pois.minSpacing = 1;
        cfg.binding.castPerArchetype = kManifestMaxPerArchetype;
        cfg.binding.tensionCount = 4096;
        const ForgedWorld w = forgeWorld(77, kSmall, cfg);
        CHECK(w.complete() != w.failedPhase().has_value());
        CHECK(w.completedPhases() >= 3);
    }
    SECTION("params that are out of range stop at their phase")
    {
        ForgeConfig cfg = smallConfig();
        cfg.erosion.iterations = kErosionMaxIterations + 1;
        CHECK(*forgeWorld(1, kSmall, cfg).failedPhase() == ForgePhase::Erosion);
        cfg = smallConfig();
        cfg.climate.windDirection = 8;
        CHECK(*forgeWorld(1, kSmall, cfg).failedPhase() == ForgePhase::Climate);
        cfg = smallConfig();
        cfg.biomes.suppressMask = ~0u;
        CHECK(*forgeWorld(1, kSmall, cfg).failedPhase() == ForgePhase::Biomes);
        cfg = smallConfig();
        cfg.factions.assign(kPoliticsMaxFactions + 1, FactionSpec{});
        CHECK(*forgeWorld(1, kSmall, cfg).failedPhase() == ForgePhase::Politics);
        cfg = smallConfig();
        cfg.landmarkHeightM = 0;
        CHECK(*forgeWorld(1, kSmall, cfg).failedPhase() == ForgePhase::Pois);
        cfg = smallConfig();
        cfg.binding.castPerArchetype = kManifestMaxPerArchetype + 1;
        CHECK(*forgeWorld(1, kSmall, cfg).failedPhase() == ForgePhase::Content);
    }
    SECTION("empty and over-long id lists")
    {
        ForgeConfig cfg = smallConfig();
        const ForgedWorld empty = forgeWorld(1, kSmall, cfg);
        requireComplete(empty);
        CHECK(empty.manifest()->items.empty());
        CHECK(empty.manifest()->cast.empty());
        cfg.itemIds = { std::string(kManifestMaxIdLength + 1, 'x') };
        CHECK(*forgeWorld(1, kSmall, cfg).failedPhase() == ForgePhase::Content);
        cfg.itemIds = { "" };
        CHECK(*forgeWorld(1, kSmall, cfg).failedPhase() == ForgePhase::Content);
    }
}

TEST_CASE("golden world hashes for fixed seeds", "[worldforge][forge][golden]")
{
    const ForgeConfig cfg = richConfig();
    struct Case {
        std::uint64_t seed;
        std::uint64_t hash;
    };
    const std::array<Case, 3> cases = { {
        { 1, 0x12d24c6d7aaf8922ull },
        { 42, 0x5708a205631597d2ull },
        { 0xDEADBEEFull, 0x0d8ee750c6282ff0ull },
    } };
    for (const Case& c : cases) {
        INFO("seed " << c.seed);
        const ForgedWorld w = forgeWorld(c.seed, kSmall, cfg);
        requireComplete(w);
        CHECK(w.worldHash() == c.hash);
    }
}

TEST_CASE("full run at the default size", "[worldforge][forge][.timing]")
{
    std::vector<std::pair<ForgePhase, std::chrono::steady_clock::time_point>> marks;
    const auto start = std::chrono::steady_clock::now();
    const ForgedWorld w = forgeWorld(1, kForgeDefaultSize, richConfig(), ForgePhase::Content, [&](const ForgeProgress& p) {
        if (p.finished) marks.emplace_back(p.phase, std::chrono::steady_clock::now());
    });
    auto prev = start;
    for (const auto& [phase, at] : marks) {
        WARN(forgePhaseName(phase) << " " << std::chrono::duration_cast<std::chrono::milliseconds>(at - prev).count() << " ms");
        prev = at;
    }
    WARN("total " << std::chrono::duration_cast<std::chrono::milliseconds>(prev - start).count() << " ms, ok " << w.ok() << " " << w.error());
    CHECK(w.completedPhases() > 0);
}
