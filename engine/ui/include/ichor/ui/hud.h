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

// How full the health bar is, as a whole percent from 0 to 100. Health below nothing or above
// the maximum is clamped; a maximum that is not a positive number, or a value that is not finite, reads as empty.
int healthPercent(float health, float maxHealth);

// A time of day as HH:MM. Hours run 0 to 24 and wrap.
std::string clockLabel(float hour);

// Where a target lies from the player, in words: "north-east, 450 m". Positions are world
// metres on the ground (x east, z south). The direction is the compass point the target is in,
// not relative to where the player faces, so it reads against the compass above it. The
// distance is deliberately rough: to the nearest 10 m under 100 m, 50 m under 1 km, and 0.5 km
// from there up to 99.5 km, beyond which it is "far away". Within 5 m it is just "here". Any
// position that is not finite gives an empty string.
std::string targetGuide(double playerX, double playerZ, double targetX, double targetZ);

// The always-on screen furniture: the compass, the clock and the health bar, in the game's fonts. The
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
    void update(float headingRadians, float hour, float wadingDepth, float health, float maxHealth);

    // The line under the compass pointing at a target (see targetGuide); empty hides it.
    void setGuide(const std::string& text);

private:
    UiContext& ui_;
    Rml::ElementDocument* document_ = nullptr;
    Rml::Element* compass_ = nullptr;
    Rml::Element* clock_ = nullptr;
    Rml::Element* water_ = nullptr;
    Rml::Element* guide_ = nullptr;
    Rml::Element* healthFill_ = nullptr;
    std::string guideText_;
    std::string compassText_;
    std::string clockText_;
    std::string waterText_;
    int healthPercent_ = -1;
};

}
