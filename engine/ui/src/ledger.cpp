#include "ichor/ui/ledger.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/ElementDocument.h>

#include <algorithm>

namespace ichor {

namespace {

constexpr const char* kLedgerDocument = R"(<rml><head><style>
    body, div { display: block; }
    body { width: 100%; height: 100%; }
    #card { position: absolute; top: 12%; bottom: 12%; left: 12%; right: 12%; overflow: hidden; padding: 22px 28px 22px 28px; background-color: #10141af0; border: 3px #a08850; }
    #close { position: absolute; top: 10px; right: 14px; width: 34px; height: 34px; font-family: Alegreya Sans; font-size: 30px; line-height: 34px; text-align: center; color: #a08850; }
    #close:hover { color: #e8d8a8; background-color: #2c3340; }
    #title { font-family: Cinzel; font-size: 34px; color: #e8d8a8; letter-spacing: 3px; text-align: center; padding-bottom: 12px; }
    .row { word-break: break-word; padding: 5px 0px 5px 0px; border-bottom: 1px #2c3340; font-family: Alegreya Sans; font-size: 19px; color: #d8d8d8; }
    .when { color: #8890a0; font-size: 16px; padding-right: 10px; }
    .kind { color: #f0b040; font-weight: bold; padding-right: 10px; }
    .empty { font-family: Alegreya Sans; font-size: 19px; color: #8890a0; text-align: center; padding: 18px 0px 18px 0px; }
</style></head><body>
    <div id="card"><div id="close">&#215;</div><div id="title">THE LEDGER</div><div id="list"></div></div>
</body></rml>)";

}

namespace {

// How many bytes the UTF-8 sequence starting at text[at] has, or zero when it is not a valid one.
std::size_t utf8Length(std::string_view text, std::size_t at)
{
    const auto byte = [&](std::size_t i) { return static_cast<unsigned char>(text[i]); };
    const unsigned char lead = byte(at);
    const std::size_t length = lead >= 0xC2 && lead <= 0xDF ? 2 : (lead & 0xF0) == 0xE0 ? 3 : lead >= 0xF0 && lead <= 0xF4 ? 4 : 0;
    if (length == 0 || at + length > text.size()) {
        return 0;
    }
    for (std::size_t i = 1; i < length; ++i) {
        if ((byte(at + i) & 0xC0) != 0x80) {
            return 0;
        }
    }
    return length;
}

}

std::string escapeRml(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        const char c = text[i];
        const auto byte = static_cast<unsigned char>(c);
        if (byte >= 0x80) {
            // Bad UTF-8 never reaches the parser or the font code.
            const std::size_t length = utf8Length(text, i);
            if (length == 0) {
                out += '?';
                ++i;
            } else {
                out.append(text.substr(i, length));
                i += length;
            }
            continue;
        }
        ++i;
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out += (byte < 0x20 || byte == 0x7F) ? ' ' : c; break;
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
    close_ = document_->GetElementById("close");
    if (list_ == nullptr || close_ == nullptr) {
        ui_.unloadDocument(document_);
        document_ = nullptr;
        return;
    }
    close_->AddEventListener("click", &click_);
}

Ledger::~Ledger()
{
    if (document_ != nullptr) {
        ui_.unloadDocument(document_);
    }
}

bool Ledger::takeCloseRequest()
{
    const bool requested = click_.requested;
    click_.requested = false;
    return requested;
}

std::optional<std::pair<float, float>> Ledger::closeButtonCentre() const
{
    if (close_ == nullptr || !visible_) {
        return std::nullopt;
    }
    const Rml::Vector2f offset = close_->GetAbsoluteOffset(Rml::BoxArea::Border);
    const Rml::Vector2f size = close_->GetBox().GetSize(Rml::BoxArea::Border);
    return std::pair{offset.x + size.x * 0.5f, offset.y + size.y * 0.5f};
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
    const auto end = rows.begin() + static_cast<std::ptrdiff_t>(count);
    if (written_ && count == shown_.size() && std::equal(rows.begin(), end, shown_.begin())) {
        return;
    }
    const std::vector<LedgerRow> wanted(rows.begin(), end);
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
