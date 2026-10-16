#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ichor::stage0 {

// Doc 13 accessibility: every audio telegraph needs a visual fallback. This is the data and the
// check; drawing the cues is not here.
enum class TelegraphRole : std::uint8_t { Skirmisher, Swarm, Ambusher, Anchor, Artillery, Controller };

// Cues differ by form, never by colour alone.
enum class TelegraphShape : std::uint8_t { None, Ring, Cone, Line, GroundMarker, Pulse };
enum class TelegraphPattern : std::uint8_t { None, Solid, Dashed, Dotted, Hatched, Chevrons };
enum class TelegraphDirection : std::uint8_t { None, AtAttacker, TowardTarget, AtGroundPoint, Radial };

// One audible wind-up cue: the attack it announces, by role.
struct AudioTelegraph {
    TelegraphRole role{};
    std::string attack;
};

struct VisualTelegraph {
    TelegraphRole role{};
    std::string attack;
    TelegraphShape shape = TelegraphShape::None;
    TelegraphPattern pattern = TelegraphPattern::None;
    TelegraphDirection direction = TelegraphDirection::None;
    int durationTicks = 0;       // fixed steps (60 Hz) the cue stays up: the wind-up length
    bool offscreenArrow = false; // an edge-of-screen arrow when the attacker is not visible
};

struct VisualTelegraphReport {
    std::vector<AudioTelegraph> missing; // audible with no visual fallback
    std::vector<VisualTelegraph> duplicates;
    std::vector<VisualTelegraph> invalid; // present but cannot be seen
    bool ok() const { return missing.empty() && duplicates.empty() && invalid.empty(); }
};

const std::vector<AudioTelegraph>& audioTelegraphs();
const std::vector<VisualTelegraph>& visualTelegraphTable();

const VisualTelegraph* findVisualTelegraph(const std::vector<VisualTelegraph>& table, TelegraphRole role, std::string_view attack);
VisualTelegraphReport validateVisualTelegraphs(const std::vector<VisualTelegraph>& table, const std::vector<AudioTelegraph>& audio);

const char* name(TelegraphRole role);
bool parseTelegraphRole(std::string_view text, TelegraphRole& out);

// Whole degrees in [0, 360) from the player to an off-screen attacker, counter-clockwise from +x.
// Non-finite input yields 0 rather than propagating.
int offscreenBearing(float fromX, float fromY, float toX, float toY);

} // namespace ichor::stage0
