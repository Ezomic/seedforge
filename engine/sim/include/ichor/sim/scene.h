#pragma once

#include "ichor/core/input.h"
#include "ichor/core/rng.h"
#include "ichor/sim/commands.h"
#include "ichor/sim/components.h"
#include "ichor/anim/animation.h"
#include "ichor/sim/physics.h"
#include "ichor/sim/sound_event.h"

#include <entt/entity/registry.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ichor {

// The simulation: an EnTT registry driven by a fixed-step tick. State changes
// enter through commands, every system draws from its own seeded generator,
// and stateHash() summarises everything in StableId order so two scenes that
// agree on content agree on the hash whatever their registry order.
class Scene {
public:
    explicit Scene(std::uint64_t seed);
    ~Scene();
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    void enqueue(Command command);
    void tick(double dt, const InputFrame& input = {});

    std::uint64_t seed() const { return seed_; }
    std::uint64_t tickCount() const { return tickCount_; }
    double time() const { return time_; }
    std::uint64_t stateHash() const;

    entt::registry& registry() { return registry_; }
    const entt::registry& registry() const { return registry_; }
    entt::entity find(StableId id) const;

    // One generator per system, derived from the scene seed and the name.
    Rng& rng(std::string_view system);

    const CommandLog& log() const { return log_; }
    const PhysicsWorld& physics() const { return *physics_; }

    // The clips Animator components refer to. The scene does not own it; the
    // app or test keeps it alive and loads bundles into it.
    void setAnimations(const AnimationSet* animations) { animations_ = animations; }
    const AnimationSet* animations() const { return animations_; }

    // What became audible during the last tick, cleared at the start of the next.
    const std::vector<SoundEvent>& soundEvents() const { return soundEvents_; }

    // Snapshot support: scene_io writes and restores these directly.
    struct Clock {
        std::uint64_t tick = 0;
        double time = 0.0;
    };
    Clock clock() const { return {tickCount_, time_}; }
    void restoreClock(Clock clock);
    const std::map<std::string, Rng, std::less<>>& rngs() const { return rngs_; }
    void restoreRng(const std::string& system, const Rng& rng);
    // Drops entities, bodies, generators, pending commands and the log: a
    // restore starts a new timeline.
    void clearForRestore();
    // Spawns without logging a command; snapshots are restored, not replayed.
    void restore(const SpawnCommand& spawn, const std::optional<MontagePlayer>& montage = {})
    {
        apply(spawn);
        if (montage) {
            registry_.emplace_or_replace<MontagePlayer>(find(spawn.id), *montage);
        }
    }

private:
    void apply(const Command& command);
    void applyInput(const InputFrame& input, double dt);
    void rememberPreviousTransforms();
    void spinSystem(double dt);
    void animationSystem(double dt);
    void physicsSystem(double dt);

    std::uint64_t seed_;
    entt::registry registry_;
    std::unordered_map<std::uint64_t, entt::entity> index_;
    std::unique_ptr<PhysicsWorld> physics_;
    const AnimationSet* animations_ = nullptr;
    std::map<std::string, Rng, std::less<>> rngs_;
    std::vector<Command> pending_;
    std::vector<SoundEvent> soundEvents_;
    CommandLog log_;
    std::uint64_t tickCount_ = 0;
    double time_ = 0.0;
};

}
