#pragma once

#include "ichor/core/rng.h"

#include <cstdint>

namespace ichor {

// The simulation. E1 has no entities yet; it exists so the loop, the fixed
// step and the determinism tests have something real to tick and hash.
class Scene {
public:
    explicit Scene(std::uint64_t seed);

    void tick(double dt);

    std::uint64_t seed() const { return seed_; }
    std::uint64_t tickCount() const { return tickCount_; }
    double time() const { return time_; }

    // Hash of everything that constitutes simulation state. Two runs with the
    // same seed and the same ticks must agree; the replay test depends on it.
    std::uint64_t stateHash() const;

private:
    std::uint64_t seed_;
    Rng rng_;
    std::uint64_t tickCount_ = 0;
    double time_ = 0.0;
    std::uint64_t lastDraw_ = 0;
};

}
