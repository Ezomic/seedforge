#include <forgecli/forge.h>
#include <forgecli/overlay.h>
#include <forgecli/png.h>

#include <worldforge/world_validation.h>

#include <algorithm>
#include <exception>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>

namespace forgecli {

std::string layerHash(const Layer& layer)
{
    std::uint64_t h = 14695981039346656037ull;
    const auto mix = [&h](std::uint8_t b) {
        h ^= b;
        h *= 1099511628211ull;
    };
    for (const int v : {static_cast<int>(layer.name.size())}) {
        for (int shift = 0; shift < 32; shift += 8) mix(static_cast<std::uint8_t>(static_cast<std::uint32_t>(v) >> shift));
    }
    for (const char c : layer.name) mix(static_cast<std::uint8_t>(c));
    for (const int v : {layer.width, layer.height, layer.channels}) {
        for (int shift = 0; shift < 32; shift += 8) mix(static_cast<std::uint8_t>(static_cast<std::uint32_t>(v) >> shift));
    }
    for (const std::uint8_t b : layer.pixels) mix(b);
    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(h));
    return buf;
}

namespace {

std::string jsonEscape(std::string_view s)
{
    std::string out;
    for (const char c : s) {
        const auto u = static_cast<unsigned char>(c);
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (u < 0x20) {
            char buf[8];
            std::snprintf(buf, sizeof buf, "\\u%04x", u);
            out += buf;
        } else {
            out += c;
        }
    }
    return out;
}

std::string jsonString(std::string_view s) { return "\"" + jsonEscape(s) + "\""; }

bool layerValid(const Layer& l)
{
    return l.width > 0 && l.height > 0 && (l.channels == 1 || l.channels == 3)
        && l.pixels.size() == static_cast<std::size_t>(l.width) * static_cast<std::size_t>(l.height) * static_cast<std::size_t>(l.channels);
}

bool writeFile(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
{
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(f);
}

void validateSeed(const RunOptions& options, const worldforge::ForgeConfig& config, SeedRecord& record, RunResult& result)
{
    const worldforge::ValidatedForge v = worldforge::forgeValidated(record.seed, options.size, config, sanityThresholds());
    record.validated = true;
    record.rejected = !v.accepted;
    const worldforge::ValidationReport& last = v.attempts.back().report;
    record.landSharePermille = worldforge::landSharePermille(last);
    if (!last.error.empty()) record.causes.push_back(last.error);
    for (const worldforge::GateResult& g : last.gates) {
        if (g.status == worldforge::GateStatus::Fail) record.causes.push_back(g.id + ": " + g.cause);
    }
    for (const worldforge::ValidationAttempt& a : v.attempts) result.validationTable += "attempt " + std::to_string(a.index) + " " + worldforge::validationText(a.report);
    std::string json = worldforge::validatedForgeJson(v);
    json.pop_back();
    result.validation += (result.validation.empty() ? "" : ",") + ("\n" + json);
}

}

worldforge::GateThresholds sanityThresholds()
{
    worldforge::GateThresholds thresholds;
    thresholds.minQuestItems = 0;
    thresholds.minQuestCast = 0;
    return thresholds;
}

std::string manifestJson(const RunOptions& options, const std::vector<SeedRecord>& seeds)
{
    std::string out = "{\n  \"tool\": \"forge-cli\",\n  \"size\": " + std::to_string(options.size) + ",\n  \"phases\": [";
    for (std::size_t i = 0; i < options.phases.size(); ++i) out += (i ? ", " : "") + jsonString(options.phases[i]);
    out += "],\n";
    if (options.axioms) out += "  \"axioms\": {\"ids\": " + jsonString(options.axioms->ids) + ", \"digest\": " + jsonString(options.axioms->digest) + "},\n";
    out += "  \"runs\": [";
    for (std::size_t i = 0; i < seeds.size(); ++i) {
        const SeedRecord& s = seeds[i];
        out += std::string(i ? ",\n" : "\n") + "    {\"seed\": " + std::to_string(s.seed);
        if (!s.error.empty()) out += ", \"error\": " + jsonString(s.error);
        if (s.validated) {
            out += std::string(", \"validation\": {\"rejected\": ") + (s.rejected ? "true" : "false") + ", \"land_share_permille\": " + std::to_string(s.landSharePermille) + ", \"causes\": [";
            for (std::size_t c = 0; c < s.causes.size(); ++c) out += (c ? ", " : "") + jsonString(s.causes[c]);
            out += "]}";
        }
        if (!s.overlay.empty()) out += ", \"overlay\": " + jsonString(s.overlay);
        out += ", \"layers\": [";
        for (std::size_t j = 0; j < s.layers.size(); ++j) {
            const LayerRecord& l = s.layers[j];
            out += std::string(j ? ",\n" : "\n") + "      {\"name\": " + jsonString(l.name) + ", \"phase\": " + jsonString(l.phase);
            if (!l.file.empty()) out += ", \"file\": " + jsonString(l.file);
            out += ", \"width\": " + std::to_string(l.width) + ", \"height\": " + std::to_string(l.height)
                + ", \"channels\": " + std::to_string(l.channels) + ", \"hash\": " + jsonString(l.hash) + "}";
        }
        out += s.layers.empty() ? "]}" : "\n    ]}";
    }
    out += seeds.empty() ? "]\n}\n" : "\n  ]\n}\n";
    return out;
}

RunResult runForge(const RunOptions& in)
{
    RunResult result;
    RunOptions options = in;
    if (options.seedEnd < options.seedBegin) {
        result.error = "seed range is empty";
        return result;
    }
    if (options.seedEnd - options.seedBegin >= kMaxBatchSeeds) {
        result.error = "seed range exceeds " + std::to_string(kMaxBatchSeeds) + " seeds";
        return result;
    }
    if (options.phases.empty()) {
        for (const Phase& p : phaseRegistry()) options.phases.emplace_back(p.name);
    }
    std::vector<const Phase*> selected;
    for (const std::string& name : options.phases) {
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

    worldforge::ForgeConfig config = defaultForgeConfig(options.size);
    if (!options.dataDir.empty()) {
        std::string scanError;
        ContentPools pools = scanContentPools(options.dataDir, scanError);
        if (!scanError.empty()) {
            result.error = scanError;
            return result;
        }
        config.itemIds = std::move(pools.itemIds);
        config.npcArchetypes = std::move(pools.npcArchetypes);
        config.dungeonArchetypes = std::move(pools.dungeonArchetypes);
    }
    const worldforge::ForgePhase stopAfter = highestNeed(selected);

    const std::filesystem::path outDir = options.outDir;
    std::error_code ec;
    if (!options.outDir.empty()) {
        std::filesystem::create_directories(outDir, ec);
        if (ec) {
            result.error = "cannot create " + options.outDir + ": " + ec.message();
            return result;
        }
    }

    for (std::uint64_t seed = options.seedBegin;; ++seed) {
        SeedRecord record;
        record.seed = seed;
        const std::string dirName = "seed_" + std::to_string(seed);
        bool dirReady = options.outDir.empty();
        bool failedWrite = false;
        std::optional<worldforge::ForgedWorld> world;
        std::string forgeError;
        try {
            world.emplace(worldforge::forgeWorld(seed, options.size, config, stopAfter));
        } catch (const std::exception& e) {
            forgeError = e.what();
        }
        for (const Phase* phase : selected) {
            PhaseResult r;
            if (!world) {
                r.error = forgeError;
            } else {
                try {
                    r = phaseLayers(*phase, *world);
                } catch (const std::exception& e) {
                    r.error = e.what();
                }
            }
            if (!r.error.empty()) {
                record.error = std::string(phase->name) + ": " + r.error;
                break;
            }
            for (const Layer& layer : r.layers) {
                if (!layerValid(layer)) {
                    record.error = std::string(phase->name) + ": produced an invalid layer " + layer.name;
                    break;
                }
                LayerRecord lr{layer.name, std::string(phase->name), "", layer.width, layer.height, layer.channels, layerHash(layer)};
                if (!options.outDir.empty()) {
                    if (!dirReady) {
                        std::filesystem::create_directories(outDir / dirName, ec);
                        if (ec) {
                            result.error = "cannot create " + dirName + ": " + ec.message();
                            failedWrite = true;
                            break;
                        }
                        dirReady = true;
                    }
                    const auto png = encodePng(layer.width, layer.height, layer.channels, layer.pixels);
                    lr.file = dirName + "/" + layer.name + ".png";
                    if (!png) {
                        record.error = std::string(phase->name) + ": cannot encode " + layer.name;
                        break;
                    }
                    if (!writeFile(outDir / lr.file, *png)) {
                        result.error = "cannot write " + lr.file;
                        failedWrite = true;
                        break;
                    }
                }
                record.layers.push_back(std::move(lr));
            }
            if (!record.error.empty() || failedWrite) break;
        }
        if (!options.outDir.empty() && world && record.error.empty() && !failedWrite) {
            const std::string overlay = overlayJson(*world);
            if (!overlay.empty()) {
                std::filesystem::create_directories(outDir / dirName, ec);
                const std::vector<std::uint8_t> bytes(overlay.begin(), overlay.end());
                if (ec || !writeFile(outDir / dirName / "overlay.json", bytes)) {
                    result.error = "cannot write " + dirName + "/overlay.json";
                    failedWrite = true;
                } else {
                    record.overlay = dirName + "/overlay.json";
                }
            }
        }
        if (options.validate && record.error.empty()) validateSeed(options, config, record, result);
        result.seeds.push_back(std::move(record));
        if (failedWrite || seed == options.seedEnd) break;
    }

    result.manifest = manifestJson(options, result.seeds);
    if (options.validate) result.validation = "{\"tool\": \"forge-cli\", \"size\": " + std::to_string(options.size) + ", \"seeds\": [" + result.validation + "\n]}\n";
    if (!options.outDir.empty()) {
        const std::vector<std::uint8_t> bytes(result.manifest.begin(), result.manifest.end());
        if (!writeFile(outDir / "manifest.json", bytes) && result.error.empty()) result.error = "cannot write manifest.json";
        if (options.validate) {
            const std::vector<std::uint8_t> report(result.validation.begin(), result.validation.end());
            if (!writeFile(outDir / "validation.json", report) && result.error.empty()) result.error = "cannot write validation.json";
        }
    }
    return result;
}

}
