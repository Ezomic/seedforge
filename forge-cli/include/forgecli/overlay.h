#pragma once

#include <string>

#include <worldforge/forge_world.h>

namespace forgecli {

// The manifest overlay the map viewer draws, as JSON: one entry per settlement and POI with its
// cell, kind (settlement, dungeon, ruin, landmark) and what the content manifest binds to it
// (items, cast, dungeon slot). Empty when the world did not reach the binding phase.
// Dungeon seeds are strings: a u64 does not survive a JS number.
std::string overlayJson(const worldforge::ForgedWorld& world);

}
