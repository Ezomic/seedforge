#include "ichor/assets/bundle_io.h"
#include "ichor/assets/cooker.h"
#include "ichor/assets/image.h"
#include "test_temp.h"

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

using namespace ichor;

namespace {

const std::filesystem::path kKit = std::filesystem::path(ICHOR_TEST_DATA) / "kit.glb";

}

TEST_CASE("cooker turns the hoplite kit into a bundle")
{
    SceneBundle bundle;
    CookStats stats;
    REQUIRE(cookGltf(kKit, bundle, stats));

    // dory (butt, head, shaft), helmet (crest, skull), aspis, ground
    REQUIRE(bundle.meshes.size() == 7);
    REQUIRE(bundle.nodes.size() == 7);
    REQUIRE(bundle.materials.size() == 7);
    REQUIRE(bundle.textures.size() == 4);
    REQUIRE(stats.triangles > 1000);
    REQUIRE(stats.verticesOut <= stats.verticesIn);

    for (const Mesh& mesh : bundle.meshes) {
        REQUIRE_FALSE(mesh.vertices.empty());
        REQUIRE(mesh.indices.size() % 3 == 0);
        REQUIRE_FALSE(mesh.submeshes.empty());
        for (std::uint32_t index : mesh.indices) {
            REQUIRE(index < mesh.vertices.size());
        }
        for (const Submesh& sub : mesh.submeshes) {
            REQUIRE(sub.material < bundle.materials.size());
            REQUIRE(sub.firstIndex + sub.indexCount <= mesh.indices.size());
        }
        REQUIRE(mesh.boundsMin.x <= mesh.boundsMax.x);
    }
    for (const Material& material : bundle.materials) {
        REQUIRE(material.baseColorTexture < bundle.textures.size());
        REQUIRE(bundle.textures[material.baseColorTexture].format == TextureFormat::Rgba8Srgb);
    }
    for (const Texture& texture : bundle.textures) {
        REQUIRE(texture.width == 64);
        REQUIRE(texture.mipCount == 7);
        std::size_t expected = 0;
        for (std::uint32_t w = 64, h = 64;; w = std::max(1u, w / 2), h = std::max(1u, h / 2)) {
            expected += static_cast<std::size_t>(w) * h * 4;
            if (w == 1 && h == 1) break;
        }
        REQUIRE(texture.pixels.size() == expected);
    }
    for (const Node& node : bundle.nodes) {
        REQUIRE(node.mesh < bundle.meshes.size());
    }
}

TEST_CASE("bundle round-trips through the binary format")
{
    SceneBundle cooked;
    CookStats stats;
    REQUIRE(cookGltf(kKit, cooked, stats));

    const auto path = test::tempPath("ichor_roundtrip.iscene");
    REQUIRE(writeBundle(path, cooked));

    SceneBundle loaded;
    REQUIRE(readBundle(path, loaded));
    std::filesystem::remove(path);

    REQUIRE(loaded.meshes.size() == cooked.meshes.size());
    REQUIRE(loaded.textures.size() == cooked.textures.size());
    REQUIRE(loaded.materials.size() == cooked.materials.size());
    REQUIRE(loaded.nodes.size() == cooked.nodes.size());
    for (std::size_t i = 0; i < cooked.meshes.size(); ++i) {
        REQUIRE(loaded.meshes[i].name == cooked.meshes[i].name);
        REQUIRE(loaded.meshes[i].indices == cooked.meshes[i].indices);
        REQUIRE(loaded.meshes[i].vertices.size() == cooked.meshes[i].vertices.size());
        REQUIRE(std::memcmp(loaded.meshes[i].vertices.data(), cooked.meshes[i].vertices.data(), cooked.meshes[i].vertices.size() * sizeof(Vertex)) == 0);
    }
    for (std::size_t i = 0; i < cooked.textures.size(); ++i) {
        REQUIRE(loaded.textures[i].pixels == cooked.textures[i].pixels);
    }
    for (std::size_t i = 0; i < cooked.nodes.size(); ++i) {
        REQUIRE(loaded.nodes[i].world == cooked.nodes[i].world);
    }
}

TEST_CASE("reading garbage is refused")
{
    const auto path = test::tempPath("ichor_garbage.iscene");
    REQUIRE(writePng(path, 2, 2, std::vector<std::uint8_t>(16, 0).data()));
    SceneBundle bundle;
    REQUIRE_FALSE(readBundle(path, bundle));
    std::filesystem::remove(path);
}

TEST_CASE("mip chains average sRGB in linear light")
{
    // A 2x2 checker of pure white and pure black. Its 1x1 mip is mid grey in
    // linear light, which is 188 in sRGB; a data texture averages to 128.
    const auto path = test::tempPath("ichor_checker.png");
    const std::uint8_t checker[16] = {255, 255, 255, 255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255, 255};
    REQUIRE(writePng(path, 2, 2, checker));
    std::ifstream in(path, std::ios::binary);
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    std::filesystem::remove(path);

    Texture srgb;
    REQUIRE(decodeTexture(bytes.data(), bytes.size(), TextureFormat::Rgba8Srgb, srgb));
    REQUIRE(srgb.mipCount == 2);
    REQUIRE(srgb.pixels.size() == 16 + 4);
    REQUIRE(srgb.pixels[16] == 188);
    REQUIRE(srgb.pixels[19] == 255);

    Texture data;
    REQUIRE(decodeTexture(bytes.data(), bytes.size(), TextureFormat::Rgba8Unorm, data));
    REQUIRE(data.pixels[16] == 128);
}
