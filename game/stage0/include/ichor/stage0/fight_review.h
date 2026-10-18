#pragma once

#include "ichor/stage0/fight_record.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace ichor::stage0 {

// <csv without extension>.fights, next to the fight log.
std::filesystem::path fightRecordDirectory(const std::filesystem::path& csv);

// <dir>/fight_007.ifrd
std::filesystem::path fightRecordPath(const std::filesystem::path& directory, std::uint32_t index);

// Writes to a temporary sibling and renames it into place, so a crash leaves
// either the whole record or none, never half of one.
bool saveFightRecord(const std::filesystem::path& path, const FightRecord& record);

// Nullopt for a missing, oversized, truncated or corrupted file; never throws.
std::optional<FightRecord> loadFightRecord(const std::filesystem::path& path);

// The review replay: the outcome, the per-side summary and every event in order.
std::string describeFight(const FightRecord& record);

// `stage0 --review-fight <file>`: prints describeFight, 0 on success, 1 when the file is refused.
int reviewFight(const std::filesystem::path& path);

}
