#include "ichor/ui/pause_menu.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

#include <cmath>
#include <format>
#include <utility>

namespace ichor {

namespace {

constexpr const char* kPauseDocument = R"(<rml><head><style>
    body, div { display: block; }
    body { width: 100%; height: 100%; }
    #veil { position: absolute; top: 0px; left: 0px; right: 0px; bottom: 0px; background-color: #05070bb0; }
    #card { position: absolute; top: 2%; left: 50%; margin-left: -203px; width: 340px; padding: 14px 30px 16px 30px; background-color: #10141af4; border: 3px #a08850; }
    #title { font-family: Cinzel; font-size: 32px; color: #e8d8a8; letter-spacing: 4px; text-align: center; padding-bottom: 8px; }
    .button { font-family: Alegreya Sans; font-size: 24px; text-align: center; color: #d8d8d8; padding: 4px 0px 6px 0px; margin-top: 6px; background-color: #1c222c; border: 2px #3a4252; }
    .button:hover { color: #f4e8c0; background-color: #2a3242; border-color: #a08850; }
    .row { height: 36px; margin-top: 4px; }
    .name { float: left; width: 140px; font-family: Alegreya Sans; font-size: 20px; color: #a8a090; padding-top: 6px; }
    .toggle { float: right; width: 160px; font-family: Alegreya Sans; font-size: 22px; text-align: center; color: #f4e8c0; padding: 2px 0px 4px 0px; background-color: #1c222c; border: 2px #3a4252; }
    .toggle:hover { background-color: #2a3242; border-color: #a08850; }
    .res { float: right; width: 42px; font-family: Alegreya Sans; font-size: 22px; text-align: center; color: #d8d8d8; padding: 2px 0px 4px 0px; background-color: #1c222c; border: 2px #3a4252; }
    .res:hover { color: #f4e8c0; background-color: #2a3242; border-color: #a08850; }
    .value { float: right; width: 100px; font-family: Alegreya Sans; font-size: 22px; text-align: center; color: #f4e8c0; padding-top: 4px; }
</style></head><body>
    <div id="veil"></div>
    <div id="card"><div id="title">PAUSED</div><div class="row"><div class="name">Field of view</div><div id="fovup" class="res">+</div><div id="fovvalue" class="value">45 deg</div><div id="fovdown" class="res">-</div></div><div class="row"><div class="name">Mouse sensitivity</div><div id="sensup" class="res">+</div><div id="sensvalue" class="value">1.0</div><div id="sensdown" class="res">-</div></div><div class="row"><div class="name">Display</div><div id="mode" class="toggle">Windowed</div></div><div class="row"><div class="name">Resolution</div><div id="resup" class="res">&gt;</div><div id="resvalue" class="value">1280 x 720</div><div id="resdown" class="res">&lt;</div></div><div class="row"><div class="name">Vsync</div><div id="vsync" class="toggle">On</div></div><div class="row"><div class="name">UI scale</div><div id="scaleup" class="res">+</div><div id="scalevalue" class="value">100%</div><div id="scaledown" class="res">-</div></div><div class="row"><div class="name">Palette</div><div id="palette" class="toggle">Default</div></div><div id="controls" class="button">Controls</div><div id="resume" class="button">Resume</div><div id="load" class="button">Load</div><div id="quit" class="button">Quit</div></div>
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
    load_ = document_->GetElementById("load");
    quit_ = document_->GetElementById("quit");
    controls_ = document_->GetElementById("controls");
    fovDown_ = document_->GetElementById("fovdown");
    fovUp_ = document_->GetElementById("fovup");
    fovValue_ = document_->GetElementById("fovvalue");
    sensDown_ = document_->GetElementById("sensdown");
    sensUp_ = document_->GetElementById("sensup");
    sensValue_ = document_->GetElementById("sensvalue");
    mode_ = document_->GetElementById("mode");
    resDown_ = document_->GetElementById("resdown");
    resUp_ = document_->GetElementById("resup");
    resValue_ = document_->GetElementById("resvalue");
    vsync_ = document_->GetElementById("vsync");
    scaleDown_ = document_->GetElementById("scaledown");
    scaleUp_ = document_->GetElementById("scaleup");
    scaleValue_ = document_->GetElementById("scalevalue");
    palette_ = document_->GetElementById("palette");
    if (resume_ == nullptr || load_ == nullptr || quit_ == nullptr || controls_ == nullptr || fovDown_ == nullptr || fovUp_ == nullptr || fovValue_ == nullptr || sensDown_ == nullptr || sensUp_ == nullptr || sensValue_ == nullptr
        || mode_ == nullptr || resDown_ == nullptr || resUp_ == nullptr || resValue_ == nullptr || vsync_ == nullptr || scaleDown_ == nullptr || scaleUp_ == nullptr || scaleValue_ == nullptr || palette_ == nullptr) {
        ui_.unloadDocument(document_);
        document_ = nullptr;
        return;
    }
    resume_->AddEventListener("click", &resumeClick_);
    load_->AddEventListener("click", &loadClick_);
    quit_->AddEventListener("click", &quitClick_);
    controls_->AddEventListener("click", &controlsClick_);
    fovDown_->AddEventListener("click", &fovDownClicks_);
    fovUp_->AddEventListener("click", &fovUpClicks_);
    sensDown_->AddEventListener("click", &sensDownClicks_);
    sensUp_->AddEventListener("click", &sensUpClicks_);
    mode_->AddEventListener("click", &modeClicks_);
    resDown_->AddEventListener("click", &resDownClicks_);
    resUp_->AddEventListener("click", &resUpClicks_);
    vsync_->AddEventListener("click", &vsyncClicks_);
    scaleDown_->AddEventListener("click", &scaleDownClicks_);
    scaleUp_->AddEventListener("click", &scaleUpClicks_);
    palette_->AddEventListener("click", &paletteClicks_);
}

PauseMenu::~PauseMenu()
{
    // The elements may live on until the context next updates, and the listeners go first.
    if (resume_ != nullptr) {
        resume_->RemoveEventListener("click", &resumeClick_);
    }
    if (load_ != nullptr) {
        load_->RemoveEventListener("click", &loadClick_);
    }
    if (quit_ != nullptr) {
        quit_->RemoveEventListener("click", &quitClick_);
    }
    if (controls_ != nullptr) {
        controls_->RemoveEventListener("click", &controlsClick_);
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
    if (mode_ != nullptr) {
        mode_->RemoveEventListener("click", &modeClicks_);
    }
    if (resDown_ != nullptr) {
        resDown_->RemoveEventListener("click", &resDownClicks_);
    }
    if (resUp_ != nullptr) {
        resUp_->RemoveEventListener("click", &resUpClicks_);
    }
    if (vsync_ != nullptr) {
        vsync_->RemoveEventListener("click", &vsyncClicks_);
    }
    if (scaleDown_ != nullptr) {
        scaleDown_->RemoveEventListener("click", &scaleDownClicks_);
    }
    if (scaleUp_ != nullptr) {
        scaleUp_->RemoveEventListener("click", &scaleUpClicks_);
    }
    if (palette_ != nullptr) {
        palette_->RemoveEventListener("click", &paletteClicks_);
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
        loadClick_.requested = false;
        quitClick_.requested = false;
        controlsClick_.requested = false;
        fovDownClicks_.clicks = 0;
        fovUpClicks_.clicks = 0;
        sensDownClicks_.clicks = 0;
        sensUpClicks_.clicks = 0;
        modeClicks_.clicks = 0;
        resDownClicks_.clicks = 0;
        resUpClicks_.clicks = 0;
        vsyncClicks_.clicks = 0;
        scaleDownClicks_.clicks = 0;
        scaleUpClicks_.clicks = 0;
        paletteClicks_.clicks = 0;
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

bool PauseMenu::takeLoad()
{
    const bool requested = loadClick_.requested;
    loadClick_.requested = false;
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

void PauseMenu::setFullscreen(bool fullscreen)
{
    if (mode_ != nullptr) {
        mode_->SetInnerRML(fullscreen ? "Fullscreen" : "Windowed");
    }
}

int PauseMenu::takeDisplayModeClicks()
{
    return std::exchange(modeClicks_.clicks, 0);
}

void PauseMenu::setResolution(int width, int height)
{
    if (resValue_ != nullptr && width > 0 && height > 0) {
        resValue_->SetInnerRML(std::format("{} x {}", width, height));
    }
}

int PauseMenu::takeResolutionSteps()
{
    const int steps = resUpClicks_.clicks - resDownClicks_.clicks;
    resUpClicks_.clicks = 0;
    resDownClicks_.clicks = 0;
    return steps;
}

void PauseMenu::setVsync(bool on)
{
    if (vsync_ != nullptr) {
        vsync_->SetInnerRML(on ? "On" : "Off");
    }
}

int PauseMenu::takeVsyncClicks()
{
    return std::exchange(vsyncClicks_.clicks, 0);
}

void PauseMenu::setUiScale(float scale)
{
    if (scaleValue_ != nullptr && std::isfinite(scale)) {
        scaleValue_->SetInnerRML(std::format("{}%", static_cast<int>(std::lround(scale * 100.0f))));
    }
}

int PauseMenu::takeUiScaleSteps()
{
    const int steps = scaleUpClicks_.clicks - scaleDownClicks_.clicks;
    scaleUpClicks_.clicks = 0;
    scaleDownClicks_.clicks = 0;
    return steps;
}

void PauseMenu::setColourblindSafe(bool on)
{
    if (palette_ != nullptr) {
        palette_->SetInnerRML(on ? "Colourblind" : "Default");
    }
}

int PauseMenu::takePaletteClicks()
{
    return std::exchange(paletteClicks_.clicks, 0);
}

bool PauseMenu::takeControls()
{
    return std::exchange(controlsClick_.requested, false);
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
