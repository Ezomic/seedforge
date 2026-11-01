#pragma once

#include "ichor/audio/reverb.h"
#include "ichor/world/dungeon_content.h"

#include <glm/vec3.hpp>

#include <optional>

namespace ichor {

// The reverb of the dungeon room a point stands in (docs/12-audio-atmosphere.md): the Sabine preset of the room's clear
// box, from the grey-box's own dimensions, in the stone every grey-box wall is. Null outside every room, so the caller
// keeps whatever reverb it had. Audio only: nothing here is read by the simulation.
std::optional<ReverbSettings> dungeonReverbAt(const DungeonContent& content, const glm::dvec3& point);

}
