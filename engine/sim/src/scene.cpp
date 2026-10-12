#include "ichor/sim/scene.h"

#include "ichor/core/hash.h"

#include <bit>

namespace ichor {

Scene::Scene(std::uint64_t seed)
    : seed_(seed)
    , rng_(Rng(seed).derive("scene"))
{
}

void Scene::tick(double dt)
{
    ++tickCount_;
    time_ += dt;
    lastDraw_ = rng_.nextU64();
}

std::uint64_t Scene::stateHash() const
{
    std::uint64_t h = hashCombine(seed_, tickCount_);
    h = hashCombine(h, std::bit_cast<std::uint64_t>(time_));
    h = hashCombine(h, lastDraw_);
    return hashCombine(h, rng_.state());
}

}
