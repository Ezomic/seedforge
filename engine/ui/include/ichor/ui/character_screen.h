#pragma once

#include "ichor/ui/character_screen_model.h"
#include "ichor/ui/ui_context.h"

#include <filesystem>
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

// The character screen: the spine stats, capabilities, verbs and faction standing as a card over
// the world. The document and its style are read from `document` (data/ui/character_screen.rml).
// Hidden until shown. The context must have the Cinzel and Alegreya Sans faces loaded.
class CharacterScreen {
public:
    CharacterScreen(UiContext& ui, const std::filesystem::path& document);
    ~CharacterScreen();
    CharacterScreen(const CharacterScreen&) = delete;
    CharacterScreen& operator=(const CharacterScreen&) = delete;

    bool ok() const { return document_ != nullptr; }

    void setVisible(bool visible);
    bool visible() const { return visible_; }

    // Builds the sections from the model input and writes them to the card; nothing is rewritten
    // when they are the ones already on it.
    void bind(const CharacterScreenInput& input);

    // The markup currently on the card, for tests.
    std::string contentRml() const;

    // True once after the close button was clicked.
    bool takeCloseRequest();

    // Where the close button sits on screen, once the card has been laid out.
    std::optional<std::pair<float, float>> closeButtonCentre() const;

private:
    struct Click final : Rml::EventListener {
        void ProcessEvent(Rml::Event&) override { requested = true; }
        bool requested = false;
    };

    UiContext& ui_;
    Click click_;
    Rml::Element* close_ = nullptr;
    Rml::ElementDocument* document_ = nullptr;
    Rml::Element* list_ = nullptr;
    std::vector<CharacterScreenSection> shown_;
    bool visible_ = false;
    bool written_ = false;
};

}
