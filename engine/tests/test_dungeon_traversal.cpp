#include "ichor/graph/dungeon_forge.h"
#include "ichor/sim/scene.h"
#include "ichor/world/dungeon_content.h"
#include "ichor/world/dungeon_entry.h"
#include "ichor/world/dungeon_greybox.h"
#include "ichor/world/placed_dungeons.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace ichor;

namespace {

constexpr double kDt = 1.0 / 60.0;
constexpr int kLegTickLimit = 60 * 60; // a waypoint a minute away is a stuck body
constexpr int kQuietTicks = 120;       // 2 s without moving 5 cm

struct Traversal {
    bool reached = false;
    std::string stuck;           // empty when reached
    graph::NodeId objective = 0;
    std::size_t rooms = 0;
    double seconds = 0.0;
};

// The bot walks the player's character body, under the scene's real physics, through the grey-box's own doorways and ramps
// from the entry room to the objective, one room at a time. It knows the rooms and where the openings lie, as the playthrough
// bot does; it does not know whether the body fits through them, which is what is under test.
class Bot {
public:
    explicit Bot(const DungeonSkeleton& skeleton)
        : scene_(7)
    {
        dungeon_.skeletonId = skeleton.id;
        dungeon_.archetype = skeleton.theme;
        dungeon_.graph = skeleton.graph;
        dungeon_.cells = skeleton.cells;
        box_ = buildDungeonGreyBox(dungeon_, glm::dvec3(dungeon_entry::kPocketX, 0.0, dungeon_entry::kPocketX));
        if (!box_.ok()) return;
        content_ = buildDungeonContent(dungeon_, box_, {});
        for (std::size_t i = 0; i < box_.pieces.size(); ++i) {
            Transform transform;
            transform.position = box_.pieces[i].centre;
            transform.rotation = box_.pieces[i].rotation;
            scene_.physics().addStaticBox(StableId::of("dungeon_box", i), StaticBox{box_.pieces[i].halfExtents}, transform);
        }
        SpawnCommand player;
        player.id = StableId::of("player");
        player.name.value = "player";
        player.transform.position = box_.spawn;
        player.player = Player{};
        player.character = CharacterBody{};
        scene_.enqueue(player);
        for (int i = 0; i < 30; ++i) scene_.tick(kDt);
    }

    Traversal run()
    {
        Traversal out;
        if (!box_.ok()) {
            out.stuck = "the grey-box does not build: " + box_.error;
            return out;
        }
        out.rooms = content_.rooms.size();
        const DungeonRoom* start = content_.roomAt(at());
        if (start == nullptr) {
            out.stuck = std::format("spawned in no room at ({:.1f}, {:.2f}, {:.1f})", at().x, at().y, at().z);
            return out;
        }
        out.objective = objective(start->id);
        if (out.objective == 0) {
            out.stuck = "no objective: the dungeon has no climax room and no other room is reachable";
            return out;
        }
        const std::uint64_t first = scene_.tickCount();
        out.reached = walkRooms(*start, out.objective, out.stuck);
        out.seconds = static_cast<double>(scene_.tickCount() - first) * kDt;
        return out;
    }

private:
    enum class Leg : std::uint8_t { Arrived, Stuck };

    glm::dvec3 at() const
    {
        const entt::entity e = scene_.find(StableId::of("player"));
        return e == entt::null ? glm::dvec3(0.0) : scene_.registry().get<Transform>(e).position;
    }

    // The climax room when there is one, else the reachable room farthest from the entry by rooms.
    graph::NodeId objective(graph::NodeId from) const
    {
        if (content_.bossRoom != 0) return content_.bossRoom;
        graph::NodeId best = 0;
        std::size_t bestLength = 0;
        for (const DungeonRoom& room : content_.rooms) {
            const std::optional<std::vector<RoomStep>> steps = routeBetweenRooms(box_, from, room.id);
            if (steps && steps->size() > bestLength) {
                best = room.id;
                bestLength = steps->size();
            }
        }
        return best;
    }

    Leg steerTo(double x, double z, double within, std::string& why)
    {
        glm::dvec3 anchor = at();
        int quiet = 0;
        for (int i = 0; i < kLegTickLimit; ++i) {
            const glm::dvec3 here = at();
            const double dx = x - here.x;
            const double dz = z - here.z;
            const double length = std::hypot(dx, dz);
            if (length <= within) return Leg::Arrived;
            if (here.y < box_.lowestY) {
                why = std::format("fell out of the dungeon at ({:.2f}, {:.2f}, {:.2f})", here.x, here.y, here.z);
                return Leg::Stuck;
            }
            InputFrame frame;
            frame.moveX = static_cast<float>(dx / length);
            frame.moveY = static_cast<float>(-dz / length);
            scene_.tick(kDt, frame);
            if (glm::length(at() - anchor) > 0.05) {
                anchor = at();
                quiet = 0;
            } else if (++quiet >= kQuietTicks) {
                why = std::format("stopped at ({:.2f}, {:.2f}, {:.2f}) heading for ({:.1f}, {:.1f})", here.x, here.y, here.z, x, z);
                return Leg::Stuck;
            }
        }
        const glm::dvec3 here = at();
        why = std::format("ran out of time at ({:.2f}, {:.2f}, {:.2f}) heading for ({:.1f}, {:.1f})", here.x, here.y, here.z, x, z);
        return Leg::Stuck;
    }

    // Where the ramp of a room lies, as an offset from its centre along x: the plank is the only tilted floor piece.
    double rampOffset(const glm::dvec3& lowerCentre) const
    {
        for (const GreyBoxPiece& piece : box_.pieces) {
            if (piece.kind != GreyBoxKind::Floor || std::abs(piece.rotation.w - 1.0f) < 1e-4f) continue;
            if (std::abs(piece.centre.z - lowerCentre.z) < 0.5 && std::abs(piece.centre.y - (lowerCentre.y + 2.25)) < 0.5
                && std::abs(piece.centre.x - lowerCentre.x) < greybox::kRoomHalfM) {
                return piece.centre.x - lowerCentre.x;
            }
        }
        return 0.0;
    }

    // The path links of the layout with a reachable room at one end and an unreachable one at the other: where the grey-box
    // fails to join what the layout joins.
    std::string blockedLinks(graph::NodeId from) const
    {
        const auto reachable = [&](graph::NodeId id) { return id == from || routeBetweenRooms(box_, from, id).has_value(); };
        std::string out;
        for (const auto& [id, edge] : dungeon_.graph.edges()) {
            if (edge.type != "path" || reachable(edge.from) == reachable(edge.to)) continue;
            const auto a = dungeon_.cells.at(edge.from);
            const auto b = dungeon_.cells.at(edge.to);
            out += std::format("{}blocked link {}>{} (cell {},{} floor {} to cell {},{} floor {})", out.empty() ? "" : "; ", edge.from, edge.to, a.x, a.y, a.floor, b.x, b.y, b.floor);
        }
        return out.empty() ? "no path link is cut, the objective is joined only by unlocks or shortcut links" : out;
    }

    bool walkRooms(const DungeonRoom& start, graph::NodeId to, std::string& stuck)
    {
        constexpr double R = greybox::kRoomHalfM;
        graph::NodeId hereId = start.id;
        while (hereId != to) {
            const std::optional<std::vector<RoomStep>> steps = routeBetweenRooms(box_, hereId, to);
            if (!steps || steps->empty()) {
                stuck = std::format("the grey-box has no doorway or ramp route from room {} to room {} (unreachable objective); {}", hereId, to, blockedLinks(hereId));
                return false;
            }
            const RoomStep& step = steps->front();
            const glm::dvec3 a = content_.room(hereId)->centre;
            const glm::dvec3 b = content_.room(step.room)->centre;
            std::vector<glm::dvec2> via;
            const char* what = "doorway";
            if (step.link == RoomLink::Door) {
                via.push_back({(a.x + b.x) * 0.5, (a.z + b.z) * 0.5});
            } else {
                const glm::dvec3& lower = step.link == RoomLink::RampUp ? a : b;
                const double xs = rampOffset(lower);
                what = step.link == RoomLink::RampUp ? "ramp up" : "ledge down the ramp";
                if (step.link == RoomLink::RampUp) {
                    via = {{a.x, a.z - (R - 0.5)}, {a.x + xs, a.z - (R - 0.5)}, {a.x + xs, a.z + (R - 0.4)}, {a.x, a.z + (R - 0.4)}};
                } else {
                    via = {{a.x, a.z + (R - 0.4)}, {a.x + xs, a.z + (R - 0.4)}, {a.x + xs, a.z - (R - 0.5)}, {a.x, a.z - (R - 0.5)}};
                }
            }
            via.push_back({b.x, b.z});
            std::string why;
            for (const glm::dvec2& point : via) {
                if (steerTo(point.x, point.y, 0.5, why) == Leg::Stuck) {
                    stuck = std::format("{} from room {} to room {}: {}", what, hereId, step.room, why);
                    return false;
                }
            }
            if (std::abs(at().y - b.y) > 1.5) {
                stuck = std::format("{} from room {} to room {}: not on the floor of room {} (feet at y {:.2f}, floor {:.2f})", what, hereId, step.room, step.room, at().y, b.y);
                return false;
            }
            hereId = step.room;
        }
        return true;
    }

    Scene scene_;
    BakedDungeon dungeon_;
    DungeonGreyBox box_;
    DungeonContent content_;
};

// Skeletons whose objective the grey-box does not join to the entry today (SEED-765 found them). The night is red for a
// skeleton off this list that is stuck, and for one on it that now passes: then remove it here.
constexpr std::array<std::string_view, 14> kKnownStuck = {"sunless_aqueduct", "storm_bastion", "bramble_hollow", "salt_cavern", "cistern_maze", "iron_citadel",
    "ash_crater", "sunken_crypt", "ring_fort", "foundry_depths", "thorn_gardens", "coral_grotto", "clockwork_keep", "gilded_menagerie"};

bool knownStuck(std::string_view id) { return std::find(kKnownStuck.begin(), kKnownStuck.end(), id) != kKnownStuck.end(); }

// Empty when the result is what the list says it should be, else what to do about it.
std::string verdict(std::string_view id, bool listed, bool reached, const std::string& stuck)
{
    if (!reached && !listed) return std::string(id) + ": " + stuck;
    if (reached && listed) return std::string(id) + " now reaches its objective: remove it from kKnownStuck in engine/tests/test_dungeon_traversal.cpp";
    return {};
}

const DungeonSkeletonCatalog& catalog()
{
    static const DungeonSkeletonCatalog loaded = DungeonSkeletonCatalog::loadDirectory(std::filesystem::path(ICHOR_REPO_DATA) / "dungeon_skeletons");
    return loaded;
}

// Walks the skeletons at these indices of the catalogue's (theme, id) order; every line is printed so the log names the
// skeleton and where a body stuck.
int traverse(const std::vector<std::size_t>& indices)
{
    const std::vector<DungeonSkeleton>& all = catalog().all();
    const std::size_t count = indices.size();
    int stuck = 0;
    for (const std::size_t i : indices) {
        REQUIRE(i < all.size());
        const DungeonSkeleton& skeleton = all[i];
        BakedDungeon baked;
        baked.graph = skeleton.graph;
        baked.cells = skeleton.cells;
        const std::uint64_t hash = graph::forge::layoutHash(baked.graph, baked.cells);
        const Traversal result = Bot(skeleton).run();
        std::printf("%s\n", std::format("TRAVERSE #{} {} ({}), layout hash {:016x}: {} | {} rooms, objective room {}, {:.1f} s scene{}{}", i, skeleton.id, skeleton.theme, hash,
                                  result.reached ? "ok" : "STUCK", result.rooms, result.objective, result.seconds, result.stuck.empty() ? "" : ", ", result.stuck).c_str());
        const std::string problem = verdict(skeleton.id, knownStuck(skeleton.id), result.reached, result.stuck);
        if (!problem.empty()) {
            ++stuck;
            FAIL_CHECK(problem);
        }
    }
    std::printf("%s\n", std::format("TRAVERSE summary: {} of {} skeleton(s) were as expected (known stuck ones included), {} not", count - static_cast<std::size_t>(stuck), count, stuck).c_str());
    std::fflush(stdout);
    return stuck;
}

}

// The pull request variant walks three skeletons by id, the ones whose objective the grey-box joins to the entry today
// (the nightly case lists the rest), so a regression in the grey-box or the body goes red here.
TEST_CASE("the traversal bot walks three dungeon skeletons to their objective with real physics", "[dungeon_traversal]")
{
    std::vector<std::size_t> indices;
    for (const char* id : {"lantern_catacomb", "smuggler_cellar", "mirror_labyrinth"}) {
        const std::vector<DungeonSkeleton>& all = catalog().all();
        for (std::size_t i = 0; i < all.size(); ++i) {
            if (all[i].id == id) indices.push_back(i);
        }
    }
    REQUIRE(indices.size() == 3);
    CHECK(traverse(indices) == 0);
}

// ICHOR_NIGHTLY_DUNGEON_BOT_SKELETONS is how many skeletons to walk (20 in the nightly workflow).
TEST_CASE("nightly: the dungeon traversal bot walks every baked skeleton to its objective or names where it stuck", "[dungeon_traversal][nightly]")
{
    const char* text = std::getenv("ICHOR_NIGHTLY_DUNGEON_BOT_SKELETONS");
    if (text == nullptr || text[0] == '\0') SKIP("set ICHOR_NIGHTLY_DUNGEON_BOT_SKELETONS (for example 20) to run the nightly dungeon traversal");
    const long count = std::strtol(text, nullptr, 10);
    REQUIRE(count > 0);
    std::vector<std::size_t> indices;
    for (std::size_t i = 0; i < static_cast<std::size_t>(count) && i < catalog().all().size(); ++i) indices.push_back(i);
    REQUIRE(indices.size() == static_cast<std::size_t>(count));
    CHECK(traverse(indices) == 0);
}

TEST_CASE("the known-stuck list fails an unlisted stuck skeleton and a listed one that now passes", "[dungeon_traversal]")
{
    CHECK(verdict("a", false, true, "").empty());
    CHECK(verdict("a", true, false, "no route").empty());
    CHECK(verdict("a", false, false, "no route").find("no route") != std::string::npos);
    CHECK(verdict("a", true, true, "").find("remove it from kKnownStuck") != std::string::npos);
    CHECK(knownStuck("sunken_crypt"));
    CHECK_FALSE(knownStuck("smuggler_cellar"));
    for (const std::string_view id : kKnownStuck) {
        const auto& all = catalog().all();
        CHECK(std::any_of(all.begin(), all.end(), [&](const DungeonSkeleton& k) { return k.id == id; }));
    }
}
