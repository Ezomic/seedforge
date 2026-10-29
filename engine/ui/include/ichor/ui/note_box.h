#pragma once

#include "ichor/ui/ui_context.h"

#include <optional>
#include <string>

namespace Rml {
class Element;
class ElementDocument;
}

namespace ichor {

// The F8 feedback note's one-line text box, low across the screen. The context must have the Alegreya Sans face loaded.
class NoteBox {
public:
    explicit NoteBox(UiContext& ui);
    ~NoteBox();
    NoteBox(const NoteBox&) = delete;
    NoteBox& operator=(const NoteBox&) = delete;

    bool ok() const { return document_ != nullptr; }

    // The line typed so far shows the box; none hides it.
    void bind(const std::optional<std::string>& text);
    bool visible() const;

    std::string markup() const;

private:
    UiContext& ui_;
    Rml::ElementDocument* document_ = nullptr;
    Rml::Element* line_ = nullptr;
    std::optional<std::string> shown_;
};

}
