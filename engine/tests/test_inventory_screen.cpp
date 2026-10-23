#include "ichor/ui/inventory_screen.h"
#include "ichor/ui/recording_renderer.h"
#include "ichor/ui/ui_context.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <limits>
#include <string>

using namespace ichor;

namespace {

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

CarryViewRow row(const char* id, const char* name, const char* itemClass, std::uint32_t count, std::uint64_t unit, bool usable)
{
    return {id, name, itemClass, "", count, unit, usable};
}

CarryView sample()
{
    CarryView view;
    view.rows = {row("spear", "The Eleventh Spear", "gear", 1, 3400, false), row("tallow", "Huntress's Hush Tallow", "consumable", 2, 500, true),
        row("cruet", "The Ichor Cruet", "consumable", 1, 400, true)};
    view.totalUnits = 3400 + 1000 + 400;
    view.capacityUnits = 40000;
    view.level = EncumbranceLevel::Light;
    return view;
}

}

TEST_CASE("the inventory model lists rows by kind then name with stack weights and the load", "[inventory_screen]")
{
    const InventoryScreenView view = buildInventoryScreen(sample(), "");
    REQUIRE(view.rows.size() == 3);
    REQUIRE(view.rows[0].id == "tallow");
    REQUIRE(view.rows[1].id == "cruet");
    REQUIRE(view.rows[2].id == "spear");
    REQUIRE(view.rows[0].count == "x2");
    REQUIRE(view.rows[0].unitWeight == "0.5");
    REQUIRE(view.rows[0].weight == "1.0");
    REQUIRE(view.rows[0].usable);
    REQUIRE_FALSE(view.rows[2].usable);
    REQUIRE(view.load == "4.8 / 40.0");
    REQUIRE(view.levelName == "Light");
    REQUIRE(view.fillPermille == 120);
}

TEST_CASE("the load bar fills with the weight, reads full when at or over capacity, and names the level", "[inventory_screen]")
{
    CarryView view = sample();
    const std::uint32_t light = buildInventoryScreen(view, "").fillPermille;
    view.totalUnits = 32000;
    view.level = EncumbranceLevel::Heavy;
    const InventoryScreenView heavy = buildInventoryScreen(view, "");
    REQUIRE(heavy.fillPermille == 800);
    REQUIRE(heavy.fillPermille > light);
    REQUIRE(heavy.levelName == "Heavy");

    view.totalUnits = 52000;
    view.level = EncumbranceLevel::Over;
    const InventoryScreenView over = buildInventoryScreen(view, "");
    REQUIRE(over.fillPermille == 1000);
    REQUIRE(over.levelName == "Over");

    view.capacityUnits = 0;
    view.totalUnits = 0;
    REQUIRE(buildInventoryScreen(view, "").fillPermille == 0);
    view.totalUnits = std::numeric_limits<std::uint64_t>::max();
    REQUIRE(buildInventoryScreen(view, "").fillPermille == 1000);
}

TEST_CASE("the inventory model is bounded and sanitises hostile text and counts", "[inventory_screen]")
{
    CarryView view;
    for (int i = 0; i < 100; ++i) view.rows.push_back(row(("id" + std::to_string(i)).c_str(), "N", "gear", 1, 100, false));
    view.rows.push_back(row("zero", "Nothing", "gear", 0, 100, false));
    view.rows.push_back(row("nameless", "", "gear", 1, 100, false));
    view.rows.push_back(row("rude", "<b>bad\n\tname</b>", "gear", 1, 100, false));
    view.rows.push_back(row("long", std::string(500, 'x').c_str(), "gear", 1, 100, false));
    view.rows.push_back(row("big", "Big", "gear", 4000000000u, 1000000, false));
    const InventoryScreenView built = buildInventoryScreen(view, std::string(300, 'n') + "\n");
    REQUIRE(built.rows.size() == kInventoryScreenMaxRows);
    REQUIRE(built.omitted == 100 + 5 - 1 - kInventoryScreenMaxRows);
    REQUIRE(built.notice.size() == kInventoryScreenMaxTextBytes);
    for (const InventoryScreenRow& r : built.rows) {
        REQUIRE(r.name.size() <= kInventoryScreenMaxTextBytes);
        REQUIRE(r.name.find('\n') == std::string::npos);
        REQUIRE(r.id != "zero");
    }
}

TEST_CASE("the inventory screen draws nothing until shown, more as rows arrive, and only relays out on change", "[inventory_screen]")
{
    Fixture f;
    InventoryScreen screen(f.ui);
    REQUIRE(screen.ok());
    screen.bind(buildInventoryScreen({}, ""));
    frame(f.ui, f.renderer, 0.0);
    REQUIRE(f.renderer.draws().empty());

    screen.setVisible(true);
    frame(f.ui, f.renderer, 0.1);
    const std::size_t empty = f.renderer.draws().size();
    REQUIRE(empty >= 2);
    REQUIRE(screen.contentRml().find("You carry nothing") != std::string::npos);

    screen.bind(buildInventoryScreen(sample(), ""));
    frame(f.ui, f.renderer, 0.2);
    const std::size_t full = f.renderer.draws().size();
    REQUIRE(full > empty);
    REQUIRE(screen.contentRml().find("The Eleventh Spear") != std::string::npos);

    screen.bind(buildInventoryScreen(sample(), ""));
    frame(f.ui, f.renderer, 0.3);
    REQUIRE(f.renderer.draws().size() == full);

    screen.setVisible(false);
    frame(f.ui, f.renderer, 0.4);
    REQUIRE(f.renderer.draws().empty());
}

TEST_CASE("item names never act as markup", "[inventory_screen]")
{
    Fixture f;
    InventoryScreen screen(f.ui);
    CarryView view = sample();
    view.rows[0].name = "<div id=\"close\">x</div>";
    screen.setVisible(true);
    screen.bind(buildInventoryScreen(view, ""));
    frame(f.ui, f.renderer, 0.1);
    REQUIRE(screen.contentRml().find("<div id=\"close\">") == std::string::npos);
}

TEST_CASE("clicking a row's Use or Drop button asks for that item, and only consumables have Use", "[inventory_screen]")
{
    Fixture f;
    InventoryScreen screen(f.ui);
    screen.setVisible(true);
    screen.bind(buildInventoryScreen(sample(), ""));
    frame(f.ui, f.renderer, 0.1);
    frame(f.ui, f.renderer, 0.2);

    const auto click = [&](std::pair<float, float> at) {
        f.ui.mouseMove(static_cast<int>(at.first), static_cast<int>(at.second));
        frame(f.ui, f.renderer, 0.25);
        f.ui.mouseButton(0, true);
        f.ui.mouseButton(0, false);
        frame(f.ui, f.renderer, 0.3);
    };
    REQUIRE(screen.takeActions().empty());
    const auto use = screen.actionButtonCentre(0, InventoryAction::Kind::Use);
    REQUIRE(use);
    click(*use);
    const auto drop = screen.actionButtonCentre(2, InventoryAction::Kind::Drop);
    REQUIRE(drop);
    click(*drop);
    const std::vector<InventoryAction> actions = screen.takeActions();
    REQUIRE(actions.size() == 2);
    REQUIRE(actions[0] == InventoryAction{InventoryAction::Kind::Use, "tallow"});
    REQUIRE(actions[1] == InventoryAction{InventoryAction::Kind::Drop, "spear"});
    REQUIRE(screen.takeActions().empty());
    REQUIRE_FALSE(screen.actionButtonCentre(2, InventoryAction::Kind::Use));

    const auto close = screen.closeButtonCentre();
    REQUIRE(close);
    REQUIRE_FALSE(screen.takeCloseRequest());
    click(*close);
    REQUIRE(screen.takeCloseRequest());
}
