#pragma once

#include "ichor/core/key_bindings.h"
#include "ichor/ui/ui_context.h"

#include <array>
#include <optional>
#include <utility>

#include <RmlUi/Core/EventListener.h>

namespace Rml {
class Element;
class ElementDocument;
}

namespace ichor {

// The controls screen the pause menu opens: one row per rebindable action with its key as a button, Reset and Back.
// Clicking a key button makes that row wait for a key; the owner hears the click, reads the key, and puts the
// bindings back here. The screen never decides a binding. Hidden until shown.
// The context must have the Cinzel and Alegreya Sans faces loaded.
class RebindScreen {
public:
    explicit RebindScreen(UiContext& ui);
    ~RebindScreen();
    RebindScreen(const RebindScreen&) = delete;
    RebindScreen& operator=(const RebindScreen&) = delete;

    bool ok() const { return document_ != nullptr; }

    void setVisible(bool visible);
    bool visible() const { return visible_; }

    // The keys the rows show. The row waiting for a key shows a prompt instead.
    void setBindings(const KeyBindings& bindings);
    void setWaiting(std::optional<BindAction> action);
    std::optional<BindAction> waiting() const { return waiting_; }

    // The row whose key button was clicked since the last call, once.
    std::optional<BindAction> takeRebindClick();
    bool takeReset();
    bool takeBack();

    std::optional<std::pair<float, float>> keyCentre(BindAction action) const { return centreOf(keys_[static_cast<std::size_t>(action)]); }
    std::optional<std::pair<float, float>> resetCentre() const { return centreOf(reset_); }
    std::optional<std::pair<float, float>> backCentre() const { return centreOf(back_); }

private:
    struct Click final : Rml::EventListener {
        void ProcessEvent(Rml::Event&) override { requested = true; }
        bool requested = false;
    };

    void refresh();
    std::optional<std::pair<float, float>> centreOf(Rml::Element* element) const;

    UiContext& ui_;
    std::array<Click, kBindActionCount> keyClicks_;
    Click resetClick_;
    Click backClick_;
    std::array<Rml::Element*, kBindActionCount> keys_{};
    Rml::Element* reset_ = nullptr;
    Rml::Element* back_ = nullptr;
    Rml::ElementDocument* document_ = nullptr;
    KeyBindings bindings_;
    std::optional<BindAction> waiting_;
    bool visible_ = false;
};

}
