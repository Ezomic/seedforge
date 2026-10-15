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
