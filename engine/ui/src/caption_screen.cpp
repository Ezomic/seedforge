#include "ichor/ui/caption_screen.h"

#include "ichor/ui/ledger.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

namespace ichor {

namespace {

constexpr const char* kCaptionDocument = R"(<rml><head><style>
    body, div { display: block; }
    body { width: 100%; height: 100%; }
    #captions { position: absolute; left: 0px; right: 0px; bottom: 96px; text-align: center; font-family: Alegreya Sans; font-size: 20px; color: #f0e6c8; }
    .caption { margin: 2px auto; padding: 2px 10px; width: 320px; background-color: #10141acc; border: 1px #a08850; }
</style></head><body>
    <div id="captions"></div>
</body></rml>)";

}

CaptionScreen::CaptionScreen(UiContext& ui)
    : ui_(ui)
{
    document_ = ui_.loadDocumentFromMemory(kCaptionDocument, "[captions]");
    if (document_ == nullptr) {
        return;
    }
    strip_ = document_->GetElementById("captions");
    if (strip_ == nullptr) {
        ui_.unloadDocument(document_);
        document_ = nullptr;
        return;
    }
    document_->Show();
}

CaptionScreen::~CaptionScreen()
{
    if (document_ != nullptr) {
        ui_.unloadDocument(document_);
    }
}

void CaptionScreen::bind(const std::vector<std::string>& lines)
{
    if (strip_ == nullptr || lines == shown_) {
        return;
    }
    shown_ = lines;
    std::string rml;
    for (const std::string& line : lines) {
        rml += "<div class=\"caption\">" + escapeRml(line) + "</div>";
    }
    strip_->SetInnerRML(rml);
}

std::string CaptionScreen::markup() const
{
    return strip_ != nullptr ? strip_->GetInnerRML() : std::string();
}

}
