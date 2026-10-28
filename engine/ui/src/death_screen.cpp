#include "ichor/ui/death_screen.h"

#include "ichor/ui/ledger.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

namespace ichor {

namespace {

constexpr const char* kDeathDocument = R"(<rml><head><style>
    body, div { display: block; }
    body { width: 100%; height: 100%; }
    #band { position: absolute; left: 0px; right: 0px; top: 38%; padding: 18px 0px; text-align: center; font-family: Alegreya Sans; background-color: #10141add; border-top: 1px #a08850; border-bottom: 1px #a08850; }
    #title { font-size: 44px; color: #c84040; }
    #cause { margin-top: 8px; font-size: 22px; color: #f0e6c8; }
    #retry { margin-top: 10px; font-size: 18px; color: #c8b888; }
</style></head><body>
    <div id="band"><div id="title">YOU DIED</div><div id="cause"></div><div id="retry"></div></div>
</body></rml>)";

}

DeathScreen::DeathScreen(UiContext& ui)
    : ui_(ui)
{
    document_ = ui_.loadDocumentFromMemory(kDeathDocument, "[death]");
    if (document_ == nullptr) {
        return;
    }
    cause_ = document_->GetElementById("cause");
    retry_ = document_->GetElementById("retry");
    if (cause_ == nullptr || retry_ == nullptr) {
        ui_.unloadDocument(document_);
        document_ = nullptr;
    }
}

DeathScreen::~DeathScreen()
{
    if (document_ != nullptr) {
        ui_.unloadDocument(document_);
    }
}

void DeathScreen::bind(const std::optional<DeathScreenView>& view)
{
    if (document_ == nullptr) {
        return;
    }
    if (!view) {
        if (shown_) {
            shown_.reset();
            document_->Hide();
        }
        return;
    }
    if (shown_ && shown_->cause == view->cause && shown_->retry == view->retry) {
        return;
    }
    cause_->SetInnerRML(escapeRml(view->cause));
    retry_->SetInnerRML(escapeRml(view->retry));
    if (!shown_) {
        document_->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
    }
    shown_ = view;
}

bool DeathScreen::visible() const
{
    return shown_.has_value();
}

std::string DeathScreen::markup() const
{
    return document_ != nullptr ? document_->GetInnerRML() : std::string();
}

}
