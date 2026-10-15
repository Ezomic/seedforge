#include "ichor/ui/new_game_screen.h"

#include "ichor/ui/ledger.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

#include <cctype>
#include <string>

namespace ichor {

namespace {

constexpr const char* kNewGameDocument = R"(<rml><head><style>
    body, div { display: block; }
    body { width: 100%; height: 100%; }
    #veil { position: absolute; top: 0px; left: 0px; right: 0px; bottom: 0px; background-color: #05070bb0; }
    #card { position: absolute; top: 18%; left: 50%; margin-left: -283px; width: 500px; padding: 26px 30px 30px 30px; background-color: #10141af4; border: 3px #a08850; }
    #title { font-family: Cinzel; font-size: 36px; color: #e8d8a8; letter-spacing: 4px; text-align: center; padding-bottom: 8px; }
    #seed { font-family: Alegreya Sans; font-size: 22px; color: #a8a090; text-align: center; padding-bottom: 18px; }
    .row { padding: 10px 14px 12px 14px; margin-top: 10px; background-color: #1c222c; border: 2px #3a4252; }
    .slot { font-family: Alegreya Sans; font-size: 20px; color: #a8a090; letter-spacing: 1px; }
    .axiom { font-family: Cinzel; font-size: 26px; color: #f4e8c0; padding-top: 2px; }
    .unwritten { color: #8890a0; }
    #begin { font-family: Alegreya Sans; font-size: 26px; text-align: center; color: #d8d8d8; padding: 10px 0px 12px 0px; margin-top: 22px; background-color: #1c222c; border: 2px #3a4252; }
    #begin:hover { color: #f4e8c0; background-color: #2a3242; border-color: #a08850; }
</style></head><body>
    <div id="veil"></div>
    <div id="card"><div id="title">NEW GAME</div><div id="seed"></div><div id="list"></div><div id="begin">Begin</div></div>
</body></rml>)";

constexpr const char* kUnwritten = "Not yet written";

}

NewGameView buildNewGameView(std::uint64_t seed, const SlotRollResult& roll, const AxiomSet& axioms)
{
    NewGameView view;
    view.seed = seed;
    for (const AxiomSlot slot : {AxiomSlot::Cataclysm, AxiomSlot::Law, AxiomSlot::Powers, AxiomSlot::Pressure, AxiomSlot::Anomaly}) {
        if (!axiomSlotShownAtCreation(slot)) {
            continue;
        }
        NewGameRow row;
        row.letter = axiomSlotLetter(slot);
        row.slot = std::string(axiomSlotName(slot));
        row.slot[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(row.slot[0])));
        row.title = kUnwritten;
        if (const SlotAssignment* assignment = roll.find(slot); assignment != nullptr && assignment->shown) {
            if (const Axiom* axiom = axioms.find(assignment->axiomId); axiom != nullptr) {
                row.title = axiom->title;
            }
        }
        view.rows.push_back(std::move(row));
    }
    return view;
}

NewGameScreen::NewGameScreen(UiContext& ui)
    : ui_(ui)
{
    document_ = ui_.loadDocumentFromMemory(kNewGameDocument, "[new game screen]");
    if (document_ == nullptr) {
        return;
    }
    begin_ = document_->GetElementById("begin");
    seed_ = document_->GetElementById("seed");
    list_ = document_->GetElementById("list");
    if (begin_ == nullptr || seed_ == nullptr || list_ == nullptr) {
        ui_.unloadDocument(document_);
        document_ = nullptr;
        return;
    }
    begin_->AddEventListener("click", &click_);
}

NewGameScreen::~NewGameScreen()
{
    // The element may live on until the context next updates, and the listener goes first.
    if (begin_ != nullptr) {
        begin_->RemoveEventListener("click", &click_);
    }
    if (document_ != nullptr) {
        ui_.unloadDocument(document_);
    }
}

void NewGameScreen::setVisible(bool visible)
{
    if (document_ == nullptr || visible == visible_) {
        return;
    }
    visible_ = visible;
    if (visible_) {
        click_.requested = false; // a click from an earlier showing is not a click now
        document_->Show();
    } else {
        document_->Hide();
    }
}

void NewGameScreen::setView(const NewGameView& view)
{
    if (list_ == nullptr) {
        return;
    }
    seed_->SetInnerRML("Seed " + std::to_string(view.seed));
    std::string markup;
    for (const NewGameRow& row : view.rows) {
        const bool unwritten = row.title == kUnwritten;
        markup += "<div class=\"row\"><div class=\"slot\">Slot " + escapeRml(std::string(1, row.letter)) + ": " + escapeRml(row.slot)
            + "</div><div class=\"axiom" + (unwritten ? " unwritten" : "") + "\">" + escapeRml(row.title) + "</div></div>";
    }
    list_->SetInnerRML(markup);
}

bool NewGameScreen::takeBegin()
{
    const bool requested = click_.requested;
    click_.requested = false;
    return requested;
}

std::optional<std::pair<float, float>> NewGameScreen::beginCentre() const
{
    if (begin_ == nullptr || !visible_) {
        return std::nullopt;
    }
    const Rml::Vector2f offset = begin_->GetAbsoluteOffset(Rml::BoxArea::Border);
    const Rml::Vector2f size = begin_->GetBox().GetSize(Rml::BoxArea::Border);
    return std::pair{offset.x + size.x * 0.5f, offset.y + size.y * 0.5f};
}

std::string NewGameScreen::markup() const
{
    if (list_ == nullptr) {
        return {};
    }
    return seed_->GetInnerRML() + list_->GetInnerRML();
}

}
