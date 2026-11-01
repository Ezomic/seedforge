#include "ichor/world/dungeon_reverb.h"

#include "ichor/audio/room_reverb.h"
#include "ichor/world/dungeon_greybox.h"

namespace ichor {

std::optional<ReverbSettings> dungeonReverbAt(const DungeonContent& content, const glm::dvec3& point)
{
    if (content.roomAt(point) == nullptr) return std::nullopt;
    constexpr float kWidth = static_cast<float>(2.0 * greybox::kRoomHalfM);
    constexpr float kHeight = static_cast<float>(greybox::kStoreyM - greybox::kSlabM);
    return toReverbSettings(roomReverbForBox(kWidth, kWidth, kHeight, RoomMaterial::Stone));
}

}
