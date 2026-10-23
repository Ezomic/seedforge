#include "ichor/ui/inventory_screen.h"

#include "ichor/ui/ledger.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

namespace ichor {

namespace {

constexpr const char* kInventoryDocument = R"(<rml><head><style>
    body, div { display: block; }
    body { width: 100%; height: 100%; }
    #card { position: absolute; top: 6%; bottom: 6%; left: 14%; right: 14%; overflow: hidden; padding: 22px 28px 22px 28px; background-color: #14161af0; border: 3px #a08850; }
    #close { position: absolute; top: 10px; right: 14px; width: 34px; height: 34px; font-family: Alegreya Sans; font-size: 30px; line-height: 34px; text-align: center; color: #a08850; }
    #close:hover { color: #e8d8a8; background-color: #3a3426; }
    #title { font-family: Cinzel; font-size: 34px; color: #e8d8a8; letter-spacing: 3px; text-align: center; padding-bottom: 8px; }
    #head { padding-bottom: 10px; border-bottom: 1px #3a3426; }
    #loadline { font-family: Alegreya Sans; font-size: 22px; color: #e0d8c0; padding-bottom: 6px; }
    #level { padding-left: 14px; font-family: Cinzel; letter-spacing: 2px; }
    #level.Light { color: #b8d890; } #level.Loaded { color: #e8d8a8; } #level.Heavy { color: #e0a060; } #level.Over { color: #f06050; }
    #track { height: 12px; background-color: #2a2c30; border: 1px #3a3426; }
    #fill { height: 12px; }
    #fill.Light { background-color: #7ca860; } #fill.Loaded { background-color: #c8b868; } #fill.Heavy { background-color: #d08840; } #fill.Over { background-color: #e04838; }
    #notice { font-family: Alegreya Sans; font-size: 19px; color: #e8d8a8; padding-top: 6px; height: 24px; }
    .row { font-family: Alegreya Sans; font-size: 18px; color: #e0d8c0; padding: 2px 0px 2px 0px; border-bottom: 1px #25272b; height: 26px; }
    .row.header { color: #a08850; font-family: Cinzel; font-size: 16px; letter-spacing: 2px; }
    .cell { display: inline-block; vertical-align: middle; }
    .name { width: 34%; } .cls { width: 13%; color: #8c8468; } .qty { width: 7%; } .unit { width: 9%; text-align: right; } .wt { width: 11%; text-align: right; }
    .act { display: inline-block; vertical-align: middle; width: 8%; margin-left: 1%; text-align: center; color: #a08850; border: 1px #3a3426; font-size: 16px; }
    .act:hover { color: #e8d8a8; background-color: #3a3426; }
    .act.none { border: 1px #14161a00; }
    .empty { font-family: Alegreya Sans; font-size: 21px; color: #8c8468; text-align: center; padding: 18px 0px 18px 0px; }
    .more { font-size: 16px; color: #8c8468; padding-top: 6px; }
</style></head><body>
    <div id="card"><div id="close">&#215;</div><div id="title">INVENTORY</div><div id="head"></div><div id="list"></div></div>
</body></rml>)";

std::string headMarkup(const InventoryScreenView& view)
{
    const std::string level = escapeRml(view.levelName);
    return "<div id=\"loadline\">Carried " + escapeRml(view.load) + "<span id=\"level\" class=\"" + level + "\">" + level + "</span></div>" +
           "<div id=\"track\"><div id=\"fill\" class=\"" + level + "\" style=\"width: " + std::to_string(view.fillPermille / 10) + "%;\"></div></div>" +
           "<div id=\"notice\">" + escapeRml(view.notice) + "</div>";
}

}

void InventoryScreen::Rows::ProcessEvent(Rml::Event& event)
{
    Rml::Element* target = event.GetTargetElement();
    if (target == nullptr || shown == nullptr) {
        return;
    }
    const Rml::String act = target->GetAttribute<Rml::String>("data-act", "");
    const int row = target->GetAttribute<int>("data-row", -1);
    if ((act != "use" && act != "drop") || row < 0 || static_cast<std::size_t>(row) >= shown->size()) {
        return;
    }
    clicks.push_back({act == "use" ? InventoryAction::Kind::Use : InventoryAction::Kind::Drop, (*shown)[static_cast<std::size_t>(row)].id});
}

InventoryScreen::InventoryScreen(UiContext& ui)
    : ui_(ui)
{
    document_ = ui_.loadDocumentFromMemory(kInventoryDocument, "[inventory screen]");
    if (document_ == nullptr) {
        return;
    }
    list_ = document_->GetElementById("list");
    head_ = document_->GetElementById("head");
    closeButton_ = document_->GetElementById("close");
    if (list_ == nullptr || head_ == nullptr || closeButton_ == nullptr) {
        ui_.unloadDocument(document_);
        document_ = nullptr;
        list_ = nullptr;
        head_ = nullptr;
        closeButton_ = nullptr;
        return;
    }
    closeButton_->AddEventListener("click", &close_);
    list_->AddEventListener("click", &rows_);
    rows_.shown = &shown_.rows;
}

InventoryScreen::~InventoryScreen()
{
    if (closeButton_ != nullptr) {
        closeButton_->RemoveEventListener("click", &close_);
    }
    if (list_ != nullptr) {
        list_->RemoveEventListener("click", &rows_);
    }
    if (document_ != nullptr) {
        ui_.unloadDocument(document_);
    }
}

bool InventoryScreen::takeCloseRequest()
{
    const bool requested = close_.requested;
    close_.requested = false;
    return requested;
}

std::vector<InventoryAction> InventoryScreen::takeActions()
{
    std::vector<InventoryAction> taken;
    taken.swap(rows_.clicks);
    return taken;
}

std::optional<std::pair<float, float>> InventoryScreen::closeButtonCentre() const
{
    if (closeButton_ == nullptr || !visible_) {
        return std::nullopt;
    }
    const Rml::Vector2f offset = closeButton_->GetAbsoluteOffset(Rml::BoxArea::Border);
    const Rml::Vector2f size = closeButton_->GetBox().GetSize(Rml::BoxArea::Border);
    return std::pair{offset.x + size.x * 0.5f, offset.y + size.y * 0.5f};
}

std::optional<std::pair<float, float>> InventoryScreen::actionButtonCentre(std::size_t row, InventoryAction::Kind kind) const
{
    if (list_ == nullptr || !visible_) {
        return std::nullopt;
    }
    const std::string wanted = kind == InventoryAction::Kind::Use ? "use" : "drop";
    for (int i = 0; i < list_->GetNumChildren(); ++i) {
        Rml::Element* line = list_->GetChild(i);
        for (int j = 0; j < line->GetNumChildren(); ++j) {
            Rml::Element* button = line->GetChild(j);
            if (button->GetAttribute<Rml::String>("data-act", "") == wanted && button->GetAttribute<int>("data-row", -1) == static_cast<int>(row)) {
                const Rml::Vector2f offset = button->GetAbsoluteOffset(Rml::BoxArea::Border);
                const Rml::Vector2f size = button->GetBox().GetSize(Rml::BoxArea::Border);
                return std::pair{offset.x + size.x * 0.5f, offset.y + size.y * 0.5f};
            }
        }
    }
    return std::nullopt;
}

std::string InventoryScreen::contentRml() const
{
    return list_ == nullptr ? std::string() : std::string(list_->GetInnerRML());
}

void InventoryScreen::setVisible(bool visible)
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

void InventoryScreen::bind(const InventoryScreenView& view)
{
    if (list_ == nullptr) {
        return;
    }
    if (written_ && view == shown_) {
        return;
    }
    shown_ = view;
    written_ = true;
    head_->SetInnerRML(headMarkup(shown_));
    std::string markup;
    if (shown_.rows.empty()) {
        markup = "<div class=\"empty\">You carry nothing.</div>";
    } else {
        markup = "<div class=\"row header\"><span class=\"cell name\">ITEM</span><span class=\"cell cls\">KIND</span><span class=\"cell qty\">HELD</span>"
                 "<span class=\"cell unit\">EACH</span><span class=\"cell wt\">WEIGHT</span></div>";
    }
    for (std::size_t i = 0; i < shown_.rows.size(); ++i) {
        const InventoryScreenRow& row = shown_.rows[i];
        const std::string index = std::to_string(i);
        markup += "<div class=\"row\"><span class=\"cell name\">" + escapeRml(row.name) + "</span><span class=\"cell cls\">" + escapeRml(row.itemClass) + "</span><span class=\"cell qty\">" +
                  escapeRml(row.count) + "</span><span class=\"cell unit\">" + escapeRml(row.unitWeight) + "</span><span class=\"cell wt\">" + escapeRml(row.weight) + "</span>";
        markup += row.usable ? "<div class=\"act\" data-act=\"use\" data-row=\"" + index + "\">Use</div>" : "<div class=\"act none\"></div>";
        markup += "<div class=\"act\" data-act=\"drop\" data-row=\"" + index + "\">Drop</div></div>";
    }
    if (shown_.omitted > 0) {
        markup += "<div class=\"more\">and " + std::to_string(shown_.omitted) + " more</div>";
    }
    list_->SetInnerRML(markup);
}

}
