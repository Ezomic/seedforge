#include "ichor/world/palette.h"

namespace ichor {

Palette defaultPalette()
{
    Palette palette;
    palette.grade.saturation = 0.88f;
    palette.grade.contrast = 1.18f;
    palette.grade.exposure = 1.0f;
    palette.grade.shadows = {0.9f, 1.0f, 1.1f};
    palette.grade.highlights = {1.08f, 1.0f, 0.9f};
    palette.response.roughnessMin = 0.35f;
    palette.response.roughnessMax = 0.95f;
    palette.response.albedoMin = 0.04f;
    palette.response.albedoMax = 0.85f;
    palette.groundColor = {0.09f, 0.15f, 0.055f};
    palette.groundBlendHeight = 0.8f;
    return palette;
}

void applyPalette(const Palette& palette, RenderView& view)
{
    view.grade = palette.grade;
    view.response = palette.response;
    view.groundColor = palette.groundColor;
    view.groundBlendHeight = palette.groundBlendHeight;
}

}
