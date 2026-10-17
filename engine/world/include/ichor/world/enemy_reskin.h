#pragma once

#include "ichor/world/encounter_budget.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// Parameters laid over a role's shared skeleton. Scale and speed are permille of
// the skeleton's own (1000 is unchanged), so the resolved result is exact integer
// arithmetic on every platform.
struct ReskinParams {
    std::string material;
    std::string tint;
    std::uint32_t scalePermille = 1000;
    std::uint32_t speedPermille = 1000;
    std::vector<std::string> gear;

    friend bool operator==(const ReskinParams&, const ReskinParams&) = default;
};

enum class ReskinKind { Faction, Biome };

// One layer of dress. `key` is a faction or biome id. An unset role applies to
// every role. Empty material or tint, and empty gear, leave the lower layer alone.
struct ReskinLayer {
    std::string id;
    ReskinKind kind = ReskinKind::Faction;
    std::string key;
    bool anyRole = true;
    EnemyRole role = EnemyRole::Skirmisher;
    std::string material;
    std::string tint;
    std::uint32_t scalePermille = 1000;
    std::uint32_t speedPermille = 1000;
    std::uint32_t scaleJitterPermille = 0;
    std::vector<std::string> gear;
};

struct ReskinRequest {
    EnemyRole role = EnemyRole::Skirmisher;
    std::string faction;
    std::string biome;
    // Identifies the individual (for example a hash of its spawn); only the jitter reads it.
    std::uint64_t seed = 0;
};

constexpr std::uint32_t kMinReskinPermille = 100;
constexpr std::uint32_t kMaxReskinPermille = 4000;
constexpr std::uint32_t kMaxReskinJitterPermille = 500;
constexpr std::size_t kMaxReskinGear = 8;
constexpr std::size_t kMaxReskinLayers = 4096;
constexpr std::size_t kMaxReskinBytes = 1024 * 1024;

// Parses [{"id", "kind": "faction"|"biome", "key", "role"?, "material"?, "tint"?,
// "scale_permille"?, "speed_permille"?, "scale_jitter_permille"?, "gear"?: [..]}].
// Unknown keys, bad colours, out-of-range numbers and duplicate ids are problems
// ("<label>: <path>: <what is wrong>"). Returns true, and fills `out`, only when
// there are none. Never throws on bad input.
bool parseReskinLayers(std::string_view label, std::string_view text, std::vector<ReskinLayer>& out, std::vector<std::string>& problems);

// Layers apply base, then every matching biome layer, then every matching faction
// layer, each group in id order, a role-specific layer after a generic one of the
// same group. Scale and speed multiply (clamped to the permille limits); material
// and tint are taken from the last layer that sets them, so a faction outranks a
// biome; gear accumulates without repeats up to kMaxReskinGear. The seed moves
// scale by up to the summed jitter either way. Pure in its inputs.
ReskinParams resolveReskin(const ReskinRequest& request, const std::vector<ReskinLayer>& layers);

}
