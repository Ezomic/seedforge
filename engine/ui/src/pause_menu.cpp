#include "ichor/ui/pause_menu.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

#include <cmath>
#include <format>

namespace ichor {

namespace {

constexpr const char* kPauseDocument = R"(<rml><head><style>
    body, div { display: block; }
    body { width: 100%; height: 100%; }
    #veil { position: absolute; top: 0px; left: 0px; right: 0px; bottom: 0px; background-color: #05070bb0; }
    #card { position: absolute; top: 28%; left: 50%; margin-left: -203px; width: 340px; padding: 26px 30px 30px 30px; background-color: #10141af4; border: 3px #a08850; }
    #title { font-family: Cinzel; font-size: 36px; color: #e8d8a8; letter-spacing: 4px; text-align: center; padding-bottom: 20px; }
    .button { font-family: Alegreya Sans; font-size: 26px; text-align: center; color: #d8d8d8; padding: 10px 0px 12px 0px; margin-top: 10px; background-color: #1c222c; border: 2px #3a4252; }
    .label { font-family: Alegreya Sans; font-size: 22px; color: #a8a090; text-align: center; padding-top: 6px; }
    #fov, #sens { height: 52px; margin-top: 6px; }
    .step { float: left; width: 56px; font-family: Alegreya Sans; font-size: 28px; text-align: center; color: #d8d8d8; padding: 4px 0px 6px 0px; background-color: #1c222c; border: 2px #3a4252; }
    #fovup, #sensup { float: right; }
    .step:hover { color: #f4e8c0; background-color: #2a3242; border-color: #a08850; }
    #fovvalue, #sensvalue { font-family: Alegreya Sans; font-size: 28px; text-align: center; color: #f4e8c0; padding-top: 8px; }
    .button:hover { color: #f4e8c0; background-color: #2a3242; border-color: #a08850; }
</style></head><body>
    <div id="veil"></div>
    <div id="card"><div id="title">PAUSED</div><div class="label">Field of view</div><div id="fov"><div id="fovdown" class="step">-</div><div id="fovup" class="step">+</div><div id="fovvalue">45 deg</div></div><div class="label">Mouse sensitivity</div><div id="sens"><div id="sensdown" class="step">-</div><div id="sensup" class="step">+</div><div id="sensvalue">1.0</div></div><div id="resume" class="button">Resume</div><div id="quit" class="button">Quit</div></div>
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
    fovDown_ = document_->GetElementById("fovdown");
    fovUp_ = document_->GetElementById("fovup");
    fovValue_ = document_->GetElementById("fovvalue");
    sensDown_ = document_->GetElementById("sensdown");
    sensUp_ = document_->GetElementById("sensup");
    sensValue_ = document_->GetElementById("sensvalue");
    if (resume_ == nullptr || quit_ == nullptr || fovDown_ == nullptr || fovUp_ == nullptr || fovValue_ == nullptr || sensDown_ == nullptr || sensUp_ == nullptr || sensValue_ == nullptr) {
        ui_.unloadDocument(document_);
        document_ = nullptr;
        return;
    }
    resume_->AddEventListener("click", &resumeClick_);
    quit_->AddEventListener("click", &quitClick_);
    fovDown_->AddEventListener("click", &fovDownClicks_);
    fovUp_->AddEventListener("click", &fovUpClicks_);
    sensDown_->AddEventListener("click", &sensDownClicks_);
    sensUp_->AddEventListener("click", &sensUpClicks_);
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
    if (fovDown_ != nullptr) {
        fovDown_->RemoveEventListener("click", &fovDownClicks_);
    }
    if (fovUp_ != nullptr) {
        fovUp_->RemoveEventListener("click", &fovUpClicks_);
    }
    if (sensDown_ != nullptr) {
        sensDown_->RemoveEventListener("click", &sensDownClicks_);
    }
    if (sensUp_ != nullptr) {
        sensUp_->RemoveEventListener("click", &sensUpClicks_);
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
        fovDownClicks_.clicks = 0;
        fovUpClicks_.clicks = 0;
        sensDownClicks_.clicks = 0;
        sensUpClicks_.clicks = 0;
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

void PauseMenu::setFovDegrees(float degrees)
{
    if (fovValue_ != nullptr && std::isfinite(degrees)) {
        fovValue_->SetInnerRML(std::format("{} deg", static_cast<int>(std::lround(degrees))));
    }
}

int PauseMenu::takeFovSteps()
{
    const int steps = fovUpClicks_.clicks - fovDownClicks_.clicks;
    fovUpClicks_.clicks = 0;
    fovDownClicks_.clicks = 0;
    return steps;
}

void PauseMenu::setMouseSensitivity(float sensitivity)
{
    if (sensValue_ != nullptr && std::isfinite(sensitivity)) {
        sensValue_->SetInnerRML(std::format("{:.1f}", sensitivity));
    }
}

int PauseMenu::takeSensitivitySteps()
{
    const int steps = sensUpClicks_.clicks - sensDownClicks_.clicks;
    sensUpClicks_.clicks = 0;
    sensDownClicks_.clicks = 0;
    return steps;
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
