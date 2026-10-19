#include <forgecli/forge.h>
#include <forgecli/gallery.h>
#include <forgecli/png.h>

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string_view>

namespace forgecli {

namespace {

constexpr int kGutter = 2;
constexpr int kLabelScale = 2;
constexpr int kLabelHeight = 5 * kLabelScale + 4;
constexpr int kCaptionHeight = 5 + 3;
constexpr std::uint8_t kBackground[3] = {20, 20, 20};
constexpr std::uint8_t kFailed[3] = {120, 20, 20};
constexpr std::uint8_t kRiver[3] = {40, 110, 230};
constexpr std::uint8_t kLabelInk[3] = {235, 235, 235};
constexpr std::uint8_t kPassInk[3] = {110, 200, 120};
constexpr std::uint8_t kFailInk[3] = {240, 150, 60};
constexpr int kLitBase = 160;
constexpr std::uint64_t kMaxCanvasBytes = 128ull << 20;

// 3x5 digits, one row per entry, bit 2 is the left column.
constexpr std::uint8_t kDigits[10][5] = {
    {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7}, {5, 5, 7, 1, 1},
    {7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 1, 1, 1}, {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7},
};

// 3x5 glyphs for the caption: '/', '-' and 'X'.
constexpr std::uint8_t kSlash[5] = {1, 1, 2, 4, 4};
constexpr std::uint8_t kDash[5] = {0, 0, 7, 0, 0};
constexpr std::uint8_t kCross[5] = {5, 5, 2, 5, 5};

const std::uint8_t* glyph(char c)
{
    if (c >= '0' && c <= '9') return kDigits[c - '0'];
    if (c == '/') return kSlash;
    if (c == '-') return kDash;
    if (c == 'X') return kCross;
    return nullptr;
}

struct Canvas {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;

    void set(int x, int y, const std::uint8_t* rgb)
    {
        const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 3;
        pixels[i] = rgb[0];
        pixels[i + 1] = rgb[1];
        pixels[i + 2] = rgb[2];
    }
};

void drawText(Canvas& canvas, int x0, int y0, int maxWidth, const std::string& text, int scale, const std::uint8_t* ink)
{
    int x = x0 + 2;
    for (const char c : text) {
        if (x + 3 * scale > x0 + maxWidth) break;
        const std::uint8_t* rows = glyph(c);
        if (rows) {
            for (int row = 0; row < 5; ++row) {
                for (int col = 0; col < 3; ++col) {
                    if (!(rows[row] & (4 >> col))) continue;
                    for (int dy = 0; dy < scale; ++dy) {
                        for (int dx = 0; dx < scale; ++dx) canvas.set(x + col * scale + dx, y0 + row * scale + dy, ink);
                    }
                }
            }
        }
        x += 4 * scale;
    }
}

void fillRect(Canvas& canvas, int x0, int y0, int w, int h, const std::uint8_t* rgb)
{
    for (int y = y0; y < y0 + h; ++y) {
        for (int x = x0; x < x0 + w; ++x) canvas.set(x, y, rgb);
    }
}

std::uint8_t shaded(std::uint8_t channel, int shade)
{
    return static_cast<std::uint8_t>(std::clamp(channel * shade / kLitBase, 0, 255));
}

// Light from the north-west: a cell whose south-east neighbour is higher than its north-west one is lit.
bool drawWorld(Canvas& canvas, int x0, int y0, const worldforge::ForgedWorld& world)
{
    if (world.completedPhases() < static_cast<int>(worldforge::ForgePhase::Biomes)) return false;
    const PhaseResult biomes = phaseLayers(*findPhase("biomes"), world);
    if (!biomes.error.empty()) return false;
    const int size = world.size();
    if (biomes.layers.empty() || biomes.layers[0].width != size || biomes.layers[0].height != size || biomes.layers[0].channels != 3) return false;
    const Layer& color = biomes.layers[0];
    const auto& hydro = *world.hydrology();
    const auto height = [&](int x, int y) { return static_cast<int>(hydro.heightM[hydro.index(std::clamp(x, 0, size - 1), std::clamp(y, 0, size - 1))]); };
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const std::size_t i = hydro.index(x, y);
            std::uint8_t rgb[3];
            if (hydro.river[i]) {
                std::copy(std::begin(kRiver), std::end(kRiver), rgb);
            } else {
                const int slope = height(x + 1, y + 1) - height(x - 1, y - 1);
                const int shade = std::clamp(kLitBase + slope / 3, 60, 255);
                for (int c = 0; c < 3; ++c) rgb[c] = shaded(color.pixels[i * 3 + static_cast<std::size_t>(c)], shade);
            }
            canvas.set(x0 + x, y0 + y, rgb);
        }
    }
    return true;
}

}

SanityRow sanityRow(const worldforge::ValidationReport& report)
{
    SanityRow row;
    row.seed = report.seed;
    row.error = report.error;
    for (std::size_t i = 0; i < report.gates.size(); ++i) {
        const worldforge::GateResult& g = report.gates[i];
        row.gateIds.push_back(g.id);
        if (g.status == worldforge::GateStatus::Skipped) continue;
        ++row.evaluated;
        if (g.status == worldforge::GateStatus::Pass) {
            ++row.passed;
        } else {
            row.failedGates.push_back(g.id);
            row.failedIndexes.push_back(static_cast<int>(i) + 1);
        }
    }
    return row;
}

std::string sanityCaption(const SanityRow& row)
{
    if (!row.error.empty()) return "X";
    std::string text = std::to_string(row.passed) + "/" + std::to_string(row.evaluated);
    for (const int index : row.failedIndexes) text += " " + std::to_string(index);
    return text;
}

std::string sanitySummary(const std::vector<SanityRow>& rows)
{
    std::string out;
    for (const SanityRow& row : rows) {
        out += "seed " + std::to_string(row.seed) + " ";
        out += row.error.empty() ? std::to_string(row.passed) + "/" + std::to_string(row.evaluated) : std::string("invalid thresholds");
        out += row.error.empty() && row.failedGates.empty() ? " PASS" : " FAIL";
        for (const std::string& id : row.failedGates) out += " " + id;
        out += "\n";
    }
    if (!rows.empty()) {
        out += "caption: passed/evaluated gates, then the failed gate numbers:";
        for (std::size_t i = 0; i < rows.front().gateIds.size(); ++i) out += " " + std::to_string(i + 1) + "=" + rows.front().gateIds[i];
        out += "\n";
    }
    return out;
}

GalleryResult makeGallery(const GalleryOptions& options)
{
    GalleryResult result;
    if (options.seedEnd < options.seedBegin) {
        result.error = "--seeds range is empty";
        return result;
    }
    if (options.seedEnd - options.seedBegin >= kMaxGallerySeeds) {
        result.error = "--seeds covers more than " + std::to_string(kMaxGallerySeeds) + " seeds";
        return result;
    }
    if (options.size < worldforge::kTectonicsMinSize || options.size > worldforge::kTectonicsMaxSize) {
        result.error = "--size must be " + std::to_string(worldforge::kTectonicsMinSize) + " to " + std::to_string(worldforge::kTectonicsMaxSize);
        return result;
    }
    const int count = static_cast<int>(options.seedEnd - options.seedBegin) + 1;
    int columns = 1;
    while (columns * columns < count) ++columns;
    const int rows = (count + columns - 1) / columns;
    const int cellW = options.size;
    const int cellH = options.size + kLabelHeight + (options.sanity ? kCaptionHeight : 0);

    Canvas canvas;
    canvas.width = kGutter + columns * (cellW + kGutter);
    canvas.height = kGutter + rows * (cellH + kGutter);
    if (static_cast<std::uint64_t>(canvas.width) * static_cast<std::uint64_t>(canvas.height) * 3 > kMaxCanvasBytes) {
        result.error = "the gallery would be larger than 128 MiB, use fewer seeds or a smaller --size";
        return result;
    }
    canvas.pixels.assign(static_cast<std::size_t>(canvas.width) * static_cast<std::size_t>(canvas.height) * 3, 0);
    fillRect(canvas, 0, 0, canvas.width, canvas.height, kBackground);

    for (int n = 0; n < count; ++n) {
        const std::uint64_t seed = options.seedBegin + static_cast<std::uint64_t>(n);
        const int x0 = kGutter + (n % columns) * (cellW + kGutter);
        const int y0 = kGutter + (n / columns) * (cellH + kGutter);
        bool drawn = false;
        std::optional<SanityRow> row;
        try {
            const worldforge::ForgedWorld world = worldforge::forgeWorld(seed, options.size, options.config ? *options.config : defaultForgeConfig(options.size),
                options.sanity ? worldforge::ForgePhase::Content : worldforge::ForgePhase::Biomes);
            drawn = drawWorld(canvas, x0, y0, world);
            if (drawn && options.sanity) row = sanityRow(worldforge::validateWorld(world, options.thresholds ? *options.thresholds : sanityThresholds()));
        } catch (const std::exception&) {
        }
        if (!drawn) {
            fillRect(canvas, x0, y0, cellW, options.size, kFailed);
            result.failedSeeds.push_back(seed);
        }
        drawText(canvas, x0, y0 + options.size + 2, cellW, std::to_string(seed), kLabelScale, kLabelInk);
        if (row) {
            row->seed = seed;
            drawText(canvas, x0, y0 + options.size + kLabelHeight, cellW, sanityCaption(*row), 1, row->error.empty() && row->failedGates.empty() ? kPassInk : kFailInk);
            result.sanity.push_back(std::move(*row));
        }
    }

    auto png = encodePng(canvas.width, canvas.height, 3, canvas.pixels);
    if (!png) {
        result.error = "gallery image is too large";
        return result;
    }
    result.png = std::move(*png);
    result.width = canvas.width;
    result.height = canvas.height;
    return result;
}

int galleryMain(int argc, char** argv)
{
    const auto usage = [] {
        std::fputs("usage: ichor_forge_cli gallery --seeds A..B [--size N] [--out FILE]\n"
                   "  --seeds  inclusive seed range, or a single seed (at most 256)\n"
                   "  --size   grid size per world, also the cell width in pixels (default 128)\n"
                   "  --out    PNG to write (default gallery.png)\n",
            stderr);
        return 2;
    };
    const auto parse = [](std::string_view text, std::uint64_t& out) {
        const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
        return ec == std::errc() && end == text.data() + text.size();
    };
    GalleryOptions options;
    std::string out = "gallery.png";
    bool haveSeeds = false;
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
            haveSeeds = true;
        } else if (arg == "--size" && parse(value, n) && n <= 1000000) {
            options.size = static_cast<int>(n);
        } else if (arg == "--out") {
            out = value;
        } else {
            return usage();
        }
    }
    if (!haveSeeds) return usage();

    const GalleryResult result = makeGallery(options);
    if (!result.error.empty()) {
        std::fprintf(stderr, "forge_cli gallery: %s\n", result.error.c_str());
        return 1;
    }
    const std::filesystem::path path = out;
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (ec) file.setstate(std::ios::failbit);
    file.write(reinterpret_cast<const char*>(result.png.data()), static_cast<std::streamsize>(result.png.size()));
    if (!file) {
        std::fprintf(stderr, "forge_cli gallery: cannot write %s\n", out.c_str());
        return 1;
    }
    std::printf("%s %dx%d\n", out.c_str(), result.width, result.height);
    std::fputs(sanitySummary(result.sanity).c_str(), stdout);
    for (const std::uint64_t seed : result.failedSeeds) std::fprintf(stderr, "seed %llu failed to forge\n", static_cast<unsigned long long>(seed));
    return result.failedSeeds.empty() ? 0 : 1;
}

}
