#pragma once

#include "ichor/ui/ui_context.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <RmlUi/Core/EventListener.h>

namespace Rml {
class Element;
class ElementDocument;
}

namespace ichor {

struct LedgerRow {
    std::string time;     // when it happened, as shown
    std::string category; // what kind of event
    std::string text;     // what happened; plain text, never markup
    bool operator==(const LedgerRow&) const = default;
};

// Text made safe to place inside an RML document: markup characters become entities. Whatever
// a script or a save wrote into the chronicle goes through this, so it can never act as markup.
std::string escapeRml(std::string_view text);

// The journal screen: the newest entries of the chronicle on a card over the world. Hidden
// until shown. The context must have the Cinzel and Alegreya Sans faces loaded.
class Ledger {
public:
    static constexpr std::size_t kMaxRows = 12;

    explicit Ledger(UiContext& ui);
    ~Ledger();
    Ledger(const Ledger&) = delete;
    Ledger& operator=(const Ledger&) = delete;

    bool ok() const { return document_ != nullptr; }

    void setVisible(bool visible);
    bool visible() const { return visible_; }

    // Newest first. Only the first kMaxRows are shown, and nothing is rewritten when the rows are
    // the ones already on the card.
    void setRows(const std::vector<LedgerRow>& rows);

    // True once after the close button was clicked.
    bool takeCloseRequest();

    // Where the close button sits on screen, once the card has been laid out. For tests and
    // anything that wants to point at it.
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
    std::vector<LedgerRow> shown_;
    bool visible_ = false;
    bool written_ = false;
};

}
