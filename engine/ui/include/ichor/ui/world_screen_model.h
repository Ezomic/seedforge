#pragma once

#include "ichor/data/axiom.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ichor {

struct WorldScreenAxiom {
    std::string id;
    std::string title;
    AxiomSlot slot = AxiomSlot::Cataclysm;
    // Only read for Pressure and Anomaly, which stay hidden until the player finds them out.
    bool discovered = false;
};

struct WorldScreenSynergy {
    std::string id;
    std::string worldName;
    bool discovered = false;
};

struct WorldScreenTerritory {
    std::string faction;
    // Fraction of the map this faction holds, 0 to 1.
    double share = 0.0;
};

struct WorldScreenInput {
    std::vector<WorldScreenAxiom> axioms;
    // Fired synergies in firing order (detectSynergies).
    std::vector<WorldScreenSynergy> synergies;
    // How far the foretold Pressure has advanced, 0 to 1. Shown only once a Pressure axiom is discovered.
    std::optional<double> pressureLevel;
    std::vector<WorldScreenTerritory> territory;
};

enum class WorldScreenSectionKind : std::uint8_t { Axioms, Synergies, Pressure, Territory };

struct WorldScreenLine {
    std::string text;
    // Whole percent for Pressure and Territory lines.
    std::optional<std::int32_t> percent;

    bool operator==(const WorldScreenLine&) const = default;
};

struct WorldScreenSection {
    WorldScreenSectionKind kind = WorldScreenSectionKind::Axioms;
    std::string title;
    std::vector<WorldScreenLine> lines;
    // Entries dropped by the line cap.
    std::size_t omitted = 0;

    bool operator==(const WorldScreenSection&) const = default;
};

constexpr std::size_t kWorldScreenMaxAxiomLines = 8;
constexpr std::size_t kWorldScreenMaxSynergyLines = 4;
constexpr std::size_t kWorldScreenMaxTerritoryLines = 8;
constexpr std::size_t kWorldScreenMaxTextBytes = 80;

// Sections in fixed order (Axioms, Synergies, Pressure, Territory); a section with nothing to show is
// left out, so an empty world yields no sections. Cataclysm, Law and Powers axioms always show;
// Pressure and Anomaly axioms and every synergy show only when discovered, and the Pressure section
// only when a Pressure axiom is discovered. Axioms sort by slot then id, territory by share
// descending then faction, synergies keep firing order; repeated ids are listed once. Text is
// stripped of control characters and cut to kWorldScreenMaxTextBytes on a UTF-8 boundary, and an
// entry with no usable text falls back to its id or is dropped. Non-finite numbers are treated as
// missing. Pure and deterministic.
std::vector<WorldScreenSection> buildWorldScreen(const WorldScreenInput& input);

}
