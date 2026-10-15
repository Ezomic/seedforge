#pragma once

#include "ichor/ui/ui_context.h"
#include "ichor/world/forge_progress.h"

#include <string>

namespace Rml {
class Element;
class ElementDocument;
}

namespace ichor {

// What the loading screen shows: the phase being forged and how far along the forge is.
struct LoadingView {
    std::string label;
    int percent = 0; // 0 to 100
    bool operator==(const LoadingView&) const = default;
};

LoadingView buildLoadingView(const ForgeProgressSnapshot& progress);

// The loading screen: a veil with the phase name and a progress bar. Hidden until shown. The
// context must have the Cinzel and Alegreya Sans faces loaded.
class LoadingScreen {
public:
    explicit LoadingScreen(UiContext& ui);
    ~LoadingScreen();
    LoadingScreen(const LoadingScreen&) = delete;
    LoadingScreen& operator=(const LoadingScreen&) = delete;

    bool ok() const { return document_ != nullptr; }

    void setVisible(bool visible);
    bool visible() const { return visible_; }

    void setView(const LoadingView& view);

    // The label and the bar width, for tests.
    std::string markup() const;

private:
    UiContext& ui_;
    Rml::ElementDocument* document_ = nullptr;
    Rml::Element* label_ = nullptr;
    Rml::Element* fill_ = nullptr;
    bool visible_ = false;
};

}
