#pragma once

#include "ichor/ui/ui_context.h"

#include <optional>
#include <string>

namespace Rml {
class Element;
class ElementDocument;
}

namespace ichor {

// The first-minute prompt: one short line below the middle of the screen, clear of the HUD plate, the death band and
// the captions. The context must have the Alegreya Sans face loaded.
class PromptScreen {
public:
    explicit PromptScreen(UiContext& ui);
    ~PromptScreen();
    PromptScreen(const PromptScreen&) = delete;
    PromptScreen& operator=(const PromptScreen&) = delete;

    bool ok() const { return document_ != nullptr; }

    // A line shows the prompt; none hides it.
    void bind(const std::optional<std::string>& line);
    bool visible() const;

    std::string markup() const;

private:
    UiContext& ui_;
    Rml::ElementDocument* document_ = nullptr;
    Rml::Element* line_ = nullptr;
    std::optional<std::string> shown_;
};

}
