#include "forged_world_fixture.h"

#include "ichor/data/data_set.h"
#include "ichor/save/campaign.h"
#include "ichor/save/save_slots.h"
#include "ichor/world/dungeon_entry.h"
#include "ichor/world/dungeon_greybox.h"
#include "ichor/world/forge_job.h"
#include "ichor/world/forged_campaign_hook.h"
#include "ichor/world/forged_game.h"
#include "test_temp.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <limits>
#include <string>
#include <thread>
#include <vector>

using namespace ichor;

namespace {

constexpr double kDt = 1.0 / 60.0;
constexpr int kGrid = 128;

// ---- grey-box geometry on hand-made layouts -------------------------------------------------------------------

struct Hand {
    BakedDungeon dungeon;
    graph::NodeId add(const char* type, graph::embedding::Cell cell)
    {
        const graph::NodeId id = dungeon.graph.addNode(type);
        dungeon.cells[id] = cell;
        return id;
    }
    void link(graph::NodeId a, graph::NodeId b, const char* type = "path") { dungeon.graph.addEdge(a, b, type); }
};

bool solidAt(const DungeonGreyBox& box, const glm::dvec3& p)
{
    for (const GreyBoxPiece& piece : box.pieces) {
        const glm::dvec3 local = glm::dvec3(glm::inverse(piece.rotation) * glm::vec3(p - piece.centre));
        if (std::abs(local.x) <= piece.halfExtents.x && std::abs(local.y) <= piece.halfExtents.y && std::abs(local.z) <= piece.halfExtents.z) return true;
    }
    return false;
}

constexpr glm::dvec3 kOrigin{1000.0, 50.0, -2000.0};

// ---- a forged game with dungeons ----------------------------------------------------------------------------------

WorldDesc descFor(std::uint64_t seed)
{
    WorldDesc desc;
    desc.seed = seed;
    return desc;
}

struct Content {
    ForgedContentSetup setup;
    std::unique_ptr<ForgedGame> game;
};

Content contentFor(std::uint64_t seed)
{
    DataSet data;
    REQUIRE(data.loadDirectory(ICHOR_GAME_DATA_DIR));
    Content out;
    out.setup = loadForgedContentSetup(data, ICHOR_GAME_DATA_DIR);
    const ForgeJobResult forged = test::forgedWorldFixture(descFor(seed), kGrid, test::variantOfContentLists(out.setup.config));
    out.game = buildForgedGame(forged, descFor(seed), out.setup.inputs);
    INFO(out.game->error);
    REQUIRE(out.game->ok());
    return out;
}

const Content& shared()
{
    static const Content content = contentFor(1);
    return content;
}

const PlacedPoi& firstDungeon(const ForgedGame& game)
{
    for (const PlacedPoi& poi : game.pois->all()) {
        if (poi.kind == PlacedPoiKind::Dungeon) return poi;
    }
    FAIL("the world has no dungeon");
    return game.pois->all().front();
}

// ---- driving a scene ----------------------------------------------------------------------------------------------

void addGround(Scene& scene, const PlacedPoi& poi)
{
    Transform ground;
    ground.position = {poi.x, static_cast<double>(poi.groundY) - 1.0, poi.z};
    scene.physics().addStaticBox(StableId::of("test_ground"), StaticBox{{400.0f, 1.0f, 400.0f}}, ground);
}

void spawnPlayer(Scene& scene, const glm::dvec3& at)
{
    SpawnCommand player;
    player.id = StableId::of("player");
    player.name.value = "player";
    player.transform.position = at;
    player.player = Player{};
    player.character = CharacterBody{};
    scene.enqueue(player);
}

glm::dvec3 playerAt(const Scene& scene)
{
    const entt::entity e = scene.find(StableId::of("player"));
    REQUIRE((e != entt::null));
    return scene.registry().get<Transform>(e).position;
}

// Positions come back through the physics world's floats, so a return is exact to a few millimetres, not to the bit.
bool sameSpot(const glm::dvec3& a, const glm::dvec3& b) { return glm::length(a - b) < 0.02; }

// The scene as the overworld sees it: every entity by id, name and place to 5 cm, plus the clock. Entities that were
// never in the overworld (a dungeon marker) would change it; float noise from the physics world does not.
std::uint64_t overworldHash(const Scene& scene)
{
    std::vector<std::string> rows;
    for (auto [entity, id, name, transform] : scene.registry().view<const StableId, const Name, const Transform>().each()) {
        rows.push_back(std::to_string(id.value) + "|" + name.value + "|" + std::to_string(std::llround(transform.position.x * 20.0)) + "|"
            + std::to_string(std::llround(transform.position.y * 20.0)) + "|" + std::to_string(std::llround(transform.position.z * 20.0)));
    }
    std::sort(rows.begin(), rows.end());
    std::uint64_t h = hashCombine(scene.seed(), scene.tickCount());
    for (const std::string& row : rows) h = hashCombine(h, hash64(row));
    return h;
}

InputFrame steer(const glm::dvec3& from, double tx, double tz, std::uint32_t buttons = 0)
{
    InputFrame frame;
    const double dx = tx - from.x;
    const double dz = tz - from.z;
    const double length = std::hypot(dx, dz);
    if (length > 1e-6) {
        frame.moveX = static_cast<float>(dx / length);
        frame.moveY = static_cast<float>(-dz / length);
    }
    frame.buttons = buttons;
    return frame;
}

struct Rig {
    Scene scene{7};
    std::unique_ptr<DungeonEntry> entry;

    Rig(const PlacedPoi& poi, ForgedGame& game, bool withEntry, double startDistance = 30.0, DungeonEntryParams params = {})
    {
        addGround(scene, poi);
        if (withEntry) {
            entry = std::make_unique<DungeonEntry>(game, 0, params);
            entry->install(scene);
        }
        spawnPlayer(scene, {poi.x - startDistance, static_cast<double>(poi.groundY) + 0.5, poi.z});
        for (int i = 0; i < 30; ++i) scene.tick(kDt);
    }
    bool inside() const { return entry && entry->inside(scene); }
    std::string prompt() const { return entry ? entry->prompt(scene) : std::string(); }
    void tick(const InputFrame& input = {}) { scene.tick(kDt, input); }
    bool walkUntilPrompt(double tx, double tz, int limit = 900)
    {
        for (int i = 0; i < limit && prompt().empty(); ++i) tick(steer(playerAt(scene), tx, tz));
        return !prompt().empty();
    }
};

}

TEST_CASE("a doorway is open exactly where a path links two touching rooms", "[dungeon_entry][greybox]")
{
    for (const bool linked : {true, false}) {
        Hand h;
        const auto a = h.add("entry", {0, 0, 0});
        const auto b = h.add("climax", {1, 0, 0});
        if (linked) h.link(a, b);
        const DungeonGreyBox box = buildDungeonGreyBox(h.dungeon, kOrigin);
        REQUIRE(box.ok());
        // The wall between them, at head height and on the floor.
        const glm::dvec3 doorway = kOrigin + glm::dvec3(6.0, 1.5, 0.0);
        const glm::dvec3 floorThere = kOrigin + glm::dvec3(6.0, -0.2, 0.0);
        CHECK(solidAt(box, doorway) == !linked);
        CHECK(solidAt(box, floorThere));
        // Beside the doorway the wall stands either way.
        CHECK(solidAt(box, kOrigin + glm::dvec3(6.0, 1.5, 3.5)));
        // A lintel stays over the opening.
        CHECK(solidAt(box, kOrigin + glm::dvec3(6.0, 3.6, 0.0)));
    }
}

TEST_CASE("a path link between floors is a ramp under a hole, and an unlinked pair is two sealed floors", "[dungeon_entry][greybox]")
{
    for (const bool linked : {true, false}) {
        Hand h;
        const auto upper = h.add("entry", {0, 0, 0});
        const auto lower = h.add("climax", {0, 0, -1});
        if (linked) h.link(upper, lower);
        const DungeonGreyBox box = buildDungeonGreyBox(h.dungeon, kOrigin);
        REQUIRE(box.ok());
        // The lower floor is -1, odd, so its ramp and the hole above it are on the east side.
        const double side = 5.0 - greybox::kRampWidthM * 0.5;
        const glm::dvec3 inHole = kOrigin + glm::dvec3(side, -0.25, 0.0);
        CHECK(solidAt(box, inHole) == !linked);
        const glm::dvec3 lowerFloor = kOrigin + glm::dvec3(0.0, -greybox::kStoreyM, 0.0);
        const glm::dvec3 rampMiddleBelow = lowerFloor + glm::dvec3(side, greybox::kStoreyM * 0.5 - 0.1, 0.0);
        const glm::dvec3 rampMiddleAbove = rampMiddleBelow + glm::dvec3(0.0, 0.2, 0.0);
        CHECK(solidAt(box, rampMiddleBelow) == linked);
        CHECK_FALSE(solidAt(box, rampMiddleAbove));
        // The upper floor beside the hole is whole.
        CHECK(solidAt(box, kOrigin + glm::dvec3(-3.0, -0.25, 0.0)));
    }
}

TEST_CASE("the entry room gives a floor to stand on, room to stand in, and an exit", "[dungeon_entry][greybox]")
{
    Hand h;
    const auto a = h.add("entry", {0, 0, 0});
    const auto b = h.add("climax", {0, 1, 0});
    h.link(a, b);
    const DungeonGreyBox box = buildDungeonGreyBox(h.dungeon, kOrigin);
    REQUIRE(box.ok());
    CHECK_FALSE(solidAt(box, box.spawn + glm::dvec3(0.0, 0.3, 0.0)));
    CHECK_FALSE(solidAt(box, box.spawn + glm::dvec3(0.0, 1.8, 0.0)));
    CHECK(solidAt(box, box.spawn - glm::dvec3(0.0, 0.6, 0.0)));
    CHECK(solidAt(box, box.exitPoint + glm::dvec3(0.0, 1.0, 0.0)));
    CHECK(box.lowestY < kOrigin.y);
    // The room has a doorway south (+z, the cell's +y), so the view faces it: forward is +z at yaw pi.
    CHECK(std::abs(std::abs(box.spawnYaw) - 3.14159265358979) < 1e-9);
}

TEST_CASE("a layout that cannot be built is refused with a reason, never half built", "[dungeon_entry][greybox]")
{
    {
        Hand h;
        h.add("challenge", {0, 0, 0});
        const DungeonGreyBox box = buildDungeonGreyBox(h.dungeon, kOrigin);
        CHECK_FALSE(box.ok());
        CHECK(box.pieces.empty());
    }
    {
        Hand h;
        h.add("entry", {0, 0, 0});
        h.add("climax", {0, 0, 0});
        CHECK_FALSE(buildDungeonGreyBox(h.dungeon, kOrigin).ok());
    }
    {
        Hand h;
        h.add("entry", {0, 0, 0});
        h.add("climax", {greybox::kMaxCell + 1, 0, 0});
        CHECK_FALSE(buildDungeonGreyBox(h.dungeon, kOrigin).ok());
    }
    {
        Hand h;
        h.add("entry", {0, 0, 0});
        h.add("climax", {0, 0, 1 << 30});
        CHECK_FALSE(buildDungeonGreyBox(h.dungeon, kOrigin).ok());
    }
    {
        Hand h;
        h.add("entry", {0, 0, 0});
        CHECK_FALSE(buildDungeonGreyBox(h.dungeon, {std::nan(""), 0.0, 0.0}).ok());
        CHECK_FALSE(buildDungeonGreyBox(h.dungeon, {0.0, std::numeric_limits<double>::infinity(), 0.0}).ok());
    }
    {
        // Far more rooms than the box limit allows: refused, not a gigabyte of boxes.
        Hand h;
        h.add("entry", {0, 0, 0});
        for (std::int32_t x = -greybox::kMaxCell; x <= greybox::kMaxCell; ++x) {
            for (std::int32_t y = -greybox::kMaxCell; y <= greybox::kMaxCell; y += 8) {
                if (x != 0 || y != 0) h.add("challenge", {x, y, 0});
            }
        }
        const DungeonGreyBox box = buildDungeonGreyBox(h.dungeon, kOrigin);
        CHECK_FALSE(box.ok());
        CHECK(box.pieces.empty());
    }
    {
        // Links to rooms that have no cell, and an empty layout, are ignored or refused without a crash.
        Hand h;
        const auto a = h.add("entry", {0, 0, 0});
        const auto ghost = h.dungeon.graph.addNode("challenge");
        h.link(a, ghost);
        CHECK(buildDungeonGreyBox(h.dungeon, kOrigin).ok());
        CHECK_FALSE(buildDungeonGreyBox(BakedDungeon{}, kOrigin).ok());
    }
}

TEST_CASE("every shipped skeleton builds a dungeon whose doorways and ramps are open", "[dungeon_entry][greybox]")
{
    const DungeonSkeletonCatalog catalog = DungeonSkeletonCatalog::loadDirectory(std::filesystem::path(ICHOR_REPO_DATA) / "dungeon_skeletons");
    REQUIRE(catalog.all().size() >= 30);
    std::size_t doorways = 0;
    std::size_t ramps = 0;
    for (const DungeonSkeleton& skeleton : catalog.all()) {
        INFO(skeleton.id);
        BakedDungeon dungeon;
        dungeon.graph = skeleton.graph;
        dungeon.cells = skeleton.cells;
        const DungeonGreyBox box = buildDungeonGreyBox(dungeon, kOrigin);
        REQUIRE(box.ok());
        REQUIRE_FALSE(box.pieces.empty());
        CHECK(box.pieces.size() < greybox::kMaxPieces / 4);
        CHECK_FALSE(solidAt(box, box.spawn + glm::dvec3(0.0, 1.0, 0.0)));
        CHECK(solidAt(box, box.spawn - glm::dvec3(0.0, 0.6, 0.0)));
        const graph::embedding::Cell home = dungeon.cells.at(1);
        for (const auto& [id, edge] : dungeon.graph.edges()) {
            if (edge.type != "path") continue;
            const auto from = dungeon.cells.at(edge.from);
            const auto to = dungeon.cells.at(edge.to);
            const auto centre = [&](const graph::embedding::Cell& c) {
                return kOrigin + glm::dvec3((c.x - home.x) * greybox::kPitchM, (c.floor - home.floor) * greybox::kStoreyM, (c.y - home.y) * greybox::kPitchM);
            };
            if (from.floor == to.floor) {
                const glm::dvec3 middle = (centre(from) + centre(to)) * 0.5;
                for (const double height : {0.3, 1.0, 1.8, 2.7}) CHECK_FALSE(solidAt(box, middle + glm::dvec3(0.0, height, 0.0)));
                CHECK(solidAt(box, middle - glm::dvec3(0.0, 0.2, 0.0)));
                ++doorways;
            } else {
                const auto& low = from.floor < to.floor ? from : to;
                const double side = (low.floor & 1) == 0 ? -(5.0 - greybox::kRampWidthM * 0.5) : 5.0 - greybox::kRampWidthM * 0.5;
                const glm::dvec3 midRamp = centre(low) + glm::dvec3(side, greybox::kStoreyM * 0.5, 0.0);
                CHECK(solidAt(box, midRamp - glm::dvec3(0.0, 0.1, 0.0)));
                CHECK_FALSE(solidAt(box, midRamp + glm::dvec3(0.0, 0.1, 0.0)));
                ++ramps;
            }
        }
    }
    CHECK(doorways > 100);
    CHECK(ramps > 0);
}

TEST_CASE("the dungeon kit has a six-faced cube and a material for every piece kind", "[dungeon_entry][greybox]")
{
    const SceneBundle kit = buildDungeonKit();
    REQUIRE(kit.meshes.size() == kGreyBoxKindCount);
    REQUIRE(kit.materials.size() == kGreyBoxKindCount);
    for (std::size_t i = 0; i < kit.meshes.size(); ++i) {
        CHECK(kit.meshes[i].vertices.size() == 24);
        CHECK(kit.meshes[i].indices.size() == 36);
        CHECK(kit.meshes[i].submeshes.front().material == i);
    }
}

TEST_CASE("walking up to an entrance offers the prompt, and only in reach", "[dungeon_entry]")
{
    ForgedGame& game = *shared().game;
    const PlacedPoi& poi = firstDungeon(game);
    Rig rig(poi, game, true);
    CHECK(rig.prompt().empty());
    CHECK(nearestDungeonEntrance(game, poi.x - 30.0, poi.z, 6.0) == nullptr);
    REQUIRE(rig.walkUntilPrompt(poi.x, poi.z));
    CHECK(rig.prompt().find("Enter") == 0);
    CHECK(rig.prompt().find(poi.archetype) != std::string::npos);
    CHECK(std::hypot(playerAt(rig.scene).x - poi.x, playerAt(rig.scene).z - poi.z) <= 6.0);
    CHECK_FALSE(rig.inside());
}

TEST_CASE("Confirm held while walking in, or pressed away from an entrance, enters nothing", "[dungeon_entry]")
{
    ForgedGame& game = *shared().game;
    const PlacedPoi& poi = firstDungeon(game);
    Rig rig(poi, game, true);
    for (int i = 0; i < 40; ++i) rig.tick(steer(playerAt(rig.scene), poi.x, poi.z, InputFrame::Confirm));
    CHECK_FALSE(rig.inside());
    // Held all the way in: the press happened far away, so being in reach later is not a press.
    for (int i = 0; i < 900 && rig.prompt().empty(); ++i) rig.tick(steer(playerAt(rig.scene), poi.x, poi.z, InputFrame::Confirm));
    REQUIRE_FALSE(rig.prompt().empty());
    for (int i = 0; i < 120; ++i) rig.tick(InputFrame{.buttons = InputFrame::Confirm});
    CHECK_FALSE(rig.inside());
}

TEST_CASE("an overworld that never enters a dungeon is untouched by the entry system", "[dungeon_entry]")
{
    ForgedGame& game = *shared().game;
    const PlacedPoi& poi = firstDungeon(game);
    Rig with(poi, game, true);
    Rig without(poi, game, false);
    REQUIRE(with.scene.stateHash() == without.scene.stateHash());
    for (int i = 0; i < 240; ++i) {
        const std::uint32_t buttons = i % 7 == 0 ? InputFrame::Confirm : 0u;
        const InputFrame input = steer(playerAt(with.scene), poi.x + 20.0, poi.z + 20.0, buttons);
        with.tick(input);
        without.tick(input);
        REQUIRE(with.scene.stateHash() == without.scene.stateHash());
    }
}

TEST_CASE("entering a dungeon and leaving it returns the player to the same spot in the same overworld", "[dungeon_entry]")
{
    ForgedGame& game = *shared().game;
    const PlacedPoi& poi = firstDungeon(game);
    const std::uint64_t worldBefore = game.stateHash();
    Rig a(poi, game, true);
    REQUIRE(a.walkUntilPrompt(poi.x, poi.z));
    for (int i = 0; i < 20; ++i) a.tick();
    const glm::dvec3 stoodAt = playerAt(a.scene);
    const std::uint64_t tickBefore = a.scene.tickCount();
    const std::size_t entitiesBefore = a.scene.registry().view<StableId>().size();

    float peakFade = 0.0f;
    a.tick(InputFrame{.buttons = InputFrame::Confirm});
    for (int i = 0; i < 90 && !(a.inside() && playerAt(a.scene).x < dungeon_entry::kPocketLimitX); ++i) {
        a.tick();
        peakFade = std::max(peakFade, a.entry->fade(a.scene));
    }
    REQUIRE(a.inside());
    CHECK(inDungeonPocket(playerAt(a.scene)));
    CHECK(peakFade > 0.99f);
    CHECK(a.entry->takeViewYaw().has_value());
    CHECK_FALSE(a.entry->takeViewYaw().has_value());

    // The hall holds the player up and the time keeps running while inside.
    for (int i = 0; i < 90; ++i) a.tick();
    const CharacterBody& body = a.scene.registry().get<CharacterBody>(a.scene.find(StableId::of("player")));
    CHECK(body.onGround);
    CHECK(a.entry->fade(a.scene) == 0.0f);
    CHECK(a.scene.tickCount() > tickBefore + 100);

    const DungeonGreyBox& hall = a.entry->greyBox();
    REQUIRE(a.walkUntilPrompt(hall.exitPoint.x, hall.exitPoint.z, 400));
    CHECK(a.prompt().find("leave") != std::string::npos);
    a.tick(InputFrame{.buttons = InputFrame::Confirm});
    for (int i = 0; i < 90 && a.inside(); ++i) a.tick();
    for (int i = 0; i < 60; ++i) a.tick();
    CHECK_FALSE(a.inside());
    CHECK_FALSE(inDungeonPocket(playerAt(a.scene)));
    CHECK(a.entry->greyBox().pieces.empty());
    CHECK(a.scene.registry().view<StableId>().size() == entitiesBefore);

    const glm::dvec3 backAt = playerAt(a.scene);
    CHECK(sameSpot(backAt, stoodAt));

    CHECK(game.stateHash() == worldBefore);
}

TEST_CASE("the round trip leaves the overworld hashing as it does in a scene that stood still at the entrance", "[dungeon_entry]")
{
    ForgedGame& game = *shared().game;
    const PlacedPoi& poi = firstDungeon(game);
    // Both scenes stand 4 m from the entrance, where the prompt shows, and run the same number of ticks.
    Rig a(poi, game, true, 4.0);
    Rig b(poi, game, false, 4.0);
    REQUIRE_FALSE(a.prompt().empty());
    const glm::dvec3 stoodAt = playerAt(a.scene);
    a.tick(InputFrame{.buttons = InputFrame::Confirm});
    for (int i = 0; i < 90 && !a.inside(); ++i) a.tick();
    REQUIRE(a.inside());
    for (int i = 0; i < 30; ++i) a.tick();
    const DungeonGreyBox hall = a.entry->greyBox();
    REQUIRE(a.walkUntilPrompt(hall.exitPoint.x, hall.exitPoint.z, 400));
    a.tick(InputFrame{.buttons = InputFrame::Confirm});
    for (int i = 0; i < 90 && a.inside(); ++i) a.tick();
    for (int i = 0; i < 90; ++i) a.tick();
    REQUIRE_FALSE(a.inside());
    while (b.scene.tickCount() < a.scene.tickCount()) b.tick();
    CHECK(sameSpot(playerAt(a.scene), playerAt(b.scene)));
    CHECK(sameSpot(playerAt(a.scene), stoodAt));
    CHECK(a.scene.tickCount() == b.scene.tickCount());
    CHECK(overworldHash(a.scene) == overworldHash(b.scene));
}

TEST_CASE("the same inputs always give the same visit", "[dungeon_entry]")
{
    ForgedGame& game = *shared().game;
    const PlacedPoi& poi = firstDungeon(game);
    std::vector<std::uint64_t> trace[2];
    for (int run = 0; run < 2; ++run) {
        Rig rig(poi, game, true, 4.0);
        rig.tick(InputFrame{.buttons = InputFrame::Confirm});
        for (int i = 0; i < 150; ++i) {
            rig.tick();
            trace[run].push_back(rig.scene.stateHash());
        }
        REQUIRE(rig.inside());
    }
    CHECK(trace[0] == trace[1]);
}

TEST_CASE("walking out of reach while the screen fades cancels the visit", "[dungeon_entry]")
{
    ForgedGame& game = *shared().game;
    const PlacedPoi& poi = firstDungeon(game);
    Rig rig(poi, game, true, 5.9);
    REQUIRE_FALSE(rig.prompt().empty());
    rig.tick(InputFrame{.buttons = InputFrame::Confirm});
    for (int i = 0; i < 120; ++i) rig.tick(steer(playerAt(rig.scene), poi.x - 100.0, poi.z));
    CHECK_FALSE(rig.inside());
    CHECK_FALSE(inDungeonPocket(playerAt(rig.scene)));
    CHECK(rig.entry->fade(rig.scene) == 0.0f);
}

TEST_CASE("the autoEnter option presses Confirm once, the first time an entrance is in reach", "[dungeon_entry]")
{
    ForgedGame& game = *shared().game;
    const PlacedPoi& poi = firstDungeon(game);
    DungeonEntryParams params;
    params.autoEnter = true;
    Rig rig(poi, game, true, 4.0, params);
    for (int i = 0; i < 60 && !rig.inside(); ++i) rig.tick();
    REQUIRE(rig.inside());
    const DungeonGreyBox hall = rig.entry->greyBox();
    REQUIRE(rig.walkUntilPrompt(hall.exitPoint.x, hall.exitPoint.z, 400));
    rig.tick(InputFrame{.buttons = InputFrame::Confirm});
    for (int i = 0; i < 90 && rig.inside(); ++i) rig.tick();
    for (int i = 0; i < 90; ++i) rig.tick();
    // Back at the entrance with the entrance in reach, and it does not take the player in a second time.
    CHECK_FALSE(rig.inside());
}

TEST_CASE("a save made inside a dungeon, in the campaign folder, loads back inside and walks out", "[dungeon_entry][campaign]")
{
    ForgedGame& game = *shared().game;
    const PlacedPoi& poi = firstDungeon(game);
    const std::filesystem::path root = test::tempPath("ichor_dungeon_entry_campaign");
    std::filesystem::remove_all(root);
    REQUIRE(saveCampaignWorld(root, "alpha", descFor(1), kGrid, game) == CampaignStatus::Ok);
    const auto campaign = Campaign::open(root, "alpha");
    REQUIRE(campaign.has_value());
    SaveSlots slots = campaign->slots();

    Rig a(poi, game, true, 4.0);
    const glm::dvec3 stoodAt = playerAt(a.scene);
    a.tick(InputFrame{.buttons = InputFrame::Confirm});
    for (int i = 0; i < 90 && !a.inside(); ++i) a.tick();
    for (int i = 0; i < 60; ++i) a.tick();
    REQUIRE(a.inside());
    const glm::dvec3 insideAt = playerAt(a.scene);
    REQUIRE(slots.save("quick", a.scene, campaign->slotContext(5)));

    // A fresh process: a new scene with the system installed, then the load.
    Scene loaded(7);
    DungeonEntry entry(game, 0);
    entry.install(loaded);
    const LoadResult result = slots.load("quick", loaded, campaign->slotContext(5));
    REQUIRE(result.status == LoadStatus::Loaded);
    CHECK(entry.inside(loaded));
    CHECK(entry.greyBox().pieces.empty());
    for (int i = 0; i < 90; ++i) loaded.tick(kDt);
    CHECK(entry.inside(loaded));
    CHECK_FALSE(entry.greyBox().pieces.empty());
    CHECK(loaded.physics().hasBody(StableId::of("dungeon_box", 0)));
    const glm::dvec3 nowAt = playerAt(loaded);
    CHECK(std::abs(nowAt.x - insideAt.x) < 0.5);
    CHECK(std::abs(nowAt.z - insideAt.z) < 0.5);
    CHECK(std::abs(nowAt.y - insideAt.y) < 0.5);
    CHECK(loaded.registry().get<CharacterBody>(loaded.find(StableId::of("player"))).onGround);

    // Walk to the exit in the loaded game and come out where the first game went in.
    addGround(loaded, poi);
    const DungeonGreyBox hall = entry.greyBox();
    for (int i = 0; i < 400 && entry.prompt(loaded).empty(); ++i) loaded.tick(kDt, steer(playerAt(loaded), hall.exitPoint.x, hall.exitPoint.z));
    REQUIRE_FALSE(entry.prompt(loaded).empty());
    loaded.tick(kDt, InputFrame{.buttons = InputFrame::Confirm});
    for (int i = 0; i < 150; ++i) loaded.tick(kDt);
    CHECK_FALSE(entry.inside(loaded));
    CHECK(sameSpot(playerAt(loaded), stoodAt));

    // A save made outside restores outside, with no hall left behind.
    REQUIRE(slots.save("outside", loaded, campaign->slotContext(5)));
    std::filesystem::remove_all(root);
}

TEST_CASE("a campaign loaded from its stored files gives the same dungeon as the forged world", "[dungeon_entry][campaign]")
{
    ForgedGame& game = *shared().game;
    const PlacedPoi& poi = firstDungeon(game);
    const std::filesystem::path root = test::tempPath("ichor_dungeon_entry_stored");
    std::filesystem::remove_all(root);
    REQUIRE(saveCampaignWorld(root, "alpha", descFor(1), kGrid, game) == CampaignStatus::Ok);
    const CampaignWorldPlan plan = planCampaignWorld(root, "alpha");
    REQUIRE(plan.action == CampaignWorldAction::Load);
    const auto stored = buildStoredCampaignGame(plan, descFor(1), shared().setup.inputs);
    REQUIRE(stored->ok());
    REQUIRE(stored->dungeons);
    const DungeonLayoutResult fresh = game.dungeons->layout(poi.id);
    const DungeonLayoutResult again = stored->dungeons->layout(poi.id);
    REQUIRE(fresh.ok());
    REQUIRE(again.ok());
    CHECK(fresh.dungeon->layoutHash == again.dungeon->layoutHash);
    const DungeonGreyBox a = buildDungeonGreyBox(*fresh.dungeon, kOrigin);
    const DungeonGreyBox b = buildDungeonGreyBox(*again.dungeon, kOrigin);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    REQUIRE(a.pieces.size() == b.pieces.size());
    for (std::size_t i = 0; i < a.pieces.size(); ++i) {
        CHECK(a.pieces[i].centre == b.pieces[i].centre);
        CHECK(a.pieces[i].halfExtents == b.pieces[i].halfExtents);
    }
    std::filesystem::remove_all(root);
}

TEST_CASE("a visit marker that names no dungeon, or none that can be baked, sends the player back to the world", "[dungeon_entry]")
{
    ForgedGame& game = *shared().game;
    const PlacedPoi& poi = firstDungeon(game);
    const std::vector<std::string> names = {"dungeon_visit:poi_does_not_exist", "dungeon_visit:", "dungeon_visit:settlement_0001", "not a marker at all", std::string("dungeon_visit:") + std::string(5000, 'x')};
    for (const std::string& name : names) {
        INFO(name.substr(0, 40));
        Scene scene(3);
        addGround(scene, poi);
        DungeonEntry entry(game, 0);
        entry.install(scene);
        const glm::dvec3 world{poi.x - 15.0, static_cast<double>(poi.groundY) + 0.5, poi.z};
        spawnPlayer(scene, {dungeon_entry::kPocketX, 10.0, dungeon_entry::kPocketX});
        SpawnCommand marker;
        marker.id = StableId::of("dungeon_visit");
        marker.name.value = name;
        marker.transform.position = world;
        scene.enqueue(marker);
        for (int i = 0; i < 20; ++i) scene.tick(kDt);
        CHECK_FALSE(entry.inside(scene));
        CHECK_FALSE(inDungeonPocket(playerAt(scene)));
        CHECK(std::abs(playerAt(scene).x - world.x) < 1.0);
    }
}

TEST_CASE("a marker with a position that is not a number returns the player to the spawn", "[dungeon_entry]")
{
    ForgedGame& game = *shared().game;
    const PlacedPoi& poi = firstDungeon(game);
    Scene scene(3);
    addGround(scene, poi);
    DungeonEntry entry(game, 0);
    entry.install(scene);
    spawnPlayer(scene, {dungeon_entry::kPocketX, 10.0, dungeon_entry::kPocketX});
    SpawnCommand marker;
    marker.id = StableId::of("dungeon_visit");
    marker.name.value = "dungeon_visit:no_such_poi";
    marker.transform.position = {std::nan(""), 0.0, std::numeric_limits<double>::infinity()};
    scene.enqueue(marker);
    for (int i = 0; i < 5; ++i) scene.tick(kDt);
    CHECK_FALSE(entry.inside(scene));
    const glm::dvec3 at = playerAt(scene);
    CHECK(std::isfinite(at.x));
    CHECK(std::abs(at.x - game.spawn.x) < 1.0);
}

TEST_CASE("a player left in the dungeon pocket with no visit record is sent back to the world", "[dungeon_entry]")
{
    ForgedGame& game = *shared().game;
    const PlacedPoi& poi = firstDungeon(game);
    Scene scene(3);
    addGround(scene, poi);
    DungeonEntry entry(game, 0);
    entry.install(scene);
    spawnPlayer(scene, {dungeon_entry::kPocketX + 5.0, 3.0, dungeon_entry::kPocketX});
    for (int i = 0; i < 5; ++i) scene.tick(kDt);
    CHECK_FALSE(inDungeonPocket(playerAt(scene)));
}

TEST_CASE("a player who falls out of the dungeon is put back at its entry", "[dungeon_entry]")
{
    ForgedGame& game = *shared().game;
    const PlacedPoi& poi = firstDungeon(game);
    Rig rig(poi, game, true, 4.0);
    rig.tick(InputFrame{.buttons = InputFrame::Confirm});
    for (int i = 0; i < 90 && !rig.inside(); ++i) rig.tick();
    for (int i = 0; i < 30; ++i) rig.tick();
    REQUIRE(rig.inside());
    const DungeonGreyBox hall = rig.entry->greyBox();
    Transform fallen = rig.scene.registry().get<Transform>(rig.scene.find(StableId::of("player")));
    fallen.position.y = hall.lowestY - 50.0;
    rig.scene.enqueue(SetTransformCommand{StableId::of("player"), fallen});
    for (int i = 0; i < 40; ++i) rig.tick();
    CHECK(rig.inside());
    CHECK(playerAt(rig.scene).y > hall.lowestY);
}

TEST_CASE("the player's own state rides through a visit", "[dungeon_entry]")
{
    ForgedGame& game = *shared().game;
    const PlacedPoi& poi = firstDungeon(game);
    Rig rig(poi, game, true, 4.0);
    const entt::entity player = rig.scene.find(StableId::of("player"));
    rig.scene.registry().get<Player>(player).metresPerSecond = 4.25f;
    rig.scene.registry().get<Name>(player).value = "carries a pack";
    rig.tick(InputFrame{.buttons = InputFrame::Confirm});
    for (int i = 0; i < 90 && !rig.inside(); ++i) rig.tick();
    REQUIRE(rig.inside());
    const entt::entity same = rig.scene.find(StableId::of("player"));
    CHECK(same == player);
    CHECK(rig.scene.registry().get<Player>(same).metresPerSecond == 4.25f);
    CHECK(rig.scene.registry().get<Name>(same).value == "carries a pack");
}

TEST_CASE("the interior look turns the sky and the weather off, and the fade only darkens", "[dungeon_entry]")
{
    RenderView view;
    view.skyEnabled = true;
    view.rainIntensity = 1.0f;
    view.waterEnabled = true;
    view.fogDensity = 0.0f;
    applyDungeonInterior(view);
    CHECK_FALSE(view.skyEnabled);
    CHECK(view.rainIntensity == 0.0f);
    CHECK_FALSE(view.waterEnabled);
    CHECK(view.fogDensity > 0.0f);
    applyFade(view, 0.0f);
    CHECK(view.grade.exposure == 1.0f);
    applyFade(view, 1.0f);
    CHECK(view.grade.exposure == 0.0f);
    view.grade.exposure = 1.0f;
    applyFade(view, 5.0f);
    CHECK(view.grade.exposure == 0.0f);
    view.grade.exposure = 1.0f;
    applyFade(view, std::nanf(""));
    CHECK(view.grade.exposure == 1.0f);
}

TEST_CASE("spawning at a dungeon puts the player in reach, facing it", "[dungeon_entry]")
{
    ForgedGame& game = *shared().game;
    CHECK_FALSE(spawnAtDungeon(game, 100000));
    const ForgedSpawn original = game.spawn;
    bool any = false;
    for (std::size_t i = 0; i < 8; ++i) {
        if (!spawnAtDungeon(game, i)) continue;
        any = true;
        const PlacedPoi* near = nearestDungeonEntrance(game, game.spawn.x, game.spawn.z, 6.0);
        REQUIRE(near != nullptr);
        CHECK(game.spawn.faceX == near->x);
        CHECK(game.spawn.faceZ == near->z);
    }
    game.spawn = original;
    CHECK(any);
}
