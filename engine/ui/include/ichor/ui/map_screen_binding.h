#pragma once

#include "ichor/ui/map_screen_model.h"
#include "ichor/ui/ui_context.h"

#include <string>

namespace Rml {
class Element;
class ElementDocument;
}

namespace ichor {

// Draws a MapScreenView as RmlUi elements: the player's heading, the active waypoint, a list of the
// marks in view, and a dot for each mark point and the waypoint at its screen position. It is shown and hidden with the map screen it sits over.
class MapScreenBinding {
public:
    explicit MapScreenBinding(UiContext& ui);
    ~MapScreenBinding();
    MapScreenBinding(const MapScreenBinding&) = delete;
    MapScreenBinding& operator=(const MapScreenBinding&) = delete;

    bool ok() const { return document_ != nullptr; }

    void setVisible(bool visible);
    bool visible() const { return visible_; }

    // Replaces everything shown with this view. An invalid view clears it.
    void bind(const MapScreenView& view);

    // The text now shown in the "player", "waypoint", "list" and "hidden" elements, for tests.
    std::string textOf(const char* elementId) const;

private:
    UiContext& ui_;
    Rml::ElementDocument* document_ = nullptr;
    Rml::Element* player_ = nullptr;
    Rml::Element* waypoint_ = nullptr;
    Rml::Element* list_ = nullptr;
    Rml::Element* hidden_ = nullptr;
    Rml::Element* view_ = nullptr;
    bool visible_ = false;
};

}
