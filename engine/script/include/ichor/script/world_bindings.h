#pragma once

#include "ichor/script/script_host.h"

#include <functional>

namespace ichor {

// What a script may ask of the world around the scene. The caller supplies the answers, which
// must be pure functions of the world and the tick so scripts stay deterministic.
struct WorldQueries {
    std::function<double(double x, double z)> waterDepth; // metres of water over the ground there; zero when dry
    std::function<double()> hour;                         // the hour of the day, 0 to 24
};

// ichor.water_depth(x, z) and ichor.hour(). A query the caller left empty answers zero; a
// coordinate that is not a finite number within a hundred thousand kilometres is a Lua error,
// and so is an answer that is not a finite number.
void bindWorld(ScriptHost& host, WorldQueries queries);

}
