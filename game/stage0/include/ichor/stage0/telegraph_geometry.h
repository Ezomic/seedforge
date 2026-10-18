#pragma once

#include "ichor/sim/physics.h"
#include "ichor/stage0/camera_view.h"
#include "ichor/stage0/visual_telegraph.h"

#include <glm/vec3.hpp>

#include <vector>

namespace ichor::stage0 {

// Draws the visual_telegraph table as debug lines. The shape and pattern come from the table, so a
// light and a heavy wind-up differ in outline and stroke count, not in colour.
struct TelegraphDraw {
    glm::dvec3 origin{0.0};
    glm::vec3 forward{0.0f, 0.0f, -1.0f}; // horizontal vector the attack faces
    float reach = 1.0f;                   // how far ahead the cue ends at full charge
    float halfWidth = 0.5f;               // half the cue's width (radius for rings) at full charge
    float progress = 1.0f;                // wind-up fraction, clamped to [0, 1]
    glm::vec3 color{1.0f};
};

// Appends nothing for a non-finite or zero-length draw, or a None shape.
void appendTelegraphLines(const VisualTelegraph& cue, const TelegraphDraw& draw, std::vector<LineSegment>& out);

// Edge-of-screen arrow pointing from the screen centre toward `target`, placed in front of the camera.
// Appends nothing and returns false while the target sphere touches the frustum or is non-finite.
bool appendOffscreenArrow(const CameraPose& camera, glm::dvec3 target, float targetRadius, glm::vec3 color, std::vector<LineSegment>& out);

// The skirmisher wind-up cue from the shipped table. `camera` is non-null for the foe's wind-up, which
// also gets the off-screen arrow when the foe (at draw.origin) is out of view.
void appendWindupTelegraph(bool heavy, const TelegraphDraw& draw, const CameraPose* camera, float foeRadius, std::vector<LineSegment>& out);

} // namespace ichor::stage0
