#include "ichor/sim/scene.h"

#include "ichor/core/binary.h"
#include "ichor/core/hash.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <bit>

namespace ichor {

Scene::Scene(std::uint64_t seed)
    : seed_(seed)
{
}

void Scene::enqueue(Command command)
{
    pending_.push_back(std::move(command));
}

entt::entity Scene::find(StableId id) const
{
    for (auto [entity, stable] : registry_.view<const StableId>().each()) {
        if (stable == id) {
            return entity;
        }
    }
    return entt::null;
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

void Scene::clearEntities()
{
    registry_.clear();
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
            registry_.emplace<StableId>(entity, c.id);
            registry_.emplace<Name>(entity, c.name);
            registry_.emplace<Transform>(entity, c.transform);
            if (c.mesh) registry_.emplace<MeshRef>(entity, *c.mesh);
            if (c.spin) registry_.emplace<Spin>(entity, *c.spin);
            if (c.player) registry_.emplace<Player>(entity, *c.player);
        } else if constexpr (std::is_same_v<T, DestroyCommand>) {
            const entt::entity entity = find(c.id);
            if (entity != entt::null) {
                registry_.destroy(entity);
            }
        } else if constexpr (std::is_same_v<T, MoveCommand>) {
            const entt::entity entity = find(c.id);
            if (entity != entt::null) {
                registry_.get<Transform>(entity).position += c.delta;
            }
        }
    }, command);
}

void Scene::applyInput(const InputFrame& input, double dt)
{
    if (input.moveX == 0.0f && input.moveY == 0.0f) {
        return;
    }
    for (auto [entity, id, player] : registry_.view<const StableId, const Player>().each()) {
        const double speed = player.metresPerSecond * ((input.buttons & InputFrame::Sprint) != 0 ? 2.0 : 1.0);
        MoveCommand move;
        move.id = id;
        move.delta = glm::dvec3(input.moveX, 0.0, -input.moveY) * speed * dt;
        log_.entries.push_back({tickCount_, move});
        apply(move);
    }
}

void Scene::spinSystem(double dt)
{
    for (auto [entity, transform, spin] : registry_.view<Transform, const Spin>().each()) {
        const float angle = spin.radiansPerSecond * static_cast<float>(dt);
        transform.rotation = glm::normalize(glm::angleAxis(angle, glm::normalize(spin.axis)) * transform.rotation);
    }
}

void Scene::tick(double dt, const InputFrame& input)
{
    for (const Command& command : pending_) {
        log_.entries.push_back({tickCount_, command});
        apply(command);
    }
    pending_.clear();

    applyInput(input, dt);
    spinSystem(dt);

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
        w.write(registry_.get<StableId>(entity));
        w.write(registry_.get<Name>(entity));
        w.write(registry_.get<Transform>(entity));
        if (const auto* mesh = registry_.try_get<MeshRef>(entity)) w.write(*mesh);
        if (const auto* spin = registry_.try_get<Spin>(entity)) w.write(*spin);
        if (const auto* player = registry_.try_get<Player>(entity)) w.write(*player);
    }
    const auto& bytes = w.bytes();
    return hashCombine(h, hash64(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size())));
}

}
