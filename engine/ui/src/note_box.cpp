#include "ichor/ui/note_box.h"

#include "ichor/ui/ledger.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <string_view>

namespace ichor {

namespace {

// About what one line of the box holds; a longer note shows its newest end.
constexpr std::size_t kShownCharacters = 90;

constexpr const char* kNoteDocument = R"(<rml><head><style>
    body, div { display: block; }
    body { width: 100%; height: 100%; }
    #band { position: absolute; left: 10%; right: 10%; bottom: 14%; padding: 12px 18px; font-family: Alegreya Sans; background-color: #10141aee; border: 1px #a08850; }
    #title { font-size: 16px; color: #c8b888; }
    #line { margin-top: 6px; font-size: 22px; color: #f0e6c8; white-space: nowrap; overflow: hidden; }
</style></head><body>
    <div id="band"><div id="title">Feedback note: Enter saves, Esc cancels</div><div id="line"></div></div>
</body></rml>)";

std::string_view newestEnd(std::string_view text)
{
    std::size_t characters = 0;
    for (std::size_t i = text.size(); i > 0; --i) {
        if ((static_cast<unsigned char>(text[i - 1]) & 0xC0) != 0x80 && ++characters == kShownCharacters) {
            return text.substr(i - 1);
        }
    }
    return text;
}

}

NoteBox::NoteBox(UiContext& ui)
    : ui_(ui)
{
    document_ = ui_.loadDocumentFromMemory(kNoteDocument, "[note]");
    if (document_ == nullptr) {
        return;
    }
    line_ = document_->GetElementById("line");
    if (line_ == nullptr) {
        ui_.unloadDocument(document_);
        document_ = nullptr;
    }
}

NoteBox::~NoteBox()
{
    if (document_ != nullptr) {
        ui_.unloadDocument(document_);
    }
}

void NoteBox::bind(const std::optional<std::string>& text)
{
    if (document_ == nullptr) {
        return;
    }
    if (!text) {
        if (shown_) {
            shown_.reset();
            document_->Hide();
        }
        return;
    }
    if (shown_ == text) {
        return;
    }
    const std::string_view end = newestEnd(*text);
    line_->SetInnerRML((end.size() < text->size() ? "..." : "") + escapeRml(end) + "_");
    if (!shown_) {
        document_->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
    }
    shown_ = text;
}

bool NoteBox::visible() const
{
    return shown_.has_value();
}

std::string NoteBox::markup() const
{
    return document_ != nullptr ? document_->GetInnerRML() : std::string();
}

}
