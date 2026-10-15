#pragma once

#include "ichor/ui/ui_context.h"

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

struct JournalRow {
    std::string time; // when it was written, as shown
    std::string text; // the prose; plain text, never markup
    bool operator==(const JournalRow&) const = default;
};

// The journal screen: what the player was asked, by whom, where, and what they decided, as
// prose on a card over the world. Hidden until shown. The context must have the Cinzel and
// Alegreya Sans faces loaded.
class JournalScreen {
public:
    // Prose runs to a few lines, so fewer fit than on the ledger.
    static constexpr std::size_t kMaxRows = 6;

    explicit JournalScreen(UiContext& ui);
    ~JournalScreen();
    JournalScreen(const JournalScreen&) = delete;
    JournalScreen& operator=(const JournalScreen&) = delete;

    bool ok() const { return document_ != nullptr; }

    void setVisible(bool visible);
    bool visible() const { return visible_; }

    // Newest first. Only the first kMaxRows are shown, and nothing is rewritten when the rows are
    // the ones already on the card.
    void setRows(const std::vector<JournalRow>& rows);

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
    std::vector<JournalRow> shown_;
    bool visible_ = false;
    bool written_ = false;
};

}
