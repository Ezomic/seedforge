#include "ichor/assets/bundle_io.h"
#include "ichor/assets/cooker.h"
#include "ichor/assets/image.h"
#include "test_temp.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/geometric.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
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

namespace {

struct SynthNode {
    std::string name;
    float size = 0.0f; // edge length of the cube mesh this node carries; 0 is an empty
    glm::vec3 translation{0.0f};
    std::vector<int> children;
};

std::string base64(const std::vector<std::uint8_t>& bytes)
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

// A glTF with one cube mesh per node that has a size; nodes listed in `roots` form the scene.
std::string synthGltf(const std::vector<SynthNode>& nodes, const std::vector<int>& roots)
{
    static constexpr int kCorner[8][3] = {{-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1}, {-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}};
    static constexpr std::uint32_t kIndex[36] = {0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4, 3, 7, 6, 3, 6, 2, 0, 4, 7, 0, 7, 3, 1, 2, 6, 1, 6, 5};

    nlohmann::json root;
    root["asset"] = {{"version", "2.0"}};
    std::vector<std::uint8_t> buffer;
    const auto append = [&buffer](const void* data, std::size_t size) {
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        buffer.insert(buffer.end(), bytes, bytes + size);
    };

    nlohmann::json jsonNodes = nlohmann::json::array();
    nlohmann::json meshes = nlohmann::json::array();
    nlohmann::json accessors = nlohmann::json::array();
    nlohmann::json views = nlohmann::json::array();
    for (const SynthNode& node : nodes) {
        nlohmann::json n = {{"name", node.name}};
        if (node.translation != glm::vec3(0.0f)) n["translation"] = {node.translation.x, node.translation.y, node.translation.z};
        if (!node.children.empty()) n["children"] = node.children;
        if (node.size > 0.0f) {
            const float h = node.size * 0.5f;
            const int base = static_cast<int>(accessors.size());
            std::vector<float> positions;
            std::vector<float> normals;
            for (const auto& c : kCorner) {
                positions.insert(positions.end(), {c[0] * h, c[1] * h, c[2] * h});
                normals.insert(normals.end(), {0.0f, 0.0f, 1.0f});
            }
            const std::pair<const void*, std::size_t> blobs[3] = {{positions.data(), positions.size() * 4}, {normals.data(), normals.size() * 4}, {kIndex, sizeof(kIndex)}};
            for (int b = 0; b < 3; ++b) {
                views.push_back({{"buffer", 0}, {"byteOffset", buffer.size()}, {"byteLength", blobs[b].second}});
                append(blobs[b].first, blobs[b].second);
            }
            const int view = static_cast<int>(views.size()) - 3;
            accessors.push_back({{"bufferView", view}, {"componentType", 5126}, {"count", 8}, {"type", "VEC3"}, {"min", {-h, -h, -h}}, {"max", {h, h, h}}});
            accessors.push_back({{"bufferView", view + 1}, {"componentType", 5126}, {"count", 8}, {"type", "VEC3"}});
            accessors.push_back({{"bufferView", view + 2}, {"componentType", 5125}, {"count", 36}, {"type", "SCALAR"}});
            n["mesh"] = static_cast<int>(meshes.size());
            nlohmann::json primitive = {{"attributes", {{"POSITION", base}, {"NORMAL", base + 1}}}, {"indices", base + 2}, {"mode", 4}};
            nlohmann::json mesh = {{"name", node.name}};
            mesh["primitives"] = nlohmann::json::array({primitive});
            meshes.push_back(mesh);
        }
        jsonNodes.push_back(n);
    }
    root["nodes"] = jsonNodes;
    if (!meshes.empty()) {
        root["meshes"] = meshes;
        root["accessors"] = accessors;
        root["bufferViews"] = views;
    }
    nlohmann::json bufferJson = {{"byteLength", buffer.size()}, {"uri", "data:application/octet-stream;base64," + base64(buffer)}};
    nlohmann::json scene = {{"nodes", roots}};
    root["buffers"] = nlohmann::json::array({bufferJson});
    root["scenes"] = nlohmann::json::array({scene});
    root["scene"] = 0;
    return root.dump();
}

std::filesystem::path writeText(const std::filesystem::path& name, const std::string& text)
{
    const auto path = test::tempPath(name);
    std::ofstream(path, std::ios::binary) << text;
    return path;
}

// SM_Foo has three LODs, a socket and both collision kinds; SM_Plain has nothing special.
std::string conventionGltf()
{
    return synthGltf(
        {
            {.name = "SM_Foo_LOD0", .size = 2.0f, .translation = {10.0f, 0.0f, 0.0f}, .children = {3}},
            {.name = "SM_Foo_LOD1", .size = 2.0f},
            {.name = "SM_Foo_LOD2", .size = 2.0f},
            {.name = "SOCKET_hand", .translation = {1.0f, 2.0f, 0.0f}},
            {.name = "UCX_SM_Foo", .size = 1.0f, .translation = {10.0f, 0.0f, 0.0f}},
            {.name = "UBX_SM_Foo", .size = 4.0f, .translation = {10.0f, 1.0f, 0.0f}},
            {.name = "SM_Plain", .size = 1.0f},
        },
        {0, 1, 2, 4, 5, 6});
}

}

TEST_CASE("sockets, LODs and collision shapes cook, round-trip and are found by name")
{
    const auto source = writeText("ichor_conventions.gltf", conventionGltf());
    SceneBundle cooked;
    PieceData pieces;
    CookStats stats;
    REQUIRE(cookGltf(source, cooked, pieces, stats));
    std::filesystem::remove(source);

    // Four drawn meshes (three LODs and the plain one); the collision cubes are not meshes.
    REQUIRE(cooked.meshes.size() == 4);
    REQUIRE(cooked.nodes.size() == 2);
    REQUIRE(cooked.nodes[0].name == "SM_Foo_LOD0");
    REQUIRE(cooked.nodes[1].name == "SM_Plain");

    const Socket* hand = findSocket(pieces, "SM_Foo", "hand");
    REQUIRE(hand != nullptr);
    REQUIRE(glm::distance(hand->position, glm::vec3(1.0f, 2.0f, 0.0f)) < 1e-5f);
    REQUIRE(glm::length(hand->rotation) == Catch::Approx(1.0f));
    REQUIRE(findSocket(pieces, "SM_Plain", "hand") == nullptr);

    const LodGroup* group = findLodGroup(pieces, "SM_Foo");
    REQUIRE(group != nullptr);
    REQUIRE(group->levels.size() == 3);
    REQUIRE(group->levels[0].distance == 0.0f);
    REQUIRE(group->levels[1].distance > 0.0f);
    REQUIRE(group->levels[2].distance > group->levels[1].distance);
    REQUIRE(cooked.meshes[group->levels[1].mesh].name == "SM_Foo_LOD1");
    REQUIRE(lodMeshAt(*group, 0.0f) == group->levels[0].mesh);
    REQUIRE(lodMeshAt(*group, group->levels[1].distance) == group->levels[1].mesh);
    REQUIRE(lodMeshAt(*group, 1.0e6f) == group->levels[2].mesh);
    REQUIRE(findLodGroup(pieces, "SM_Plain") == nullptr);

    REQUIRE(pieces.collision.size() == 2);
    const CollisionShape& convex = pieces.collision[0];
    REQUIRE(convex.kind == CollisionKind::Convex);
    REQUIRE(convex.piece == "SM_Foo");
    REQUIRE(convex.points.size() == 8);
    REQUIRE(glm::distance(convex.boundsMin, glm::vec3(-0.5f)) < 1e-5f);
    REQUIRE(glm::distance(convex.boundsMax, glm::vec3(0.5f)) < 1e-5f);
    const CollisionShape& box = pieces.collision[1];
    REQUIRE(box.kind == CollisionKind::Box);
    REQUIRE(box.points.empty());
    REQUIRE(glm::distance(box.boundsMin, glm::vec3(-2.0f, -1.0f, -2.0f)) < 1e-5f);
    REQUIRE(glm::distance(box.boundsMax, glm::vec3(2.0f, 3.0f, 2.0f)) < 1e-5f);

    const auto path = test::tempPath("ichor_conventions.iscene");
    REQUIRE(writeBundle(path, cooked, pieces));
    SceneBundle loaded;
    PieceData loadedPieces;
    REQUIRE(readBundle(path, loaded, loadedPieces));
    SceneBundle plainRead;
    REQUIRE(readBundle(path, plainRead));
    std::filesystem::remove(path);
    REQUIRE(plainRead.meshes.size() == cooked.meshes.size());
    REQUIRE(loadedPieces.sockets.size() == 1);
    REQUIRE(loadedPieces.sockets[0].piece == "SM_Foo");
    REQUIRE(loadedPieces.sockets[0].position == hand->position);
    REQUIRE(loadedPieces.sockets[0].rotation == hand->rotation);
    REQUIRE(loadedPieces.lodGroups.size() == 1);
    REQUIRE(loadedPieces.lodGroups[0].levels.size() == 3);
    REQUIRE(loadedPieces.lodGroups[0].levels[2].distance == group->levels[2].distance);
    REQUIRE(loadedPieces.collision.size() == 2);
    REQUIRE(loadedPieces.collision[0].points == convex.points);
    REQUIRE(loadedPieces.collision[1].boundsMax == box.boundsMax);
}

TEST_CASE("a file without socket, LOD or collision nodes cooks with none and writes no extra bytes")
{
    const auto source = writeText("ichor_plain.gltf", synthGltf({{.name = "SM_Plain", .size = 1.0f}, {.name = "Empty"}}, {0, 1}));
    SceneBundle plain;
    PieceData plainPieces;
    CookStats stats;
    REQUIRE(cookGltf(source, plain, plainPieces, stats));
    std::filesystem::remove(source);
    REQUIRE(plain.nodes.size() == 1);
    REQUIRE(plainPieces.empty());

    const auto withPath = test::tempPath("ichor_plain_a.iscene");
    REQUIRE(writeBundle(withPath, plain, plainPieces));
    PieceData other;
    other.sockets.push_back(Socket{.name = "hand"});
    const auto extraPath = test::tempPath("ichor_plain_b.iscene");
    REQUIRE(writeBundle(extraPath, plain, other));
    // The plain bundle is the same file as the extended one up to the tag that introduces the extra data.
    REQUIRE(std::filesystem::file_size(extraPath) > std::filesystem::file_size(withPath) + 4);
    std::ifstream a(withPath, std::ios::binary);
    std::ifstream b(extraPath, std::ios::binary);
    const std::vector<char> plainBytes((std::istreambuf_iterator<char>(a)), std::istreambuf_iterator<char>());
    const std::vector<char> extraBytes((std::istreambuf_iterator<char>(b)), std::istreambuf_iterator<char>());
    a.close();
    b.close();
    std::filesystem::remove(withPath);
    std::filesystem::remove(extraPath);
    REQUIRE(std::equal(plainBytes.begin(), plainBytes.end(), extraBytes.begin()));
    REQUIRE(std::string(extraBytes.begin() + static_cast<std::ptrdiff_t>(plainBytes.size()), extraBytes.begin() + static_cast<std::ptrdiff_t>(plainBytes.size()) + 4) == "IPC1");
}

TEST_CASE("malformed socket, LOD and collision names are reported and the pieces stay drawn")
{
    const auto source = writeText("ichor_malformed.gltf",
        synthGltf(
            {
                {.name = "SM_Bar", .size = 1.0f, .children = {1, 2, 3, 4}},
                {.name = "SOCKET_"},
                {.name = "socket_low"},
                {.name = "SOCKET_twin", .translation = {1.0f, 0.0f, 0.0f}},
                {.name = "SOCKET_twin", .translation = {2.0f, 0.0f, 0.0f}},
                {.name = "SM_Bar_LOD5", .size = 1.0f},
                {.name = "SM_Solo_LOD1", .size = 1.0f},
                {.name = "UCX_Missing", .size = 1.0f},
                {.name = "UBX_"},
                {.name = "SOCKET_solid", .size = 1.0f},
            },
            {0, 5, 6, 7, 8, 9}));
    SceneBundle bundle;
    PieceData pieces;
    CookStats stats;
    ConventionScan scan;
    REQUIRE(inspectGltf(source, scan));
    REQUIRE(cookGltf(source, bundle, pieces, stats));
    std::filesystem::remove(source);

    // empty socket name, wrong case, duplicate, LOD5, a lone LOD1, collision without a piece, UBX_ without a mesh, a socket with a mesh
    REQUIRE(scan.issues.size() >= 8);
    REQUIRE(pieces.sockets.size() == 1);
    REQUIRE(pieces.sockets[0].name == "twin");
    REQUIRE(glm::distance(pieces.sockets[0].position, glm::vec3(1.0f, 0.0f, 0.0f)) < 1e-5f);
    REQUIRE(pieces.lodGroups.empty());
    REQUIRE(pieces.collision.size() == 1);
    REQUIRE(pieces.collision[0].piece == "Missing");
    // SM_Bar, SM_Bar_LOD5, SM_Solo_LOD1 and the mesh-carrying SOCKET_solid stay ordinary nodes.
    REQUIRE(bundle.nodes.size() == 4);
    REQUIRE(describeConventions(scan).size() == scan.sockets.size() + scan.lods.size() + scan.collision.size() + scan.issues.size());
}

TEST_CASE("hostile: scene files with and without conventions never crash the cooker or the reader")
{
    ConventionScan scan;
    SceneBundle bundle;
    CookStats stats;

    const auto garbage = writeText("ichor_garbage.glb", std::string("glTF\x02\x00\x00\x00\xff\xff\xff\x7f", 12) + std::string(64, '\xa5'));
    REQUIRE_FALSE(inspectGltf(garbage, scan));
    REQUIRE_FALSE(cookGltf(garbage, bundle, stats));
    std::filesystem::remove(garbage);

    const std::string json = conventionGltf();
    for (const std::size_t cut : {std::size_t{0}, std::size_t{1}, json.size() / 3, json.size() / 2, json.size() - 1}) {
        const auto truncated = writeText("ichor_cut.gltf", json.substr(0, cut));
        SceneBundle partial;
        REQUIRE_FALSE(cookGltf(truncated, partial, stats));
        std::filesystem::remove(truncated);
    }

    const auto source = writeText("ichor_hostile_src.gltf", json);
    SceneBundle cooked;
    PieceData cookedPieces;
    REQUIRE(cookGltf(source, cooked, cookedPieces, stats));
    std::filesystem::remove(source);
    const auto path = test::tempPath("ichor_hostile.iscene");
    REQUIRE(writeBundle(path, cooked, cookedPieces));
    std::ifstream in(path, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    // The extension block is the tail; cut the file inside it and flip every byte of it in turn.
    REQUIRE(writeBundle(path, cooked));
    const std::size_t tail = std::filesystem::file_size(path);
    REQUIRE(tail < bytes.size());
    const auto readBack = [&](const std::vector<char>& data) {
        std::ofstream(path, std::ios::binary | std::ios::trunc).write(data.data(), static_cast<std::streamsize>(data.size()));
        SceneBundle out;
        return readBundle(path, out);
    };
    for (std::size_t cut = tail + 1; cut < bytes.size(); ++cut) {
        REQUIRE_FALSE(readBack(std::vector<char>(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(cut))));
    }
    for (std::size_t at = tail; at < bytes.size(); ++at) {
        std::vector<char> flipped = bytes;
        flipped[at] = static_cast<char>(flipped[at] ^ 0xff);
        readBack(flipped);
    }
    std::vector<char> extended = bytes;
    extended.push_back('x');
    REQUIRE_FALSE(readBack(extended));
    std::filesystem::remove(path);
}
