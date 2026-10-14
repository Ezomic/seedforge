#pragma once

#include "ichor/ui/ui_context.h"

#include <string>

namespace Rml {
class Element;
class ElementDocument;
}

namespace ichor {

// The point of the compass a heading falls in: N, NE, E, SE, S, SW, W or NW. The heading is
// in radians clockwise from north, and any value wraps. A value that is not a number reads as north.
std::string compassLabel(float headingRadians);

// The heading of a view that looks along (x, z) on the ground: zero towards -Z, which is
// north, and growing clockwise, so +X is a quarter turn.
float headingOf(float forwardX, float forwardZ);

// What wading is called at this depth of water in metres: nothing over dry land or a puddle
// (see core/wading.h), then ankle, knee, waist and chest deep in equal steps up to chest height. A depth that is not a number reads as none.
std::string waterLabel(float depth);

// A time of day as HH:MM. Hours run 0 to 24 and wrap.
std::string clockLabel(float hour);

// The always-on screen furniture: the compass and the clock, in the game's fonts. The
// context must have the Cinzel and Alegreya Sans faces loaded. There are no waypoints and no
// minimap (doc 18), so this is how the player finds their way.
class Hud {
public:
    explicit Hud(UiContext& ui);
    ~Hud();
    Hud(const Hud&) = delete;
    Hud& operator=(const Hud&) = delete;

    bool ok() const { return document_ != nullptr; }

    // Only changed text is rewritten, so a steady heading costs no layout.
    void update(float headingRadians, float hour, float wadingDepth);

private:
    UiContext& ui_;
    Rml::ElementDocument* document_ = nullptr;
    Rml::Element* compass_ = nullptr;
    Rml::Element* clock_ = nullptr;
    Rml::Element* water_ = nullptr;
    std::string compassText_;
    std::string clockText_;
    std::string waterText_;
};

}
