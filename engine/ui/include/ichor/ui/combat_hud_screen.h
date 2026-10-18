#pragma once

#include "ichor/ui/combat_hud.h"
#include "ichor/ui/ui_context.h"

#include <filesystem>
#include <string>

namespace Rml {
class Element;
class ElementDocument;
}

namespace ichor {

// The combat HUD drawn from a CombatHudView: health and stamina bars, the Law resource, a ring of
// damage direction markers and the target's stagger tick. Always shown. The markup is the RML file
// at `rml` (data/ui/combat_hud.rml); the context must have the Alegreya Sans face loaded.
class CombatHudScreen {
public:
    CombatHudScreen(UiContext& ui, const std::filesystem::path& rml);
    ~CombatHudScreen();
    CombatHudScreen(const CombatHudScreen&) = delete;
    CombatHudScreen& operator=(const CombatHudScreen&) = delete;

    bool ok() const { return document_ != nullptr; }

    // Shows the view; an absent Law resource, hit or target hides its element.
    void bind(const CombatHudView& view);

    // The markup of the Law readout and the hit ring, for tests.
    std::string markup() const;

private:
    UiContext& ui_;
    Rml::ElementDocument* document_ = nullptr;
    Rml::Element* health_ = nullptr;
    Rml::Element* healthFill_ = nullptr;
    Rml::Element* stamina_ = nullptr;
    Rml::Element* staminaFill_ = nullptr;
    Rml::Element* law_ = nullptr;
    Rml::Element* lawText_ = nullptr;
    Rml::Element* lawFill_ = nullptr;
    Rml::Element* ring_ = nullptr;
    Rml::Element* reticle_ = nullptr;
    Rml::Element* staggerFill_ = nullptr;
};

}
