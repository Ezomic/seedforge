#pragma once

#include "ichor/ui/ui_context.h"
#include "ichor/world/map_image.h"
#include "ichor/world/map_layers.h"

#include <array>
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

    // The forged world's layers over the picture (biomes, roads, territories, rivers), each toggled by a
    // button under it. Setting a stack draws it with the layers already on; an empty one goes back to the
    // plain picture. setImage drops the stack, since its layers belong to the old picture. The buttons of
    // a layer the stack has nothing for are hidden.
    void setLayerStack(MapLayerStack stack);
    MapLayerSet layers() const { return layers_; }
    void setLayer(MapLayer layer, bool on);
    void toggleLayer(MapLayer layer) { setLayer(layer, !layers_.on(layer)); }

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

    // The same for a layer's button; nothing while the screen is hidden or the stack has no such layer.
    std::optional<std::pair<float, float>> layerButtonCentre(MapLayer layer) const;

private:
    struct Click final : Rml::EventListener {
        void ProcessEvent(Rml::Event&) override { requested = true; }
        bool requested = false;
    };

    struct LayerClick final : Rml::EventListener {
        void ProcessEvent(Rml::Event&) override { screen->toggleLayer(layer); }
        MapScreen* screen = nullptr;
        MapLayer layer = MapLayer::Biomes;
    };

    void redraw();

    UiContext& ui_;
    Click click_;
    std::array<LayerClick, kMapLayerCount> layerClicks_;
    Rml::Element* layersRow_ = nullptr;
    std::array<Rml::Element*, kMapLayerCount> layerButtons_{};
    MapImage plain_;
    MapLayerStack stack_;
    MapLayerSet layers_;
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
