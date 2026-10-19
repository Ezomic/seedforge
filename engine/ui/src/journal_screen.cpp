#include "ichor/ui/journal_screen.h"

#include "ichor/ui/journal_screen_bind.h"
#include "ichor/ui/ledger.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

#include <algorithm>

namespace ichor {

namespace {

constexpr const char* kJournalDocument = R"(<rml><head><style>
    body, div { display: block; }
    body { width: 100%; height: 100%; }
    #card { position: absolute; top: 12%; bottom: 12%; left: 12%; right: 12%; overflow: hidden; padding: 22px 28px 22px 28px; background-color: #1a1710f0; border: 3px #a08850; }
    #close { position: absolute; top: 10px; right: 14px; width: 34px; height: 34px; font-family: Alegreya Sans; font-size: 30px; line-height: 34px; text-align: center; color: #a08850; }
    #close:hover { color: #e8d8a8; background-color: #3a3426; }
    #title { font-family: Cinzel; font-size: 34px; color: #e8d8a8; letter-spacing: 3px; text-align: center; padding-bottom: 12px; }
    #tabs { text-align: center; padding-bottom: 10px; }
    .tab { display: inline-block; padding: 4px 18px 4px 18px; margin: 0px 6px 0px 6px; font-family: Cinzel; font-size: 20px; color: #8c8468; border-bottom: 2px #3a3426; }
    #list { position: absolute; top: 135px; bottom: 22px; left: 28px; right: 28px; overflow-y: auto; }
    scrollbarvertical { width: 10px; }
    scrollbarvertical slidertrack { background-color: #2a261c; }
    scrollbarvertical sliderbar { width: 10px; background-color: #a08850; }
    scrollbarvertical sliderarrowdec, scrollbarvertical sliderarrowinc { width: 0px; height: 0px; }
    .tab:hover { color: #e8d8a8; }
    .tab.active { color: #e8d8a8; border-bottom: 2px #a08850; }
    .group { font-family: Cinzel; font-size: 20px; color: #a08850; letter-spacing: 2px; padding: 12px 0px 4px 0px; }
    .major { color: #f4e8c0; }
    .more { font-family: Alegreya Sans; font-size: 16px; color: #8c8468; padding: 6px 0px 6px 0px; }
    .entry { word-break: break-word; padding: 8px 0px 8px 0px; border-bottom: 1px #3a3426; font-family: Alegreya Sans; font-size: 21px; color: #e0d8c0; }
    .when { color: #8c8468; font-size: 16px; padding-right: 10px; }
    .empty { font-family: Alegreya Sans; font-size: 21px; color: #8c8468; text-align: center; padding: 18px 0px 18px 0px; }
</style></head><body>
    <div id="card"><div id="close">&#215;</div><div id="title">THE JOURNAL</div><div id="tabs"></div><div id="list"></div></div>
</body></rml>)";

}

JournalScreen::JournalScreen(UiContext& ui)
    : ui_(ui)
{
    document_ = ui_.loadDocumentFromMemory(kJournalDocument, "[journal]");
    if (document_ == nullptr) {
        return;
    }
    list_ = document_->GetElementById("list");
    close_ = document_->GetElementById("close");
    if (list_ == nullptr || close_ == nullptr) {
        ui_.unloadDocument(document_);
        document_ = nullptr;
        return;
    }
    close_->AddEventListener("click", &click_);
    tabs_ = document_->GetElementById("tabs");
    if (tabs_ != nullptr) {
        tabClick_.screen = this;
        tabs_->AddEventListener("click", &tabClick_);
    }
}

void JournalScreen::TabClick::ProcessEvent(Rml::Event& event)
{
    const Rml::Element* target = event.GetTargetElement();
    while (target != nullptr && target->GetId() != "tab-prose" && target->GetId() != "tab-consequences") {
        target = target->GetParentNode();
    }
    if (target != nullptr) {
        screen->selectTab(target->GetId() == "tab-prose" ? JournalTab::Prose : JournalTab::Consequences);
    }
}

JournalScreen::~JournalScreen()
{
    if (tabs_ != nullptr) {
        tabs_->RemoveEventListener("click", &tabClick_);
    }
    if (close_ != nullptr) {
        // The element may live on until the context next updates, and the listener goes first.
        close_->RemoveEventListener("click", &click_);
    }
    if (document_ != nullptr) {
        ui_.unloadDocument(document_);
    }
}

bool JournalScreen::takeCloseRequest()
{
    const bool requested = click_.requested;
    click_.requested = false;
    return requested;
}

std::optional<std::pair<float, float>> JournalScreen::closeButtonCentre() const
{
    if (close_ == nullptr || !visible_) {
        return std::nullopt;
    }
    const Rml::Vector2f offset = close_->GetAbsoluteOffset(Rml::BoxArea::Border);
    const Rml::Vector2f size = close_->GetBox().GetSize(Rml::BoxArea::Border);
    return std::pair{offset.x + size.x * 0.5f, offset.y + size.y * 0.5f};
}

void JournalScreen::setVisible(bool visible)
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

void JournalScreen::setRows(const std::vector<JournalRow>& rows)
{
    if (list_ == nullptr) {
        return;
    }
    const std::size_t count = std::min(rows.size(), kMaxRows);
    const auto end = rows.begin() + static_cast<std::ptrdiff_t>(count);
    if (written_ && count == shown_.size() && std::equal(rows.begin(), end, shown_.begin())) {
        return;
    }
    shown_.assign(rows.begin(), end);
    written_ = true;
    std::string markup;
    if (shown_.empty()) {
        markup = "<div class=\"empty\">Nothing has been written down yet.</div>";
    }
    for (const JournalRow& row : shown_) {
        markup += "<div class=\"entry\"><span class=\"when\">" + escapeRml(row.time) + "</span>" + escapeRml(row.text) + "</div>";
    }
    list_->SetProperty("top", "78px");
    list_->SetInnerRML(markup);
}

void JournalScreen::setTabs(const std::vector<JournalModelTab>& tabs)
{
    if (list_ == nullptr || tabs_ == nullptr) {
        return;
    }
    if (tabsWritten_ && shownTab_ == active_ && shownTabs_ == tabs) {
        return;
    }
    shownTabs_ = tabs;
    shownTab_ = active_;
    tabsWritten_ = true;
    tabs_->SetInnerRML(journalTabBarMarkup(shownTabs_, active_));
    list_->SetProperty("top", "135px");
    list_->SetInnerRML(journalTabBodyMarkup(shownTabs_, active_));
}

}
