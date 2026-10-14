#pragma once

namespace ichor {

// How deep water has to be before it slows a walker, and how deep before it slows them the most.
// The speed and what the HUD calls the depth both come from these, so they cannot disagree.
constexpr float kWadeStart = 0.1f; // metres: over the ankles
constexpr float kWadeFull = 1.5f;  // metres: chest height

}
