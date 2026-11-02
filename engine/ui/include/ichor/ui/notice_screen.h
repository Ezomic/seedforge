#pragma once

#include "ichor/ui/ui_context.h"

#include <optional>
#include <string>

namespace Rml {
class Element;
class ElementDocument;
}

namespace ichor {

// A one-line notice banner near the top of the screen, for things the player should know at startup, such as a save restored
// from a backup. The context must have the Alegreya Sans face loaded.
class NoticeScreen {
public:
    explicit NoticeScreen(UiContext& ui);
    ~NoticeScreen();
    NoticeScreen(const NoticeScreen&) = delete;
    NoticeScreen& operator=(const NoticeScreen&) = delete;

    bool ok() const { return document_ != nullptr; }

    // A line shows the notice; none hides it.
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
