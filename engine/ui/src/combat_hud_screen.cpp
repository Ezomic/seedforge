#include "ichor/ui/combat_hud_screen.h"

#include "ichor/ui/ledger.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ichor {

namespace {

constexpr const char* kCombatHudDocument = R"(<rml><head><style>
    body, div { display: block; }
    body { width: 100%; height: 100%; }
    .bar { position: absolute; left: 24px; width: 260px; height: 14px; background-color: #1a0d0dcc; border: 1px #a08850; }
    .fill { width: 0%; height: 14px; }
    #health { bottom: 56px; }
    #healthfill { background-color: #a83232; }
    #health.low #healthfill { background-color: #e04040; }
    #stamina { bottom: 36px; height: 10px; }
    #stamina .fill { height: 10px; background-color: #5a9a48; }
    #stamina.low .fill { background-color: #b0a040; }
    #law { position: absolute; left: 24px; bottom: 74px; width: 260px; font-family: Alegreya Sans; font-size: 18px; color: #e8d8a8; }
    #lawbar { width: 260px; height: 6px; background-color: #10141acc; border: 1px #a08850; }
    #lawfill { width: 0%; height: 6px; background-color: #d8b048; }
    #ring { position: absolute; left: 50%; top: 50%; width: 0px; height: 0px; }
    .hit { position: absolute; width: 16px; height: 16px; margin-left: -8px; margin-top: -8px; }
    #reticle { position: absolute; left: 50%; top: 50%; margin-left: 18px; margin-top: -20px; width: 6px; height: 40px; background-color: #10141acc; border: 1px #a08850; }
    #staggerfill { position: absolute; left: 0px; bottom: 0px; width: 6px; height: 0%; background-color: #e8d8a8; }
</style></head><body>
    <div id="health" class="bar"><div id="healthfill" class="fill"></div></div>
    <div id="stamina" class="bar"><div id="staminafill" class="fill"></div></div>
    <div id="law"><div id="lawtext"></div><div id="lawbar"><div id="lawfill"></div></div></div>
    <div id="ring"></div>
    <div id="reticle"><div id="staggerfill"></div></div>
</body></rml>)";

constexpr float kRingRadius = 120.0f;

void setPercent(Rml::Element* element, float fraction)
{
    const float clamped = std::isfinite(fraction) ? std::clamp(fraction, 0.0f, 1.0f) : 0.0f;
    char text[16];
    std::snprintf(text, sizeof(text), "%.1f%%", static_cast<double>(clamped * 100.0f));
    element->SetProperty("width", text);
}

void setHeightPercent(Rml::Element* element, float fraction)
{
    const float clamped = std::isfinite(fraction) ? std::clamp(fraction, 0.0f, 1.0f) : 0.0f;
    char text[16];
    std::snprintf(text, sizeof(text), "%.1f%%", static_cast<double>(clamped * 100.0f));
    element->SetProperty("height", text);
}

std::string hitMarker(const CombatHudIndicator& indicator)
{
    const float angle = std::isfinite(indicator.angle) ? indicator.angle : 0.0f;
    const float opacity = std::isfinite(indicator.opacity) ? std::clamp(indicator.opacity, 0.0f, 1.0f) : 0.0f;
    char text[160];
    std::snprintf(text, sizeof(text), "<div class=\"hit\" style=\"left: %.0fpx; top: %.0fpx; background-color: rgba(224,48,48,%d);\"></div>",
        static_cast<double>(std::sin(angle) * kRingRadius), static_cast<double>(-std::cos(angle) * kRingRadius), static_cast<int>(opacity * 255.0f));
    return text;
}

}

CombatHudScreen::CombatHudScreen(UiContext& ui)
    : ui_(ui)
{
    document_ = ui_.loadDocumentFromMemory(kCombatHudDocument, "[combat hud]");
    if (document_ == nullptr) {
        return;
    }
    health_ = document_->GetElementById("health");
    healthFill_ = document_->GetElementById("healthfill");
    stamina_ = document_->GetElementById("stamina");
    staminaFill_ = document_->GetElementById("staminafill");
    law_ = document_->GetElementById("law");
    lawText_ = document_->GetElementById("lawtext");
    lawFill_ = document_->GetElementById("lawfill");
    ring_ = document_->GetElementById("ring");
    reticle_ = document_->GetElementById("reticle");
    staggerFill_ = document_->GetElementById("staggerfill");
    if (health_ == nullptr || healthFill_ == nullptr || stamina_ == nullptr || staminaFill_ == nullptr || law_ == nullptr || lawText_ == nullptr
        || lawFill_ == nullptr || ring_ == nullptr || reticle_ == nullptr || staggerFill_ == nullptr) {
        ui_.unloadDocument(document_);
        document_ = nullptr;
        return;
    }
    law_->SetProperty("display", "none");
    reticle_->SetProperty("display", "none");
    document_->Show();
}

CombatHudScreen::~CombatHudScreen()
{
    if (document_ != nullptr) {
        ui_.unloadDocument(document_);
    }
}

void CombatHudScreen::bind(const CombatHudView& view)
{
    if (document_ == nullptr) {
        return;
    }
    setPercent(healthFill_, view.health.fraction);
    health_->SetClass("low", view.health.low);
    setPercent(staminaFill_, view.stamina.fraction);
    stamina_->SetClass("low", view.stamina.low);

    if (view.lawResource) {
        law_->SetProperty("display", "block");
        char value[32];
        std::snprintf(value, sizeof(value), " %.0f", static_cast<double>(view.lawResource->value));
        lawText_->SetInnerRML(escapeRml(view.lawResource->id) + value);
        setPercent(lawFill_, view.lawResource->fraction);
    } else {
        law_->SetProperty("display", "none");
    }

    std::string hits;
    for (const CombatHudIndicator& indicator : view.indicators) {
        hits += hitMarker(indicator);
    }
    ring_->SetInnerRML(hits);

    if (view.targetStagger) {
        reticle_->SetProperty("display", "block");
        setHeightPercent(staggerFill_, *view.targetStagger);
    } else {
        reticle_->SetProperty("display", "none");
    }
}

std::string CombatHudScreen::markup() const
{
    if (ring_ == nullptr) {
        return {};
    }
    return lawText_->GetInnerRML() + ring_->GetInnerRML();
}

}
