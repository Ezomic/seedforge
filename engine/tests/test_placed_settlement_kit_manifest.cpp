#include "test_temp.h"

#include "ichor/assets/cooker.h"
#include "ichor/world/settlement_kit.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <set>
#include <string>

using namespace ichor;

namespace {

std::filesystem::path kitFolder()
{
    return std::filesystem::path(ICHOR_ART_DIR) / "settlement_kit";
}

std::string readFile(const std::filesystem::path& file)
{
    std::ifstream in(file, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::string manifestWith(const std::string& extraPieces)
{
    std::string text = R"({"pieces": [)";
    for (const KitManifestPiece& piece : builtinKitManifest().pieces) {
        text += R"({"id": ")" + piece.id + R"(", "file": ")" + piece.file + R"(", "category": ")" + piece.category + R"(", "footprint": {"halfWidthM": )"
            + std::format("{}", piece.halfWidthM) + R"(, "halfDepthM": )" + std::format("{}", piece.halfDepthM) + R"(, "heightM": )" + std::format("{}", piece.heightM) + "}},";
    }
    text += extraPieces + "]}";
    if (extraPieces.empty()) text.erase(text.find_last_of(',') , 1);
    return text;
}

constexpr const char* kExtra =
    R"({"id": "Hut2", "file": "Hut2.glb", "category": "building", "footprint": {"halfWidthM": 2.0, "halfDepthM": 2.0, "heightM": 3.0}})";

Material materialNamed(const std::string& name, float roughness)
{
    Material m;
    m.name = name;
    m.roughnessFactor = roughness;
    return m;
}

// A cooked piece with `count` materials named from a pool of `distinct` names, one submesh per material.
SceneBundle syntheticPiece(std::size_t count, std::size_t distinct, std::size_t offset)
{
    SceneBundle piece;
    Mesh mesh;
    for (std::size_t i = 0; i < count; ++i) {
        piece.materials.push_back(materialNamed("mat_" + std::to_string((offset + i) % distinct), 0.5f));
        mesh.submeshes.push_back({static_cast<std::uint32_t>(i * 3), 3, static_cast<std::uint32_t>(i)});
    }
    piece.meshes.push_back(std::move(mesh));
    return piece;
}

}

TEST_CASE("the checked-in kit manifest matches the built-in one", "[bake][settlements][kit][manifest]")
{
    const KitManifestResult loaded = loadKitManifest(kitFolder() / kKitManifestFile);
    INFO(loaded.error);
    REQUIRE(loaded.ok());
    const KitManifest& builtin = builtinKitManifest();
    REQUIRE(loaded.manifest.pieces.size() == builtin.pieces.size());
    for (std::size_t i = 0; i < builtin.pieces.size(); ++i) {
        const KitManifestPiece& a = loaded.manifest.pieces[i];
        const KitManifestPiece& b = builtin.pieces[i];
        INFO(b.id);
        CHECK(a.id == b.id);
        CHECK(a.file == b.file);
        CHECK(a.category == b.category);
        CHECK(a.halfWidthM == b.halfWidthM);
        CHECK(a.halfDepthM == b.halfDepthM);
        CHECK(a.heightM == b.heightM);
        CHECK(std::filesystem::exists(kitFolder() / a.file));
    }
}

TEST_CASE("a piece added through the manifest alone is cooked and reachable by its id", "[bake][settlements][kit][manifest]")
{
    const std::filesystem::path folder = test::tempPath("kit_manifest_folder");
    std::filesystem::create_directories(folder);
    std::filesystem::copy_file(kitFolder() / "SM_PalisadeRunGrid.glb", folder / "Hut2.glb", std::filesystem::copy_options::overwrite_existing);
    const KitManifestResult parsed = parseKitManifest(manifestWith(kExtra));
    INFO(parsed.error);
    REQUIRE(parsed.ok());
    REQUIRE(parsed.manifest.pieces.size() == kKitPieceCount + 1);
    const int added = parsed.manifest.find("Hut2");
    REQUIRE(added == static_cast<int>(kKitPieceCount));
    CHECK(parsed.manifest.pieces[static_cast<std::size_t>(added)].category == "building");

    std::vector<bool> wanted(parsed.manifest.pieces.size(), false);
    wanted[static_cast<std::size_t>(added)] = true;
    SceneBundle bundle;
    KitMeshes meshes;
    REQUIRE(cookSettlementKit(folder, parsed.manifest, wanted, bundle, meshes));
    REQUIRE(bundle.meshes.size() == 1);
    REQUIRE(meshes.byManifest.size() == parsed.manifest.pieces.size());
    CHECK(meshes.byManifest[static_cast<std::size_t>(added)] == 0);
    CHECK_FALSE(bundle.materials.empty());

    SceneBundle none;
    wanted[static_cast<std::size_t>(added)] = false;
    wanted[0] = true;
    CHECK_FALSE(cookSettlementKit(folder, parsed.manifest, wanted, none, meshes));
    CHECK(none.meshes.empty());
    std::filesystem::remove_all(folder);
}

TEST_CASE("329 materials with 51 distinct names fit the renderer after sharing", "[bake][settlements][kit][manifest]")
{
    std::vector<SceneBundle> pieces;
    std::size_t total = 0;
    for (std::size_t p = 0; p < 7; ++p) {
        pieces.push_back(syntheticPiece(47, 51, p * 47));
        total += pieces.back().materials.size();
    }
    REQUIRE(total == 329);
    SceneBundle bundle;
    bundle.materials.push_back(materialNamed("road", 1.0f));
    std::vector<std::uint32_t> index;
    KitMergeStats stats;
    std::string error;
    const std::vector<bool> wanted(pieces.size(), true);
    REQUIRE(mergeKitPieces(pieces, wanted, bundle, index, stats, error));
    CHECK(stats.materialsIn == 329);
    CHECK(stats.materialsOut == 51);
    CHECK(bundle.materials.size() == 52);
    CHECK(bundle.meshes.size() == 7);
    for (const Mesh& mesh : bundle.meshes) {
        for (const Submesh& submesh : mesh.submeshes) CHECK(submesh.material < bundle.materials.size());
    }
    for (std::size_t p = 0; p < 7; ++p) {
        for (std::size_t i = 0; i < 47; ++i) {
            const Submesh& submesh = bundle.meshes[index[p]].submeshes[i];
            CHECK(bundle.materials[submesh.material].name == "mat_" + std::to_string((p * 47 + i) % 51));
        }
    }
}

TEST_CASE("materials with the same name but other parameters or images keep their own slots", "[bake][settlements][kit][manifest]")
{
    std::vector<SceneBundle> pieces(2);
    for (SceneBundle& piece : pieces) {
        Texture texture;
        texture.width = 1;
        texture.height = 1;
        texture.mipCount = 1;
        texture.pixels = {1, 2, 3, 4};
        piece.textures.push_back(texture);
        Material shared = materialNamed("stone", 0.5f);
        shared.baseColorTexture = 0;
        piece.materials.push_back(shared);
        piece.meshes.push_back({});
        piece.meshes[0].submeshes.push_back({0, 3, 0});
    }
    pieces[1].materials.push_back(materialNamed("stone", 0.9f));
    pieces[1].textures.push_back(pieces[1].textures[0]);
    pieces[1].textures[1].pixels = {9, 9, 9, 9};
    Material other = materialNamed("stone", 0.5f);
    other.baseColorTexture = 1;
    pieces[1].materials.push_back(other);
    SceneBundle bundle;
    std::vector<std::uint32_t> index;
    KitMergeStats stats;
    std::string error;
    REQUIRE(mergeKitPieces(pieces, {true, true}, bundle, index, stats, error));
    CHECK(stats.materialsIn == 4);
    CHECK(bundle.materials.size() == 3);
    CHECK(bundle.textures.size() == 2);
    CHECK(stats.texturesIn == 3);
    CHECK(bundle.meshes[0].submeshes[0].material == bundle.meshes[1].submeshes[0].material);
}

TEST_CASE("a kit that would pass the renderer's material cap is refused and leaves the bundle alone", "[bake][settlements][kit][manifest]")
{
    std::vector<SceneBundle> pieces;
    pieces.push_back(syntheticPiece(kMaxSceneMaterials + 1, kMaxSceneMaterials + 1, 0));
    SceneBundle bundle;
    bundle.materials.push_back(materialNamed("road", 1.0f));
    std::vector<std::uint32_t> index;
    KitMergeStats stats;
    std::string error;
    CHECK_FALSE(mergeKitPieces(pieces, {true}, bundle, index, stats, error));
    CHECK(error.find("256") != std::string::npos);
    CHECK(error.find("material slots") != std::string::npos);
    CHECK(bundle.materials.size() == 1);
    CHECK(bundle.meshes.empty());

    std::vector<SceneBundle> fits;
    fits.push_back(syntheticPiece(kMaxSceneMaterials - 1, kMaxSceneMaterials - 1, 0));
    CHECK(mergeKitPieces(fits, {true}, bundle, index, stats, error));
    CHECK(bundle.materials.size() == kMaxSceneMaterials);
}

TEST_CASE("a cooked piece with a dangling material or texture index is refused", "[bake][settlements][kit][manifest]")
{
    SceneBundle bundle;
    std::vector<std::uint32_t> index;
    KitMergeStats stats;
    std::string error;
    std::vector<SceneBundle> badSubmesh;
    badSubmesh.push_back(syntheticPiece(2, 2, 0));
    badSubmesh[0].meshes[0].submeshes[1].material = 9;
    CHECK_FALSE(mergeKitPieces(badSubmesh, {true}, bundle, index, stats, error));
    std::vector<SceneBundle> badTexture;
    badTexture.push_back(syntheticPiece(1, 1, 0));
    badTexture[0].materials[0].normalTexture = 4;
    CHECK_FALSE(mergeKitPieces(badTexture, {true}, bundle, index, stats, error));
    std::vector<SceneBundle> noMesh(1);
    CHECK_FALSE(mergeKitPieces(noMesh, {true}, bundle, index, stats, error));
    CHECK(bundle.materials.empty());
    CHECK(bundle.meshes.empty());
}

TEST_CASE("the real settlement kit art shares its materials under the renderer cap", "[bake][settlements][kit][manifest]")
{
    std::vector<SceneBundle> pieces;
    std::size_t files = 0;
    for (const auto& entry : std::filesystem::directory_iterator(kitFolder())) {
        if (entry.path().extension() != ".glb") continue;
        ++files;
        SceneBundle cooked;
        CookStats stats;
        REQUIRE(cookGltf(entry.path(), cooked, stats));
        pieces.push_back(std::move(cooked));
    }
    REQUIRE(files >= kKitPieceCount);
    SceneBundle bundle;
    std::vector<std::uint32_t> index;
    KitMergeStats stats;
    std::string error;
    INFO(error);
    REQUIRE(mergeKitPieces(pieces, std::vector<bool>(pieces.size(), true), bundle, index, stats, error));
    INFO(stats.materialsIn << " materials in " << stats.materialsOut << " slots");
    CHECK(stats.materialsOut <= stats.materialsIn);
    CHECK(bundle.materials.size() <= kMaxSceneMaterials);
    CHECK(bundle.textures.size() <= stats.texturesIn);
}

TEST_CASE("manifest rules name the entry and the rule", "[bake][settlements][kit][manifest]")
{
    const std::string good = manifestWith("");
    REQUIRE(parseKitManifest(good).ok());
    const auto failsWith = [&](std::string text, const std::string& needle) {
        const KitManifestResult result = parseKitManifest(text);
        INFO(text);
        INFO(result.error);
        CHECK_FALSE(result.ok());
        CHECK(result.error.find(needle) != std::string::npos);
    };
    const auto replaced = [&](const std::string& from, const std::string& to) {
        std::string text = good;
        const auto at = text.find(from);
        REQUIRE(at != std::string::npos);
        text.replace(at, from.size(), to);
        return text;
    };
    failsWith("", "not valid JSON");
    failsWith("[]", "not an object");
    failsWith("{}", "\"pieces\"");
    failsWith(R"({"pieces": {}})", "\"pieces\"");
    failsWith(R"({"pieces": []})", "SmallHouse");
    failsWith(replaced("\"SmallHouse\"", "\"Small House\""), "invalid id");
    failsWith(replaced("\"SM_TempleFront.glb\"", "\"SM_SmallHouse.glb\""), "reuses the file");
    failsWith(replaced("\"SM_TempleFront.glb\"", "\"../SM_TempleFront.glb\""), "TempleFront");
    failsWith(replaced("\"SM_TempleFront.glb\"", "\"sub/SM_TempleFront.glb\""), "TempleFront");
    failsWith(replaced("\"SM_TempleFront.glb\"", "\"C:/SM_TempleFront.glb\""), "TempleFront");
    failsWith(replaced("\"SM_TempleFront.glb\"", "\"SM_TempleFront.fbx\""), "TempleFront");
    failsWith(replaced("\"WallTower\"", "\"WallTowerX\""), "WallTower");
    failsWith(replaced("\"id\": \"GateBayGrid\"", "\"id\": \"WallTower\""), "listed twice");
    failsWith(replaced("\"heightM\": 7.03", "\"heightM\": -1"), "TempleFront");
    failsWith(replaced("\"heightM\": 7.03", "\"heightM\": 0"), "TempleFront");
    failsWith(replaced("\"heightM\": 7.03", "\"heightM\": \"tall\""), "TempleFront");
    failsWith(replaced("\"heightM\": 7.03", "\"heightM\": 1e400"), "kit manifest");
    failsWith(replaced("\"heightM\": 7.03", "\"heightM\": 5000"), "TempleFront");
    failsWith(replaced("\"category\": \"gate\"", "\"category\": 3"), "GateBayGrid");
}

TEST_CASE("hostile manifest bytes are refused and never crash", "[bake][settlements][kit][manifest]")
{
    const std::string good = manifestWith(kExtra);
    REQUIRE(parseKitManifest(good).ok());
    for (std::size_t cut = 0; cut < good.size(); cut += 7) CHECK_FALSE(parseKitManifest(good.substr(0, cut)).ok());
    std::uint64_t state = 0x9e3779b97f4a7c15ull;
    const auto next = [&] {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    };
    std::size_t accepted = 0;
    for (int round = 0; round < 3000; ++round) {
        std::string text = good;
        const int edits = 1 + static_cast<int>(next() % 4);
        for (int e = 0; e < edits; ++e) {
            const std::size_t at = static_cast<std::size_t>(next() % text.size());
            switch (next() % 3) {
            case 0: text[at] = static_cast<char>(next() & 0xff); break;
            case 1: text.erase(at, 1 + static_cast<std::size_t>(next() % 5)); break;
            default: text.insert(at, 1, "{}[]\":,\\0e-"[next() % 11]); break;
            }
            if (text.empty()) break;
        }
        const KitManifestResult result = parseKitManifest(text);
        if (result.ok()) {
            ++accepted;
            CHECK(result.manifest.find("SmallHouse") >= 0);
            for (const KitManifestPiece& piece : result.manifest.pieces) {
                CHECK(piece.file.find('/') == std::string::npos);
                CHECK(piece.file.find('\\') == std::string::npos);
                CHECK(piece.file.find("..") == std::string::npos);
                CHECK(piece.halfWidthM > 0.0f);
            }
        } else {
            CHECK_FALSE(result.error.empty());
        }
    }
    CHECK(accepted < 3000);
    CHECK_FALSE(parseKitManifest(std::string(2000000, ' ')).ok());
    CHECK_FALSE(parseKitManifest(std::string(100000, '[')).ok());
    CHECK_FALSE(parseKitManifest(std::string("\0\0\0\0", 4)).ok());
    CHECK_FALSE(parseKitManifest("\xff\xfe\xfd").ok());
    std::string many = R"({"pieces": [)";
    for (std::size_t i = 0; i < kMaxKitManifestPieces + 1; ++i) many += (i ? "," : "") + std::string("{}");
    CHECK_FALSE(parseKitManifest(many + "]}").ok());
    CHECK_FALSE(loadKitManifest(kitFolder() / "no_such_manifest.json").ok());
    CHECK_FALSE(loadKitManifest(kitFolder()).ok());
}
