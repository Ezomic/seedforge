#include "ichor/ui/new_game_screen.h"

#include "ichor/ui/ledger.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

#include <algorithm>
#include <cctype>
#include <string>

namespace ichor {

namespace {

constexpr const char* kNewGameDocument = R"(<rml><head><style>
    body, div { display: block; }
    body { width: 100%; height: 100%; }
    #veil { position: absolute; top: 0px; left: 0px; right: 0px; bottom: 0px; background-color: #05070bb0; }
    #card { position: absolute; top: 1%; left: 50%; margin-left: -283px; width: 500px; padding: 16px 30px 18px 30px; background-color: #10141af4; border: 3px #a08850; }
    #title { font-family: Cinzel; font-size: 30px; color: #e8d8a8; letter-spacing: 4px; text-align: center; padding-bottom: 4px; }
    #seed { font-family: Alegreya Sans; font-size: 22px; color: #a8a090; text-align: center; padding-bottom: 8px; }
    .row { padding: 6px 14px 8px 14px; margin-top: 8px; background-color: #1c222c; border: 2px #3a4252; }
    .slot { font-family: Alegreya Sans; font-size: 20px; color: #a8a090; letter-spacing: 1px; }
    .axiom { font-family: Cinzel; font-size: 24px; color: #f4e8c0; padding-top: 2px; }
    .unwritten { color: #8890a0; }
    #featured { padding-top: 6px; }
    #fhead { font-family: Alegreya Sans; font-size: 20px; color: #a8a090; letter-spacing: 2px; padding-top: 10px; }
    .pick { padding: 4px 14px 6px 14px; margin-top: 5px; background-color: #1c222c; border: 2px #3a4252; }
    .pick:hover { background-color: #2a3242; border-color: #a08850; }
    .ptitle { font-family: Cinzel; font-size: 18px; color: #f4e8c0; display: inline; }
    .psum { font-family: Alegreya Sans; font-size: 17px; color: #a8a090; display: inline; padding-left: 10px; }
    #begin { font-family: Alegreya Sans; font-size: 26px; text-align: center; color: #d8d8d8; padding: 6px 0px 8px 0px; margin-top: 14px; background-color: #1c222c; border: 2px #3a4252; }
    #begin:hover { color: #f4e8c0; background-color: #2a3242; border-color: #a08850; }
</style></head><body>
    <div id="veil"></div>
    <div id="card"><div id="title">NEW GAME</div><div id="seed"></div><div id="list"></div><div id="featured"></div><div id="begin">Begin</div></div>
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
    featured_ = document_->GetElementById("featured");
    if (begin_ == nullptr || seed_ == nullptr || list_ == nullptr || featured_ == nullptr) {
        ui_.unloadDocument(document_);
        document_ = nullptr;
        return;
    }
    begin_->AddEventListener("click", &click_);
    featured_->AddEventListener("click", &pick_);
}

NewGameScreen::~NewGameScreen()
{
    // The element may live on until the context next updates, and the listener goes first.
    if (begin_ != nullptr) {
        begin_->RemoveEventListener("click", &click_);
        featured_->RemoveEventListener("click", &pick_);
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
        pick_.picked.reset();
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
    return seed_->GetInnerRML() + list_->GetInnerRML() + featured_->GetInnerRML();
}

}

namespace ichor {

std::vector<FeaturedRow> buildFeaturedRows(std::span<const FeaturedSeed> list, std::span<const Axiom> pool, const AxiomSet& axioms)
{
    std::vector<FeaturedRow> rows;
    for (const FeaturedSeed& entry : list) {
        if (!validateFeaturedSeed(entry, pool).ok()) {
            continue;
        }
        const NewGameView view = buildNewGameView(entry.seed, rollSlots(entry.seed, pool), axioms);
        std::string summary;
        for (const NewGameRow& row : view.rows) {
            if (row.title == kUnwritten) {
                continue;
            }
            summary += summary.empty() ? row.title : ", " + row.title;
        }
        if (!summary.empty()) {
            rows.push_back({entry.seed, entry.title, std::move(summary)});
        }
    }
    return rows;
}

void NewGameScreen::Pick::ProcessEvent(Rml::Event& event)
{
    for (Rml::Element* element = event.GetTargetElement(); element != nullptr; element = element->GetParentNode()) {
        const Rml::String& id = element->GetId();
        if (id.size() > 1 && id[0] == 'f' && std::all_of(id.begin() + 1, id.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) {
            const std::size_t index = std::stoul(id.substr(1));
            if (index < seeds.size()) {
                picked = seeds[index];
            }
            return;
        }
    }
}

void NewGameScreen::setFeatured(const std::vector<FeaturedRow>& rows)
{
    if (featured_ == nullptr) {
        return;
    }
    pick_.seeds.clear();
    std::string markup;
    if (!rows.empty()) {
        markup = "<div id=\"fhead\">FEATURED SEEDS</div>";
    }
    for (const FeaturedRow& row : rows) {
        markup += "<div class=\"pick\" id=\"f" + std::to_string(pick_.seeds.size()) + "\"><div class=\"ptitle\">" + escapeRml(row.title)
            + "</div><div class=\"psum\">" + escapeRml(row.summary) + "</div></div>";
        pick_.seeds.push_back(row.seed);
    }
    featured_->SetInnerRML(markup);
}

std::optional<std::uint64_t> NewGameScreen::takePickedSeed()
{
    const std::optional<std::uint64_t> seed = pick_.picked;
    pick_.picked.reset();
    return seed;
}

}
