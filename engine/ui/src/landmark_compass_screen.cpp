#include "ichor/ui/landmark_compass_screen.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

namespace ichor {

namespace {

constexpr const char* kCompassDocument = R"(<rml><head><style>
    body, div { display: block; }
    body { width: 100%; height: 100%; }
    #landmark-compass { position: absolute; bottom: 14px; left: 14px; width: 420px; }
    .line { margin-top: 4px; padding: 4px 8px 4px 8px; font-family: Alegreya Sans; font-size: 17px; color: #e8d8a8; background-color: #10141acc; border: 1px #a08850; }
</style></head><body>
    <div id="landmark-compass"><div id="landmarks"></div></div>
</body></rml>)";

std::string escaped(const std::string& text)
{
    std::string out;
    for (const char c : text) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out += c;
        }
    }
    return out;
}

}

LandmarkCompassScreen::LandmarkCompassScreen(UiContext& ui)
    : ui_(ui)
{
    document_ = ui_.loadDocumentFromMemory(kCompassDocument, "[landmark compass]");
    if (document_ == nullptr) {
        return;
    }
    list_ = document_->GetElementById("landmarks");
    if (list_ == nullptr) {
        ui_.unloadDocument(document_);
        document_ = nullptr;
        return;
    }
    document_->Show();
}

LandmarkCompassScreen::~LandmarkCompassScreen()
{
    if (document_ != nullptr) {
        ui_.unloadDocument(document_);
    }
}

void LandmarkCompassScreen::setReading(const CompassReading& reading)
{
    if (list_ == nullptr) {
        return;
    }
    std::vector<std::string> lines;
    if (reading.status == CompassStatus::Ok) {
        for (const CompassEntry& entry : reading.nearest) {
            lines.push_back(entry.text);
        }
    }
    if (lines == shown_) {
        return;
    }
    shown_ = std::move(lines);
    std::string rml;
    for (const std::string& line : shown_) {
        rml += "<div class=\"line\">" + escaped(line) + "</div>";
    }
    list_->SetInnerRML(rml);
}

void bindLandmarkCompass(LandmarkCompassScreen& screen, AnnotationPoint from, AnnotationPoint facing,
    const std::vector<NavLandmark>& landmarks, const CompassLimits& limits)
{
    screen.setReading(readCompass(from, facing, landmarks, limits));
}

}
