#pragma once

#include "ichor/ui/ui_context.h"
#include "ichor/world/map_discovery.h"
#include "ichor/world/map_image.h"
#include "ichor/world/map_layers.h"

#include <array>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <RmlUi/Core/EventListener.h>

namespace Rml {
class Element;
class ElementDocument;
}

namespace ichor {

// The map screen: a picture of the whole world on a card over the game, with the player's marker
// and the way they face. Fog and settlement icons are set on it by the caller. Hidden until shown. The
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

    // Fog of war: what the player has not seen is drawn dark, layers and settlements included. Without a
    // discovery everything shows. A discovery of another size than the picture is ignored.
    void setDiscovery(const MapDiscovery& discovery);
    void clearDiscovery();

    // The forged world's settlements, drawn as an icon by tier with the name under it. Those on ground the
    // discovery has not seen are left out. Replaces the previous set.
    void setSettlements(std::vector<MapSettlement> settlements);

    // The names of the settlements now drawn, in order, for tests.
    std::vector<std::string> placeNames() const { return shownNames_; }

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
    void redrawPlaces();

    UiContext& ui_;
    Click click_;
    std::array<LayerClick, kMapLayerCount> layerClicks_;
    Rml::Element* layersRow_ = nullptr;
    std::array<Rml::Element*, kMapLayerCount> layerButtons_{};
    MapImage plain_;
    MapLayerStack stack_;
    MapLayerSet layers_;
    std::optional<MapDiscovery> discovery_;
    std::vector<MapSettlement> settlements_;
    std::vector<std::string> shownNames_;
    Rml::Element* places_ = nullptr;
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
