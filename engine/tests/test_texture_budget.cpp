#include "ichor/assets/bundle.h"
#include "ichor/assets/cooker.h"
#include "ichor/gfx/null_renderer.h"
#include "ichor/gfx/texture_residency.h"
#include "ichor/loop/engine.h"
#include "ichor/platform/null_platform.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <vector>

using namespace ichor;

namespace {

constexpr std::uint64_t kMiB = 1024 * 1024;

// A 2048 square BC7 chain: 12 levels, the tail from the 64 level down.
constexpr std::uint32_t kEdge = 2048;
constexpr std::uint32_t kMips = 12;
const std::uint64_t kFull = mipRangeByteSize(TextureFormat::Bc7Srgb, kEdge, kEdge, kMips, 0);
const std::uint64_t kTail = mipRangeByteSize(TextureFormat::Bc7Srgb, kEdge, kEdge, kMips, mipTailStart(kEdge, kEdge, kMips));

std::uint32_t addBig(TextureResidency& residency, std::vector<std::uint32_t>& evicted)
{
    return residency.add(kFull, kTail, evicted);
}

}

TEST_CASE("the mip tail starts at the first level no larger than 64 and always keeps the last level")
{
    REQUIRE(mipTailStart(2048, 2048, 12) == 5);
    REQUIRE(mipTailStart(2048, 512, 12) == 5);
    REQUIRE(mipTailStart(64, 64, 7) == 0);
    REQUIRE(mipTailStart(32, 8, 6) == 0);
    REQUIRE(mipTailStart(2048, 2048, 1) == 0);
    REQUIRE(mipTailStart(2048, 2048, 3) == 2);
    REQUIRE(kTail == 16 * 16 * 16 + 8 * 8 * 16 + 4 * 4 * 16 + 2 * 2 * 16 + 3 * 16);
    REQUIRE(kTail < kFull);
    REQUIRE(mipRangeByteSize(TextureFormat::Rgba8Unorm, 8, 8, 4, 0) == (64 + 16 + 4 + 1) * 4);
}

TEST_CASE("over the cap the least recently drawn texture is evicted and only its tail stays resident")
{
    TextureResidency residency(2 * kFull + kTail);
    std::vector<std::uint32_t> evicted;
    const std::uint32_t a = addBig(residency, evicted);
    const std::uint32_t b = addBig(residency, evicted);
    REQUIRE(evicted.empty());
    REQUIRE(residency.usedBytes() == 2 * kFull);

    residency.beginFrame(1.0 / 60.0);
    residency.touch(a, evicted);
    residency.beginFrame(1.0 / 60.0);
    residency.touch(b, evicted);
    residency.beginFrame(1.0 / 60.0);
    residency.touch(a, evicted);
    REQUIRE(evicted.empty());

    // A third full image does not fit beside two: b was drawn longer ago than a, so b goes.
    const std::uint32_t c = addBig(residency, evicted);
    REQUIRE(evicted == std::vector<std::uint32_t>{b});
    REQUIRE(residency.isFull(a));
    REQUIRE_FALSE(residency.isFull(b));
    REQUIRE(residency.isFull(c));
    REQUIRE(residency.usedBytes() == 2 * kFull + kTail);
    REQUIRE(residency.usedBytes() <= residency.cap());

    const TextureResidencyStats stats = residency.stats();
    REQUIRE(stats.evictions == 1);
    REQUIRE(stats.evicted == 1);
    REQUIRE(stats.textures == 3);
    REQUIRE(stats.reloads == 0);
}

TEST_CASE("an evicted texture is reloaded in full the frame it is drawn again, at the cost of the least recently drawn")
{
    TextureResidency residency(2 * kFull + 2 * kTail);
    std::vector<std::uint32_t> evicted;
    const std::uint32_t a = addBig(residency, evicted);
    const std::uint32_t b = addBig(residency, evicted);
    const std::uint32_t c = addBig(residency, evicted);
    REQUIRE(evicted == std::vector<std::uint32_t>{a});

    residency.beginFrame(1.0 / 60.0);
    evicted.clear();
    REQUIRE(residency.touch(a, evicted));
    REQUIRE(residency.isFull(a));
    REQUIRE(evicted == std::vector<std::uint32_t>{b}); // b and c were added together, so the lower id goes first
    REQUIRE(residency.isFull(c));
    REQUIRE(residency.usedBytes() == 2 * kFull + kTail);

    // Drawn again in the same frame, a stays full and costs nothing more.
    evicted.clear();
    REQUIRE_FALSE(residency.touch(a, evicted));
    REQUIRE(evicted.empty());
    REQUIRE(residency.stats().reloads == 1);
}

TEST_CASE("a texture whose tail is its whole image is never evicted")
{
    TextureResidency residency(kTail);
    std::vector<std::uint32_t> evicted;
    const std::uint64_t small = mipRangeByteSize(TextureFormat::Bc7Srgb, 64, 64, 7, 0);
    residency.add(small, small, evicted);
    residency.add(small, small, evicted);
    REQUIRE(evicted.empty());
    REQUIRE(residency.usedBytes() == 2 * small);
    REQUIRE(residency.usedBytes() > residency.cap());
    REQUIRE(residency.stats().evictions == 0);
}

TEST_CASE("at most a few evicted textures are reloaded per frame, and the rest wait for the next")
{
    TextureResidency residency(kTail);
    std::vector<std::uint32_t> evicted;
    for (int i = 0; i < 5; ++i) {
        addBig(residency, evicted);
    }
    REQUIRE(residency.stats().evicted == 4); // the last added is kept; nothing else fits

    residency.beginFrame(1.0 / 60.0);
    int reloaded = 0;
    for (std::uint32_t id = 0; id < 5; ++id) {
        evicted.clear();
        reloaded += residency.touch(id, evicted) ? 1 : 0;
    }
    REQUIRE(reloaded == static_cast<int>(kTextureReloadsPerFrame));
    // What came back this frame is not thrown out again in it, so the frame ends over the cap rather than thrashing.
    REQUIRE(residency.stats().evicted == 5 - static_cast<int>(kTextureReloadsPerFrame));

    residency.beginFrame(1.0 / 60.0);
    reloaded = 0;
    for (std::uint32_t id = 0; id < 5; ++id) {
        evicted.clear();
        reloaded += residency.touch(id, evicted) ? 1 : 0;
    }
    REQUIRE(reloaded == static_cast<int>(kTextureReloadsPerFrame));
    REQUIRE(residency.stats().reloads == 2 * kTextureReloadsPerFrame);
}

TEST_CASE("the eviction rate is the evictions over the last whole second of frames")
{
    TextureResidency residency(kFull);
    std::vector<std::uint32_t> evicted;
    addBig(residency, evicted);
    addBig(residency, evicted);
    REQUIRE(residency.stats().evictionsPerSecond == 0.0);
    for (int frame = 0; frame < 60; ++frame) {
        residency.beginFrame(1.0 / 60.0);
        residency.touch(static_cast<std::uint32_t>(frame % 2), evicted);
    }
    const double rate = residency.stats().evictionsPerSecond;
    REQUIRE(rate > 50.0);
    REQUIRE(rate <= 61.0);
}

TEST_CASE("a failed upload forgets the textures it had registered")
{
    TextureResidency residency(10 * kFull);
    std::vector<std::uint32_t> evicted;
    addBig(residency, evicted);
    addBig(residency, evicted);
    addBig(residency, evicted);
    residency.truncate(1);
    REQUIRE(residency.usedBytes() == kFull);
    REQUIRE(residency.stats().textures == 1);
}

// Three meshes, each with its own 256 square RGBA8 material texture (9 levels, the tail from the 64 level down).
SceneBundle threeTexturedMeshes()
{
    SceneBundle bundle;
    for (std::uint32_t i = 0; i < 3; ++i) {
        Texture texture;
        texture.name = "t" + std::to_string(i);
        texture.width = 256;
        texture.height = 256;
        texture.mipCount = 9;
        texture.format = TextureFormat::Rgba8Unorm;
        texture.pixels.resize(static_cast<std::size_t>(mipRangeByteSize(texture.format, 256, 256, 9, 0)), static_cast<std::uint8_t>(i));
        bundle.textures.push_back(std::move(texture));
        Material material;
        material.baseColorTexture = i;
        bundle.materials.push_back(material);
        Mesh mesh;
        mesh.name = "m" + std::to_string(i);
        mesh.vertices.resize(3);
        mesh.indices = {0, 1, 2};
        mesh.submeshes.push_back({0, 3, i});
        bundle.meshes.push_back(std::move(mesh));
    }
    return bundle;
}

TEST_CASE("the null renderer keeps the budget over the bundle textures it is given")
{
    SECTION("the default cap holds the kit in full with no evictions")
    {
        SceneBundle kit;
        CookStats stats;
        REQUIRE(cookGltf(std::filesystem::path(ICHOR_TEST_DATA) / "kit.glb", kit, stats));
        REQUIRE(kit.textures.size() == 4);
        std::uint64_t total = 0;
        for (const Texture& texture : kit.textures) {
            total += texture.pixels.size();
        }
        NullRenderer renderer;
        REQUIRE(renderer.uploadBundle(kit));
        const TextureResidencyStats held = renderer.textureStats();
        REQUIRE(held.capBytes == kDefaultTextureBudgetBytes);
        REQUIRE(held.usedBytes == total);
        REQUIRE(held.textures == 4);
        REQUIRE(held.evictions == 0);
        REQUIRE(total < kMiB);
    }

    SECTION("a cap below the textures evicts, and drawing the meshes reloads a few per frame")
    {
        const SceneBundle bundle = threeTexturedMeshes();
        const std::uint64_t one = bundle.textures.front().pixels.size();
        NullPlatform platform;
        NullRenderer renderer;
        renderer.setTextureBudget(one + one / 2);
        REQUIRE(renderer.uploadBundle(bundle));
        const TextureResidencyStats afterUpload = renderer.textureStats();
        REQUIRE(afterUpload.textures == 3);
        REQUIRE(afterUpload.evictions == 2);
        REQUIRE(afterUpload.evicted == 2);
        REQUIRE(afterUpload.usedBytes <= afterUpload.capBytes);
        REQUIRE(renderer.textureResidentInFull(2));
        REQUIRE_FALSE(renderer.textureResidentInFull(0));

        Engine engine(platform, renderer, {});
        engine.setViewBuilder([&](const Scene&, double, RenderView& view) {
            view = RenderView{};
            for (std::uint32_t mesh = 0; mesh < 3; ++mesh) {
                RenderInstance instance;
                instance.mesh = mesh;
                view.instances.push_back(instance);
            }
        });
        REQUIRE(engine.frame(1.0 / 60.0));
        const TextureResidencyStats afterFrame = renderer.textureStats();
        REQUIRE(afterFrame.reloads == kTextureReloadsPerFrame);
        REQUIRE(afterFrame.evictions > afterUpload.evictions);
        REQUIRE(renderer.textureResidentInFull(0));
        REQUIRE(renderer.textureResidentInFull(1));
        REQUIRE_FALSE(renderer.textureResidentInFull(2));
    }
}
