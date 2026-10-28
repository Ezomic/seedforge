#include "ichor/assets/bundle_io.h"
#include "ichor/assets/cooker.h"
#include "ichor/assets/image.h"
#include "ichor/assets/texture_cook.h"
#include "test_temp.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/geometric.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

using namespace ichor;

namespace {

using Bytes = std::vector<std::uint8_t>;

std::uint32_t crc32(const std::uint8_t* data, std::size_t size)
{
    std::uint32_t crc = 0xffffffffu;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

void putU32(Bytes& out, std::uint32_t v)
{
    for (int shift = 24; shift >= 0; shift -= 8) out.push_back(static_cast<std::uint8_t>(v >> shift));
}

void putChunk(Bytes& png, const char* type, const Bytes& body)
{
    putU32(png, static_cast<std::uint32_t>(body.size()));
    Bytes typed(type, type + 4);
    typed.insert(typed.end(), body.begin(), body.end());
    png.insert(png.end(), typed.begin(), typed.end());
    putU32(png, crc32(typed.data(), typed.size()));
}

// An 8-bit PNG with stored (uncompressed) deflate blocks. colorType 0 is greyscale, 6 is RGBA.
Bytes makePng(std::uint32_t w, std::uint32_t h, int colorType, const Bytes& pixels)
{
    const std::size_t channels = colorType == 0 ? 1 : 4;
    REQUIRE(pixels.size() == static_cast<std::size_t>(w) * h * channels);
    Bytes raw;
    for (std::uint32_t y = 0; y < h; ++y) {
        raw.push_back(0);
        raw.insert(raw.end(), pixels.begin() + static_cast<std::ptrdiff_t>(y * w * channels), pixels.begin() + static_cast<std::ptrdiff_t>((y + 1) * w * channels));
    }
    Bytes z = {0x78, 0x01};
    for (std::size_t at = 0; at < raw.size() || at == 0;) {
        const std::size_t n = std::min<std::size_t>(65535, raw.size() - at);
        const bool last = at + n >= raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(static_cast<std::uint8_t>(n & 255));
        z.push_back(static_cast<std::uint8_t>(n >> 8));
        z.push_back(static_cast<std::uint8_t>(~n & 255));
        z.push_back(static_cast<std::uint8_t>((~n >> 8) & 255));
        z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(at), raw.begin() + static_cast<std::ptrdiff_t>(at + n));
        at += n;
        if (last) break;
    }
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (std::uint8_t v : raw) {
        a = (a + v) % 65521;
        b = (b + a) % 65521;
    }
    putU32(z, (b << 16) | a);

    Bytes png = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    Bytes header;
    putU32(header, w);
    putU32(header, h);
    header.insert(header.end(), {8, static_cast<std::uint8_t>(colorType), 0, 0, 0});
    putChunk(png, "IHDR", header);
    putChunk(png, "IDAT", z);
    putChunk(png, "IEND", {});
    return png;
}

// Smooth colour with edges, fine ripples and a little noise: hard enough to show a bad encoder, easy enough for a good one.
Bytes colorPixels(std::uint32_t w, std::uint32_t h, std::uint8_t alpha = 255)
{
    Bytes px;
    std::uint32_t seed = 12345;
    for (std::uint32_t y = 0; y < h; ++y) {
        for (std::uint32_t x = 0; x < w; ++x) {
            seed = seed * 1664525u + 1013904223u;
            const float u = static_cast<float>(x) / static_cast<float>(w);
            const float v = static_cast<float>(y) / static_cast<float>(h);
            const float edge = ((x / 16 + y / 16) % 2 == 0) ? 0.12f : -0.12f;
            const float noise = (static_cast<float>(seed >> 24) / 255.0f - 0.5f) * 0.04f;
            const float r = 0.5f + 0.4f * std::sin(u * 6.0f) + edge + noise;
            const float g = 0.4f + 0.3f * v + 0.1f * std::sin((u + v) * 25.0f) + noise;
            const float b = 0.3f + 0.5f * u * v + edge * 0.5f + noise;
            px.push_back(static_cast<std::uint8_t>(std::clamp(r, 0.0f, 1.0f) * 255.0f));
            px.push_back(static_cast<std::uint8_t>(std::clamp(g, 0.0f, 1.0f) * 255.0f));
            px.push_back(static_cast<std::uint8_t>(std::clamp(b, 0.0f, 1.0f) * 255.0f));
            px.push_back(alpha);
        }
    }
    return px;
}

// A bumpy surface turned into tangent-space normals.
Bytes normalPixels(std::uint32_t w, std::uint32_t h)
{
    const auto height = [&](float x, float y) {
        return 0.06f * std::sin(x * 0.45f) * std::cos(y * 0.35f) + 0.03f * std::sin((x + y) * 0.9f);
    };
    Bytes px;
    for (std::uint32_t y = 0; y < h; ++y) {
        for (std::uint32_t x = 0; x < w; ++x) {
            const float fx = static_cast<float>(x);
            const float fy = static_cast<float>(y);
            const glm::vec3 n = glm::normalize(glm::vec3(-(height(fx + 1, fy) - height(fx - 1, fy)) * 6.0f, -(height(fx, fy + 1) - height(fx, fy - 1)) * 6.0f, 1.0f));
            for (int c = 0; c < 3; ++c) px.push_back(static_cast<std::uint8_t>(std::lround((n[c] * 0.5f + 0.5f) * 255.0f)));
            px.push_back(255);
        }
    }
    return px;
}

std::string base64(const Bytes& bytes)
{
    static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const std::uint32_t chunk = (bytes[i] << 16) | (i + 1 < bytes.size() ? bytes[i + 1] << 8 : 0) | (i + 2 < bytes.size() ? bytes[i + 2] : 0);
        out += kAlphabet[(chunk >> 18) & 63];
        out += kAlphabet[(chunk >> 12) & 63];
        out += i + 1 < bytes.size() ? kAlphabet[(chunk >> 6) & 63] : '=';
        out += i + 2 < bytes.size() ? kAlphabet[chunk & 63] : '=';
    }
    return out;
}

struct SynthMaterial {
    int base = -1;
    int metalRough = -1;
    int normal = -1;
};

// One triangle with a material per entry of `materials`, textures embedded as image buffer views.
std::string textureGltf(const std::vector<Bytes>& images, const std::vector<SynthMaterial>& materials)
{
    Bytes buffer;
    const auto append = [&buffer](const void* data, std::size_t size) {
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        buffer.insert(buffer.end(), bytes, bytes + size);
        while (buffer.size() % 4 != 0) buffer.push_back(0);
    };
    nlohmann::json views = nlohmann::json::array();
    const auto addView = [&](const void* data, std::size_t size) {
        views.push_back({{"buffer", 0}, {"byteOffset", buffer.size()}, {"byteLength", size}});
        append(data, size);
        return static_cast<int>(views.size()) - 1;
    };
    const float positions[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    const float normals[9] = {0, 0, 1, 0, 0, 1, 0, 0, 1};
    const float uvs[6] = {0, 0, 1, 0, 0, 1};
    const std::uint32_t indices[3] = {0, 1, 2};
    const int positionView = addView(positions, sizeof(positions));
    const int normalView = addView(normals, sizeof(normals));
    const int uvView = addView(uvs, sizeof(uvs));
    const int indexView = addView(indices, sizeof(indices));

    nlohmann::json root;
    root["asset"] = {{"version", "2.0"}};
    root["accessors"] = nlohmann::json::array({
        {{"bufferView", positionView}, {"componentType", 5126}, {"count", 3}, {"type", "VEC3"}, {"min", {0, 0, 0}}, {"max", {1, 1, 0}}},
        {{"bufferView", normalView}, {"componentType", 5126}, {"count", 3}, {"type", "VEC3"}},
        {{"bufferView", uvView}, {"componentType", 5126}, {"count", 3}, {"type", "VEC2"}},
        {{"bufferView", indexView}, {"componentType", 5125}, {"count", 3}, {"type", "SCALAR"}},
    });
    nlohmann::json primitives = nlohmann::json::array();
    for (std::size_t m = 0; m < std::max<std::size_t>(materials.size(), 1); ++m) {
        nlohmann::json primitive = {{"attributes", {{"POSITION", 0}, {"NORMAL", 1}, {"TEXCOORD_0", 2}}}, {"indices", 3}, {"mode", 4}};
        if (!materials.empty()) primitive["material"] = m;
        primitives.push_back(primitive);
    }
    root["meshes"] = nlohmann::json::array({{{"name", "tri"}, {"primitives", primitives}}});
    root["nodes"] = nlohmann::json::array({{{"name", "tri"}, {"mesh", 0}}});
    root["scenes"] = nlohmann::json::array({{{"nodes", {0}}}});
    root["scene"] = 0;

    nlohmann::json jsonImages = nlohmann::json::array();
    nlohmann::json jsonTextures = nlohmann::json::array();
    for (std::size_t i = 0; i < images.size(); ++i) {
        jsonImages.push_back({{"bufferView", addView(images[i].data(), images[i].size())}, {"mimeType", "image/png"}, {"name", "image" + std::to_string(i)}});
        jsonTextures.push_back({{"source", i}});
    }
    nlohmann::json jsonMaterials = nlohmann::json::array();
    for (const SynthMaterial& m : materials) {
        nlohmann::json material = {{"name", "m"}, {"pbrMetallicRoughness", nlohmann::json::object()}};
        if (m.base >= 0) material["pbrMetallicRoughness"]["baseColorTexture"] = {{"index", m.base}};
        if (m.metalRough >= 0) material["pbrMetallicRoughness"]["metallicRoughnessTexture"] = {{"index", m.metalRough}};
        if (m.normal >= 0) material["normalTexture"] = {{"index", m.normal}};
        jsonMaterials.push_back(material);
    }
    if (!images.empty()) {
        root["images"] = jsonImages;
        root["textures"] = jsonTextures;
    }
    if (!materials.empty()) root["materials"] = jsonMaterials;
    root["bufferViews"] = views;
    root["buffers"] = nlohmann::json::array({{{"byteLength", buffer.size()}, {"uri", "data:application/octet-stream;base64," + base64(buffer)}}});
    return root.dump();
}

// Cooks `gltf` written at <temp>/<relativeFolder>/<name>.gltf, so the budget sees the folder it lives in.
bool cookSynth(const std::string& gltf, const std::filesystem::path& relativeFolder, SceneBundle& bundle, const TextureBudget& budget = {})
{
    const std::filesystem::path root = test::tempPath("ichor_texbudget");
    const std::filesystem::path folder = root / relativeFolder;
    std::filesystem::create_directories(folder);
    const std::filesystem::path file = folder / "piece.gltf";
    std::ofstream(file, std::ios::binary) << gltf;
    CookOptions options;
    options.textureBudget = budget;
    CookStats stats;
    const bool ok = cookGltf(file, bundle, stats, options);
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    return ok;
}

double psnr(const Bytes& a, const Bytes& b, std::initializer_list<int> channels)
{
    REQUIRE(a.size() == b.size());
    double sum = 0.0;
    std::size_t n = 0;
    for (std::size_t i = 0; i < a.size(); i += 4) {
        for (int c : channels) {
            const double d = static_cast<double>(a[i + static_cast<std::size_t>(c)]) - static_cast<double>(b[i + static_cast<std::size_t>(c)]);
            sum += d * d;
            ++n;
        }
    }
    const double mse = sum / static_cast<double>(n);
    return mse == 0.0 ? 99.0 : 10.0 * std::log10(255.0 * 255.0 / mse);
}

Bytes mipZero(const Texture& rgba8)
{
    return Bytes(rgba8.pixels.begin(), rgba8.pixels.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(rgba8.width) * rgba8.height * 4));
}

TextureBudget tableBudget()
{
    TextureBudget budget;
    budget.defaultMax = 1024;
    budget.entries = {{"art/props/", 1024}, {"art/props/terrain/", 512}, {"art/exile/", 2048}};
    return budget;
}

}

TEST_CASE("the size table picks the longest matching prefix and falls back to the default", "[cooker][texture]")
{
    TextureBudget budget = tableBudget();
    budget.entries = {{"art/props/", 1024}, {"art/props/terrain/", 512}, {"art/exile/", 2048}, {"art/props/terrain/big/", 256}};
    CHECK(maxTextureSize(budget, "art/props/ruins/SM_A.glb") == 1024);
    CHECK(maxTextureSize(budget, "art/props/terrain/SM_B.glb") == 512);
    CHECK(maxTextureSize(budget, "art/props/terrain/big/SM_C.glb") == 256);
    CHECK(maxTextureSize(budget, "art/exile/SK_Exile.glb") == 2048);
    CHECK(maxTextureSize(budget, "art/unmatched/SM_D.glb") == 1024);
    CHECK(maxTextureSize(budget, "tests/data/kit.glb") == 1024);
    CHECK(maxTextureSize(budget, "SM_E.glb") == 1024);
    // An absolute path (how the app and ichor_cook see it) is read from its art/ folder on.
    CHECK(maxTextureSize(budget, "C:/work/seedforge/art/props/terrain/SM_B.glb") == 512);
    CHECK(maxTextureSize(budget, "C:\\work\\seedforge\\art\\exile\\SK_Exile.glb") == 2048);
    // A folder that merely starts with the same letters is not that folder.
    CHECK(maxTextureSize(budget, "art/props_old/SM_F.glb") == 1024);
    CHECK(maxTextureSize(budget, "art/props/terrainy/SM_G.glb") == 1024);
}

TEST_CASE("the size table file loads and a bad one is reported, not trusted", "[cooker][texture]")
{
    const auto good = test::tempPath("ichor_budget_good.json");
    std::ofstream(good) << R"({"default": 512, "prefixes": [{"prefix": "art/a/", "max": 2048}, {"prefix": "art/a/b/", "max": 128}]})";
    TextureBudget budget;
    std::vector<std::string> problems;
    REQUIRE(loadTextureBudget(good, budget, problems));
    CHECK(problems.empty());
    CHECK(budget.defaultMax == 512);
    CHECK(maxTextureSize(budget, "art/a/b/x.glb") == 128);
    std::filesystem::remove(good);

    const auto bad = test::tempPath("ichor_budget_bad.json");
    std::ofstream(bad) << R"({"default": 0, "prefixes": [{"prefix": "art/a/", "max": 0}, {"prefix": "", "max": 64}, {"prefix": "art/ok/", "max": 256}, {"max": 4}, {"prefix": "art/huge/", "max": 70000}]})";
    TextureBudget partial;
    problems.clear();
    REQUIRE(loadTextureBudget(bad, partial, problems));
    CHECK(problems.size() == 5);
    CHECK(partial.defaultMax == 1024);
    CHECK(partial.entries.size() == 1);
    CHECK(maxTextureSize(partial, "art/ok/x.glb") == 256);
    std::filesystem::remove(bad);

    CHECK_FALSE(loadTextureBudget(test::tempPath("ichor_budget_missing.json"), partial, problems));
    const auto garbage = test::tempPath("ichor_budget_garbage.json");
    std::ofstream(garbage) << "{not json";
    CHECK_FALSE(loadTextureBudget(garbage, partial, problems));
    std::filesystem::remove(garbage);
}

TEST_CASE("the cooker reads the shipped art/texture_budget.json, which maps the art folders", "[cooker][texture]")
{
    TextureBudget budget;
    std::vector<std::string> problems;
    REQUIRE(loadTextureBudget(std::filesystem::path(ICHOR_ART_DIR) / "texture_budget.json", budget, problems));
    CHECK(problems.empty());
    CHECK(budget.defaultMax == 1024);
    CHECK(maxTextureSize(budget, "art/props/ruins/SM_X.glb") == 1024);
    CHECK(maxTextureSize(budget, "art/props/terrain/SM_BoulderA.glb") == 512);
    CHECK(maxTextureSize(budget, "art/exile/SK_Exile.glb") == 2048);
    CHECK(maxTextureSize(budget, "art/monsters/bosses/SK_Boss.glb") == 2048);
}

TEST_CASE("cooking caps oversize textures by the folder and never upscales", "[cooker][texture]")
{
    const std::vector<Bytes> images = {
        makePng(2048, 1024, 6, colorPixels(2048, 1024)), // over every cap but exile
        makePng(256, 128, 6, colorPixels(256, 128)), // under every cap
    };
    const std::string gltf = textureGltf(images, {{.base = 0}, {.base = 1}});

    SceneBundle props;
    REQUIRE(cookSynth(gltf, "art/props/ruins", props, tableBudget()));
    REQUIRE(props.textures.size() == 2);
    CHECK(props.textures[0].width == 1024);
    CHECK(props.textures[0].height == 512);
    CHECK(props.textures[0].mipCount == mipLevelCount(1024, 512));
    CHECK(props.textures[1].width == 256);
    CHECK(props.textures[1].height == 128);

    SceneBundle terrain;
    REQUIRE(cookSynth(gltf, "art/props/terrain", terrain, tableBudget()));
    CHECK(terrain.textures[0].width == 512);
    CHECK(terrain.textures[0].height == 256);
    CHECK(terrain.textures[1].width == 256);

    SceneBundle hero;
    REQUIRE(cookSynth(gltf, "art/exile", hero, tableBudget()));
    CHECK(hero.textures[0].width == 2048);
    CHECK(hero.textures[0].height == 1024);

    SceneBundle unmatched;
    REQUIRE(cookSynth(gltf, "somewhere/else", unmatched, tableBudget()));
    CHECK(unmatched.textures[0].width == 1024);
}

TEST_CASE("each texture gets the format its use calls for", "[cooker][texture]")
{
    const std::vector<Bytes> images = {
        makePng(64, 64, 6, colorPixels(64, 64)), // 0 colour
        makePng(64, 64, 6, normalPixels(64, 64)), // 1 normal
        makePng(64, 64, 6, colorPixels(64, 64)), // 2 metal/rough
        makePng(32, 32, 0, Bytes(32 * 32, 128)), // 3 greyscale used as metal/rough
        makePng(32, 32, 0, Bytes(32 * 32, 200)), // 4 greyscale used as colour
        makePng(64, 64, 6, normalPixels(64, 64)), // 5 normal shared with metal/rough
        makePng(16, 16, 6, colorPixels(16, 16)), // 6 not used by any material
    };
    SceneBundle bundle;
    REQUIRE(cookSynth(textureGltf(images, {{.base = 0, .metalRough = 2, .normal = 1}, {.base = 4, .metalRough = 3}, {.metalRough = 5, .normal = 5}}), "art", bundle));
    REQUIRE(bundle.textures.size() == 7);
    CHECK(bundle.textures[0].format == TextureFormat::Bc7Srgb);
    CHECK(bundle.textures[1].format == TextureFormat::Bc5Unorm);
    CHECK(bundle.textures[2].format == TextureFormat::Bc7Unorm);
    CHECK(bundle.textures[3].format == TextureFormat::Bc4Unorm);
    CHECK(bundle.textures[4].format == TextureFormat::Bc7Srgb);
    CHECK(bundle.textures[5].format == TextureFormat::Bc7Unorm);
    CHECK(bundle.textures[6].format == TextureFormat::Bc7Unorm);
    for (const Texture& texture : bundle.textures) {
        CHECK(texture.pixels.size() == textureByteSize(texture.format, texture.width, texture.height, texture.mipCount));
    }
    CHECK(chooseTextureFormat(TextureUse::Color, true) == TextureFormat::Bc7Srgb);
    CHECK(chooseTextureFormat(TextureUse::Normal, true) == TextureFormat::Bc5Unorm);
    CHECK(chooseTextureFormat(TextureUse::Data, true) == TextureFormat::Bc4Unorm);
    CHECK(chooseTextureFormat(TextureUse::Data, false) == TextureFormat::Bc7Unorm);
}

TEST_CASE("odd sizes are padded to whole blocks and keep a correct mip chain", "[cooker][texture]")
{
    const std::array<std::pair<std::uint32_t, std::uint32_t>, 9> sizes{{{1, 1}, {2, 2}, {3, 5}, {4, 4}, {5, 3}, {30, 18}, {7, 13}, {100, 1}, {1, 64}}};
    std::vector<Bytes> images;
    for (const auto& [w, h] : sizes) images.push_back(makePng(w, h, 6, colorPixels(w, h)));
    std::vector<SynthMaterial> materials;
    for (int i = 0; i < static_cast<int>(images.size()); ++i) materials.push_back({.base = i});
    SceneBundle bundle;
    REQUIRE(cookSynth(textureGltf(images, materials), "art", bundle));
    REQUIRE(bundle.textures.size() == sizes.size());
    for (std::size_t i = 0; i < sizes.size(); ++i) {
        const Texture& t = bundle.textures[i];
        INFO(sizes[i].first << "x" << sizes[i].second);
        CHECK(t.width == sizes[i].first);
        CHECK(t.height == sizes[i].second);
        CHECK(t.mipCount == mipLevelCount(t.width, t.height));
        CHECK(t.pixels.size() == textureByteSize(t.format, t.width, t.height, t.mipCount));

        Texture rgba;
        REQUIRE(decompressTexture(t, rgba));
        CHECK(rgba.width == t.width);
        CHECK(rgba.mipCount == t.mipCount);
        CHECK(rgba.pixels.size() == textureByteSize(TextureFormat::Rgba8Srgb, t.width, t.height, t.mipCount));
    }
    // 1x1 is one block, 30x18 is 8x5 blocks.
    CHECK(mipByteSize(TextureFormat::Bc7Srgb, 1, 1) == 16);
    CHECK(mipByteSize(TextureFormat::Bc7Srgb, 30, 18) == 8 * 5 * 16);
    CHECK(mipByteSize(TextureFormat::Bc4Unorm, 5, 3) == 2 * 1 * 8);
}

TEST_CASE("colour survives BC7 within 38 dB, and opaque stays opaque", "[cooker][texture]")
{
    const Bytes source = colorPixels(128, 128);
    SceneBundle bundle;
    REQUIRE(cookSynth(textureGltf({makePng(128, 128, 6, source)}, {{.base = 0}}), "art", bundle));
    Texture rgba;
    REQUIRE(decompressTexture(bundle.textures[0], rgba));
    CHECK(rgba.format == TextureFormat::Rgba8Srgb);
    const Bytes decoded = mipZero(rgba);
    const double db = psnr(source, decoded, {0, 1, 2});
    INFO("colour PSNR " << db);
    CHECK(db >= 38.0);
    // BC7 mode 6 cannot store 255 in every block (its alpha is 7 bits plus a shared parity bit), so an opaque source may come back as 254.
    int lowest = 255;
    for (std::size_t i = 3; i < decoded.size(); i += 4) {
        lowest = std::min<int>(lowest, decoded[i]);
    }
    CHECK(lowest >= 254);
}

TEST_CASE("alpha is kept", "[cooker][texture]")
{
    Bytes source = colorPixels(64, 64);
    for (std::size_t i = 0; i < 64 * 64; ++i) source[i * 4 + 3] = static_cast<std::uint8_t>((i % 64) * 4);
    SceneBundle bundle;
    REQUIRE(cookSynth(textureGltf({makePng(64, 64, 6, source)}, {{.base = 0}}), "art", bundle));
    Texture rgba;
    REQUIRE(decompressTexture(bundle.textures[0], rgba));
    const Bytes decoded = mipZero(rgba);
    const double alphaDb = psnr(source, decoded, {3});
    INFO("alpha PSNR " << alphaDb);
    CHECK(alphaDb >= 38.0);
    CHECK(psnr(source, decoded, {0, 1, 2}) >= 34.0);
}

TEST_CASE("metallic roughness keeps its channels as data", "[cooker][texture]")
{
    const Bytes source = colorPixels(64, 64);
    SceneBundle bundle;
    REQUIRE(cookSynth(textureGltf({makePng(64, 64, 6, source)}, {{.metalRough = 0}}), "art", bundle));
    REQUIRE(bundle.textures[0].format == TextureFormat::Bc7Unorm);
    Texture rgba;
    REQUIRE(decompressTexture(bundle.textures[0], rgba));
    CHECK(rgba.format == TextureFormat::Rgba8Unorm);
    const double db = psnr(source, mipZero(rgba), {1, 2});
    INFO("metal/rough PSNR " << db);
    CHECK(db >= 38.0);
}

TEST_CASE("normals survive BC5 within a small angle", "[cooker][texture]")
{
    const Bytes source = normalPixels(128, 128);
    SceneBundle only;
    REQUIRE(cookSynth(textureGltf({makePng(128, 128, 6, source)}, {{.normal = 0}}), "art", only));
    REQUIRE(only.textures[0].format == TextureFormat::Bc5Unorm);
    Texture rgba;
    REQUIRE(decompressTexture(only.textures[0], rgba));
    const Bytes decoded = mipZero(rgba);
    double worst = 0.0;
    double total = 0.0;
    for (std::size_t i = 0; i < source.size(); i += 4) {
        glm::vec3 a;
        glm::vec3 b;
        for (int c = 0; c < 3; ++c) {
            a[c] = static_cast<float>(source[i + static_cast<std::size_t>(c)]) / 127.5f - 1.0f;
            b[c] = static_cast<float>(decoded[i + static_cast<std::size_t>(c)]) / 127.5f - 1.0f;
        }
        const double degrees = std::acos(std::clamp(static_cast<double>(glm::dot(glm::normalize(a), glm::normalize(b))), -1.0, 1.0)) * 180.0 / 3.14159265358979;
        worst = std::max(worst, degrees);
        total += degrees;
    }
    const double mean = total / static_cast<double>(source.size() / 4);
    INFO("normal angle mean " << mean << " worst " << worst);
    CHECK(mean < 2.5);
    CHECK(worst < 6.5);
}

TEST_CASE("a greyscale data texture goes through BC4 and reads back as grey", "[cooker][texture]")
{
    Bytes grey;
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) grey.push_back(static_cast<std::uint8_t>(x * 7 + y));
    }
    SceneBundle bundle;
    REQUIRE(cookSynth(textureGltf({makePng(32, 32, 0, grey)}, {{.metalRough = 0}}), "art", bundle));
    const Texture& t = bundle.textures[0];
    REQUIRE(t.format == TextureFormat::Bc4Unorm);
    CHECK(t.pixels.size() == textureByteSize(TextureFormat::Bc4Unorm, 32, 32, t.mipCount));
    Texture rgba;
    REQUIRE(decompressTexture(t, rgba));
    const Bytes decoded = mipZero(rgba);
    int worst = 0;
    for (std::size_t i = 0; i < 32 * 32; ++i) {
        CHECK(decoded[i * 4 + 0] == decoded[i * 4 + 1]);
        CHECK(decoded[i * 4 + 1] == decoded[i * 4 + 2]);
        CHECK(decoded[i * 4 + 3] == 255);
        worst = std::max(worst, std::abs(static_cast<int>(decoded[i * 4]) - static_cast<int>(grey[i])));
    }
    CHECK(worst <= 3);
}

TEST_CASE("the same cook gives the same bytes", "[cooker][texture]")
{
    const std::string gltf = textureGltf({makePng(96, 80, 6, colorPixels(96, 80)), makePng(96, 80, 6, normalPixels(96, 80))}, {{.base = 0, .normal = 1}});
    SceneBundle first;
    SceneBundle second;
    REQUIRE(cookSynth(gltf, "art", first));
    REQUIRE(cookSynth(gltf, "art", second));
    REQUIRE(first.textures.size() == 2);
    for (std::size_t i = 0; i < 2; ++i) {
        CHECK(first.textures[i].pixels == second.textures[i].pixels);
    }
}

TEST_CASE("a corrupt or truncated image fails the cook cleanly", "[cooker][texture]")
{
    const Bytes good = makePng(64, 64, 6, colorPixels(64, 64));
    Bytes truncated(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(good.size() / 2));
    Bytes flipped = good;
    for (std::size_t i = 40; i < flipped.size() - 20; i += 7) flipped[i] ^= 0x5a;
    const Bytes garbage = {'n', 'o', 't', ' ', 'a', ' ', 'p', 'n', 'g'};
    for (const Bytes& bad : {truncated, flipped, garbage, Bytes{}}) {
        SceneBundle bundle;
        const std::string gltf = textureGltf({bad.empty() ? Bytes{0} : bad}, {{.base = 0}});
        CHECK_FALSE(cookSynth(gltf, "art", bundle));
    }
}

TEST_CASE("a compressed bundle round-trips and an old version is refused", "[cooker][texture]")
{
    SceneBundle cooked;
    REQUIRE(cookSynth(textureGltf({makePng(64, 40, 6, colorPixels(64, 40)), makePng(64, 40, 6, normalPixels(64, 40))}, {{.base = 0, .normal = 1}}), "art", cooked));
    const auto path = test::tempPath("ichor_compressed.iscene");
    REQUIRE(writeBundle(path, cooked));
    SceneBundle loaded;
    REQUIRE(readBundle(path, loaded));
    REQUIRE(loaded.textures.size() == 2);
    for (std::size_t i = 0; i < 2; ++i) {
        CHECK(loaded.textures[i].format == cooked.textures[i].format);
        CHECK(loaded.textures[i].pixels == cooked.textures[i].pixels);
    }

    // The same file with the version a pre-compression cooker wrote.
    Bytes file;
    {
        std::ifstream in(path, std::ios::binary);
        file.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    std::uint32_t version = 0;
    std::memcpy(&version, file.data() + 4, 4);
    CHECK(version >= 4);
    const std::uint32_t old = 3;
    std::memcpy(file.data() + 4, &old, 4);
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
    }
    SceneBundle refused;
    CHECK_FALSE(readBundle(path, refused));
    std::filesystem::remove(path);
}

TEST_CASE("a bundle whose texture data does not match its format is refused", "[cooker][texture]")
{
    SceneBundle cooked;
    REQUIRE(cookSynth(textureGltf({makePng(64, 64, 6, colorPixels(64, 64))}, {{.base = 0}}), "art", cooked));
    const auto path = test::tempPath("ichor_badtexture.iscene");
    const auto readsBack = [&](const SceneBundle& bundle) {
        REQUIRE(writeBundle(path, bundle));
        SceneBundle loaded;
        return readBundle(path, loaded);
    };
    CHECK(readsBack(cooked));

    SceneBundle short_ = cooked;
    short_.textures[0].pixels.pop_back();
    CHECK_FALSE(readsBack(short_));

    SceneBundle wrongFormat = cooked;
    wrongFormat.textures[0].format = TextureFormat::Rgba8Srgb; // block data read as pixels
    CHECK_FALSE(readsBack(wrongFormat));

    SceneBundle unknown = cooked;
    unknown.textures[0].format = static_cast<TextureFormat>(99);
    CHECK_FALSE(readsBack(unknown));

    SceneBundle huge = cooked;
    huge.textures[0].width = 1u << 20;
    huge.textures[0].mipCount = mipLevelCount(huge.textures[0].width, huge.textures[0].height);
    CHECK_FALSE(readsBack(huge));
    std::filesystem::remove(path);
}

TEST_CASE("a device without block compression gets the RGBA8 decode", "[cooker][texture]")
{
    SceneBundle bundle;
    const Bytes source = colorPixels(60, 36);
    REQUIRE(cookSynth(textureGltf({makePng(60, 36, 6, source)}, {{.base = 0}}), "art", bundle));
    const Texture& compressed = bundle.textures[0];
    REQUIRE(isBlockCompressed(compressed.format));

    Texture scratch;
    const Texture* supported = textureForDevice(compressed, true, scratch);
    CHECK(supported == &compressed);
    CHECK(scratch.pixels.empty());

    const Texture* fallback = textureForDevice(compressed, false, scratch);
    REQUIRE(fallback == &scratch);
    CHECK(fallback->format == TextureFormat::Rgba8Srgb);
    CHECK(fallback->width == compressed.width);
    CHECK(fallback->height == compressed.height);
    CHECK(fallback->mipCount == compressed.mipCount);
    CHECK(fallback->pixels.size() == textureByteSize(TextureFormat::Rgba8Srgb, 60, 36, compressed.mipCount));
    CHECK(psnr(source, mipZero(*fallback), {0, 1, 2}) >= 38.0);

    // Already uncompressed data is passed through untouched on either kind of device.
    Texture plain;
    plain.width = 2;
    plain.height = 2;
    plain.mipCount = 2;
    plain.format = TextureFormat::Rgba8Unorm;
    plain.pixels.assign(16 + 4, 7);
    Texture unused;
    CHECK(textureForDevice(plain, false, unused) == &plain);
    CHECK(textureForDevice(plain, true, unused) == &plain);

    // A texture that claims to be compressed but is cut short cannot be decoded.
    Texture broken = compressed;
    broken.pixels.resize(broken.pixels.size() / 2);
    CHECK(textureForDevice(broken, false, unused) == nullptr);
}

TEST_CASE("the cooked kit keeps its textures usable", "[cooker][texture]")
{
    SceneBundle bundle;
    CookStats stats;
    REQUIRE(cookGltf(std::filesystem::path(ICHOR_TEST_DATA) / "kit.glb", bundle, stats));
    REQUIRE_FALSE(bundle.textures.empty());
    for (const Texture& texture : bundle.textures) {
        CHECK(isBlockCompressed(texture.format));
        Texture rgba;
        REQUIRE(decompressTexture(texture, rgba));
        CHECK(rgba.pixels.size() == textureByteSize(rgba.format, rgba.width, rgba.height, rgba.mipCount));
    }
}
