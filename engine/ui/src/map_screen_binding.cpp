#include "ichor/ui/map_screen_binding.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/StringUtilities.h>

#include <cmath>
#include <format>

namespace ichor {

namespace {

constexpr const char* kBindingDocument = R"RML(<rml>
<head>
<style>
    body, div { display: block; }
    body { width: 100%; height: 100%; }
    #panel { position: absolute; top: 8%; left: 1%; width: 24%; padding: 14px 16px 14px 16px; background-color: #10141af0; border: 2px #a08850; font-family: Alegreya Sans; font-size: 18px; color: #e8d8a8; }
    .head { font-family: Cinzel; font-size: 20px; color: #a08850; letter-spacing: 2px; padding: 8px 0px 4px 0px; }
    .row { padding: 2px 0px 2px 0px; }
    .kind { color: #a08850; }
    #hidden { color: #8890a0; padding-top: 8px; }
    #view { position: absolute; left: 0px; top: 0px; width: 0px; height: 0px; }
    .dot { position: absolute; width: 8px; height: 8px; border-radius: 6px; background-color: #60c0f0; border: 2px #10141a; }
    .dot.waypoint { background-color: #f06060; }
</style>
</head>
<body>
    <div id="panel">
        <div class="head">YOU</div>
        <div id="player" class="row"></div>
        <div class="head">WAYPOINT</div>
        <div id="waypoint" class="row"></div>
        <div class="head">MARKS</div>
        <div id="list"></div>
        <div id="hidden"></div>
    </div>
    <div id="view"></div>
</body>
</rml>)RML";

const char* kindName(AnnotationKind kind)
{
    switch (kind) {
    case AnnotationKind::Mark: return "Mark";
    case AnnotationKind::Note: return "Note";
    case AnnotationKind::Path: return "Path";
    }
    return "Mark";
}

const char* symbolName(AnnotationSymbol symbol)
{
    switch (symbol) {
    case AnnotationSymbol::Camp: return "camp";
    case AnnotationSymbol::Water: return "water";
    case AnnotationSymbol::Danger: return "danger";
    case AnnotationSymbol::Loot: return "loot";
    case AnnotationSymbol::Door: return "door";
    case AnnotationSymbol::Direction: return "direction";
    case AnnotationSymbol::Other: return "other";
    }
    return "other";
}

std::string escaped(const std::string& text)
{
    return Rml::StringUtilities::EncodeRml(text);
}

std::string dotMarkup(const MapScreenPoint& at, const char* extraClass)
{
    if (!std::isfinite(at.x) || !std::isfinite(at.y)) {
        return {};
    }
    return std::format("<div class=\"dot {}\" style=\"left: {:.1f}px; top: {:.1f}px;\"></div>", extraClass, at.x - 6.0f, at.y - 6.0f);
}

}

MapScreenBinding::MapScreenBinding(UiContext& ui)
    : ui_(ui)
{
    document_ = ui_.loadDocumentFromMemory(kBindingDocument, "[map binding]");
    if (document_ == nullptr) {
        return;
    }
    player_ = document_->GetElementById("player");
    waypoint_ = document_->GetElementById("waypoint");
    list_ = document_->GetElementById("list");
    hidden_ = document_->GetElementById("hidden");
    view_ = document_->GetElementById("view");
    if (player_ == nullptr || waypoint_ == nullptr || list_ == nullptr || hidden_ == nullptr || view_ == nullptr) {
        ui_.unloadDocument(document_);
        document_ = nullptr;
        return;
    }
    bind(MapScreenView{});
}

MapScreenBinding::~MapScreenBinding()
{
    if (document_ != nullptr) {
        ui_.unloadDocument(document_);
    }
}

void MapScreenBinding::setVisible(bool visible)
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

void MapScreenBinding::bind(const MapScreenView& view)
{
    if (document_ == nullptr) {
        return;
    }
    std::string dots;
    std::string rows;
    std::string hidden;
    std::string playerText = "Position unknown.";
    std::string waypointText = "None.";
    if (view.valid) {
        for (const MapScreenAnnotation& annotation : view.annotations) {
            rows += std::format("<div class=\"row\"><span class=\"kind\">{} ({})</span> {}</div>", kindName(annotation.kind), symbolName(annotation.symbol), escaped(annotation.text));
            for (const MapScreenPoint& point : annotation.points) {
                dots += dotMarkup(point, "mark");
            }
        }
        if (view.hiddenAnnotations > 0) {
            hidden = std::format("{} more out of view.", view.hiddenAnnotations);
        }
        if (view.player) {
            const double degrees = std::fmod(std::fmod(static_cast<double>(view.player->heading) * 180.0 / 3.14159265358979323846, 360.0) + 360.0, 360.0);
            playerText = std::format("Facing {:.0f} degrees{}.", degrees, view.player->onScreen ? "" : ", off the view");
        }
        if (view.waypoint) {
            waypointText = std::format("{:.0f} away, {:.0f} degrees off your heading.", view.waypoint->distance, view.waypoint->relativeBearing * 180.0f / 3.14159265f);
            dots += dotMarkup(view.waypoint->onScreen ? view.waypoint->position : view.waypoint->edge, "waypoint");
        }
    }
    player_->SetInnerRML(playerText);
    waypoint_->SetInnerRML(waypointText);
    list_->SetInnerRML(rows);
    hidden_->SetInnerRML(hidden);
    view_->SetInnerRML(dots);
}

std::string MapScreenBinding::textOf(const char* elementId) const
{
    const Rml::Element* element = document_ == nullptr ? nullptr : document_->GetElementById(elementId);
    return element == nullptr ? std::string() : element->GetInnerRML();
}

}
