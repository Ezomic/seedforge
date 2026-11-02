#pragma once

#include "ichor/ui/ui_context.h"

#include <optional>
#include <utility>

#include <RmlUi/Core/EventListener.h>

namespace Rml {
class Element;
class ElementDocument;
}

namespace ichor {

// The menu Esc opens: a veil over the world and a card with the field of view, the mouse
// sensitivity, the display mode, resolution and vsync, the UI scale and colour palette, Controls, Resume and Quit.
// Hidden until shown.
// The context must have the Cinzel and Alegreya Sans faces loaded.
class PauseMenu {
public:
    explicit PauseMenu(UiContext& ui);
    ~PauseMenu();
    PauseMenu(const PauseMenu&) = delete;
    PauseMenu& operator=(const PauseMenu&) = delete;

    bool ok() const { return document_ != nullptr; }

    void setVisible(bool visible);
    bool visible() const { return visible_; }

    // True once after the button was clicked.
    bool takeResume();
    bool takeQuit();
    // True once after the Controls button was clicked; the owner shows the rebinding screen.
    bool takeControls();

    // The field of view the card shows. The menu only reports the player's clicks on the
    // minus and plus buttons; the owner decides the value and puts it back here.
    void setFovDegrees(float degrees);
    // The clicks on plus minus those on minus since the last call.
    int takeFovSteps();

    // The mouse sensitivity, shown and reported the same way as the field of view.
    void setMouseSensitivity(float sensitivity);
    int takeSensitivitySteps();

    // The display rows: the owner shows what the window is and hears the clicks. A toggle clicked
    // twice since the last call is no change, so the count comes back, not a flag.
    void setFullscreen(bool fullscreen);
    int takeDisplayModeClicks();
    void setResolution(int width, int height);
    int takeResolutionSteps();
    void setVsync(bool on);
    int takeVsyncClicks();

    // The UI scale steps and the palette toggle, reported like the display rows.
    void setUiScale(float scale);
    int takeUiScaleSteps();
    void setColourblindSafe(bool on);
    int takePaletteClicks();

    // Where the buttons sit on screen once laid out, for tests and for anything that wants to
    // point at them. Nothing while the menu is hidden.
    std::optional<std::pair<float, float>> resumeCentre() const { return centreOf(resume_); }
    std::optional<std::pair<float, float>> quitCentre() const { return centreOf(quit_); }
    std::optional<std::pair<float, float>> controlsCentre() const { return centreOf(controls_); }
    std::optional<std::pair<float, float>> fovDownCentre() const { return centreOf(fovDown_); }
    std::optional<std::pair<float, float>> fovUpCentre() const { return centreOf(fovUp_); }
    std::optional<std::pair<float, float>> sensitivityDownCentre() const { return centreOf(sensDown_); }
    std::optional<std::pair<float, float>> sensitivityUpCentre() const { return centreOf(sensUp_); }
    std::optional<std::pair<float, float>> displayModeCentre() const { return centreOf(mode_); }
    std::optional<std::pair<float, float>> resolutionDownCentre() const { return centreOf(resDown_); }
    std::optional<std::pair<float, float>> resolutionUpCentre() const { return centreOf(resUp_); }
    std::optional<std::pair<float, float>> vsyncCentre() const { return centreOf(vsync_); }
    std::optional<std::pair<float, float>> uiScaleDownCentre() const { return centreOf(scaleDown_); }
    std::optional<std::pair<float, float>> uiScaleUpCentre() const { return centreOf(scaleUp_); }
    std::optional<std::pair<float, float>> paletteCentre() const { return centreOf(palette_); }

private:
    struct Click final : Rml::EventListener {
        void ProcessEvent(Rml::Event&) override { requested = true; }
        bool requested = false;
    };

    struct Count final : Rml::EventListener {
        void ProcessEvent(Rml::Event&) override { ++clicks; }
        int clicks = 0;
    };

    std::optional<std::pair<float, float>> centreOf(Rml::Element* element) const;

    UiContext& ui_;
    Click resumeClick_;
    Click quitClick_;
    Click controlsClick_;
    Count fovDownClicks_;
    Count fovUpClicks_;
    Count sensDownClicks_;
    Count sensUpClicks_;
    Count modeClicks_;
    Count resDownClicks_;
    Count resUpClicks_;
    Count vsyncClicks_;
    Count scaleDownClicks_;
    Count scaleUpClicks_;
    Count paletteClicks_;
    Rml::Element* resume_ = nullptr;
    Rml::Element* quit_ = nullptr;
    Rml::Element* controls_ = nullptr;
    Rml::Element* fovDown_ = nullptr;
    Rml::Element* fovUp_ = nullptr;
    Rml::Element* fovValue_ = nullptr;
    Rml::Element* sensDown_ = nullptr;
    Rml::Element* sensUp_ = nullptr;
    Rml::Element* sensValue_ = nullptr;
    Rml::Element* mode_ = nullptr;
    Rml::Element* resDown_ = nullptr;
    Rml::Element* resUp_ = nullptr;
    Rml::Element* resValue_ = nullptr;
    Rml::Element* vsync_ = nullptr;
    Rml::Element* scaleDown_ = nullptr;
    Rml::Element* scaleUp_ = nullptr;
    Rml::Element* scaleValue_ = nullptr;
    Rml::Element* palette_ = nullptr;
    Rml::ElementDocument* document_ = nullptr;
    bool visible_ = false;
};

}
