#include "ichor/sim/scene.h"

#include "ichor/core/binary.h"
#include "ichor/core/dmath.h"
#include "ichor/core/hash.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <bit>
#include <cmath>

namespace ichor {

Scene::Scene(std::uint64_t seed)
    : seed_(seed)
    , physics_(std::make_unique<PhysicsWorld>())
{
}

Scene::~Scene() = default;

void Scene::enqueue(Command command)
{
    pending_.push_back(std::move(command));
}

entt::entity Scene::find(StableId id) const
{
    const auto found = index_.find(id.value);
    return found == index_.end() ? entt::null : found->second;
}

Rng& Scene::rng(std::string_view system)
{
    auto found = rngs_.find(system);
    if (found == rngs_.end()) {
        found = rngs_.emplace(std::string(system), Rng(seed_).derive(system)).first;
    }
    return found->second;
}

void Scene::restoreClock(Clock clock)
{
    tickCount_ = clock.tick;
    time_ = clock.time;
}

void Scene::restoreRng(const std::string& system, const Rng& rng)
{
    rngs_.insert_or_assign(system, rng);
}

void Scene::clearForRestore()
{
    registry_.clear();
    index_.clear();
    physics_->clear();
    rngs_.clear();
    pending_.clear();
    log_.entries.clear();
}

void Scene::apply(const Command& command)
{
    std::visit([&](const auto& c) {
        using T = std::decay_t<decltype(c)>;
        if constexpr (std::is_same_v<T, SpawnCommand>) {
            if (find(c.id) != entt::null) {
                return;
            }
            const entt::entity entity = registry_.create();
            index_.emplace(c.id.value, entity);
            registry_.emplace<StableId>(entity, c.id);
            registry_.emplace<Name>(entity, c.name);
            registry_.emplace<Transform>(entity, c.transform);
            registry_.emplace<PreviousTransform>(entity, c.transform.position, c.transform.rotation);
            if (c.mesh) registry_.emplace<MeshRef>(entity, *c.mesh);
            if (c.spin) registry_.emplace<Spin>(entity, *c.spin);
            if (c.player) registry_.emplace<Player>(entity, *c.player);
            if (c.staticBox) {
                registry_.emplace<StaticBox>(entity, *c.staticBox);
                physics_->addStaticBox(c.id, *c.staticBox, c.transform);
            }
            if (c.character) {
                registry_.emplace<CharacterBody>(entity, *c.character);
                physics_->addCharacter(c.id, *c.character, c.transform);
            }
        } else if constexpr (std::is_same_v<T, DestroyCommand>) {
            const entt::entity entity = find(c.id);
            if (entity != entt::null) {
                registry_.destroy(entity);
                index_.erase(c.id.value);
                physics_->remove(c.id);
            }
        } else if constexpr (std::is_same_v<T, MoveCommand>) {
            const entt::entity entity = find(c.id);
            if (entity != entt::null) {
                Transform& transform = registry_.get<Transform>(entity);
                transform.position += c.delta;
                // A teleport must not be interpolated across.
                registry_.get<PreviousTransform>(entity).position = transform.position;
                if (registry_.all_of<CharacterBody>(entity)) {
                    physics_->setCharacterPosition(c.id, transform.position);
                }
            }
        }
    }, command);
}

void Scene::applyInput(const InputFrame& input, double dt)
{
    // Replay files are data: clamp the stick to the unit disc and drop anything that is not a number.
    glm::dvec2 stick(std::isfinite(input.moveX) ? input.moveX : 0.0f, std::isfinite(input.moveY) ? input.moveY : 0.0f);
    const double length = glm::length(stick);
    if (length > 1.0) {
        stick /= length;
    }
    for (auto [entity, id, player] : registry_.view<const StableId, const Player>().each()) {
        const double speed = player.metresPerSecond * ((input.buttons & InputFrame::Sprint) != 0 ? 2.0 : 1.0);
        const glm::dvec3 velocity(stick.x * speed, 0.0, -stick.y * speed);
        if (auto* body = registry_.try_get<CharacterBody>(entity)) {
            body->intent = glm::vec3(velocity);
            physics_->setCharacterIntent(id, body->intent);
            continue;
        }
        if (stick.x == 0.0 && stick.y == 0.0) {
            continue;
        }
        MoveCommand move;
        move.id = id;
        move.delta = velocity * dt;
        log_.entries.push_back({tickCount_, move});
        apply(move);
    }
}

void Scene::rememberPreviousTransforms()
{
    for (auto [entity, transform, previous] : registry_.view<const Transform, PreviousTransform>().each()) {
        previous.position = transform.position;
        previous.rotation = transform.rotation;
    }
}

void Scene::spinSystem(double dt)
{
    for (auto [entity, transform, spin] : registry_.view<Transform, const Spin>().each()) {
        // Built by hand from deterministic sine and cosine; glm::angleAxis would call libm.
        const float half = 0.5f * spin.radiansPerSecond * static_cast<float>(dt);
        const glm::vec3 axis = glm::normalize(spin.axis);
        const float s = dmath::sin(half);
        const glm::quat delta(dmath::cos(half), axis.x * s, axis.y * s, axis.z * s);
        transform.rotation = glm::normalize(delta * transform.rotation);
    }
}

void Scene::physicsSystem(double dt)
{
    physics_->step(dt);
    for (auto [entity, id, transform, body] : registry_.view<const StableId, Transform, CharacterBody>().each()) {
        PhysicsWorld::CharacterState state;
        if (physics_->character(id, state)) {
            transform.position = state.position;
            body.velocity = state.velocity;
            body.fallSpeed = state.fallSpeed;
            body.onGround = state.onGround;
        }
    }
}

void Scene::tick(double dt, const InputFrame& input)
{
    rememberPreviousTransforms();
    for (const Command& command : pending_) {
        log_.entries.push_back({tickCount_, command});
        apply(command);
    }
    pending_.clear();

    applyInput(input, dt);
    spinSystem(dt);
    physicsSystem(dt);

    ++tickCount_;
    time_ += dt;
}

std::uint64_t Scene::stateHash() const
{
    std::uint64_t h = hashCombine(seed_, tickCount_);
    h = hashCombine(h, std::bit_cast<std::uint64_t>(time_));
    for (const auto& [name, rng] : rngs_) {
        h = hashCombine(h, hashCombine(hash64(name), rng.state()));
    }

    std::vector<entt::entity> entities;
    for (auto entity : registry_.view<const StableId>()) {
        entities.push_back(entity);
    }
    std::sort(entities.begin(), entities.end(), [&](entt::entity a, entt::entity b) {
        return registry_.get<StableId>(a) < registry_.get<StableId>(b);
    });

    BinaryWriter w;
    for (entt::entity entity : entities) {
        const auto* mesh = registry_.try_get<MeshRef>(entity);
        const auto* spin = registry_.try_get<Spin>(entity);
        const auto* player = registry_.try_get<Player>(entity);
        const auto* box = registry_.try_get<StaticBox>(entity);
        const auto* body = registry_.try_get<CharacterBody>(entity);
        const std::uint32_t present = (mesh ? 1u : 0u) | (spin ? 2u : 0u) | (player ? 4u : 0u) | (box ? 8u : 0u) | (body ? 16u : 0u);
        w.write(registry_.get<StableId>(entity));
        w.write(registry_.get<Name>(entity));
        w.write(registry_.get<Transform>(entity));
        w.write(present);
        if (mesh) w.write(*mesh);
        if (spin) w.write(*spin);
        if (player) w.write(*player);
        if (box) w.write(*box);
        if (body) w.write(*body);
    }
    const auto& bytes = w.bytes();
    return hashCombine(h, hash64(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size())));
}

}
