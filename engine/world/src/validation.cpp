#include "ichor/world/validation.h"

#include "ichor/core/binary.h"
#include "ichor/core/hash.h"
#include "ichor/data/world_config.h"
#include "ichor/world/forge.h"
#include "ichor/world/forged_heightfield.h"

#include <algorithm>
#include <exception>
#include <stdexcept>
#include <format>
#include <map>
#include <variant>
#include <vector>
#include <string_view>
#include <utility>

namespace ichor {

namespace {

constexpr std::uint64_t kMinLandPerMille = 20;
constexpr std::uint16_t kMinSampleRange = 256;
constexpr std::uint32_t kRerollCeiling = 64;
constexpr std::uint64_t kMinSettleablePerMille = 10;
constexpr float kDiveDepthM = 3.0f;

Heightfield forgedTerrainOrThrow(const WorldDesc& desc)
{
    ForgedHeightfield forged = forgeHeightfieldFromWorldforge(desc);
    if (!forged.ok()) {
        throw std::runtime_error(std::format("forged terrain: {}", forged.error));
    }
    return std::move(forged.field);
}

std::uint64_t hydrologyDigest(const Hydrology& h)
{
    BinaryWriter w;
    w.write(h.side);
    w.write(h.filled);
    w.write(h.direction);
    w.write(h.accumulation);
    w.write(h.lake);
    w.write(h.lakeDeepest);
    const std::vector<std::uint8_t> bytes = w.take();
    return hash64(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

std::uint64_t heightfieldDigest(const Heightfield& h)
{
    BinaryWriter w;
    w.write(h.desc());
    const std::vector<std::uint8_t> bytes = w.take();
    return hashCombine(h.digest(), hash64(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size())));
}

std::optional<std::string> landAboveWater(const ForgedWorld& world, const ForgeRequest& request)
{
    if (!request.desc.hasWater()) {
        return std::nullopt;
    }
    const std::uint32_t n = request.desc.samplesPerSide();
    std::uint64_t land = 0;
    for (std::uint32_t z = 0; z < n; ++z) {
        for (std::uint32_t x = 0; x < n; ++x) {
            land += world.heightfield.heightAtSample(x, z) > request.desc.waterLevel() ? 1 : 0;
        }
    }
    const std::uint64_t total = static_cast<std::uint64_t>(n) * n;
    if (land * 1000 < total * kMinLandPerMille) {
        return std::format("{} of {} samples are above the water, under {}%", land, total, kMinLandPerMille / 10);
    }
    return std::nullopt;
}

std::optional<std::string> notFlat(const ForgedWorld& world, const ForgeRequest&)
{
    const auto& samples = world.heightfield.samples();
    if (samples.empty()) {
        return "the world has no samples";
    }
    const auto [low, high] = std::minmax_element(samples.begin(), samples.end());
    if (static_cast<std::uint32_t>(*high - *low) < kMinSampleRange) {
        return std::format("the heights span only {} of 65535 sample units", *high - *low);
    }
    return std::nullopt;
}

std::optional<std::string> hasRiver(const ForgedWorld& world, const ForgeRequest& request)
{
    if (!request.carveRivers) {
        return std::nullopt;
    }
    for (std::size_t i = 0; i < world.hydrology.accumulation.size(); ++i) {
        if (isRiver(world.hydrology, i, request.rivers)) {
            return std::nullopt;
        }
    }
    return std::format("no sample has {} or more draining through it", request.rivers.riverThreshold);
}

std::optional<std::string> hydrologyAgrees(const ForgedWorld& world, const ForgeRequest& request)
{
    const Hydrology& h = world.hydrology;
    const std::size_t count = static_cast<std::size_t>(request.desc.samplesPerSide()) * request.desc.samplesPerSide();
    if (h.side != request.desc.samplesPerSide() || h.filled.size() != count || h.direction.size() != count || h.accumulation.size() != count || h.lake.size() != count) {
        return "the hydrology does not cover the heightfield";
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (h.lake[i] > h.lakeDeepest.size()) {
            return std::format("sample {} names lake {} but only {} lakes exist", i, h.lake[i], h.lakeDeepest.size());
        }
        if (h.accumulation[i] == 0) {
            return std::format("sample {} drains nothing, not even itself", i);
        }
    }
    return std::nullopt;
}

const AxiomValue* directive(const ForgeRequest& request, std::string_view block, std::string_view key)
{
    if (!request.axioms) {
        return nullptr;
    }
    const auto& blocks = request.axioms->merged.directives.blocks;
    const auto b = blocks.find(block);
    if (b == blocks.end()) {
        return nullptr;
    }
    const auto v = b->second.find(key);
    return v == b->second.end() ? nullptr : &v->second;
}

bool listHas(const ForgeRequest& request, std::string_view block, std::string_view key, std::string_view word)
{
    const AxiomValue* v = directive(request, block, key);
    const auto* list = v ? std::get_if<std::vector<std::string>>(v) : nullptr;
    return list && std::find(list->begin(), list->end(), word) != list->end();
}

bool weighted(const ForgeRequest& request, std::string_view poi)
{
    const AxiomValue* v = directive(request, "poi", "weights");
    const auto* weights = v ? std::get_if<std::map<std::string, double, std::less<>>>(v) : nullptr;
    if (!weights) {
        return false;
    }
    const auto it = weights->find(poi);
    return it != weights->end() && it->second > 0.0;
}

bool isWaterSample(const ForgedWorld& world, const ForgeRequest& request, std::uint32_t x, std::uint32_t z)
{
    return world.heightfield.heightAtSample(x, z) <= request.desc.waterLevel();
}

template <typename F>
void forEachNeighbour(std::uint32_t n, std::uint32_t x, std::uint32_t z, F&& visit)
{
    if (x > 0) visit(x - 1, z);
    if (z > 0) visit(x, z - 1);
    if (x + 1 < n) visit(x + 1, z);
    if (z + 1 < n) visit(x, z + 1);
}

bool hasShore(const ForgedWorld& world, const ForgeRequest& request)
{
    const std::uint32_t n = request.desc.samplesPerSide();
    for (std::uint32_t z = 0; z < n; ++z) {
        for (std::uint32_t x = 0; x < n; ++x) {
            if (isWaterSample(world, request, x, z)) {
                continue;
            }
            bool touches = false;
            forEachNeighbour(n, x, z, [&](std::uint32_t nx, std::uint32_t nz) { touches = touches || isWaterSample(world, request, nx, nz); });
            if (touches) {
                return true;
            }
        }
    }
    return false;
}

std::optional<std::string> settlementElevationLand(const ForgedWorld& world, const ForgeRequest& request)
{
    const AxiomValue* v = directive(request, "settlement", "min_elevation_m");
    const double* minElevation = v ? std::get_if<double>(v) : nullptr;
    if (!minElevation) {
        return std::nullopt;
    }
    const std::uint32_t n = request.desc.samplesPerSide();
    std::uint64_t high = 0;
    for (std::uint32_t z = 0; z < n; ++z) {
        for (std::uint32_t x = 0; x < n; ++x) {
            const float h = world.heightfield.heightAtSample(x, z);
            high += (h >= *minElevation && h > request.desc.waterLevel()) ? 1 : 0;
        }
    }
    const std::uint64_t total = static_cast<std::uint64_t>(n) * n;
    if (high * 1000 < total * kMinSettleablePerMille) {
        return std::format("{} of {} samples are dry land at or above {} m, under {}%", high, total, *minElevation, kMinSettleablePerMille / 10.0);
    }
    return std::nullopt;
}

std::optional<std::string> diveWaterReachable(const ForgedWorld& world, const ForgeRequest& request)
{
    if (!listHas(request, "traversal", "unlock", "swim_deep")) {
        return std::nullopt;
    }
    if (!request.desc.hasWater()) {
        return "swim_deep is unlocked but the world has no water";
    }
    const std::uint32_t n = request.desc.samplesPerSide();
    const float deepLevel = request.desc.waterLevel() - kDiveDepthM;
    std::vector<std::uint8_t> seen(static_cast<std::size_t>(n) * n, 0);
    std::vector<std::pair<std::uint32_t, std::uint32_t>> queue;
    const auto enqueueWater = [&](std::uint32_t nx, std::uint32_t nz) {
        std::uint8_t& s = seen[static_cast<std::size_t>(nz) * n + nx];
        if (!s && isWaterSample(world, request, nx, nz)) {
            s = 1;
            queue.emplace_back(nx, nz);
        }
    };
    for (std::uint32_t z = 0; z < n; ++z) {
        for (std::uint32_t x = 0; x < n; ++x) {
            if (!isWaterSample(world, request, x, z)) {
                forEachNeighbour(n, x, z, enqueueWater);
            }
        }
    }
    for (std::size_t i = 0; i < queue.size(); ++i) {
        const auto [x, z] = queue[i];
        if (world.heightfield.heightAtSample(x, z) <= deepLevel) {
            return std::nullopt;
        }
        forEachNeighbour(n, x, z, enqueueWater);
    }
    return std::format("no water {} m deep or more can be reached from a shore, so nothing can be dived to", kDiveDepthM);
}

std::optional<std::string> springNeedsWater(const ForgedWorld& world, const ForgeRequest& request)
{
    if (!weighted(request, "sacred_spring")) {
        return std::nullopt;
    }
    const Hydrology& h = world.hydrology;
    for (std::size_t i = 0; i < h.accumulation.size() && i < h.lake.size(); ++i) {
        if (h.lake[i] != 0 || isRiver(h, i, request.rivers)) {
            return std::nullopt;
        }
    }
    if (request.desc.hasWater()) {
        const std::uint32_t n = request.desc.samplesPerSide();
        for (std::uint32_t z = 0; z < n; ++z) {
            for (std::uint32_t x = 0; x < n; ++x) {
                if (isWaterSample(world, request, x, z)) {
                    return std::nullopt;
                }
            }
        }
    }
    return "sacred_spring has weight but the world has no river, lake or sea for a spring to feed";
}

std::optional<std::string> coastNeedsShore(const ForgedWorld& world, const ForgeRequest& request)
{
    const bool wanted = listHas(request, "biomes", "inject", "tidal_ruin") || listHas(request, "biomes", "inject", "salt_blight") || weighted(request, "pharos")
        || weighted(request, "drowned_polis");
    if (!wanted || hasShore(world, request)) {
        return std::nullopt;
    }
    return "the axioms ask for coast (tidal_ruin, salt_blight, pharos or drowned_polis) but no land meets the water";
}

}

std::uint64_t attemptSeed(std::uint64_t worldSeed, std::uint32_t attempt)
{
    return attempt == 0 ? worldSeed : hashCombine(worldSeed, attempt);
}

PhasePipeline forgePipeline(const ForgeRequest& request)
{
    return PhasePipeline({
        makePhase<Heightfield>(
            kPhaseTerrain, [request](const PhaseView&) { return request.forgedTerrain ? forgedTerrainOrThrow(request.desc) : forgeHeightfield(request.desc); },
            heightfieldDigest),
        makePhase<Heightfield>(
            kPhaseRivers,
            [request](const PhaseView& earlier) {
                const Heightfield& terrain = earlier.get<Heightfield>(kPhaseTerrain);
                if (!request.carveRivers) {
                    return terrain;
                }
                return carveRivers(terrain, computeHydrology(terrain), request.rivers, CarveParams{});
            },
            heightfieldDigest),
        makePhase<Hydrology>(
            kPhaseHydrology, [](const PhaseView& earlier) { return computeHydrology(earlier.get<Heightfield>(kPhaseRivers)); }, hydrologyDigest),
    });
}

ForgedWorld forgedWorld(const PhaseRun& run)
{
    return {run.get<Heightfield>(kPhaseRivers), run.get<Hydrology>(kPhaseHydrology)};
}

ForgedWorld forgeWorld(const ForgeRequest& request)
{
    return forgedWorld(forgePipeline(request).run());
}

std::vector<GateFailure> runGates(const std::vector<Gate>& gates, const ForgedWorld& world, const ForgeRequest& request)
{
    std::vector<GateFailure> failures;
    for (const Gate& gate : gates) {
        try {
            if (auto reason = gate.check(world, request)) {
                failures.push_back({gate.name, std::move(*reason)});
            }
        } catch (const std::exception& e) {
            failures.push_back({gate.name, std::string("threw: ") + e.what()});
        } catch (...) {
            failures.push_back({gate.name, "threw a non-standard exception"});
        }
    }
    return failures;
}

std::vector<Gate> defaultGates()
{
    std::vector<Gate> gates{
        {"land-above-water", landAboveWater},
        {"not-flat", notFlat},
        {"has-river", hasRiver},
        {"hydrology-agrees", hydrologyAgrees},
    };
    for (Gate& gate : axiomGates()) {
        gates.push_back(std::move(gate));
    }
    return gates;
}

std::vector<Gate> axiomGates()
{
    return {
        {"settlement-elevation-land", settlementElevationLand},
        {"dive-water-reachable", diveWaterReachable},
        {"spring-needs-water", springNeedsWater},
        {"coast-needs-shore", coastNeedsShore},
    };
}

ValidatedWorld forgeValidated(const ForgeRequest& request, const std::vector<Gate>& gates, std::uint32_t maxRerolls)
{
    ValidatedWorld result;
    result.seed = request.desc.seed;
    maxRerolls = std::min(maxRerolls, kRerollCeiling);
    for (std::uint32_t attempt = 0; attempt <= maxRerolls; ++attempt) {
        ForgeRequest tried = request;
        tried.desc.seed = attemptSeed(request.desc.seed, attempt);
        ForgedWorld world = forgeWorld(tried);
        std::vector<GateFailure> failures = runGates(gates, world, tried);
        if (failures.empty()) {
            if (attempt > 0) {
                world.heightfield = Heightfield(request.desc, world.heightfield.samples());
            }
            result.world = std::move(world);
            result.attempt = attempt;
            result.passed = true;
            return result;
        }
        if (attempt == 0) {
            result.world = std::move(world);
        }
        result.rejected.push_back({attempt, tried.desc.seed, std::move(failures)});
    }
    return result;
}

}
