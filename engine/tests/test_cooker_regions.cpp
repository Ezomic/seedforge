#include "ichor/assets/bundle.h"
#include "ichor/assets/body_regions.h"
#include "ichor/assets/bundle_io.h"
#include "ichor/assets/cooker.h"
#include "test_temp.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace ichor;

namespace {

// One mesh with a triangle per region name; an empty name leaves `extras` off that primitive.
std::string regionGltf(const std::vector<std::string>& regions)
{
    std::vector<float> floats;
    std::vector<std::uint16_t> indices;
    nlohmann::json accessors = nlohmann::json::array();
    nlohmann::json views = nlohmann::json::array();
    nlohmann::json primitives = nlohmann::json::array();
    std::vector<unsigned char> buffer;
    const auto add = [&](const void* data, std::size_t bytes) {
        views.push_back({{"buffer", 0}, {"byteOffset", buffer.size()}, {"byteLength", bytes}});
        const auto* p = static_cast<const unsigned char*>(data);
        buffer.insert(buffer.end(), p, p + bytes);
        return static_cast<int>(views.size()) - 1;
    };
    for (std::size_t r = 0; r < regions.size(); ++r) {
        const float x = static_cast<float>(r);
        const float positions[9] = {x, 0, 0, x + 0.5f, 0, 0, x, 1, 0};
        const float normals[9] = {0, 0, 1, 0, 0, 1, 0, 0, 1};
        const std::uint16_t tri[4] = {0, 1, 2, 0};
        const int base = static_cast<int>(accessors.size());
        accessors.push_back({{"bufferView", add(positions, sizeof(positions))}, {"componentType", 5126}, {"count", 3}, {"type", "VEC3"}, {"min", {x, 0, 0}}, {"max", {x + 0.5f, 1, 0}}});
        accessors.push_back({{"bufferView", add(normals, sizeof(normals))}, {"componentType", 5126}, {"count", 3}, {"type", "VEC3"}});
        accessors.push_back({{"bufferView", add(tri, 8)}, {"componentType", 5123}, {"count", 3}, {"type", "SCALAR"}});
        nlohmann::json primitive = {{"attributes", {{"POSITION", base}, {"NORMAL", base + 1}}}, {"indices", base + 2}, {"mode", 4}};
        if (!regions[r].empty()) {
            primitive["extras"] = {{"region", regions[r]}};
        }
        primitives.push_back(primitive);
    }
    static const char* kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string encoded;
    for (std::size_t i = 0; i < buffer.size(); i += 3) {
        const std::size_t n = std::min<std::size_t>(3, buffer.size() - i);
        std::uint32_t v = static_cast<std::uint32_t>(buffer[i]) << 16 | (n > 1 ? static_cast<std::uint32_t>(buffer[i + 1]) << 8 : 0u) | (n > 2 ? buffer[i + 2] : 0u);
        encoded += kAlphabet[(v >> 18) & 63];
        encoded += kAlphabet[(v >> 12) & 63];
        encoded += n > 1 ? kAlphabet[(v >> 6) & 63] : '=';
        encoded += n > 2 ? kAlphabet[v & 63] : '=';
    }
    nlohmann::json root = {{"asset", {{"version", "2.0"}},}};
    root["buffers"] = nlohmann::json::array({{{"byteLength", buffer.size()}, {"uri", "data:application/octet-stream;base64," + encoded}}});
    root["bufferViews"] = views;
    root["accessors"] = accessors;
    root["meshes"] = nlohmann::json::array({{{"name", "Body"}, {"primitives", primitives}}});
    root["nodes"] = nlohmann::json::array({{{"name", "Body"}, {"mesh", 0}}});
    root["scenes"] = nlohmann::json::array({{{"nodes", {0}}}});
    root["scene"] = 0;
    return root.dump();
}

bool cookRegions(const std::vector<std::string>& regions, SceneBundle& bundle)
{
    const auto path = test::tempPath("ichor_regions.gltf");
    std::ofstream(path, std::ios::binary) << regionGltf(regions);
    CookStats stats;
    const bool ok = cookGltf(path, bundle, stats);
    std::filesystem::remove(path);
    return ok;
}

std::vector<std::string> allRegions()
{
    return std::vector<std::string>(kBodyRegions.begin(), kBodyRegions.end());
}

std::vector<std::string_view> names(const Mesh& mesh)
{
    std::vector<std::string_view> out;
    for (const Submesh& sub : mesh.submeshes) {
        out.push_back(regionName(sub));
    }
    return out;
}

}

TEST_CASE("each primitive's extras.region survives the cook and the bundle round trip")
{
    SceneBundle cooked;
    REQUIRE(cookRegions(allRegions(), cooked));
    REQUIRE(cooked.meshes.size() == 1);
    REQUIRE(cooked.meshes[0].submeshes.size() == kBodyRegions.size());
    for (std::size_t i = 0; i < kBodyRegions.size(); ++i) {
        REQUIRE(regionName(cooked.meshes[0].submeshes[i]) == kBodyRegions[i]);
    }

    const auto path = test::tempPath("ichor_regions.iscene");
    REQUIRE(writeBundle(path, cooked));
    SceneBundle loaded;
    REQUIRE(readBundle(path, loaded));
    std::filesystem::remove(path);
    REQUIRE(names(loaded.meshes[0]) == names(cooked.meshes[0]));
}

TEST_CASE("a primitive without a region cooks with an empty name and is never hidden")
{
    SceneBundle cooked;
    REQUIRE(cookRegions({"head", ""}, cooked));
    const Mesh& mesh = cooked.meshes[0];
    REQUIRE(regionName(mesh.submeshes[0]) == "head");
    REQUIRE(regionName(mesh.submeshes[1]).empty());
    REQUIRE(submeshHidden(mesh.submeshes[0], kAllBodyRegions));
    REQUIRE_FALSE(submeshHidden(mesh.submeshes[1], kAllBodyRegions));
}

TEST_CASE("a region name that does not fit the submesh is a cook error")
{
    SceneBundle cooked;
    REQUIRE_FALSE(cookRegions({"a_region_name_far_too_long"}, cooked));
}

TEST_CASE("hiding regions leaves exactly the submeshes outside them drawn")
{
    SceneBundle cooked;
    REQUIRE(cookRegions(allRegions(), cooked));
    const Mesh& mesh = cooked.meshes[0];
    const auto visible = [&](std::uint32_t mask) {
        std::size_t count = 0;
        for (const Submesh& sub : mesh.submeshes) {
            count += submeshHidden(sub, mask) ? 0 : 1;
        }
        return count;
    };

    REQUIRE(visible(0) == 9);

    const std::vector<std::string_view> gauntlets{"hands", "forearms"};
    const std::uint32_t partial = regionMask(gauntlets);
    REQUIRE(visible(partial) == 7);
    for (const Submesh& sub : mesh.submeshes) {
        const bool covered = regionName(sub) == "hands" || regionName(sub) == "forearms";
        REQUIRE(submeshHidden(sub, partial) == covered);
    }

    // The Conqueror armour carries its own limbs and head, so it covers all nine.
    REQUIRE(visible(kAllBodyRegions) == 0);
    REQUIRE(regionMask(std::vector<std::string_view>(kBodyRegions.begin(), kBodyRegions.end())) == kAllBodyRegions);
    REQUIRE(regionMask(std::vector<std::string_view>{"not_a_region"}) == 0);
}
