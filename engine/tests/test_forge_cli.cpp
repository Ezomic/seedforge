#include <catch2/catch_test_macros.hpp>

#include <forgecli/forge.h>
#include <forgecli/png.h>

#include <nlohmann/json.hpp>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>

namespace {

using Bytes = std::vector<std::uint8_t>;

std::uint32_t be32(const Bytes& b, std::size_t at)
{
    return (std::uint32_t(b[at]) << 24) | (std::uint32_t(b[at + 1]) << 16) | (std::uint32_t(b[at + 2]) << 8) | b[at + 3];
}

// Reads back what encodePng wrote, checking every CRC and the Adler-32, and returns the
// unfiltered pixel bytes. Empty on any structural fault.
Bytes decodePng(const Bytes& png, int& w, int& h, int& channels)
{
    static const std::uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    if (png.size() < 8 || std::memcmp(png.data(), sig, 8) != 0) return {};
    Bytes z;
    bool sawEnd = false;
    for (std::size_t pos = 8; pos + 12 <= png.size() && !sawEnd;) {
        const std::uint32_t len = be32(png, pos);
        if (pos + 12 + len > png.size()) return {};
        const std::span<const std::uint8_t> body(png.data() + pos + 4, len + 4);
        if (forgecli::crc32(body) != be32(png, pos + 8 + len)) return {};
        const std::string type(reinterpret_cast<const char*>(png.data() + pos + 4), 4);
        if (type == "IHDR") {
            w = int(be32(png, pos + 8));
            h = int(be32(png, pos + 12));
            channels = png[pos + 17] == 0 ? 1 : 3;
        } else if (type == "IDAT") {
            z.insert(z.end(), png.begin() + long(pos + 8), png.begin() + long(pos + 8 + len));
        } else if (type == "IEND") {
            sawEnd = true;
        }
        pos += 12 + len;
    }
    if (!sawEnd || z.size() < 6) return {};
    Bytes raw;
    std::size_t p = 2;
    for (bool last = false; !last;) {
        if (p + 5 > z.size()) return {};
        last = (z[p] & 1) != 0;
        if ((z[p] & 6) != 0) return {};
        const std::size_t n = z[p + 1] | (z[p + 2] << 8);
        if ((z[p + 3] | (z[p + 4] << 8)) != (~n & 0xffff)) return {};
        p += 5;
        if (p + n > z.size()) return {};
        raw.insert(raw.end(), z.begin() + long(p), z.begin() + long(p + n));
        p += n;
    }
    if (p + 4 != z.size() || be32(z, p) != forgecli::adler32(raw)) return {};
    const std::size_t row = std::size_t(w) * std::size_t(channels);
    Bytes pixels;
    for (int y = 0; y < h; ++y) {
        const std::size_t at = std::size_t(y) * (row + 1);
        if (at + row + 1 > raw.size() || raw[at] != 0) return {};
        pixels.insert(pixels.end(), raw.begin() + long(at + 1), raw.begin() + long(at + 1 + row));
    }
    return pixels;
}

std::filesystem::path scratch(const char* name)
{
    const auto dir = std::filesystem::path(ICHOR_TEST_OUTPUT) / "forge_cli" / name;
    std::filesystem::remove_all(dir);
    return dir;
}

forgecli::RunOptions oneSeed(std::uint64_t seed, int size = 64)
{
    forgecli::RunOptions o;
    o.seedBegin = o.seedEnd = seed;
    o.size = size;
    return o;
}

}

TEST_CASE("crc32 and adler32 match their published check values", "[forge_cli]")
{
    const std::string s = "123456789";
    const Bytes b(s.begin(), s.end());
    CHECK(forgecli::crc32(b) == 0xcbf43926u);
    const std::string w = "Wikipedia";
    CHECK(forgecli::adler32(Bytes(w.begin(), w.end())) == 0x11e60398u);
}

TEST_CASE("encodePng round-trips greyscale and RGB, including multi-block images", "[forge_cli]")
{
    for (const int channels : {1, 3}) {
        for (const int size : {1, 7, 300}) {
            Bytes px(std::size_t(size) * size * channels);
            for (std::size_t i = 0; i < px.size(); ++i) px[i] = std::uint8_t(i * 31 + i / 251);
            const auto png = forgecli::encodePng(size, size, channels, px);
            REQUIRE(png);
            int w = 0, h = 0, c = 0;
            const Bytes back = decodePng(*png, w, h, c);
            REQUIRE(back == px);
            CHECK(w == size);
            CHECK(h == size);
            CHECK(c == channels);
        }
    }
    // 300 * 300 * 3 is more than one 65535 byte stored block.
}

TEST_CASE("encodePng rejects hostile dimensions and buffers", "[forge_cli]")
{
    const Bytes px(12, 0);
    CHECK_FALSE(forgecli::encodePng(0, 4, 1, px));
    CHECK_FALSE(forgecli::encodePng(4, 0, 1, px));
    CHECK_FALSE(forgecli::encodePng(-1, 4, 1, px));
    CHECK_FALSE(forgecli::encodePng(4, 3, 2, px));
    CHECK_FALSE(forgecli::encodePng(4, 3, 3, px));
    CHECK_FALSE(forgecli::encodePng(1 << 30, 1 << 30, 3, px));
    CHECK_FALSE(forgecli::encodePng(4, 3, 1, Bytes{}));
}

TEST_CASE("the registry names every phase once and finds them", "[forge_cli]")
{
    std::set<std::string_view> names;
    for (const auto& p : forgecli::phaseRegistry()) {
        CHECK(names.insert(p.name).second);
        CHECK(p.run != nullptr);
        CHECK(forgecli::findPhase(p.name) == &p);
    }
    CHECK(names.count("tectonics") == 1);
    CHECK(forgecli::findPhase("nope") == nullptr);
    CHECK(forgecli::findPhase("") == nullptr);
}

TEST_CASE("a run is reproducible and the layer hash follows the seed", "[forge_cli]")
{
    const auto a = forgecli::runForge(oneSeed(7));
    const auto b = forgecli::runForge(oneSeed(7));
    const auto c = forgecli::runForge(oneSeed(8));
    REQUIRE(a.error.empty());
    REQUIRE(a.seeds.size() == 1);
    REQUIRE(a.seeds[0].error.empty());
    CHECK(a.manifest == b.manifest);
    REQUIRE(a.seeds[0].layers.size() == c.seeds[0].layers.size());
    int differing = 0;
    for (std::size_t i = 0; i < a.seeds[0].layers.size(); ++i) {
        CHECK(a.seeds[0].layers[i].hash.size() == 16);
        if (a.seeds[0].layers[i].hash != c.seeds[0].layers[i].hash) ++differing;
    }
    CHECK(differing == int(a.seeds[0].layers.size()));
}

TEST_CASE("layer hash covers the pixels, the name and the dimensions", "[forge_cli]")
{
    forgecli::Layer l;
    l.name = "x";
    l.width = 2;
    l.height = 2;
    l.pixels = {1, 2, 3, 4};
    const auto base = forgecli::layerHash(l);
    auto pixel = l;
    pixel.pixels[3] = 5;
    auto name = l;
    name.name = "y";
    auto shape = l;
    shape.width = 4;
    shape.height = 1;
    CHECK(forgecli::layerHash(pixel) != base);
    CHECK(forgecli::layerHash(name) != base);
    CHECK(forgecli::layerHash(shape) != base);
}

TEST_CASE("the manifest is valid JSON that describes every layer and the files on disk", "[forge_cli]")
{
    auto options = oneSeed(3);
    options.outDir = scratch("manifest").string();
    const auto result = forgecli::runForge(options);
    REQUIRE(result.error.empty());

    std::ifstream f(std::filesystem::path(options.outDir) / "manifest.json");
    const std::string onDisk((std::istreambuf_iterator<char>(f)), {});
    CHECK(onDisk == result.manifest);

    const auto json = nlohmann::json::parse(result.manifest);
    CHECK(json["size"] == 64);
    REQUIRE(json["runs"].size() == 1);
    CHECK(json["runs"][0]["seed"] == 3);
    const auto& layers = json["runs"][0]["layers"];
    REQUIRE(layers.size() >= 4);
    for (const auto& layer : layers) {
        const auto file = std::filesystem::path(options.outDir) / layer["file"].get<std::string>();
        REQUIRE(std::filesystem::exists(file));
        std::ifstream in(file, std::ios::binary);
        const Bytes png((std::istreambuf_iterator<char>(in)), {});
        int w = 0, h = 0, c = 0;
        const Bytes px = decodePng(png, w, h, c);
        REQUIRE_FALSE(px.empty());
        CHECK(w == layer["width"].get<int>());
        CHECK(c == layer["channels"].get<int>());
        forgecli::Layer back;
        back.name = layer["name"].get<std::string>();
        back.width = w;
        back.height = h;
        back.channels = c;
        back.pixels = px;
        CHECK(forgecli::layerHash(back) == layer["hash"].get<std::string>());
    }
}

TEST_CASE("a layer image is not flat: the height field varies", "[forge_cli]")
{
    const auto result = forgecli::runForge(oneSeed(11, 128));
    REQUIRE(result.error.empty());
    const auto* tect = forgecli::findPhase("tectonics");
    const auto r = tect->run(11, 128);
    REQUIRE(r.error.empty());
    for (const auto& layer : r.layers) {
        std::set<std::uint8_t> distinct(layer.pixels.begin(), layer.pixels.end());
        CHECK(distinct.size() > 3);
    }
}

TEST_CASE("a batch covers the inclusive range and lays out one directory per seed", "[forge_cli]")
{
    auto options = oneSeed(10, 32);
    options.seedEnd = 13;
    options.phases = {"tectonics"};
    options.outDir = scratch("batch").string();
    const auto result = forgecli::runForge(options);
    REQUIRE(result.error.empty());
    REQUIRE(result.seeds.size() == 4);
    for (std::uint64_t s = 10; s <= 13; ++s) {
        CHECK(result.seeds[s - 10].seed == s);
        CHECK(std::filesystem::exists(std::filesystem::path(options.outDir) / ("seed_" + std::to_string(s)) / "height.png"));
    }
    CHECK_FALSE(std::filesystem::exists(std::filesystem::path(options.outDir) / "seed_14"));
    CHECK(nlohmann::json::parse(result.manifest)["runs"].size() == 4);
}

TEST_CASE("a phase subset runs only those phases", "[forge_cli]")
{
    auto options = oneSeed(1);
    options.phases = {"region_tiers"};
    const auto result = forgecli::runForge(options);
    REQUIRE(result.error.empty());
    REQUIRE(result.seeds[0].layers.size() == 1);
    CHECK(result.seeds[0].layers[0].name == "region_tier");
    CHECK(result.seeds[0].layers[0].file.empty());
}

TEST_CASE("bad options are reported, not crashed on", "[forge_cli]")
{
    auto unknown = oneSeed(1);
    unknown.phases = {"tectonics", "bogus"};
    CHECK(forgecli::runForge(unknown).error.find("bogus") != std::string::npos);

    auto empty = oneSeed(5);
    empty.seedEnd = 4;
    CHECK_FALSE(forgecli::runForge(empty).error.empty());

    auto huge = oneSeed(0);
    huge.seedEnd = ~std::uint64_t(0);
    const auto hugeResult = forgecli::runForge(huge);
    CHECK_FALSE(hugeResult.error.empty());
    CHECK(hugeResult.seeds.empty());

    auto edge = oneSeed(~std::uint64_t(0) - 1, 32);
    edge.seedEnd = ~std::uint64_t(0);
    edge.phases = {"tectonics"};
    const auto edgeResult = forgecli::runForge(edge);
    CHECK(edgeResult.error.empty());
    CHECK(edgeResult.seeds.size() == 2);
}

TEST_CASE("an out-of-range size fails that seed with a cause and writes nothing", "[forge_cli]")
{
    for (const int size : {-5, 0, 1, 15, 1025, 1 << 30}) {
        auto options = oneSeed(2, size);
        options.outDir = scratch("badsize").string();
        const auto result = forgecli::runForge(options);
        REQUIRE(result.error.empty());
        REQUIRE(result.seeds.size() == 1);
        CHECK(result.seeds[0].error.find("tectonics") != std::string::npos);
        CHECK(result.seeds[0].layers.empty());
        CHECK_FALSE(std::filesystem::exists(std::filesystem::path(options.outDir) / "seed_2"));
        CHECK(nlohmann::json::parse(result.manifest)["runs"][0].contains("error"));
    }
}

TEST_CASE("an unwritable output directory is an error", "[forge_cli]")
{
    const auto blocker = scratch("blocked");
    std::filesystem::create_directories(blocker.parent_path());
    std::ofstream(blocker) << "a file, not a directory";
    auto options = oneSeed(1, 32);
    options.outDir = (blocker / "sub").string();
    CHECK_FALSE(forgecli::runForge(options).error.empty());
}

TEST_CASE("phase errors with hostile characters stay valid JSON", "[forge_cli]")
{
    std::vector<forgecli::SeedRecord> seeds(1);
    seeds[0].error = "quote \" backslash \\ newline \n tab \t bell \x07";
    forgecli::RunOptions options;
    options.phases = {"a\"b"};
    const auto json = nlohmann::json::parse(forgecli::manifestJson(options, seeds));
    CHECK(json["runs"][0]["error"] == seeds[0].error);
    CHECK(json["phases"][0] == "a\"b");
}

TEST_CASE("duplicate phases are rejected", "[forge_cli]")
{
    auto options = oneSeed(1, 32);
    options.phases = {"tectonics", "tectonics"};
    CHECK(forgecli::runForge(options).error.find("duplicate") != std::string::npos);
}

TEST_CASE("a write failure keeps the manifest of the seeds already done", "[forge_cli]")
{
    auto options = oneSeed(1, 32);
    options.seedEnd = 2;
    options.phases = {"tectonics"};
    options.outDir = scratch("partial").string();
    std::filesystem::create_directories(std::filesystem::path(options.outDir) / "seed_2");
    std::filesystem::create_directories(std::filesystem::path(options.outDir) / "seed_2" / "plates.png");
    const auto result = forgecli::runForge(options);
    CHECK_FALSE(result.error.empty());
    CHECK(result.seeds.size() == 2);
    CHECK(nlohmann::json::parse(result.manifest)["runs"][0]["layers"].size() == 3);
}
