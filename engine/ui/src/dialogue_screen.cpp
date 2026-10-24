#include "ichor/ui/dialogue_screen.h"

#include "ichor/ui/ledger.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

#include <algorithm>

namespace ichor {

namespace {

constexpr const char* kDialogueDocument = R"(<rml><head><style>
    body, div { display: block; }
    body { width: 100%; height: 100%; }
    #card { position: absolute; bottom: 4%; left: 12%; right: 12%; overflow: hidden; padding: 18px 28px 18px 28px; background-color: #14161af0; border: 3px #a08850; }
    #close { position: absolute; top: 8px; right: 12px; width: 32px; height: 32px; font-family: Alegreya Sans; font-size: 28px; line-height: 32px; text-align: center; color: #a08850; }
    #close:hover { color: #e8d8a8; background-color: #3a3426; }
    #who { font-family: Cinzel; font-size: 30px; color: #e8d8a8; letter-spacing: 3px; padding-bottom: 4px; }
    #role { font-family: Alegreya Sans; font-size: 18px; color: #8c8468; letter-spacing: 2px; padding-left: 14px; }
    #purse { font-family: Alegreya Sans; font-size: 20px; color: #c8b868; padding-left: 24px; }
    #says { font-family: Alegreya Sans; font-size: 23px; color: #e0d8c0; padding: 8px 0px 10px 0px; border-bottom: 1px #3a3426; }
    #notice { font-family: Alegreya Sans; font-size: 19px; color: #b8d890; height: 24px; padding-top: 6px; }
    .head { font-family: Cinzel; font-size: 16px; color: #a08850; letter-spacing: 2px; padding: 8px 0px 2px 0px; }
    .row { font-family: Alegreya Sans; font-size: 19px; color: #e0d8c0; padding: 2px 0px 2px 0px; border-bottom: 1px #25272b; height: 28px; }
    .cell { display: inline-block; vertical-align: middle; }
    .tname { width: 38%; } .tdetail { width: 20%; color: #8c8468; } .tprice { width: 16%; text-align: right; color: #c8b868; }
    .act { display: inline-block; vertical-align: middle; width: 10%; margin-left: 2%; text-align: center; color: #a08850; border: 1px #3a3426; font-size: 17px; }
    .act:hover { color: #e8d8a8; background-color: #3a3426; }
    .act.off { color: #5a5646; border: 1px #25272b; }
    .act.off:hover { color: #5a5646; background-color: #14161a00; }
    .empty { font-family: Alegreya Sans; font-size: 19px; color: #8c8468; padding: 6px 0px 6px 0px; }
    .choice { font-family: Alegreya Sans; font-size: 21px; color: #a08850; padding: 5px 10px 5px 10px; margin-top: 6px; border: 1px #3a3426; }
    .choice:hover { color: #e8d8a8; background-color: #3a3426; }
</style></head><body>
    <div id="card"><div id="close">&#215;</div><div id="content"></div></div>
</body></rml>)";

std::string clip(const std::string& text, std::size_t limit)
{
    std::string out;
    for (const char c : text) {
        if (static_cast<unsigned char>(c) >= 0x20 && c != 0x7f) out.push_back(c);
    }
    if (out.size() > limit) {
        std::size_t cut = limit;
        while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) --cut;
        out.resize(cut);
    }
    return out;
}

std::string rowsMarkup(const std::vector<ParleyTradeRow>& rows, const char* heading, const char* none, const char* verb)
{
    std::string markup = std::string("<div class=\"head\">") + heading + "</div>";
    if (rows.empty()) {
        return markup + "<div class=\"empty\">" + none + "</div>";
    }
    const std::size_t shown = std::min(rows.size(), kDialogueMaxRows);
    for (std::size_t i = 0; i < shown; ++i) {
        const ParleyTradeRow& row = rows[i];
        markup += "<div class=\"row\"><span class=\"cell tname\">" + escapeRml(clip(row.name, 60)) + "</span><span class=\"cell tdetail\">" + escapeRml(clip(row.detail, 30)) +
                  "</span><span class=\"cell tprice\">" + escapeRml(clip(row.price, 30)) + "</span>";
        markup += row.enabled ? "<div class=\"act\" data-key=\"" + escapeRml(row.key) + "\">" + verb + "</div>" : std::string("<div class=\"act off\">") + verb + "</div>";
        markup += "</div>";
    }
    if (rows.size() > shown) {
        markup += "<div class=\"empty\">and " + std::to_string(rows.size() - shown) + " more</div>";
    }
    return markup;
}

}

void DialogueScreen::Clicks::ProcessEvent(Rml::Event& event)
{
    for (Rml::Element* element = event.GetTargetElement(); element != nullptr; element = element->GetParentNode()) {
        const Rml::String key = element->GetAttribute<Rml::String>("data-key", "");
        if (!key.empty()) {
            keys.push_back(key);
            return;
        }
    }
}

DialogueScreen::DialogueScreen(UiContext& ui)
    : ui_(ui)
{
    document_ = ui_.loadDocumentFromMemory(kDialogueDocument, "[dialogue screen]");
    if (document_ == nullptr) {
        return;
    }
    content_ = document_->GetElementById("content");
    closeButton_ = document_->GetElementById("close");
    if (content_ == nullptr || closeButton_ == nullptr) {
        ui_.unloadDocument(document_);
        document_ = nullptr;
        content_ = nullptr;
        closeButton_ = nullptr;
        return;
    }
    content_->AddEventListener("click", &clicks_);
    closeButton_->SetAttribute("data-key", "bye");
    closeButton_->AddEventListener("click", &clicks_);
}

DialogueScreen::~DialogueScreen()
{
    if (content_ != nullptr) {
        content_->RemoveEventListener("click", &clicks_);
    }
    if (closeButton_ != nullptr) {
        closeButton_->RemoveEventListener("click", &clicks_);
    }
    if (document_ != nullptr) {
        ui_.unloadDocument(document_);
    }
}

std::vector<std::string> DialogueScreen::takeChoices()
{
    std::vector<std::string> taken;
    taken.swap(clicks_.keys);
    return taken;
}

std::optional<std::pair<float, float>> DialogueScreen::buttonCentre(const std::string& key) const
{
    if (document_ == nullptr || !visible_) {
        return std::nullopt;
    }
    Rml::ElementList found;
    document_->GetElementsByTagName(found, "div");
    for (Rml::Element* element : found) {
        if (element->GetAttribute<Rml::String>("data-key", "") == key) {
            const Rml::Vector2f offset = element->GetAbsoluteOffset(Rml::BoxArea::Border);
            const Rml::Vector2f size = element->GetBox().GetSize(Rml::BoxArea::Border);
            return std::pair{offset.x + size.x * 0.5f, offset.y + size.y * 0.5f};
        }
    }
    return std::nullopt;
}

std::string DialogueScreen::contentRml() const
{
    return content_ == nullptr ? std::string() : std::string(content_->GetInnerRML());
}

void DialogueScreen::setVisible(bool visible)
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

void DialogueScreen::bind(const ParleyView& view)
{
    if (content_ == nullptr || (written_ && view == shown_)) {
        return;
    }
    shown_ = view;
    written_ = true;
    std::string markup = "<div id=\"who\">" + escapeRml(clip(view.speaker, 60)) + "<span id=\"role\">" + escapeRml(clip(view.role, 40)) + "</span><span id=\"purse\">" +
                         escapeRml(clip(view.purse, 30)) + "</span></div>";
    markup += "<div id=\"says\">" + escapeRml(clip(view.text, kDialogueMaxTextBytes)) + "</div>";
    markup += "<div id=\"notice\">" + escapeRml(clip(view.notice, 120)) + "</div>";
    if (view.page == ParleyView::Page::Trade) {
        markup += rowsMarkup(view.buying, "FOR SALE", "Nothing is left to sell.", "Buy");
        markup += rowsMarkup(view.selling, "YOU CARRY", "You carry nothing the merchant will buy.", "Sell");
    }
    for (const ParleyChoice& choice : view.choices) {
        markup += "<div class=\"choice\" data-key=\"" + escapeRml(choice.key) + "\">" + escapeRml(clip(choice.label, 100)) + "</div>";
    }
    content_->SetInnerRML(markup);
}

}
