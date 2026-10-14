#include "ichor/ui/pause_menu.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

namespace ichor {

namespace {

constexpr const char* kPauseDocument = R"(<rml><head><style>
    body, div { display: block; }
    body { width: 100%; height: 100%; }
    #veil { position: absolute; top: 0px; left: 0px; right: 0px; bottom: 0px; background-color: #05070bb0; }
    #card { position: absolute; top: 28%; left: 50%; margin-left: -203px; width: 340px; padding: 26px 30px 30px 30px; background-color: #10141af4; border: 3px #a08850; }
    #title { font-family: Cinzel; font-size: 36px; color: #e8d8a8; letter-spacing: 4px; text-align: center; padding-bottom: 20px; }
    .button { font-family: Alegreya Sans; font-size: 26px; text-align: center; color: #d8d8d8; padding: 10px 0px 12px 0px; margin-top: 10px; background-color: #1c222c; border: 2px #3a4252; }
    .button:hover { color: #f4e8c0; background-color: #2a3242; border-color: #a08850; }
</style></head><body>
    <div id="veil"></div>
    <div id="card"><div id="title">PAUSED</div><div id="resume" class="button">Resume</div><div id="quit" class="button">Quit</div></div>
</body></rml>)";

}

PauseMenu::PauseMenu(UiContext& ui)
    : ui_(ui)
{
    document_ = ui_.loadDocumentFromMemory(kPauseDocument, "[pause menu]");
    if (document_ == nullptr) {
        return;
    }
    resume_ = document_->GetElementById("resume");
    quit_ = document_->GetElementById("quit");
    if (resume_ == nullptr || quit_ == nullptr) {
        ui_.unloadDocument(document_);
        document_ = nullptr;
        return;
    }
    resume_->AddEventListener("click", &resumeClick_);
    quit_->AddEventListener("click", &quitClick_);
}

PauseMenu::~PauseMenu()
{
    // The elements may live on until the context next updates, and the listeners go first.
    if (resume_ != nullptr) {
        resume_->RemoveEventListener("click", &resumeClick_);
    }
    if (quit_ != nullptr) {
        quit_->RemoveEventListener("click", &quitClick_);
    }
    if (document_ != nullptr) {
        ui_.unloadDocument(document_);
    }
}

void PauseMenu::setVisible(bool visible)
{
    if (document_ == nullptr || visible == visible_) {
        return;
    }
    visible_ = visible;
    if (visible_) {
        resumeClick_.requested = false; // a click from an earlier showing is not a click now
        quitClick_.requested = false;
        document_->Show();
    } else {
        document_->Hide();
    }
}

bool PauseMenu::takeResume()
{
    const bool requested = resumeClick_.requested;
    resumeClick_.requested = false;
    return requested;
}

bool PauseMenu::takeQuit()
{
    const bool requested = quitClick_.requested;
    quitClick_.requested = false;
    return requested;
}

std::optional<std::pair<float, float>> PauseMenu::centreOf(Rml::Element* element) const
{
    if (element == nullptr || !visible_) {
        return std::nullopt;
    }
    const Rml::Vector2f offset = element->GetAbsoluteOffset(Rml::BoxArea::Border);
    const Rml::Vector2f size = element->GetBox().GetSize(Rml::BoxArea::Border);
    return std::pair{offset.x + size.x * 0.5f, offset.y + size.y * 0.5f};
}

}
