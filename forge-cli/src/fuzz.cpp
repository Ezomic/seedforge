#include <forgecli/fuzz.h>

#include <worldforge/erosion_map.h>
#include <worldforge/tectonics_map.h>

#include <algorithm>
#include <exception>
#include <optional>
#include <set>

namespace forgecli {

namespace {

constexpr std::uint64_t kMaxFuzzSeeds = 1000000;
constexpr std::int64_t kLayerBytesPerCell = 40;
constexpr int kMinRiverSize = 32;

const Layer* findLayer(const std::vector<Layer>& layers, std::string_view name)
{
    for (const Layer& l : layers) {
        if (l.name == name) return &l;
    }
    return nullptr;
}

// A float that went NaN or infinite and was cast to a byte lands on one value for the whole layer,
// so a layer that should vary but is a single value is treated as NaN-derived.
std::string requireVariation(const std::vector<Layer>& layers, std::string_view name)
{
    const Layer* l = findLayer(layers, name);
    if (!l) return "layer " + std::string(name) + " is missing";
    const std::set<std::uint8_t> values(l->pixels.begin(), l->pixels.end());
    if (values.size() < 2) return "layer " + std::string(name) + " is a single value";
    return {};
}

std::string tectonicsRule(const std::vector<Layer>& layers, int)
{
    for (const char* name : {"height", "plates", "boundary"}) {
        if (auto e = requireVariation(layers, name); !e.empty()) return e;
    }
    return {};
}

std::string regionTierRule(const std::vector<Layer>& layers, int)
{
    return requireVariation(layers, "region_tier");
}

// Sea level is 0 m and the carved height layer maps the tectonics range onto bytes, so land starts at this byte.
constexpr int kLandByte = (0 - worldforge::kTectonicsMinHeightM) * 255 / (worldforge::kTectonicsMaxHeightM - worldforge::kTectonicsMinHeightM) + 1;
constexpr std::size_t kLandShareForRivers = 4; // rivers and flow variation are expected once a quarter of the map is land

std::string hydrologyRule(const std::vector<Layer>& layers, int size)
{
    const Layer* height = findLayer(layers, "height_carved");
    const Layer* rivers = findLayer(layers, "rivers");
    if (!height || !rivers) return "hydrology layers are missing";
    if (auto e = requireVariation(layers, "height_carved"); !e.empty()) return e;
    const auto land = static_cast<std::size_t>(std::count_if(height->pixels.begin(), height->pixels.end(), [](std::uint8_t p) { return p >= kLandByte; }));
    if (size < kMinRiverSize || land * kLandShareForRivers < height->pixels.size()) return {};
    if (auto e = requireVariation(layers, "flow"); !e.empty()) return e;
    if (std::none_of(rivers->pixels.begin(), rivers->pixels.end(), [](std::uint8_t p) { return p != 0; })) return "no river cell on a map with a quarter or more land";
    return {};
}

std::string biomeRule(const std::vector<Layer>& layers, int)
{
    return requireVariation(layers, "biome");
}

std::string layerShapeRule(const std::vector<Layer>& layers, int size)
{
    for (const Layer& l : layers) {
        if (l.width != size || l.height != size) return "layer " + l.name + " is not size x size";
    }
    return {};
}

}

const std::vector<PhaseRule>& phaseRules()
{
    static const std::vector<PhaseRule> rules = {
        {"tectonics", "varied_layers", tectonicsRule},
        {"region_tiers", "varied_tiers", regionTierRule},
        {"hydrology", "rivers_and_variation", hydrologyRule},
        {"biomes", "varied_biomes", biomeRule},
    };
    return rules;
}

FuzzResult runFuzz(const FuzzOptions& in)
{
    FuzzResult result;
    if (in.seedEnd < in.seedBegin) {
        result.error = "seed range is empty";
        return result;
    }
    if (in.seedEnd - in.seedBegin >= kMaxFuzzSeeds) {
        result.error = "seed range exceeds " + std::to_string(kMaxFuzzSeeds) + " seeds";
        return result;
    }
    if (in.size < 1 || in.size > worldforge::kTectonicsMaxSize) {
        result.error = "size is outside 1..1024";
        return result;
    }
    std::vector<const Phase*> selected;
    if (in.phases.empty()) {
        for (const Phase& p : phaseRegistry()) selected.push_back(&p);
    }
    for (const std::string& name : in.phases) {
        const Phase* p = findPhase(name);
        if (!p) {
            result.error = "unknown phase: " + name;
            return result;
        }
        if (std::find(selected.begin(), selected.end(), p) != selected.end()) {
            result.error = "duplicate phase: " + name;
            return result;
        }
        selected.push_back(p);
    }

    const worldforge::ForgeConfig config = defaultForgeConfig(in.size);
    const worldforge::ForgePhase stopAfter = highestNeed(selected);
    const std::int64_t cells = static_cast<std::int64_t>(in.size) * in.size;
    const std::int64_t maxPasses = in.maxCellPasses > 0 ? in.maxCellPasses : worldforge::kErosionMaxCellPasses;
    const std::int64_t maxBytes = in.maxLayerBytes > 0 ? in.maxLayerBytes : cells * kLayerBytesPerCell;

    for (std::uint64_t seed = in.seedBegin;; ++seed) {
        ++result.seedsRun;
        const auto flag = [&](std::string_view phase, std::string_view rule, std::string detail) {
            result.violations.push_back({seed, std::string(phase), std::string(rule), std::move(detail)});
        };
        std::optional<worldforge::ForgedWorld> first;
        std::optional<worldforge::ForgedWorld> second;
        std::string thrown;
        try {
            first.emplace(worldforge::forgeWorld(seed, in.size, config, stopAfter));
            second.emplace(worldforge::forgeWorld(seed, in.size, config, stopAfter));
        } catch (const std::exception& e) {
            thrown = e.what();
        } catch (...) {
            thrown = "unknown exception";
        }
        std::int64_t passes = 0;
        std::int64_t bytes = 0;
        if (first && static_cast<int>(stopAfter) >= static_cast<int>(worldforge::ForgePhase::Erosion)) {
            const worldforge::ErosionParams erosion = config.erosion;
            passes += static_cast<std::int64_t>(in.size) * in.size * erosion.iterations * (1 + erosion.thermalPasses);
        }
        if (first && second && first->worldHash() != second->worldHash()) flag("", "deterministic", "world hash differs on repeat run");
        for (const Phase* phase : selected) {
            if (!first || !second) {
                flag(phase->name, "no_throw", thrown);
                continue;
            }
            PhaseResult firstLayers;
            PhaseResult secondLayers;
            try {
                firstLayers = phaseLayers(*phase, *first);
                secondLayers = phaseLayers(*phase, *second);
            } catch (const std::exception& e) {
                flag(phase->name, "no_throw", e.what());
                continue;
            } catch (...) {
                flag(phase->name, "no_throw", "unknown exception");
                continue;
            }
            if (!firstLayers.error.empty()) {
                flag(phase->name, "no_error", firstLayers.error);
                continue;
            }
            if (!secondLayers.error.empty() || firstLayers.layers.size() != secondLayers.layers.size()) {
                flag(phase->name, "deterministic", "repeat run differs in outcome");
                continue;
            }
            for (std::size_t i = 0; i < firstLayers.layers.size(); ++i) {
                if (layerHash(firstLayers.layers[i]) != layerHash(secondLayers.layers[i])) {
                    flag(phase->name, "deterministic", "layer " + firstLayers.layers[i].name + " hash differs on repeat run");
                }
            }
            if (auto e = layerShapeRule(firstLayers.layers, in.size); !e.empty()) flag(phase->name, "layer_shape", e);
            passes += cells * static_cast<std::int64_t>(firstLayers.layers.size());
            for (const Layer& l : firstLayers.layers) bytes += static_cast<std::int64_t>(l.pixels.size());
            for (const PhaseRule& rule : phaseRules()) {
                if (rule.phase != phase->name) continue;
                if (auto e = rule.check(firstLayers.layers, in.size); !e.empty()) flag(phase->name, rule.name, e);
            }
        }
        if (passes > maxPasses) flag("", "cell_pass_budget", std::to_string(passes) + " cell passes exceeds " + std::to_string(maxPasses));
        if (bytes > maxBytes) flag("", "layer_byte_budget", std::to_string(bytes) + " layer bytes exceeds " + std::to_string(maxBytes));
        if (seed == in.seedEnd) break;
    }
    return result;
}

std::string fuzzSummary(const FuzzOptions& options, const FuzzResult& result)
{
    std::set<std::uint64_t> seeds;
    for (const Violation& v : result.violations) seeds.insert(v.seed);
    std::string out = "fuzz: " + std::to_string(result.seedsRun) + " seeds, size " + std::to_string(options.size) + ", "
        + std::to_string(seeds.size()) + " violating\n";
    for (const Violation& v : result.violations) {
        out += "  seed " + std::to_string(v.seed) + " [" + (v.phase.empty() ? "all" : v.phase) + "] " + v.rule + ": " + v.detail + "\n";
    }
    return out;
}

}
