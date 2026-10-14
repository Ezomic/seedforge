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

// The menu Esc opens: a veil over the world and a card with Resume and Quit. Hidden until shown.
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

    // Where the buttons sit on screen once laid out, for tests and for anything that wants to
    // point at them. Nothing while the menu is hidden.
    std::optional<std::pair<float, float>> resumeCentre() const { return centreOf(resume_); }
    std::optional<std::pair<float, float>> quitCentre() const { return centreOf(quit_); }

private:
    struct Click final : Rml::EventListener {
        void ProcessEvent(Rml::Event&) override { requested = true; }
        bool requested = false;
    };

    std::optional<std::pair<float, float>> centreOf(Rml::Element* element) const;

    UiContext& ui_;
    Click resumeClick_;
    Click quitClick_;
    Rml::Element* resume_ = nullptr;
    Rml::Element* quit_ = nullptr;
    Rml::ElementDocument* document_ = nullptr;
    bool visible_ = false;
};

}
