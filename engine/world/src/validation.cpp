#include "ichor/world/validation.h"

#include "ichor/core/binary.h"
#include "ichor/core/hash.h"
#include "ichor/world/forge.h"

#include <algorithm>
#include <exception>
#include <format>
#include <string_view>
#include <utility>

namespace ichor {

namespace {

constexpr std::uint64_t kMinLandPerMille = 20;
constexpr std::uint16_t kMinSampleRange = 256;
constexpr std::uint32_t kRerollCeiling = 64;

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

}

std::uint64_t attemptSeed(std::uint64_t worldSeed, std::uint32_t attempt)
{
    return attempt == 0 ? worldSeed : hashCombine(worldSeed, attempt);
}

PhasePipeline forgePipeline(const ForgeRequest& request)
{
    return PhasePipeline({
        makePhase<Heightfield>(
            kPhaseTerrain, [request](const PhaseView&) { return forgeHeightfield(request.desc); }, heightfieldDigest),
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
    return {
        {"land-above-water", landAboveWater},
        {"not-flat", notFlat},
        {"has-river", hasRiver},
        {"hydrology-agrees", hydrologyAgrees},
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
