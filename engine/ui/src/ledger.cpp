#include "ichor/ui/ledger.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <algorithm>

namespace ichor {

namespace {

constexpr const char* kLedgerDocument = R"(<rml><head><style>
    body, div { display: block; }
    body { width: 100%; height: 100%; }
    #card { position: absolute; top: 12%; left: 12%; right: 12%; padding: 22px 28px 22px 28px; background-color: #10141af0; border: 3px #a08850; }
    #title { font-family: Cinzel; font-size: 34px; color: #e8d8a8; letter-spacing: 3px; text-align: center; padding-bottom: 12px; }
    .row { padding: 5px 0px 5px 0px; border-bottom: 1px #2c3340; font-family: Alegreya Sans; font-size: 19px; color: #d8d8d8; }
    .when { color: #8890a0; font-size: 16px; padding-right: 10px; }
    .kind { color: #f0b040; font-weight: bold; padding-right: 10px; }
    .empty { font-family: Alegreya Sans; font-size: 19px; color: #8890a0; text-align: center; padding: 18px 0px 18px 0px; }
</style></head><body>
    <div id="card"><div id="title">THE LEDGER</div><div id="list"></div></div>
</body></rml>)";

}

std::string escapeRml(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out += c; break;
        }
    }
    return out;
}

Ledger::Ledger(UiContext& ui)
    : ui_(ui)
{
    document_ = ui_.loadDocumentFromMemory(kLedgerDocument, "[ledger]");
    if (document_ == nullptr) {
        return;
    }
    list_ = document_->GetElementById("list");
    if (list_ == nullptr) {
        ui_.unloadDocument(document_);
        document_ = nullptr;
    }
}

Ledger::~Ledger()
{
    if (document_ != nullptr) {
        ui_.unloadDocument(document_);
    }
}

void Ledger::setVisible(bool visible)
{
    if (document_ == nullptr || visible == visible_) {
        return;
    }
    visible_ = visible;
    if (visible_) {
        document_->Show();
    } else {
        document_->Hide();
    }
}

void Ledger::setRows(const std::vector<LedgerRow>& rows)
{
    if (list_ == nullptr) {
        return;
    }
    const std::size_t count = std::min(rows.size(), kMaxRows);
    const std::vector<LedgerRow> wanted(rows.begin(), rows.begin() + static_cast<std::ptrdiff_t>(count));
    if (written_ && wanted == shown_) {
        return;
    }
    shown_ = wanted;
    written_ = true;
    std::string markup;
    if (wanted.empty()) {
        markup = "<div class=\"empty\">Nothing has been written yet.</div>";
    }
    for (const LedgerRow& row : wanted) {
        markup += "<div class=\"row\"><span class=\"when\">" + escapeRml(row.time) + "</span><span class=\"kind\">" + escapeRml(row.category)
            + "</span>" + escapeRml(row.text) + "</div>";
    }
    list_->SetInnerRML(markup);
}

}
