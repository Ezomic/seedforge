#include "ichor/ui/loading_screen.h"

#include "ichor/ui/ledger.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <algorithm>
#include <cmath>

namespace ichor {

namespace {

constexpr const char* kLoadingDocument = R"(<rml><head><style>
    body, div { display: block; }
    body { width: 100%; height: 100%; }
    #veil { position: absolute; top: 0px; left: 0px; right: 0px; bottom: 0px; background-color: #05070bf0; }
    #card { position: absolute; top: 40%; left: 50%; margin-left: -280px; width: 500px; padding: 26px 30px 30px 30px; }
    #title { font-family: Cinzel; font-size: 36px; color: #e8d8a8; letter-spacing: 4px; text-align: center; padding-bottom: 8px; }
    #label { font-family: Alegreya Sans; font-size: 22px; color: #a8a090; text-align: center; padding-bottom: 18px; }
    #bar { height: 14px; background-color: #1c222c; border: 2px #3a4252; }
    #fill { width: 0%; height: 14px; background-color: #a08850; }
</style></head><body>
    <div id="veil"></div>
    <div id="card"><div id="title">FORGING THE WORLD</div><div id="label"></div><div id="bar"><div id="fill"></div></div></div>
</body></rml>)";

}

LoadingView buildLoadingView(const ForgeProgressSnapshot& progress)
{
    LoadingView view;
    view.percent = static_cast<int>(std::lround(std::clamp(progress.fraction(), 0.0f, 1.0f) * 100.0f));
    view.label = progress.started ? progress.phase : "Waking the forge";
    return view;
}

LoadingScreen::LoadingScreen(UiContext& ui)
    : ui_(ui)
{
    document_ = ui_.loadDocumentFromMemory(kLoadingDocument, "[loading screen]");
    if (document_ == nullptr) {
        return;
    }
    label_ = document_->GetElementById("label");
    fill_ = document_->GetElementById("fill");
    if (label_ == nullptr || fill_ == nullptr) {
        ui_.unloadDocument(document_);
        document_ = nullptr;
    }
}

LoadingScreen::~LoadingScreen()
{
    if (document_ != nullptr) {
        ui_.unloadDocument(document_);
    }
}

void LoadingScreen::setVisible(bool visible)
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

void LoadingScreen::setView(const LoadingView& view)
{
    if (label_ == nullptr) {
        return;
    }
    label_->SetInnerRML(escapeRml(view.label));
    fill_->SetProperty("width", std::to_string(std::clamp(view.percent, 0, 100)) + "%");
}

std::string LoadingScreen::markup() const
{
    if (label_ == nullptr) {
        return {};
    }
    return label_->GetInnerRML() + " " + fill_->GetProperty("width")->ToString();
}

}
