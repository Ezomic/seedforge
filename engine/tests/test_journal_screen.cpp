#include "ichor/ui/journal_screen.h"
#include "ichor/ui/recording_renderer.h"
#include "ichor/ui/ui_context.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>

using namespace ichor;

namespace {

bool drewColour(const RecordingUiRenderer& renderer, int r, int g, int b)
{
    const auto& draws = renderer.draws();
    return std::any_of(draws.begin(), draws.end(), [&](const RecordingUiRenderer::Draw& draw) {
        return draw.firstColour.red == r && draw.firstColour.green == g && draw.firstColour.blue == b;
    });
}

void frame(UiContext& ui, RecordingUiRenderer& renderer, double time)
{
    renderer.clearDraws();
    ui.update(time);
    ui.render();
}

struct Fixture {
    RecordingUiRenderer renderer;
    UiContext ui{renderer, 1280, 720};

    Fixture()
    {
        REQUIRE(ui.ok());
        const std::filesystem::path fonts = std::filesystem::path(ICHOR_ART_DIR) / "fonts";
        REQUIRE(ui.loadFontFace(fonts / "AlegreyaSans-Regular.ttf"));
        REQUIRE(ui.loadFontFace(fonts / "Cinzel.ttf"));
    }
};

}

TEST_CASE("the journal draws nothing until shown, then its card and entries, and only relays out when they change")
{
    Fixture f;
    JournalScreen journal(f.ui);
    REQUIRE(journal.ok());
    REQUIRE_FALSE(journal.visible());

    const std::vector<JournalRow> rows = {{"0:01:05", "Kallias met me at the weir and asked me to carry the ash. In the end, I agreed to go."}};
    journal.setRows(rows);
    frame(f.ui, f.renderer, 0.0);
    REQUIRE(f.renderer.draws().empty());

    journal.setVisible(true);
    frame(f.ui, f.renderer, 0.1);
    const std::size_t shown = f.renderer.draws().size();
    REQUIRE(shown >= 3);

    journal.setRows(rows);
    frame(f.ui, f.renderer, 0.2);
    REQUIRE(f.renderer.draws().size() == shown);

    journal.setRows({{"0:01:05", "Kallias met me at the weir and asked me to carry the ash. In the end, I agreed to go."}, {"0:02:00", "More."}});
    frame(f.ui, f.renderer, 0.3);
    REQUIRE(f.renderer.draws().size() > shown);

    journal.setVisible(false);
    frame(f.ui, f.renderer, 0.4);
    REQUIRE(f.renderer.draws().empty());
}

TEST_CASE("an empty journal says so, and more entries than fit are left off")
{
    Fixture f;
    JournalScreen journal(f.ui);
    journal.setVisible(true);
    journal.setRows({});
    frame(f.ui, f.renderer, 0.0);
    const std::size_t empty = f.renderer.draws().size();
    REQUIRE(empty >= 2);

    journal.setRows(std::vector<JournalRow>(10000, JournalRow{"0:00:01", "A line of prose."}));
    frame(f.ui, f.renderer, 0.1);
    const std::size_t full = f.renderer.draws().size();
    journal.setRows(std::vector<JournalRow>(JournalScreen::kMaxRows, JournalRow{"0:00:01", "A line of prose."}));
    frame(f.ui, f.renderer, 0.2);
    REQUIRE(f.renderer.draws().size() == full);
    REQUIRE(full > empty);
}

TEST_CASE("an entry is text on the card, never markup")
{
    Fixture f;
    JournalScreen journal(f.ui);
    journal.setVisible(true);
    journal.setRows({{"0:02:00", "<div style=\"background-color: #ff0000; width: 900px; height: 900px\">{who}</div>"}});
    frame(f.ui, f.renderer, 0.0);
    REQUIRE_FALSE(drewColour(f.renderer, 255, 0, 0));
}

TEST_CASE("long entries stay inside the card")
{
    Fixture f;
    JournalScreen journal(f.ui);
    journal.setVisible(true);
    journal.setRows(std::vector<JournalRow>(JournalScreen::kMaxRows, JournalRow{"9:59:59", std::string(500, 'w')}));
    frame(f.ui, f.renderer, 0.0);
    REQUIRE_FALSE(f.renderer.draws().empty());
    for (const auto& draw : f.renderer.draws()) {
        if (draw.scissored) {
            REQUIRE(draw.scissor.Bottom() <= 720 * 88 / 100 + 2);
        }
    }
}

TEST_CASE("clicking the journal's close button asks for it to be closed, and clicking elsewhere does not")
{
    Fixture f;
    JournalScreen journal(f.ui);
    REQUIRE_FALSE(journal.closeButtonCentre().has_value());
    journal.setVisible(true);
    frame(f.ui, f.renderer, 0.0);
    const auto centre = journal.closeButtonCentre();
    REQUIRE(centre.has_value());

    REQUIRE(centre->first > 640.0f);
    REQUIRE(centre->second < 360.0f);

    f.ui.mouseMove(640, 400);
    frame(f.ui, f.renderer, 0.2);
    f.ui.mouseButton(0, true);
    f.ui.mouseButton(0, false);
    frame(f.ui, f.renderer, 0.3);
    REQUIRE_FALSE(journal.takeCloseRequest());

    f.ui.mouseMove(static_cast<int>(centre->first), static_cast<int>(centre->second));
    frame(f.ui, f.renderer, 0.4);
    f.ui.mouseButton(0, true);
    frame(f.ui, f.renderer, 0.5);
    f.ui.mouseButton(0, false);
    frame(f.ui, f.renderer, 0.6);
    REQUIRE(journal.takeCloseRequest());
    REQUIRE_FALSE(journal.takeCloseRequest());
}

#include "ichor/ui/journal_screen_bind.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>

#include <cmath>

namespace {

std::size_t drawsWith(const std::vector<JournalModelEntry>& entries, JournalTab tab)
{
    Fixture f;
    JournalScreen journal(f.ui);
    REQUIRE(journal.ok());
    journal.setVisible(true);
    journal.selectTab(tab);
    bindJournalModel(journal, entries);
    frame(f.ui, f.renderer, 0.0);
    return f.renderer.draws().size();
}

const std::vector<JournalModelEntry> kEntries = {
    {10, "0:00:01", "journal", "I agreed to carry the ash.", 200},
    {20, "0:00:02", "trade", "Sold a pelt.", 10},
    {30, "0:00:03", "combat-log", "Killed a wolf.", 0},
};

}

TEST_CASE("the bound journal draws its entries, tab by tab, and a different model draws differently")
{
    const std::size_t none = drawsWith({}, JournalTab::Prose);
    const std::size_t prose = drawsWith(kEntries, JournalTab::Prose);
    const std::size_t consequences = drawsWith(kEntries, JournalTab::Consequences);
    REQUIRE(prose > none);
    REQUIRE(consequences > prose);
}

TEST_CASE("clicking the other tab of the bound journal shows it, and unchanged models are not rewritten")
{
    Fixture f;
    JournalScreen journal(f.ui);
    journal.setVisible(true);
    const auto model = buildJournalModel(kEntries);
    journal.setTabs(model);
    frame(f.ui, f.renderer, 0.0);
    const std::size_t prose = f.renderer.draws().size();
    journal.setTabs(model);
    frame(f.ui, f.renderer, 0.1);
    REQUIRE(f.renderer.draws().size() == prose);

    Rml::Element* tab = f.ui.context()->GetRootElement()->GetElementById("tab-consequences");
    REQUIRE(tab != nullptr);
    const Rml::Vector2f at = tab->GetAbsoluteOffset(Rml::BoxArea::Border) + tab->GetBox().GetSize(Rml::BoxArea::Border) * 0.5f;
    f.ui.mouseMove(static_cast<int>(at.x), static_cast<int>(at.y));
    frame(f.ui, f.renderer, 0.2);
    f.ui.mouseButton(0, true);
    f.ui.mouseButton(0, false);
    frame(f.ui, f.renderer, 0.3);
    REQUIRE(journal.activeTab() == JournalTab::Consequences);
    journal.setTabs(model);
    frame(f.ui, f.renderer, 0.4);
    REQUIRE(f.renderer.draws().size() > prose);
}

TEST_CASE("bound journal text is never markup")
{
    Fixture f;
    JournalScreen journal(f.ui);
    journal.setVisible(true);
    bindJournalModel(journal, {{1, "0:00:01", "journal", "<div style=\"background-color: #ff0000; width: 900px; height: 900px\">x</div>", 0}});
    frame(f.ui, f.renderer, 0.0);
    REQUIRE_FALSE(drewColour(f.renderer, 255, 0, 0));
}

TEST_CASE("play time formats and survives a bad rate")
{
    REQUIRE(journalPlayTime(3725, 1.0) == "1:02:05");
    REQUIRE(journalPlayTime(5, -1.0) == "0:00:00");
    REQUIRE(journalPlayTime(5, std::nan("")) == "0:00:00");
}

TEST_CASE("a journal longer than its card keeps the width of its list")
{
    for (const std::uint32_t height : {360u, 480u}) {
        RecordingUiRenderer renderer;
        UiContext ui{renderer, height / 9 * 16, height};
        const std::filesystem::path fonts = std::filesystem::path(ICHOR_ART_DIR) / "fonts";
        REQUIRE(ui.loadFontFace(fonts / "AlegreyaSans-Regular.ttf"));
        REQUIRE(ui.loadFontFace(fonts / "Cinzel.ttf"));
        JournalScreen journal(ui);
        std::vector<JournalRow> rows;
        for (int i = 0; i < 6; ++i) {
            rows.push_back({"0:00:01", "Kallias met me at the weir and asked me to carry the ash. In the end, I agreed to go."});
        }
        journal.setRows(rows);
        journal.setVisible(true);
        frame(ui, renderer, 0.0);
        frame(ui, renderer, 0.1);
        Rml::Element* list = ui.context()->GetRootElement()->GetElementById("list");
        REQUIRE(list != nullptr);
        CAPTURE(height);
        REQUIRE(list->GetScrollHeight() > list->GetClientHeight());
        REQUIRE(list->GetClientWidth() > list->GetBox().GetSize(Rml::BoxArea::Border).x * 0.9f);
    }
}
