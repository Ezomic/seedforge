#include "ichor/ui/hud.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <array>
#include <cmath>
#include <cstdio>

namespace ichor {

namespace {

constexpr float kTwoPi = 6.28318530718f;

constexpr const char* kHudDocument = R"(<rml><head><style>
    body, div { display: block; }
    body { width: 100%; height: 100%; }
    #plate { position: absolute; top: 14px; left: 50%; margin-left: -90px; width: 180px; padding: 6px 0px 8px 0px; text-align: center; background-color: #10141acc; border: 2px #a08850; }
    #compass { font-family: Cinzel; font-size: 30px; color: #e8d8a8; letter-spacing: 3px; }
    #clock { font-family: Alegreya Sans; font-size: 18px; color: #d8d8d8; }
    #water { font-family: Alegreya Sans; font-size: 16px; color: #8fc0d0; }
</style></head><body>
    <div id="plate"><div id="compass">N</div><div id="clock">09:00</div><div id="water"></div></div>
</body></rml>)";

}

float headingOf(float forwardX, float forwardZ)
{
    return std::atan2(forwardX, -forwardZ);
}

std::string compassLabel(float headingRadians)
{
    if (!std::isfinite(headingRadians)) {
        return "N";
    }
    static constexpr std::array<const char*, 8> kPoints{"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    float turns = std::fmod(headingRadians, kTwoPi);
    if (turns < 0.0f) {
        turns += kTwoPi;
    }
    const auto index = static_cast<std::size_t>(std::floor(turns / kTwoPi * 8.0f + 0.5f)) % kPoints.size();
    return kPoints[index];
}

std::string waterLabel(float depth)
{
    if (!(depth > 0.1f)) {
        return {};
    }
    if (depth < 0.4f) return "Ankle deep";
    if (depth < 0.8f) return "Knee deep";
    if (depth < 1.2f) return "Waist deep";
    return "Chest deep";
}

std::string clockLabel(float hour)
{
    if (!std::isfinite(hour)) {
        hour = 0.0f;
    }
    float wrapped = std::fmod(hour, 24.0f);
    if (wrapped < 0.0f) {
        wrapped += 24.0f;
    }
    const int minutes = static_cast<int>(wrapped * 60.0f) % (24 * 60);
    char text[8];
    std::snprintf(text, sizeof(text), "%02d:%02d", minutes / 60, minutes % 60);
    return text;
}

Hud::Hud(UiContext& ui)
    : ui_(ui)
{
    document_ = ui_.loadDocumentFromMemory(kHudDocument, "[hud]");
    if (document_ == nullptr) {
        return;
    }
    compass_ = document_->GetElementById("compass");
    clock_ = document_->GetElementById("clock");
    water_ = document_->GetElementById("water");
    document_->Show();
}

Hud::~Hud()
{
    if (document_ != nullptr) {
        ui_.unloadDocument(document_);
    }
}

void Hud::update(float headingRadians, float hour, float wadingDepth)
{
    if (compass_ == nullptr || clock_ == nullptr || water_ == nullptr) {
        return;
    }
    const std::string compass = compassLabel(headingRadians);
    if (compass != compassText_) {
        compassText_ = compass;
        compass_->SetInnerRML(compass);
    }
    const std::string clock = clockLabel(hour);
    if (clock != clockText_) {
        clockText_ = clock;
        clock_->SetInnerRML(clock);
    }
    const std::string water = waterLabel(wadingDepth);
    if (water != waterText_) {
        waterText_ = water;
        water_->SetInnerRML(water);
    }
}

}
