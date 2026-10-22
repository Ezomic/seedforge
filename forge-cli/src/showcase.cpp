#include <forgecli/forge.h>
#include <forgecli/gallery.h>
#include <forgecli/showcase.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>

namespace forgecli {

namespace {

int wholeMetres(double v) { return std::isfinite(v) ? static_cast<int>(std::clamp(v, 0.0, 1.0e9)) : 0; }

int clampPermille(std::int64_t v) { return static_cast<int>(std::clamp<std::int64_t>(v, 0, 1000)); }

int saturate(int count, int forFull) { return forFull <= 0 ? 1000 : clampPermille(static_cast<std::int64_t>(std::max(count, 0)) * 1000 / forFull); }

std::string percentNumber(int permille) { return std::to_string(permille / 10) + "." + std::to_string(permille % 10); }

std::string percentText(int permille) { return percentNumber(permille) + "%"; }

std::string plural(int n, const char* one, const char* many) { return std::to_string(n) + " " + (n == 1 ? one : many); }

int landPermille(const ShowcaseMetrics& m, const ShowcaseWeights& w)
{
    if (m.landSharePermille < 0) return 0;
    if (m.landSharePermille >= w.landIdealMinPermille && m.landSharePermille <= w.landIdealMaxPermille) return 1000;
    const int away = m.landSharePermille < w.landIdealMinPermille ? w.landIdealMinPermille - m.landSharePermille : m.landSharePermille - w.landIdealMaxPermille;
    if (w.landFalloffPermille <= 0) return 0;
    return clampPermille(1000 - static_cast<std::int64_t>(away) * 1000 / w.landFalloffPermille);
}

std::string gatesNote(const ShowcaseMetrics& m)
{
    std::string note = "gates " + std::to_string(m.gatesPassed) + "/" + std::to_string(m.gatesEvaluated) + " on the first try";
    if (!m.failedGates.empty()) {
        note += " (failed";
        for (const std::string& g : m.failedGates) note += " " + g;
        note += ")";
    }
    return note;
}

std::string walkNote(const ShowcaseMetrics& m)
{
    if (!m.walkMeasured) return "no walk target";
    if (m.walkArrived) return "walk arrived (" + std::to_string(m.tripMetres) + " m)";
    return "walk blocked" + (m.walkBlocker.empty() ? std::string() : " by " + m.walkBlocker) + " after " + std::to_string(m.walkedMetres) + " m (trip " + std::to_string(m.tripMetres) + " m)";
}

std::string spawnNote(const ShowcaseMetrics& m)
{
    if (!m.walkError.empty()) return "walk probe failed: " + m.walkError;
    if (!m.spawnFound) return "no spawn";
    return m.pocketSpawn ? "pocket spawn" : "open spawn";
}

ScoredTerm term(ShowcaseTerm t, int permille, const ShowcaseWeights& w, std::string note)
{
    ScoredTerm s;
    s.term = t;
    s.permille = permille;
    s.weight = w.weight[static_cast<std::size_t>(t)];
    s.pointsMilli = s.weight * permille;
    s.note = std::move(note);
    return s;
}

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

std::string csvField(std::string_view s)
{
    if (s.find_first_of(",\"\n\r") == std::string_view::npos) return std::string(s);
    std::string out = "\"";
    for (const char c : s) {
        if (c == '"') out += '"';
        out += c;
    }
    return out + "\"";
}

const ShowcaseMetrics* metricsOf(const std::vector<ShowcaseMetrics>& metrics, std::uint64_t seed)
{
    for (const ShowcaseMetrics& m : metrics) {
        if (m.seed == seed) return &m;
    }
    return nullptr;
}

int countComponents(int width, int height, int minCells, const std::vector<std::uint8_t>& mask)
{
    if (width <= 0 || height <= 0 || mask.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) return 0;
    std::vector<std::uint8_t> seen(mask.size(), 0);
    std::vector<std::size_t> stack;
    int components = 0;
    for (std::size_t start = 0; start < mask.size(); ++start) {
        if (!mask[start] || seen[start]) continue;
        int cells = 0;
        stack.assign(1, start);
        seen[start] = 1;
        while (!stack.empty()) {
            const std::size_t at = stack.back();
            stack.pop_back();
            ++cells;
            const int x = static_cast<int>(at % static_cast<std::size_t>(width));
            const int y = static_cast<int>(at / static_cast<std::size_t>(width));
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = x + dx;
                    const int ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= width || ny >= height) continue;
                    const std::size_t n = static_cast<std::size_t>(ny) * static_cast<std::size_t>(width) + static_cast<std::size_t>(nx);
                    if (!mask[n] || seen[n]) continue;
                    seen[n] = 1;
                    stack.push_back(n);
                }
            }
        }
        if (cells >= minCells) ++components;
    }
    return components;
}

int landBiomesNear(const worldforge::BiomeMap& biomes, double cellX, double cellY, double radiusCells)
{
    std::uint32_t seen = 0;
    const int r = static_cast<int>(radiusCells) + 1;
    const int cx = static_cast<int>(cellX);
    const int cy = static_cast<int>(cellY);
    for (int y = std::max(0, cy - r); y <= std::min(biomes.height - 1, cy + r); ++y) {
        for (int x = std::max(0, cx - r); x <= std::min(biomes.width - 1, cx + r); ++x) {
            const double dx = x - cellX;
            const double dy = y - cellY;
            if (dx * dx + dy * dy > radiusCells * radiusCells) continue;
            const std::uint8_t id = biomes.biome[biomes.index(x, y)];
            if (id == static_cast<std::uint8_t>(worldforge::BiomeId::Ocean) || id == static_cast<std::uint8_t>(worldforge::BiomeId::Lake)) continue;
            if (id < worldforge::kBiomeCount) seen |= std::uint32_t{1} << id;
        }
    }
    int count = 0;
    for (; seen != 0; seen &= seen - 1) ++count;
    return count;
}

}

ShowcaseScore scoreShowcase(const ShowcaseMetrics& m, const ShowcaseWeights& w)
{
    ShowcaseScore score;
    score.seed = m.seed;
    score.size = m.size;
    const bool usable = m.error.empty();
    const auto at = [&](ShowcaseTerm t) -> ScoredTerm& { return score.terms[static_cast<std::size_t>(t)]; };

    at(ShowcaseTerm::Gates) = term(ShowcaseTerm::Gates, usable && m.gatesEvaluated > 0 && m.gatesPassed == m.gatesEvaluated ? 1000 : 0, w, gatesNote(m));
    at(ShowcaseTerm::LandShare) = term(ShowcaseTerm::LandShare, usable ? landPermille(m, w) : 0, w, m.landSharePermille < 0 ? "land not measured" : "land " + percentText(m.landSharePermille));
    at(ShowcaseTerm::Rivers) = term(ShowcaseTerm::Rivers, usable ? saturate(m.riverSystems, w.riversForFull) : 0, w, plural(m.riverSystems, "river", "rivers"));
    at(ShowcaseTerm::Lakes) = term(ShowcaseTerm::Lakes, usable ? saturate(m.lakes, w.lakesForFull) : 0, w, plural(m.lakes, "lake", "lakes"));
    at(ShowcaseTerm::Settlements) = term(ShowcaseTerm::Settlements, usable ? saturate(m.settlements, w.settlementsForFull) : 0, w, plural(m.settlements, "settlement", "settlements"));
    at(ShowcaseTerm::TierSpread) = term(ShowcaseTerm::TierSpread, usable ? saturate(m.tiersPresent, w.tiersForFull) : 0, w, plural(m.tiersPresent, "settlement tier", "settlement tiers"));
    at(ShowcaseTerm::WalkArrival) = term(ShowcaseTerm::WalkArrival, usable && m.walkMeasured && m.walkArrived ? 1000 : 0, w, walkNote(m));
    at(ShowcaseTerm::BiomeVariety) = term(ShowcaseTerm::BiomeVariety, usable && m.spawnFound ? saturate(m.biomesNearSpawn, w.biomesForFull) : 0, w,
        plural(m.biomesNearSpawn, "biome", "biomes") + " within " + std::to_string(kBiomeRadiusMetres) + " m of the spawn");
    at(ShowcaseTerm::Spawn) = term(ShowcaseTerm::Spawn, usable && m.spawnFound && !m.pocketSpawn ? 1000 : 0, w, spawnNote(m));

    std::int64_t sum = 0;
    for (const ScoredTerm& t : score.terms) sum += t.pointsMilli;
    score.totalMilli = static_cast<int>(sum);
    return score;
}

std::string pointsText(int milli)
{
    const int whole = milli / 1000;
    const int tenth = (milli % 1000) / 100;
    return std::to_string(whole) + "." + std::to_string(tenth);
}

int countRiverSystems(const worldforge::HydrologyMap& h, int minCells) { return countComponents(h.width, h.height, minCells, h.river); }

int countLakes(const worldforge::HydrologyMap& h, int minCells)
{
    std::vector<std::uint8_t> mask(h.lakeDepthM.size());
    for (std::size_t i = 0; i < mask.size(); ++i) mask[i] = h.lakeDepthM[i] > 0 ? 1 : 0;
    return countComponents(h.width, h.height, minCells, mask);
}

ShowcaseMetrics measureShowcase(const worldforge::ForgedWorld& world, const worldforge::ValidationReport& report, const WalkProbeResult& walk)
{
    ShowcaseMetrics m;
    m.seed = world.seed();
    m.size = world.size();
    if (!world.ok()) {
        m.error = world.error().empty() ? "the world did not forge" : world.error();
        return m;
    }
    if (!world.hydrology() || !world.biomes() || !world.settlements()) {
        m.error = "a phase artifact is missing";
        return m;
    }
    for (const worldforge::GateResult& g : report.gates) {
        if (g.status == worldforge::GateStatus::Skipped) continue;
        ++m.gatesEvaluated;
        if (g.status == worldforge::GateStatus::Pass) {
            ++m.gatesPassed;
        } else {
            m.failedGates.push_back(g.id);
        }
    }
    if (!report.error.empty()) m.error = report.error;
    m.landSharePermille = static_cast<int>(worldforge::landSharePermille(report));
    m.riverSystems = countRiverSystems(*world.hydrology());
    m.lakes = countLakes(*world.hydrology());
    m.settlements = static_cast<int>(world.settlements()->sites.size());
    std::uint32_t tiers = 0;
    for (const worldforge::Settlement& s : world.settlements()->sites) tiers |= std::uint32_t{1} << (s.tier & 31);
    for (; tiers != 0; tiers &= tiers - 1) ++m.tiersPresent;

    m.walkError = walk.error;
    m.spawnFound = walk.error.empty() && walk.spawnFound;
    m.pocketSpawn = m.spawnFound && walk.pocket;
    if (m.spawnFound && walk.metresPerCell > 0.0 && std::isfinite(walk.spawnX) && std::isfinite(walk.spawnZ)) {
        m.biomesNearSpawn = landBiomesNear(*world.biomes(), walk.spawnX / walk.metresPerCell, walk.spawnZ / walk.metresPerCell, kBiomeRadiusMetres / walk.metresPerCell);
    }
    m.walkMeasured = m.spawnFound && walk.hasTarget;
    m.walkArrived = m.walkMeasured && walk.arrived;
    m.tripMetres = wholeMetres(walk.tripMetres);
    m.walkedMetres = wholeMetres(walk.walkedMetres);
    m.walkBlocker = walk.blocker;
    return m;
}

std::vector<ShowcaseScore> rankShowcase(std::vector<ShowcaseScore> scores)
{
    std::stable_sort(scores.begin(), scores.end(), [](const ShowcaseScore& a, const ShowcaseScore& b) {
        return a.totalMilli != b.totalMilli ? a.totalMilli > b.totalMilli : a.seed < b.seed;
    });
    return scores;
}

std::vector<std::string> showcaseReasons(const ShowcaseScore& score)
{
    std::vector<std::string> good;
    std::vector<std::string> bad;
    for (const ScoredTerm& t : score.terms) {
        const std::string label = t.note + " (" + pointsText(t.pointsMilli) + "/" + std::to_string(t.weight) + ")";
        (t.permille >= 500 ? good : bad).push_back(t.permille >= 500 ? label : "weak: " + label);
    }
    good.insert(good.end(), bad.begin(), bad.end());
    return good;
}

std::string showcaseCsv(const std::vector<ShowcaseScore>& ranked, const std::vector<ShowcaseMetrics>& metrics)
{
    std::string out = "rank,seed,size,score";
    for (const std::string_view name : kShowcaseTermNames) out += "," + std::string(name) + "_points";
    out += ",gates_passed,gates_evaluated,failed_gates,land_share_percent,rivers,lakes,settlements,tiers,walk_arrived,trip_m,walked_m,walk_blocker,spawn_found,pocket_spawn,biomes_near_spawn,error\n";
    int rank = 0;
    for (const ShowcaseScore& s : ranked) {
        ++rank;
        out += std::to_string(rank) + "," + std::to_string(s.seed) + "," + std::to_string(s.size) + "," + pointsText(s.totalMilli);
        for (const ScoredTerm& t : s.terms) out += "," + pointsText(t.pointsMilli);
        const ShowcaseMetrics* m = metricsOf(metrics, s.seed);
        if (!m) {
            out += "\n";
            continue;
        }
        std::string failed;
        for (const std::string& g : m->failedGates) failed += (failed.empty() ? "" : " ") + g;
        out += "," + std::to_string(m->gatesPassed) + "," + std::to_string(m->gatesEvaluated) + "," + csvField(failed) + "," + (m->landSharePermille < 0 ? std::string() : percentNumber(m->landSharePermille));
        out += "," + std::to_string(m->riverSystems) + "," + std::to_string(m->lakes) + "," + std::to_string(m->settlements) + "," + std::to_string(m->tiersPresent);
        out += std::string(",") + (m->walkMeasured ? (m->walkArrived ? "yes" : "no") : "n/a") + "," + std::to_string(m->tripMetres) + "," + std::to_string(m->walkedMetres) + "," + csvField(m->walkBlocker);
        out += std::string(",") + (m->spawnFound ? "yes" : "no") + "," + (m->pocketSpawn ? "yes" : "no") + "," + std::to_string(m->biomesNearSpawn) + "," + csvField(m->error.empty() ? m->walkError : m->error) + "\n";
    }
    return out;
}

std::string showcaseMarkdown(const std::vector<ShowcaseScore>& ranked, const std::vector<ShowcaseMetrics>& metrics, int top)
{
    std::string out = "# Showcase ranking\n\n";
    out += "| Rank | Seed | Score | Gates | Land | Rivers | Lakes | Settlements | Tiers | Walk | Biomes | Spawn |\n";
    out += "|---|---|---|---|---|---|---|---|---|---|---|---|\n";
    int rank = 0;
    for (const ShowcaseScore& s : ranked) {
        ++rank;
        const ShowcaseMetrics* m = metricsOf(metrics, s.seed);
        out += "| " + std::to_string(rank) + " | " + std::to_string(s.seed) + " | " + pointsText(s.totalMilli);
        if (!m) {
            out += " | | | | | | | | | |\n";
            continue;
        }
        out += " | " + std::to_string(m->gatesPassed) + "/" + std::to_string(m->gatesEvaluated);
        out += " | " + (m->landSharePermille < 0 ? std::string("n/a") : percentText(m->landSharePermille));
        out += " | " + std::to_string(m->riverSystems) + " | " + std::to_string(m->lakes) + " | " + std::to_string(m->settlements) + " | " + std::to_string(m->tiersPresent);
        out += std::string(" | ") + (m->walkMeasured ? (m->walkArrived ? "arrived" : "blocked") : "n/a") + " | " + std::to_string(m->biomesNearSpawn);
        out += std::string(" | ") + (!m->spawnFound ? "none" : m->pocketSpawn ? "pocket" : "open") + " |\n";
    }
    out += "\n## Top " + std::to_string(std::min<std::size_t>(static_cast<std::size_t>(std::max(top, 0)), ranked.size())) + "\n\n";
    rank = 0;
    for (const ShowcaseScore& s : ranked) {
        if (++rank > top) break;
        out += std::to_string(rank) + ". seed " + std::to_string(s.seed) + ", " + pointsText(s.totalMilli) + " points\n";
        for (const std::string& reason : showcaseReasons(s)) out += "    - " + reason + "\n";
    }
    return out;
}

std::string showcaseSeedsJson(const std::vector<ShowcaseScore>& ranked, int top)
{
    std::string out = "[";
    int rank = 0;
    for (const ShowcaseScore& s : ranked) {
        if (++rank > top) break;
        out += std::string(rank > 1 ? "," : "") + "\n  {\"id\": \"showcase_" + std::to_string(s.seed) + "\", \"rank\": " + std::to_string(rank) + ", \"seed\": " + std::to_string(s.seed)
            + ", \"size\": " + std::to_string(s.size) + ", \"score\": " + pointsText(s.totalMilli) + ", \"reasons\": [";
        const std::vector<std::string> reasons = showcaseReasons(s);
        for (std::size_t i = 0; i < reasons.size(); ++i) out += std::string(i ? ", " : "") + "\"" + jsonEscape(reasons[i]) + "\"";
        out += "]}";
    }
    return out + (rank > 0 ? "\n]\n" : "]\n");
}

namespace {

bool writeText(const std::filesystem::path& path, const std::string& text)
{
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (ec) return false;
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(f);
}

bool writeBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
{
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (ec) return false;
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(f);
}

}

ShowcaseResult runShowcase(const ShowcaseOptions& options, const WalkProbe& probe)
{
    ShowcaseResult result;
    if (options.seedEnd < options.seedBegin) {
        result.error = "--seeds range is empty";
        return result;
    }
    if (options.seedEnd - options.seedBegin >= kMaxShowcaseSeeds) {
        result.error = "--seeds covers more than " + std::to_string(kMaxShowcaseSeeds) + " seeds";
        return result;
    }
    if (options.size < worldforge::kTectonicsMinSize || options.size > worldforge::kTectonicsMaxSize) {
        result.error = "--size must be " + std::to_string(worldforge::kTectonicsMinSize) + " to " + std::to_string(worldforge::kTectonicsMaxSize);
        return result;
    }
    if (options.top < 1 || options.top > 100) {
        result.error = "--top must be 1 to 100";
        return result;
    }

    const worldforge::ForgeConfig config = defaultForgeConfig(options.size);
    const worldforge::GateThresholds thresholds = sanityThresholds();
    std::vector<ShowcaseScore> scores;
    for (std::uint64_t n = 0; n <= options.seedEnd - options.seedBegin; ++n) {
        const std::uint64_t seed = options.seedBegin + n;
        ShowcaseMetrics m;
        try {
            const worldforge::ForgedWorld world = worldforge::forgeWorld(seed, options.size, config, worldforge::ForgePhase::Content);
            WalkProbeResult walk;
            if (world.ok() && probe) walk = probe(world);
            m = measureShowcase(world, world.ok() ? worldforge::validateWorld(world, thresholds) : worldforge::ValidationReport{}, walk);
        } catch (const std::exception& e) {
            m = ShowcaseMetrics{};
            m.error = e.what();
        }
        m.seed = seed;
        m.size = options.size;
        scores.push_back(scoreShowcase(m));
        result.metrics.push_back(std::move(m));
    }
    result.ranked = rankShowcase(std::move(scores));
    result.csv = showcaseCsv(result.ranked, result.metrics);
    result.markdown = showcaseMarkdown(result.ranked, result.metrics, options.top);
    result.json = showcaseSeedsJson(result.ranked, options.top);

    if (!options.outDir.empty()) {
        const std::filesystem::path dir = options.outDir;
        if (!writeText(dir / "ranking.csv", result.csv) || !writeText(dir / "ranking.md", result.markdown)) {
            result.error = "cannot write into " + options.outDir;
            return result;
        }
        int rank = 0;
        for (const ShowcaseScore& s : result.ranked) {
            if (++rank > options.top) break;
            GalleryOptions gallery;
            gallery.seedBegin = s.seed;
            gallery.seedEnd = s.seed;
            gallery.size = options.size;
            const GalleryResult thumb = makeGallery(gallery);
            if (!thumb.error.empty() || !writeBytes(dir / "top" / ("rank" + std::to_string(rank) + "_seed" + std::to_string(s.seed) + ".png"), thumb.png)) {
                result.error = "cannot write the thumbnail of seed " + std::to_string(s.seed);
                return result;
            }
        }
    }
    if (!options.seedsJson.empty() && !writeText(options.seedsJson, result.json)) result.error = "cannot write " + options.seedsJson;
    return result;
}

int showcaseMain(int argc, char** argv, const WalkProbe& probe)
{
    const auto usage = [] {
        std::fputs("usage: ichor_forge_cli showcase --seeds A..B [--size N] [--top K] [--out DIR] [--json FILE]\n"
                   "  --seeds  inclusive seed range, or a single seed (default 1..200, at most 2000)\n"
                   "  --size   grid size per world (default 256)\n"
                   "  --top    how many best seeds to keep (default 5)\n"
                   "  --out    write ranking.csv, ranking.md and top/ thumbnails here (no window is opened)\n"
                   "  --json   write the top seeds as a data file (data/showcase_seeds.json)\n",
            stderr);
        return 2;
    };
    const auto parse = [](std::string_view text, std::uint64_t& out) {
        const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
        return ec == std::errc() && end == text.data() + text.size();
    };
    ShowcaseOptions options;
    for (int i = 2; i < argc; i += 2) {
        const std::string_view arg = argv[i];
        if (i + 1 >= argc) return usage();
        const std::string_view value = argv[i + 1];
        std::uint64_t n = 0;
        if (arg == "--seeds") {
            const std::size_t dots = value.find("..");
            bool ok = false;
            if (dots == std::string_view::npos) {
                ok = parse(value, options.seedBegin);
                options.seedEnd = options.seedBegin;
            } else {
                ok = parse(value.substr(0, dots), options.seedBegin) && parse(value.substr(dots + 2), options.seedEnd);
            }
            if (!ok) return usage();
        } else if (arg == "--size" && parse(value, n) && n <= 1000000) {
            options.size = static_cast<int>(n);
        } else if (arg == "--top" && parse(value, n) && n <= 1000000) {
            options.top = static_cast<int>(n);
        } else if (arg == "--out") {
            options.outDir = value;
        } else if (arg == "--json") {
            options.seedsJson = value;
        } else {
            return usage();
        }
    }
    const ShowcaseResult result = runShowcase(options, probe);
    if (!result.error.empty()) {
        std::fprintf(stderr, "forge_cli showcase: %s\n", result.error.c_str());
        return 1;
    }
    std::fputs(result.markdown.c_str(), stdout);
    return 0;
}

}
