#include "ichor/assets/cooker.h"
#include "ichor/gfx/null_renderer.h"
#include "ichor/gfx/reference_view.h"
#include "ichor/loop/engine.h"
#include "ichor/platform/null_platform.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

using namespace ichor;

TEST_CASE("the kit renders headless through the loop")
{
    SceneBundle bundle;
    CookStats stats;
    REQUIRE(cookGltf(std::filesystem::path(ICHOR_TEST_DATA) / "kit.glb", bundle, stats));

    NullPlatform platform;
    NullRenderer renderer;
    REQUIRE(renderer.uploadBundle(bundle));
    REQUIRE(renderer.meshesUploaded() == 7);

    Engine engine(platform, renderer, {});
    engine.setViewBuilder([&](const Scene&, double, RenderView& view) {
        view = kitReferenceView(bundle, 16.0f / 9.0f);
    });
    for (int i = 0; i < 10; ++i) {
        REQUIRE(engine.frame(1.0 / 60.0));
    }
    REQUIRE(renderer.instancesSubmitted() == 70);
    REQUIRE(renderer.framesEnded() == 10);
}

TEST_CASE("the reference view is camera-relative friendly")
{
    SceneBundle bundle;
    CookStats stats;
    REQUIRE(cookGltf(std::filesystem::path(ICHOR_TEST_DATA) / "kit.glb", bundle, stats));
    const RenderView view = kitReferenceView(bundle, 1.0f);
    REQUIRE(view.instances.size() == bundle.nodes.size());
    REQUIRE_FALSE(view.lines.empty());
    REQUIRE(view.camera.position.y > 0.0);
}
