#include "ichor/assets/bundle.h"
#include "ichor/assets/bundle_io.h"
#include "ichor/assets/cooker.h"
#include "test_temp.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>

using namespace ichor;

namespace {

struct PropSpec {
    const char* piece;
    CollisionKind collision;
    float minHeight; // metres, glTF Y up
    float maxHeight;
    float maxWidth;
};

constexpr PropSpec kProps[] = {
    {"SM_BoulderLimestone", CollisionKind::Convex, 1.0f, 2.2f, 2.0f},
    {"SM_BoulderWeathered", CollisionKind::Convex, 1.2f, 2.6f, 2.4f},
    {"SM_BoulderSlab", CollisionKind::Convex, 0.5f, 1.8f, 2.4f},
    {"SM_PineTree", CollisionKind::Box, 5.5f, 8.0f, 7.0f},
    {"SM_OliveTree", CollisionKind::Box, 3.5f, 5.5f, 6.0f},
    {"SM_DeadTree", CollisionKind::Box, 3.5f, 5.5f, 4.0f},
    {"SM_BushMastic", CollisionKind::Convex, 0.6f, 1.4f, 1.8f},
    {"SM_BushOleander", CollisionKind::Convex, 0.8f, 1.6f, 1.2f},
    {"SM_BushPhrygana", CollisionKind::Convex, 0.3f, 1.2f, 2.6f},
    {"SM_GrassTuftFan", CollisionKind::Box, 0.4f, 0.9f, 1.0f},
    {"SM_TreeStump", CollisionKind::Convex, 0.4f, 0.8f, 2.0f},
};

std::filesystem::path propPath(const char* piece)
{
    return std::filesystem::path(ICHOR_ART_DIR) / "props" / "nature" / (std::string(piece) + ".glb");
}

std::uint32_t triangles(const Mesh& mesh) { return static_cast<std::uint32_t>(mesh.indices.size() / 3); }

bool inside(const glm::vec3& v, const glm::vec3& lo, const glm::vec3& hi, float slack)
{
    return v.x >= lo.x - slack && v.y >= lo.y - slack && v.z >= lo.z - slack && v.x <= hi.x + slack && v.y <= hi.y + slack && v.z <= hi.z + slack;
}

}

TEST_CASE("the nature props cook to bundles with three LODs, one collision shape and sane bounds", "[art][props][nature]")
{
    for (const PropSpec& spec : kProps) {
        DYNAMIC_SECTION(spec.piece)
        {
            SceneBundle cooked;
            PieceData pieces;
            CookStats stats;
            REQUIRE(cookGltf(propPath(spec.piece), cooked, pieces, stats));

            const auto path = test::tempPath(std::string("ichor_nature_") + spec.piece + ".iscene");
            REQUIRE(writeBundle(path, cooked, pieces));
            SceneBundle bundle;
            PieceData loaded;
            REQUIRE(readBundle(path, bundle, loaded));
            std::filesystem::remove(path);

            REQUIRE(bundle.nodes.size() == 1);
            REQUIRE(bundle.nodes[0].name == std::string(spec.piece) + "_LOD0");

            const LodGroup* group = findLodGroup(loaded, spec.piece);
            REQUIRE(group != nullptr);
            REQUIRE(group->levels.size() == 3);
            REQUIRE(group->levels[0].mesh == bundle.nodes[0].mesh);
            const Mesh& lod0 = bundle.meshes[group->levels[0].mesh];
            const Mesh& lod1 = bundle.meshes[group->levels[1].mesh];
            const Mesh& lod2 = bundle.meshes[group->levels[2].mesh];
            CHECK(triangles(lod0) < 2000);
            CHECK(triangles(lod0) > triangles(lod1));
            CHECK(triangles(lod1) > triangles(lod2));
            CHECK(triangles(lod2) >= 12);
            CHECK(group->levels[1].distance > 0.0f);
            CHECK(group->levels[2].distance > group->levels[1].distance);
            CHECK(lodMeshAt(*group, 0.0f) == group->levels[0].mesh);
            CHECK(lodMeshAt(*group, 1.0e6f) == group->levels[2].mesh);

            REQUIRE(loaded.collision.size() == 1);
            const CollisionShape& shape = loaded.collision[0];
            CHECK(shape.piece == spec.piece);
            CHECK(shape.kind == spec.collision);
            CHECK(shape.name == std::string(spec.collision == CollisionKind::Box ? "UBX_" : "UCX_") + spec.piece);
            CHECK(shape.boundsMax.x > shape.boundsMin.x);
            CHECK(shape.boundsMax.y > shape.boundsMin.y);
            CHECK(shape.boundsMax.z > shape.boundsMin.z);
            if (spec.collision == CollisionKind::Convex) {
                CHECK(shape.points.size() >= 4);
                CHECK(shape.points.size() <= 128);
            } else {
                CHECK(shape.points.empty());
            }
            CHECK(inside(shape.boundsMin, lod0.boundsMin, lod0.boundsMax, 0.05f));
            CHECK(inside(shape.boundsMax, lod0.boundsMin, lod0.boundsMax, 0.05f));

            const float height = lod0.boundsMax.y - lod0.boundsMin.y;
            const float width = std::max(lod0.boundsMax.x - lod0.boundsMin.x, lod0.boundsMax.z - lod0.boundsMin.z);
            CHECK(height >= spec.minHeight);
            CHECK(height <= spec.maxHeight);
            CHECK(width <= spec.maxWidth);
            CHECK(lod0.boundsMin.y >= -0.25f);
            CHECK(lod0.boundsMin.y <= 0.05f);
            CHECK(lod0.boundsMin.x < 0.0f);
            CHECK(lod0.boundsMax.x > 0.0f);

            for (const Mesh* lod : {&lod1, &lod2}) {
                CHECK(inside(lod->boundsMin, lod0.boundsMin, lod0.boundsMax, 0.6f * width));
                CHECK(inside(lod->boundsMax, lod0.boundsMin, lod0.boundsMax, 0.6f * width));
            }

            REQUIRE(bundle.textures.size() == 1);
            CHECK(bundle.textures[0].width <= 128);
            CHECK(bundle.materials.size() == 1);
        }
    }
}
