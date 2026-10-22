#include "ichor/world/dungeon_entry.h"

#include "ichor/core/log.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <string_view>

namespace ichor {

namespace {

StableId markerId() { return StableId::of("dungeon_visit"); }
StableId playerId() { return StableId::of("player"); }
StableId boxId(std::size_t index) { return StableId::of("dungeon_box", index); }

bool finite(const glm::dvec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

struct PlayerRef {
    entt::entity entity = entt::null;
    glm::dvec3 position{0.0};
};

PlayerRef findPlayer(const Scene& scene)
{
    PlayerRef found;
    for (auto [entity, transform, player] : scene.registry().view<const Transform, const Player>().each()) {
        found.entity = entity;
        found.position = transform.position;
        break;
    }
    return found;
}

std::string poiOfMarker(const Scene& scene, entt::entity marker)
{
    const auto* named = scene.registry().try_get<Name>(marker);
    if (named == nullptr) return {};
    const std::string_view name = named->value;
    const std::string_view prefix = dungeon_entry::kMarkerPrefix;
    return name.substr(0, prefix.size()) == prefix ? std::string(name.substr(prefix.size())) : std::string();
}

double horizontal(const glm::dvec3& a, const glm::dvec3& b) { return std::hypot(a.x - b.x, a.z - b.z); }

}

const PlacedPoi* nearestDungeonEntrance(const ForgedGame& game, double x, double z, double reachM)
{
    if (!game.pois || !std::isfinite(x) || !std::isfinite(z) || !(reachM > 0.0)) return nullptr;
    const PlacedPoi* best = nullptr;
    double bestDistance = reachM;
    for (const PlacedPoi& poi : game.pois->all()) {
        if (poi.kind != PlacedPoiKind::Dungeon) continue;
        const double distance = std::hypot(poi.x - x, poi.z - z);
        if (distance <= bestDistance && (best == nullptr || distance < bestDistance)) {
            best = &poi;
            bestDistance = distance;
        }
    }
    return best;
}

bool spawnAtDungeon(ForgedGame& game, std::size_t index, double reachM)
{
    if (!game.pois) return false;
    const PlacedPoi* poi = nullptr;
    std::size_t seen = 0;
    for (const PlacedPoi& candidate : game.pois->all()) {
        if (candidate.kind != PlacedPoiKind::Dungeon) continue;
        if (seen++ == index) {
            poi = &candidate;
            break;
        }
    }
    if (poi == nullptr) return false;
    constexpr double kClearM = 3.0;
    ForgedSpawn spawn = findForgedSpawn(game.heightfield, game.waterField.get(), {{poi->x, poi->z}}, [&](double x, double z) { return std::hypot(x - poi->x, z - poi->z) < kClearM; });
    if (!spawn.found || std::hypot(spawn.x - poi->x, spawn.z - poi->z) > reachM * 0.9) return false;
    spawn.faceX = poi->x;
    spawn.faceZ = poi->z;
    game.spawn = spawn;
    return true;
}

DungeonEntry::DungeonEntry(ForgedGame& game, std::uint32_t kitFirstMesh, DungeonEntryParams params)
    : game_(game)
    , kitFirstMesh_(kitFirstMesh)
    , params_(params)
{
}

void DungeonEntry::install(Scene& scene)
{
    scene.addSystem("dungeon_entry", [this](Scene& s, double, const InputFrame& input) { step(s, input); });
}

bool DungeonEntry::inside(const Scene& scene) const { return scene.find(markerId()) != entt::null; }

std::string DungeonEntry::prompt(const Scene& scene) const
{
    if (phase_ != Phase::Idle) return {};
    const PlayerRef player = findPlayer(scene);
    if (player.entity == entt::null) return {};
    if (inside(scene)) {
        const bool near = horizontal(player.position, current_.exitPoint) <= params_.exitReachM && std::abs(player.position.y - current_.exitPoint.y) < 3.0;
        return near && !current_.pieces.empty() ? "Enter: leave the dungeon" : std::string();
    }
    const PlacedPoi* poi = nearestDungeonEntrance(game_, player.position.x, player.position.z, params_.reachM);
    if (poi == nullptr) return {};
    if (!game_.dungeons || !game_.dungeons->layout(poi->id).ok()) return "The way into the " + poi->archetype + " is sealed";
    return "Enter: go into the " + poi->archetype;
}

float DungeonEntry::fade(const Scene& scene) const
{
    const std::uint64_t tick = scene.tickCount();
    if (phase_ == Phase::Idle || tick < phaseTick_) return 0.0f;
    const double elapsed = static_cast<double>(tick - phaseTick_);
    if (elapsed > std::max(params_.fadeOutTicks, params_.fadeInTicks)) return 0.0f; // a load moved the clock: no fade is running
    if (phase_ == Phase::FadingOut) {
        return params_.fadeOutTicks == 0 ? 1.0f : static_cast<float>(std::clamp(elapsed / params_.fadeOutTicks, 0.0, 1.0));
    }
    return params_.fadeInTicks == 0 ? 0.0f : static_cast<float>(std::clamp(1.0 - elapsed / params_.fadeInTicks, 0.0, 1.0));
}

std::optional<double> DungeonEntry::takeViewYaw()
{
    const std::optional<double> yaw = viewYaw_;
    viewYaw_.reset();
    return yaw;
}

void DungeonEntry::appendInstances(RenderView& view) const
{
    for (const GreyBoxPiece& piece : current_.pieces) {
        RenderInstance instance;
        instance.mesh = kitFirstMesh_ + static_cast<std::uint32_t>(piece.kind);
        instance.position = piece.centre;
        instance.basis = glm::mat3_cast(piece.rotation) * glm::mat3(glm::vec3(2.0f * piece.halfExtents.x, 0.0f, 0.0f), glm::vec3(0.0f, 2.0f * piece.halfExtents.y, 0.0f), glm::vec3(0.0f, 0.0f, 2.0f * piece.halfExtents.z));
        view.instances.push_back(instance);
    }
}

bool DungeonEntry::buildGeometry(Scene& scene, const std::string& poiId)
{
    removeGeometry(scene);
    current_ = DungeonGreyBox{};
    if (!game_.dungeons) return false;
    const DungeonLayoutResult layout = game_.dungeons->layout(poiId);
    if (!layout.ok()) {
        logf(LogLevel::Warn, "dungeon {} cannot be entered: {}", poiId, layout.error);
        return false;
    }
    DungeonGreyBox box = buildDungeonGreyBox(*layout.dungeon, glm::dvec3(dungeon_entry::kPocketX, 0.0, dungeon_entry::kPocketX));
    if (!box.ok()) {
        logf(LogLevel::Warn, "dungeon {} has no grey-box: {}", poiId, box.error);
        return false;
    }
    for (std::size_t i = 0; i < box.pieces.size(); ++i) {
        const GreyBoxPiece& piece = box.pieces[i];
        Transform transform;
        transform.position = piece.centre;
        transform.rotation = piece.rotation;
        scene.physics().addStaticBox(boxId(i), StaticBox{piece.halfExtents}, transform);
    }
    boxes_ = box.pieces.size();
    current_ = std::move(box);
    return true;
}

void DungeonEntry::removeGeometry(Scene& scene)
{
    for (std::size_t i = 0; scene.physics().hasBody(boxId(i)); ++i) {
        scene.physics().remove(boxId(i));
    }
    boxes_ = 0;
    current_ = DungeonGreyBox{};
}

void DungeonEntry::enter(Scene& scene, const PlacedPoi& poi)
{
    const PlayerRef player = findPlayer(scene);
    if (player.entity == entt::null || !buildGeometry(scene, poi.id)) {
        phase_ = Phase::Idle;
        return;
    }
    SpawnCommand marker;
    marker.id = markerId();
    marker.name.value = std::string(dungeon_entry::kMarkerPrefix) + poi.id;
    marker.transform.position = player.position;
    scene.enqueue(marker);
    Transform moved = scene.registry().get<Transform>(player.entity);
    moved.position = current_.spawn;
    scene.enqueue(SetTransformCommand{playerId(), moved});
    viewYaw_ = current_.spawnYaw;
    logf(LogLevel::Info, "entered dungeon {} ({}) at tick {}", poi.id, poi.archetype, scene.tickCount());
}

void DungeonEntry::leave(Scene& scene)
{
    const entt::entity marker = scene.find(markerId());
    const PlayerRef player = findPlayer(scene);
    removeGeometry(scene);
    if (marker == entt::null) return;
    const auto* where = scene.registry().try_get<Transform>(marker);
    glm::dvec3 back = where != nullptr ? where->position : glm::dvec3(std::nan(""));
    if (!finite(back) || inDungeonPocket(back)) {
        back = glm::dvec3(game_.spawn.x, static_cast<double>(game_.spawn.groundY) + 0.5, game_.spawn.z);
    }
    scene.enqueue(DestroyCommand{markerId()});
    if (player.entity != entt::null) {
        Transform moved = scene.registry().get<Transform>(player.entity);
        moved.position = back;
        scene.enqueue(SetTransformCommand{playerId(), moved});
    }
    logf(LogLevel::Info, "left the dungeon at tick {}", scene.tickCount());
}

bool DungeonEntry::advance(Scene& scene)
{
    const std::uint64_t tick = scene.tickCount();
    if (phase_ == Phase::Idle) return false;
    // A load can put the clock anywhere: a fade that does not line up with it is dropped.
    if (tick < phaseTick_) {
        phase_ = Phase::Idle;
        return false;
    }
    const std::uint64_t elapsed = tick - phaseTick_;
    if (phase_ == Phase::FadingOut) {
        if (elapsed < params_.fadeOutTicks) return false;
        if (elapsed > params_.fadeOutTicks) {
            phase_ = Phase::Idle;
            return false;
        }
        if (leaving_) {
            if (inside(scene)) leave(scene);
        } else if (!inside(scene)) {
            const PlayerRef player = findPlayer(scene);
            const PlacedPoi* poi = game_.pois ? game_.pois->find(pendingPoi_) : nullptr;
            if (poi != nullptr && player.entity != entt::null && nearestDungeonEntrance(game_, player.position.x, player.position.z, params_.reachM) == poi) {
                enter(scene, *poi);
            } else {
                phase_ = Phase::Idle;
                return false;
            }
        }
        if (phase_ == Phase::FadingOut) {
            phase_ = Phase::FadingIn;
            phaseTick_ = tick;
        }
        return true;
    }
    if (elapsed >= params_.fadeInTicks) phase_ = Phase::Idle;
    return false;
}

void DungeonEntry::step(Scene& scene, const InputFrame& input)
{
    const bool confirm = (input.buttons & InputFrame::Confirm) != 0;
    const bool pressed = confirm && !confirmHeld_;
    confirmHeld_ = confirm;
    // A swap queued commands that apply next tick, so nothing else is decided on this one.
    if (advance(scene)) return;

    const PlayerRef player = findPlayer(scene);
    if (player.entity == entt::null) return;
    const entt::entity marker = scene.find(markerId());
    if (marker == entt::null) {
        if (boxes_ != 0 || !current_.pieces.empty()) removeGeometry(scene);
        if (inDungeonPocket(player.position)) {
            // In the dungeon with no record of coming in: a hand-made or damaged save. Back to the world.
            Transform moved = scene.registry().get<Transform>(player.entity);
            moved.position = glm::dvec3(game_.spawn.x, static_cast<double>(game_.spawn.groundY) + 0.5, game_.spawn.z);
            scene.enqueue(SetTransformCommand{playerId(), moved});
            return;
        }
        if (phase_ != Phase::Idle) return;
        const PlacedPoi* poi = nearestDungeonEntrance(game_, player.position.x, player.position.z, params_.reachM);
        if (poi == nullptr) return;
        const bool autoNow = params_.autoEnter && !autoSpent_;
        if (!pressed && !autoNow) return;
        if (!game_.dungeons || !game_.dungeons->layout(poi->id).ok()) return;
        autoSpent_ = autoSpent_ || autoNow;
        pendingPoi_ = poi->id;
        leaving_ = false;
        phase_ = Phase::FadingOut;
        phaseTick_ = scene.tickCount();
        return;
    }

    // Inside. The boxes are not saved, so a restored scene brings them back from the layout.
    if (!scene.physics().hasBody(boxId(0))) {
        if (!buildGeometry(scene, poiOfMarker(scene, marker))) {
            leave(scene);
            phase_ = Phase::Idle;
            return;
        }
        // Where the player stands is left to the save, unless it is out of the dungeon.
    }
    if (player.position.y < current_.lowestY || !inDungeonPocket(player.position)) {
        Transform moved = scene.registry().get<Transform>(player.entity);
        moved.position = current_.spawn;
        scene.enqueue(SetTransformCommand{playerId(), moved});
        return;
    }
    if (phase_ != Phase::Idle || !pressed) return;
    if (horizontal(player.position, current_.exitPoint) <= params_.exitReachM && std::abs(player.position.y - current_.exitPoint.y) < 3.0) {
        leaving_ = true;
        phase_ = Phase::FadingOut;
        phaseTick_ = scene.tickCount();
    }
}

void applyDungeonInterior(RenderView& view)
{
    view.skyEnabled = false;
    view.clearColor = {0.01f, 0.01f, 0.014f};
    view.sunDirection = glm::normalize(glm::vec3(0.35f, 1.0f, 0.25f));
    view.sunColor = {1.0f, 0.82f, 0.62f};
    view.sunIntensity = 0.6f;
    view.ambient = {0.62f, 0.56f, 0.5f};
    view.fogColor = {0.02f, 0.02f, 0.03f};
    view.fogDensity = 0.012f;
    view.fogHeightFalloff = 0.0f;
    view.rainIntensity = 0.0f;
    view.wetness = 0.0f;
    view.windStrength = 0.0f;
    view.cloudCover = 0.0f;
    view.lightShafts = 0.0f;
    view.groundBlendHeight = 0.0f;
    view.waterEnabled = false;
}

void applyFade(RenderView& view, float fade)
{
    if (!std::isfinite(fade)) return;
    view.grade.exposure *= 1.0f - std::clamp(fade, 0.0f, 1.0f);
}

}
