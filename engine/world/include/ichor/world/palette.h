#pragma once

#include "ichor/gfx/render_view.h"

namespace ichor {

// The look that unifies assets from many sources (doc 16, items 2 and 3): one colour grade
// over the finished picture, and one range for how rough and how bright any surface may be,
// so a bought rock and a bought wall light the same way. One palette per world.
struct Palette {
    ColourGrade grade;
    MaterialResponse response;
    // One colour for the whole world: right where the ground is grass, which is where most objects stand.
    glm::vec3 groundColor{0.1f, 0.16f, 0.06f}; // what the ground looks like, for blending objects into it
    float groundBlendHeight = 0.0f;            // metres up an object the blend reaches
};

// The slice's palette: slightly muted, with cool shadows and warm highlights, and surfaces
// held between chalk and soot and between glossy and fully matte.
Palette defaultPalette();

// Copies a palette into a view.
void applyPalette(const Palette& palette, RenderView& view);

}
