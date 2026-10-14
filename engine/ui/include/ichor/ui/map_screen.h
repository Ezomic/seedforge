#pragma once

#include "ichor/ui/ui_context.h"
#include "ichor/world/map_image.h"

#include <optional>
#include <utility>

#include <RmlUi/Core/EventListener.h>

namespace Rml {
class Element;
class ElementDocument;
}

namespace ichor {

// The map screen: a picture of the whole world on a card over the game, with the player's marker
// and the way they face. There is no fog and there are no other markers. Hidden until shown. The
// context must have the Cinzel and Alegreya Sans faces loaded.
class MapScreen {
public:
    explicit MapScreen(UiContext& ui);
    ~MapScreen();
    MapScreen(const MapScreen&) = delete;
    MapScreen& operator=(const MapScreen&) = delete;

    bool ok() const { return document_ != nullptr; }

    void setVisible(bool visible);
    bool visible() const { return visible_; }

    // The world's picture. An empty one leaves the card saying the world is not drawn yet.
    void setImage(const MapImage& image);

    // Where the player stands on the picture, and the heading in radians clockwise from north. A
    // position that is not a number is shown in the middle, and a heading that is not one as north.
    void setPlayer(MapPoint where, float headingRadians);

    // What the marker shows now: its place as fractions of the picture, and its heading in degrees
    // from 0 up to but not including 360.
    MapPoint markerPoint() const { return point_; }
    float markerDegrees() const { return degrees_; }

    // True once after the close button was clicked.
    bool takeCloseRequest();

    // Where the close button sits on screen, once the card has been laid out.
    std::optional<std::pair<float, float>> closeButtonCentre() const;

private:
    struct Click final : Rml::EventListener {
        void ProcessEvent(Rml::Event&) override { requested = true; }
        bool requested = false;
    };

    UiContext& ui_;
    Click click_;
    Rml::ElementDocument* document_ = nullptr;
    Rml::Element* close_ = nullptr;
    Rml::Element* frame_ = nullptr;
    Rml::Element* picture_ = nullptr;
    Rml::Element* marker_ = nullptr;
    Rml::Element* empty_ = nullptr;
    MapPoint point_;
    float degrees_ = 0.0f;
    bool visible_ = false;
};

}
