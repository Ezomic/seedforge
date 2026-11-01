#include "ichor/ui/notice_screen.h"

#include "ichor/ui/ledger.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

namespace ichor {

namespace {

constexpr const char* kNoticeDocument = R"(<rml><head><style>
    body, div { display: block; }
    body { width: 100%; height: 100%; }
    #notice { position: absolute; left: 0px; right: 0px; top: 16%; text-align: center; font-family: Alegreya Sans; font-size: 24px; color: #f0e6c8; }
    #line { margin: 0px auto; padding: 4px 16px; width: 900px; background-color: #10141add; border: 1px #a08850; }
</style></head><body>
    <div id="notice"><div id="line"></div></div>
</body></rml>)";

}

NoticeScreen::NoticeScreen(UiContext& ui)
    : ui_(ui)
{
    document_ = ui_.loadDocumentFromMemory(kNoticeDocument, "[notice]");
    if (document_ == nullptr) {
        return;
    }
    line_ = document_->GetElementById("line");
    if (line_ == nullptr) {
        ui_.unloadDocument(document_);
        document_ = nullptr;
    }
}

NoticeScreen::~NoticeScreen()
{
    if (document_ != nullptr) {
        ui_.unloadDocument(document_);
    }
}

void NoticeScreen::bind(const std::optional<std::string>& line)
{
    if (document_ == nullptr || line == shown_) {
        return;
    }
    if (!line) {
        shown_.reset();
        document_->Hide();
        return;
    }
    line_->SetInnerRML(escapeRml(*line));
    if (!shown_) {
        document_->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
    }
    shown_ = line;
}

bool NoticeScreen::visible() const
{
    return shown_.has_value();
}

std::string NoticeScreen::markup() const
{
    return document_ != nullptr ? document_->GetInnerRML() : std::string();
}

}
