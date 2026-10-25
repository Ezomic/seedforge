#pragma once

#include "ichor/ui/ui_context.h"
#include "ichor/world/parley.h"

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

constexpr std::size_t kDialogueMaxRows = 14;
constexpr std::size_t kDialogueMaxTextBytes = 600;

// The grey-box conversation box (docs/10-npcs.md): who is speaking, what they say, and the things the player can
// answer, drawn along the bottom of the screen. On the trade page it also lists what the merchant sells and what
// the player may sell, each row with a Buy or Sell button. Hidden until shown. The context must have the Cinzel and
// Alegreya Sans faces loaded.
class DialogueScreen {
public:
    explicit DialogueScreen(UiContext& ui);
    ~DialogueScreen();
    DialogueScreen(const DialogueScreen&) = delete;
    DialogueScreen& operator=(const DialogueScreen&) = delete;

    bool ok() const { return document_ != nullptr; }

    void setVisible(bool visible);
    bool visible() const { return visible_; }

    // Writes the view to the box; nothing is rewritten when it is the one already on it.
    void bind(const ParleyView& view);

    // The markup currently in the box, for tests.
    std::string contentRml() const;

    // The keys of the choices and Buy or Sell buttons clicked since the last call, in order. The close button gives "bye".
    std::vector<std::string> takeChoices();

    // Where the button with this key sits on screen once laid out.
    std::optional<std::pair<float, float>> buttonCentre(const std::string& key) const;

private:
    struct Clicks final : Rml::EventListener {
        void ProcessEvent(Rml::Event& event) override;
        std::vector<std::string> keys;
    };

    UiContext& ui_;
    Clicks clicks_;
    Rml::ElementDocument* document_ = nullptr;
    Rml::Element* content_ = nullptr;
    Rml::Element* closeButton_ = nullptr;
    ParleyView shown_;
    bool visible_ = false;
    bool written_ = false;
};

}
