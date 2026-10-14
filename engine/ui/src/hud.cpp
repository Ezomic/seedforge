#include "ichor/ui/hud.h"

#include "ichor/core/wading.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <algorithm>
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
    #guide { font-family: Alegreya Sans; font-size: 16px; color: #e8d8a8; }
    #clock { font-family: Alegreya Sans; font-size: 18px; color: #d8d8d8; }
    #water { font-family: Alegreya Sans; font-size: 16px; color: #8fc0d0; }
    #health { width: 150px; height: 10px; margin: 6px 0px 0px 15px; background-color: #1a0d0d; border: 1px #a08850; }
    #healthfill { width: 0%; height: 10px; background-color: #a83232; }
</style></head><body>
    <div id="plate"><div id="compass">N</div><div id="guide"></div><div id="clock">09:00</div><div id="water"></div><div id="health"><div id="healthfill"></div></div></div>
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

std::string targetGuide(double playerX, double playerZ, double targetX, double targetZ)
{
    const double dx = targetX - playerX;
    const double dz = targetZ - playerZ;
    if (!std::isfinite(playerX) || !std::isfinite(playerZ) || !std::isfinite(targetX) || !std::isfinite(targetZ)) {
        return {};
    }
    const double metres = std::hypot(dx, dz);
    if (!std::isfinite(metres)) {
        return "far away";
    }
    if (metres < 5.0) {
        return "here";
    }
    static constexpr std::array<const char*, 8> kPoints{"north", "north-east", "east", "south-east", "south", "south-west", "west", "north-west"};
    constexpr double kPi = 3.14159265358979323846;
    double degrees = std::atan2(dx, -dz) * 180.0 / kPi;
    if (degrees < 0.0) {
        degrees += 360.0;
    }
    const auto index = static_cast<std::size_t>(std::floor(degrees / 45.0 + 0.5)) % kPoints.size();

    std::string distance;
    char text[32];
    if (metres < 100.0) {
        std::snprintf(text, sizeof(text), "%.0f m", std::round(metres / 10.0) * 10.0);
        distance = text;
    } else if (metres < 1000.0) {
        const double rounded = std::round(metres / 50.0) * 50.0;
        if (rounded < 1000.0) {
            std::snprintf(text, sizeof(text), "%.0f m", rounded);
            distance = text;
        } else {
            distance = "1 km";
        }
    } else {
        const double kilometres = std::round(metres / 500.0) * 0.5;
        if (kilometres >= 100.0) {
            distance = "far away";
        } else if (kilometres == std::floor(kilometres)) {
            std::snprintf(text, sizeof(text), "%.0f km", kilometres);
            distance = text;
        } else {
            std::snprintf(text, sizeof(text), "%.1f km", kilometres);
            distance = text;
        }
    }
    return std::string(kPoints[index]) + ", " + distance;
}

std::string waterLabel(float depth)
{
    if (!(depth > kWadeStart)) {
        return {};
    }
    // Four equal bands from the ankles to chest height, the range over which wading slows the walker.
    const float through = (depth - kWadeStart) / (kWadeFull - kWadeStart);
    if (through < 0.25f) return "Ankle deep";
    if (through < 0.5f) return "Knee deep";
    if (through < 0.75f) return "Waist deep";
    return "Chest deep";
}

int healthPercent(float health, float maxHealth)
{
    if (!std::isfinite(health) || !std::isfinite(maxHealth) || !(maxHealth > 0.0f)) {
        return 0;
    }
    return static_cast<int>(std::floor(std::clamp(health, 0.0f, maxHealth) * 100.0f / maxHealth));
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
    guide_ = document_->GetElementById("guide");
    healthFill_ = document_->GetElementById("healthfill");
    document_->Show();
}

Hud::~Hud()
{
    if (document_ != nullptr) {
        ui_.unloadDocument(document_);
    }
}

void Hud::setGuide(const std::string& text)
{
    if (guide_ == nullptr || text == guideText_) {
        return;
    }
    guideText_ = text;
    guide_->SetInnerRML(text);
}

void Hud::update(float headingRadians, float hour, float wadingDepth, float health, float maxHealth)
{
    if (compass_ == nullptr || clock_ == nullptr || water_ == nullptr || healthFill_ == nullptr) {
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
    const int percent = healthPercent(health, maxHealth);
    if (percent != healthPercent_) {
        healthPercent_ = percent;
        healthFill_->SetProperty("width", std::to_string(percent) + "%");
    }
}

}
