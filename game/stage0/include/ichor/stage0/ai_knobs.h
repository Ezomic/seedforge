#pragma once

#include "ichor/data/data_set.h"
#include "ichor/stage0/combat.h"

#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ichor::stage0 {

// The six floats the 200-fight protocol turns (doc 18), edited in data/ai_knobs without recompiling.
struct AiKnobs {
    float desireGainRate = 0.35f;
    float preferredSpacing = 2.5f;
    float restDuration = 1.5f;
    float punishReactionTime = 0.25f;
    float windupScale = 1.0f; // multiplies the light and heavy windup times
    float trackingCapDegPerSec = 30.0f;
};

struct KnobSpec {
    const char* key; // JSON field name
    float AiKnobs::* member;
    float minimum;
    float maximum;
};

constexpr std::array<KnobSpec, 6> kKnobSpecs{{
    {"desire_gain_rate", &AiKnobs::desireGainRate, 0.01f, 5.0f},
    {"preferred_spacing", &AiKnobs::preferredSpacing, 0.5f, 12.0f},
    {"rest_duration", &AiKnobs::restDuration, 0.0f, 10.0f},
    {"punish_reaction_time", &AiKnobs::punishReactionTime, 0.0f, 2.0f},
    {"windup_scale", &AiKnobs::windupScale, 0.25f, 4.0f},
    {"tracking_cap_deg_per_sec", &AiKnobs::trackingCapDegPerSec, 0.0f, 720.0f},
}};

struct KnobLoad {
    AiKnobs knobs;
    std::vector<std::string> problems; // one line per rejected, clamped or unknown field
};

// A missing knob keeps its default; a non-numeric or non-finite one keeps its default and is reported; an
// out-of-range one is clamped and reported; an unknown field is reported (a typo must not pass silently).
KnobLoad knobsFromRecord(const DataRecord& record);

// The "skirmisher" record of the "ai_knobs" table; defaults, with a problem line, when it is absent.
KnobLoad loadKnobs(const DataSet& data);

float clampKnob(const KnobSpec& spec, float value);

// Loads the data root (the directory holding ai_knobs/) and reads the skirmisher record. A root that fails to
// load gives defaults and a problem line carrying the loader's error.
KnobLoad loadKnobsFromDirectory(const std::filesystem::path& root);

// One line per session for the sidecar next to the fight log: the first fight number the session will write,
// every knob value, and where they came from. The fight CSV keeps its header; this line says which knobs
// produced the rows from firstFight on.
std::string knobStampLine(const AiKnobs& knobs, std::string_view source, int firstFight);

// The sidecar of a fight log: "<csv>.knobs.txt".
std::filesystem::path knobStampPath(const std::filesystem::path& csv);

bool appendKnobStamp(const std::filesystem::path& csv, const std::string& line);

// The tuning with the knobs laid over it. Windups are scaled, not replaced, so the light/heavy ratio holds.
Tuning applyKnobs(Tuning base, const AiKnobs& knobs);

struct OverlayInput {
    const Combatant* foe = nullptr;
    const SkirmisherBrain* brain = nullptr;
    float distance = 0.0f;         // metres to the player
    float trackingAngleDeg = 0.0f; // absolute angle between the foe's facing and the player
    float allowedTurnDegPerSec = 0.0f;
};

struct OverlayLine {
    std::string label;
    std::string value;
};

// What the always-on debug overlay shows: state, desire, spacing, active timers, tracking angle. No drawing.
std::vector<OverlayLine> overlayLines(const OverlayInput& input, const Tuning& tuning);

}
