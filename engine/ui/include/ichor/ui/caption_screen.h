#pragma once

#include "ichor/ui/ui_context.h"

#include <string>
#include <vector>

namespace Rml {
class Element;
class ElementDocument;
}

namespace ichor {

// Sound captions: a few small lines of text at the bottom centre of the screen. The context must have the Alegreya Sans face loaded.
class CaptionScreen {
public:
    explicit CaptionScreen(UiContext& ui);
    ~CaptionScreen();
    CaptionScreen(const CaptionScreen&) = delete;
    CaptionScreen& operator=(const CaptionScreen&) = delete;

    bool ok() const { return document_ != nullptr; }

    // Shows these lines, oldest first; none hides the strip.
    void bind(const std::vector<std::string>& lines);

    std::string markup() const;

private:
    UiContext& ui_;
    Rml::ElementDocument* document_ = nullptr;
    Rml::Element* strip_ = nullptr;
};

}
