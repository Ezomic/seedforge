#include "ichor/ui/prompt_screen.h"

#include "ichor/ui/ledger.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

namespace ichor {

namespace {

// Below the death band (38% down) and above the captions (96px from the bottom).
constexpr const char* kPromptDocument = R"(<rml><head><style>
    body, div { display: block; }
    body { width: 100%; height: 100%; }
    #prompt { position: absolute; left: 0px; right: 0px; top: 66%; text-align: center; font-family: Alegreya Sans; font-size: 24px; color: #f0e6c8; }
    #line { margin: 0px auto; padding: 4px 16px; width: 420px; background-color: #10141add; border: 1px #a08850; }
</style></head><body>
    <div id="prompt"><div id="line"></div></div>
</body></rml>)";

}

PromptScreen::PromptScreen(UiContext& ui)
    : ui_(ui)
{
    document_ = ui_.loadDocumentFromMemory(kPromptDocument, "[prompt]");
    if (document_ == nullptr) {
        return;
    }
    line_ = document_->GetElementById("line");
    if (line_ == nullptr) {
        ui_.unloadDocument(document_);
        document_ = nullptr;
    }
}

PromptScreen::~PromptScreen()
{
    if (document_ != nullptr) {
        ui_.unloadDocument(document_);
    }
}

void PromptScreen::bind(const std::optional<std::string>& line)
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

bool PromptScreen::visible() const
{
    return shown_.has_value();
}

std::string PromptScreen::markup() const
{
    return document_ != nullptr ? document_->GetInnerRML() : std::string();
}

}
