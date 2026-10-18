#include "ichor/ui/character_screen.h"

#include "ichor/ui/ledger.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

namespace ichor {

namespace {

constexpr const char* kCharacterDocument = R"(<rml><head><style>
    body, div { display: block; }
    body { width: 100%; height: 100%; }
    #card { position: absolute; top: 12%; bottom: 12%; left: 12%; right: 12%; overflow: hidden; padding: 22px 28px 22px 28px; background-color: #14161af0; border: 3px #a08850; }
    #close { position: absolute; top: 10px; right: 14px; width: 34px; height: 34px; font-family: Alegreya Sans; font-size: 30px; line-height: 34px; text-align: center; color: #a08850; }
    #close:hover { color: #e8d8a8; background-color: #3a3426; }
    #title { font-family: Cinzel; font-size: 34px; color: #e8d8a8; letter-spacing: 3px; text-align: center; padding-bottom: 12px; }
    .heading { font-family: Cinzel; font-size: 22px; color: #a08850; letter-spacing: 2px; padding: 10px 0px 4px 0px; border-bottom: 1px #3a3426; }
    .line { font-family: Alegreya Sans; font-size: 20px; color: #e0d8c0; padding: 3px 0px 3px 0px; word-break: break-word; }
    .mark { padding-right: 10px; color: #8c8468; }
    .mark.on { color: #b8d890; }
    .value { padding-left: 12px; color: #e8d8a8; }
    .growth { padding-left: 8px; color: #b8d890; font-size: 16px; }
    .detail { font-size: 16px; color: #8c8468; padding-left: 24px; }
    .empty { font-family: Alegreya Sans; font-size: 21px; color: #8c8468; text-align: center; padding: 18px 0px 18px 0px; }
</style></head><body>
    <div id="card"><div id="close">&#215;</div><div id="title">CHARACTER</div><div id="list"></div></div>
</body></rml>)";

std::string lineMarkup(const CharacterScreenSection& section, const CharacterScreenLine& line)
{
    std::string markup = "<div class=\"line\">";
    if (section.kind == CharacterSectionKind::Capabilities || section.kind == CharacterSectionKind::Verbs) {
        markup += line.unlocked ? "<span class=\"mark on\">[x]</span>" : "<span class=\"mark off\">[ ]</span>";
    }
    markup += "<span class=\"label\">" + escapeRml(line.text) + "</span>";
    if (line.value) {
        markup += "<span class=\"value\">" + std::to_string(*line.value) + "</span>";
    }
    if (line.growthPercent && *line.growthPercent > 0) {
        markup += "<span class=\"growth\">+" + std::to_string(*line.growthPercent) + "%</span>";
    }
    for (const std::string& detail : line.details) {
        markup += "<div class=\"detail\">" + escapeRml(detail) + "</div>";
    }
    return markup + "</div>";
}

}

CharacterScreen::CharacterScreen(UiContext& ui)
    : ui_(ui)
{
    document_ = ui_.loadDocumentFromMemory(kCharacterDocument, "[character screen]");
    if (document_ == nullptr) {
        return;
    }
    list_ = document_->GetElementById("list");
    close_ = document_->GetElementById("close");
    if (list_ == nullptr || close_ == nullptr) {
        ui_.unloadDocument(document_);
        document_ = nullptr;
        list_ = nullptr;
        close_ = nullptr;
        return;
    }
    close_->AddEventListener("click", &click_);
}

CharacterScreen::~CharacterScreen()
{
    if (close_ != nullptr) {
        // The element may live on until the context next updates, and the listener goes first.
        close_->RemoveEventListener("click", &click_);
    }
    if (document_ != nullptr) {
        ui_.unloadDocument(document_);
    }
}

bool CharacterScreen::takeCloseRequest()
{
    const bool requested = click_.requested;
    click_.requested = false;
    return requested;
}

std::optional<std::pair<float, float>> CharacterScreen::closeButtonCentre() const
{
    if (close_ == nullptr || !visible_) {
        return std::nullopt;
    }
    const Rml::Vector2f offset = close_->GetAbsoluteOffset(Rml::BoxArea::Border);
    const Rml::Vector2f size = close_->GetBox().GetSize(Rml::BoxArea::Border);
    return std::pair{offset.x + size.x * 0.5f, offset.y + size.y * 0.5f};
}

std::string CharacterScreen::contentRml() const
{
    return list_ == nullptr ? std::string() : std::string(list_->GetInnerRML());
}

void CharacterScreen::setVisible(bool visible)
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

void CharacterScreen::bind(const CharacterScreenInput& input)
{
    if (list_ == nullptr) {
        return;
    }
    std::vector<CharacterScreenSection> sections = buildCharacterScreen(input);
    if (written_ && sections == shown_) {
        return;
    }
    shown_ = std::move(sections);
    written_ = true;
    std::string markup;
    if (shown_.empty()) {
        markup = "<div class=\"empty\">Nothing to show yet.</div>";
    }
    for (const CharacterScreenSection& section : shown_) {
        markup += "<div class=\"section\"><div class=\"heading\">" + escapeRml(section.title) + "</div>";
        for (const CharacterScreenLine& line : section.lines) {
            markup += lineMarkup(section, line);
        }
        if (section.omitted > 0) {
            markup += "<div class=\"detail\">and " + std::to_string(section.omitted) + " more</div>";
        }
        markup += "</div>";
    }
    list_->SetInnerRML(markup);
}

}
