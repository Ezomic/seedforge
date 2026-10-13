#pragma once

#include "ichor/gfx/render_view.h"

namespace ichor {

// The look that unifies assets from many sources (doc 16, items 2 and 3): one colour grade
// over the finished picture, and one range for how rough and how bright any surface may be,
// so a bought rock and a bought wall light the same way. One palette per world.
struct Palette {
    ColourGrade grade;
    MaterialResponse response;
};

// The slice's palette: slightly muted, with cool shadows and warm highlights, and surfaces
// held between chalk and soot and between glossy and fully matte.
Palette defaultPalette();

// Copies a palette into a view.
void applyPalette(const Palette& palette, RenderView& view);

}
