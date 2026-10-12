#pragma once

#include "ichor/core/rng.h"
#include "ichor/sim/commands.h"
#include "ichor/sim/components.h"
#include "ichor/core/input.h"

#include <entt/entity/registry.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// The simulation: an EnTT registry driven by a fixed-step tick. State changes
// enter through commands, every system draws from its own seeded generator,
// and stateHash() summarises everything in StableId order so two scenes that
// agree on content agree on the hash whatever their registry order.
class Scene {
public:
    explicit Scene(std::uint64_t seed);

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

    // Snapshot support: scene_io writes and restores these directly.
    struct Clock {
        std::uint64_t tick = 0;
        double time = 0.0;
    };
    Clock clock() const { return {tickCount_, time_}; }
    void restoreClock(Clock clock);
    const std::map<std::string, Rng, std::less<>>& rngs() const { return rngs_; }
    void restoreRng(const std::string& system, const Rng& rng);
    void clearEntities();
    // Spawns without logging a command; snapshots are restored, not replayed.
    void restore(const SpawnCommand& spawn) { apply(spawn); }

private:
    void apply(const Command& command);
    void applyInput(const InputFrame& input, double dt);
    void spinSystem(double dt);

    std::uint64_t seed_;
    entt::registry registry_;
    std::map<std::string, Rng, std::less<>> rngs_;
    std::vector<Command> pending_;
    CommandLog log_;
    std::uint64_t tickCount_ = 0;
    double time_ = 0.0;
};

}
