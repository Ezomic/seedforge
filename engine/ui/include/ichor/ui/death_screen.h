#pragma once

#include "ichor/ui/ui_context.h"

#include <optional>
#include <string>

namespace Rml {
class Element;
class ElementDocument;
}

namespace ichor {

struct DeathScreenView {
    std::string cause; // one plain line
    std::string retry; // how to try again
};

// Shown while the player lies dead: a title, the cause and the retry key across the middle of the screen. The context
// must have the Alegreya Sans face loaded.
class DeathScreen {
public:
    explicit DeathScreen(UiContext& ui);
    ~DeathScreen();
    DeathScreen(const DeathScreen&) = delete;
    DeathScreen& operator=(const DeathScreen&) = delete;

    bool ok() const { return document_ != nullptr; }

    // A death shows the screen; none hides it.
    void bind(const std::optional<DeathScreenView>& view);
    bool visible() const;

    std::string markup() const;

private:
    UiContext& ui_;
    Rml::ElementDocument* document_ = nullptr;
    Rml::Element* cause_ = nullptr;
    Rml::Element* retry_ = nullptr;
    std::optional<DeathScreenView> shown_;
};

}
