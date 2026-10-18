#pragma once

#include "ichor/ui/landmark_compass.h"
#include "ichor/ui/ui_context.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Rml {
class Element;
class ElementDocument;
}

namespace ichor {

// The always-on landmark compass: one line per nearby landmark, nearest first, drawn from
// landmark_compass.rml and landmark_compass.rcss in the data/ui directory. The context must
// have the Alegreya Sans face loaded.
class LandmarkCompassScreen {
public:
    LandmarkCompassScreen(UiContext& ui, const std::filesystem::path& uiDirectory);
    ~LandmarkCompassScreen();
    LandmarkCompassScreen(const LandmarkCompassScreen&) = delete;
    LandmarkCompassScreen& operator=(const LandmarkCompassScreen&) = delete;

    bool ok() const { return document_ != nullptr; }

    // Nothing is rewritten when the lines are the ones already shown. A reading that is not Ok
    // shows no lines.
    void setReading(const CompassReading& reading);

private:
    UiContext& ui_;
    Rml::ElementDocument* document_ = nullptr;
    Rml::Element* list_ = nullptr;
    std::vector<std::string> shown_;
};

// Reads the compass for a player at `from` facing `facing` and shows it. Map convention: +y is
// north, +x is east.
void bindLandmarkCompass(LandmarkCompassScreen& screen, AnnotationPoint from, AnnotationPoint facing,
    const std::vector<NavLandmark>& landmarks, const CompassLimits& limits = {});

}
