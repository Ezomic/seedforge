#include "ichor/ui/load_screen.h"

#include "ichor/ui/ledger.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

#include <charconv>
#include <cmath>
#include <format>

namespace ichor {

namespace {

constexpr const char* kLoadDocument = R"(<rml><head><style>
    body, div { display: block; }
    body { width: 100%; height: 100%; }
    #veil { position: absolute; top: 0px; left: 0px; right: 0px; bottom: 0px; background-color: #05070bd0; }
    #card { position: absolute; top: 6%; left: 50%; margin-left: -363px; width: 660px; padding: 20px 30px 24px 30px; background-color: #10141af4; border: 3px #a08850; }
    #title { font-family: Cinzel; font-size: 34px; color: #e8d8a8; letter-spacing: 4px; text-align: center; padding-bottom: 8px; }
    #empty { font-family: Alegreya Sans; font-size: 22px; color: #a8a090; text-align: center; padding: 20px 0px; }
    .row { height: 108px; padding: 8px; margin-top: 10px; background-color: #1c222c; border: 2px #3a4252; }
    .row.pick:hover { background-color: #2a3242; border-color: #a08850; }
    .row.broken { background-color: #2a1a1c; border-color: #6a3a3a; }
    .pic { float: left; width: 160px; height: 90px; margin-right: 16px; }
    .none { float: left; width: 160px; height: 56px; margin-right: 16px; padding-top: 34px; background-color: #0a0d12; border: 1px #3a4252; font-family: Alegreya Sans; font-size: 18px; color: #6a7080; text-align: center; }
    .slot { font-family: Cinzel; font-size: 24px; color: #f4e8c0; }
    .world { font-family: Alegreya Sans; font-size: 22px; color: #d8d8d8; padding-top: 4px; }
    .time { font-family: Alegreya Sans; font-size: 20px; color: #a8a090; padding-top: 2px; }
    .note { font-family: Alegreya Sans; font-size: 20px; color: #e07070; padding-top: 4px; }
    #back { font-family: Alegreya Sans; font-size: 24px; text-align: center; color: #d8d8d8; padding: 6px 0px 8px 0px; margin-top: 16px; background-color: #1c222c; border: 2px #3a4252; }
    #back:hover { color: #f4e8c0; background-color: #2a3242; border-color: #a08850; }
</style></head><body>
    <div id="veil"></div>
    <div id="card"><div id="title">LOAD GAME</div><div id="list"></div><div id="back">Back</div></div>
</body></rml>)";

constexpr const char* kRowPrefix = "slot-";

}

std::string playtimeText(std::uint64_t ticks, double secondsPerTick)
{
    const double seconds = std::isfinite(secondsPerTick) && secondsPerTick > 0.0 ? static_cast<double>(ticks) * secondsPerTick : 0.0;
    const std::uint64_t whole = seconds >= 9.0e15 ? 9'000'000'000'000'000ull : static_cast<std::uint64_t>(seconds);
    return std::format("{}:{:02}:{:02}", whole / 3600, whole / 60 % 60, whole % 60);
}

LoadView buildLoadView(const std::vector<SlotEntry>& entries, const std::string& worldTitle, double secondsPerTick,
    const std::function<std::filesystem::path(const std::string&)>& thumbnailOf)
{
    LoadView view;
    for (const SlotEntry& entry : entries) {
        LoadRow row;
        row.slot = entry.info.name;
        row.damaged = entry.damaged;
        if (entry.damaged) {
            row.title = "Damaged save";
            row.note = "This slot cannot be read and cannot be loaded.";
        } else {
            row.title = worldTitle;
            row.playtime = playtimeText(entry.info.tick, secondsPerTick);
            if (thumbnailOf) {
                row.thumbnail = thumbnailOf(entry.info.name).generic_string();
            }
            if (entry.backupIndex > 0) {
                row.note = "Damaged; backup " + std::to_string(entry.backupIndex) + (entry.backupIndex == 1 ? " (your previous save) will load." : " (an older save) will load.");
            }
        }
        view.rows.push_back(std::move(row));
    }
    return view;
}

void LoadScreen::Pick::ProcessEvent(Rml::Event& event)
{
    for (Rml::Element* element = event.GetTargetElement(); element != nullptr; element = element->GetParentNode()) {
        const std::string& id = element->GetId();
        if (!id.starts_with(kRowPrefix)) {
            continue;
        }
        std::size_t index = 0;
        const char* begin = id.data() + std::char_traits<char>::length(kRowPrefix);
        const auto parsed = std::from_chars(begin, id.data() + id.size(), index);
        if (parsed.ec == std::errc() && index < screen.choosable_.size() && !screen.choosable_[index].empty()) {
            screen.chosen_ = screen.choosable_[index];
        }
        return;
    }
}

LoadScreen::LoadScreen(UiContext& ui)
    : ui_(ui)
{
    document_ = ui_.loadDocumentFromMemory(kLoadDocument, "[load screen]");
    if (document_ == nullptr) {
        return;
    }
    list_ = document_->GetElementById("list");
    backButton_ = document_->GetElementById("back");
    if (list_ == nullptr || backButton_ == nullptr) {
        ui_.unloadDocument(document_);
        document_ = nullptr;
        return;
    }
    list_->AddEventListener("click", &pick_);
    backButton_->AddEventListener("click", &back_);
}

LoadScreen::~LoadScreen()
{
    // The elements may live on until the context next updates, and the listeners go first.
    if (list_ != nullptr) {
        list_->RemoveEventListener("click", &pick_);
    }
    if (backButton_ != nullptr) {
        backButton_->RemoveEventListener("click", &back_);
    }
    if (document_ != nullptr) {
        ui_.unloadDocument(document_);
    }
}

void LoadScreen::setVisible(bool visible)
{
    if (document_ == nullptr || visible == visible_) {
        return;
    }
    visible_ = visible;
    if (visible_) {
        chosen_.reset(); // a click from an earlier showing is not a click now
        back_.requested = false;
        document_->Show();
    } else {
        document_->Hide();
    }
}

void LoadScreen::setView(const LoadView& view)
{
    if (list_ == nullptr) {
        return;
    }
    choosable_.clear();
    std::string markup;
    for (const LoadRow& row : view.rows) {
        const std::string id = kRowPrefix + std::to_string(choosable_.size());
        choosable_.push_back(row.damaged ? std::string() : row.slot);
        markup += "<div id=\"" + id + "\" class=\"row" + (row.damaged ? " broken" : " pick") + "\">";
        if (!row.thumbnail.empty()) {
            markup += "<img class=\"pic\" src=\"" + escapeRml(row.thumbnail) + "\"/>";
        } else {
            markup += "<div class=\"none\">No picture</div>";
        }
        markup += "<div class=\"slot\">" + escapeRml(row.slot) + "</div><div class=\"world\">" + escapeRml(row.title) + "</div>";
        if (!row.playtime.empty()) {
            markup += "<div class=\"time\">Played " + escapeRml(row.playtime) + "</div>";
        }
        if (!row.note.empty()) {
            markup += "<div class=\"note\">" + escapeRml(row.note) + "</div>";
        }
        markup += "</div>";
    }
    if (view.rows.empty()) {
        markup = "<div id=\"empty\">No saved games yet.</div>";
    }
    list_->SetInnerRML(markup);
}

std::optional<std::string> LoadScreen::takeChosen()
{
    std::optional<std::string> chosen;
    chosen.swap(chosen_);
    return chosen;
}

bool LoadScreen::takeBack()
{
    const bool requested = back_.requested;
    back_.requested = false;
    return requested;
}

std::optional<std::pair<float, float>> LoadScreen::rowCentre(std::size_t index) const
{
    if (document_ == nullptr || !visible_) {
        return std::nullopt;
    }
    Rml::Element* row = document_->GetElementById(kRowPrefix + std::to_string(index));
    if (row == nullptr) {
        return std::nullopt;
    }
    const Rml::Vector2f offset = row->GetAbsoluteOffset(Rml::BoxArea::Border);
    const Rml::Vector2f size = row->GetBox().GetSize(Rml::BoxArea::Border);
    return std::pair{offset.x + size.x * 0.5f, offset.y + size.y * 0.5f};
}

std::optional<std::pair<float, float>> LoadScreen::backCentre() const
{
    if (backButton_ == nullptr || !visible_) {
        return std::nullopt;
    }
    const Rml::Vector2f offset = backButton_->GetAbsoluteOffset(Rml::BoxArea::Border);
    const Rml::Vector2f size = backButton_->GetBox().GetSize(Rml::BoxArea::Border);
    return std::pair{offset.x + size.x * 0.5f, offset.y + size.y * 0.5f};
}

std::string LoadScreen::markup() const
{
    return list_ != nullptr ? list_->GetInnerRML() : std::string();
}

}
