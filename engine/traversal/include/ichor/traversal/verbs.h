#pragma once

#include "ichor/data/data_set.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ichor {

enum class Capability : std::uint8_t {
    None,
    Swim,
    Dive,
    Climb,
    Descend,
    Boat,
    ColdProtection,
};

// The capabilities a player has earned, one bit each. None is not a capability and is never stored.
class CapabilitySet {
public:
    CapabilitySet() = default;

    static CapabilitySet all();

    void add(Capability capability);
    bool has(Capability capability) const;
    bool empty() const { return bits_ == 0; }

private:
    std::uint32_t bits_ = 0;
};

// Parses "swim", "dive", "climb", "descend", "boat" or "cold_protection". Anything else, "none" included, is nothing.
std::optional<Capability> capabilityFromName(const std::string& name);

// The ground a verb works on. Each is a pure test of the Situation, see terrainFits.
enum class Terrain : std::uint8_t {
    Ground,       // dry footing on a slope gentle enough to walk
    Steep,        // footing on a slope too steep to walk
    ShallowWater, // over the ankles but short of chest height
    Water,        // over the ankles
    DeepWater,    // chest height or more
};

std::optional<Terrain> terrainFromName(const std::string& name);

constexpr float kSteepSlopeDegrees = 50.0f;
constexpr int kMaxSpeedPermille = 2000;
constexpr int kMaxStaminaPerSecond = 1000;

struct Verb {
    std::string id;
    Capability needs =Capability::None;
    Terrain terrain = Terrain::Ground;
    int speedPermille = 1000; // thousandths of the walking speed
    int staminaPerSecond = 0;
    bool depthScaled = false; // speedPermille is only reached at chest-deep water; shallower, the wading curve eases in

    // 1.0 is walking pace. For a depth scaled verb this is exactly the wading curve of core/wading.h.
    float speedFactor(float waterDepth) const;
};

// Where the player is. A depth or slope that is not a number, or is negative, reads as zero.
struct Situation {
    bool onGround = true;
    float waterDepth = 0.0f; // metres of water over the ground
    float slopeDegrees = 0.0f;
};

// The validated verb list, in a fixed order (by id).
class VerbSet {
public:
    // Reads, from each record, an optional string "capability", a string "terrain", an integer "speed_permille"
    // (0 to kMaxSpeedPermille), an integer "stamina_per_second" (0 to kMaxStaminaPerSecond) and an optional
    // boolean "depth_scaled". On failure returns nothing and sets error, naming the verb and the field.
    static std::optional<VerbSet> fromTable(const DataTable& table, std::string& error);
    static std::optional<VerbSet> fromVerbs(std::vector<Verb> verbs, std::string& error);

    const std::vector<Verb>& verbs() const { return verbs_; }

private:
    VerbSet() = default;

    std::vector<Verb> verbs_;
};

struct Availability {
    std::vector<std::size_t> verbs;  // indices into VerbSet::verbs(), in its order
    std::optional<std::size_t> best; // the available verb with the highest speed, the earlier one on a tie
    float speedFactor = 1.0f;        // that verb's speed here; 1.0 when no verb is available
};

bool terrainFits(Terrain terrain, const Situation& situation);

// Pure: the verbs whose terrain fits the situation and whose capability the player has.
Availability availableVerbs(const VerbSet& set, const CapabilitySet& capabilities, const Situation& situation);

}
