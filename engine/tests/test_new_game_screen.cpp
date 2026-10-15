#include "ichor/data/axiom.h"
#include "ichor/data/slot_roll.h"
#include "ichor/ui/new_game_screen.h"
#include "ichor/ui/recording_renderer.h"
#include "ichor/ui/ui_context.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>
#define ICHOR_PID _getpid()
#else
#include <unistd.h>
#define ICHOR_PID getpid()
#endif

using namespace ichor;

namespace {

constexpr std::array<AxiomSlot, 5> kSlots = {AxiomSlot::Cataclysm, AxiomSlot::Law, AxiomSlot::Powers, AxiomSlot::Pressure, AxiomSlot::Anomaly};
constexpr int kPerSlot = 3;

std::string titleOf(AxiomSlot slot, int index)
{
    return "Title " + std::string(axiomSlotName(slot)) + " " + std::to_string(index);
}

std::string axiomJson(AxiomSlot slot, int index, const std::string& title)
{
    const std::string name(axiomSlotName(slot));
    return R"({"id": ")" + name + ".n" + std::to_string(index) + R"(", "slot": ")" + name + R"(", "title": ")" + title + R"(",
  "tags": ["WATER"], "conflicts": [],
  "terrain": { "sea_level_offset_m": 200 },
  "settlement": { "density_mult": 0.7 },
  "biomes": { "inject": ["salt_blight"] },
  "poi": { "weights": { "pharos": 2.0 } }
})";
}

// A data root holding three axioms for every slot, each titled after its slot.
class Pool {
public:
    Pool()
    {
        static int counter = 0;
        root_ = std::filesystem::temp_directory_path() / ("ichor_new_game_test_" + std::to_string(ICHOR_PID) + "_" + std::to_string(counter++));
        std::filesystem::remove_all(root_);
        std::filesystem::create_directories(root_ / "axioms");
        for (const AxiomSlot slot : kSlots) {
            for (int i = 0; i < kPerSlot; ++i) {
                write(std::string(axiomSlotName(slot)) + "_" + std::to_string(i) + ".json", axiomJson(slot, i, titleOf(slot, i)));
            }
        }
        REQUIRE(set.loadDirectory(root_));
        for (const auto& [id, axiom] : set.axioms()) {
            axioms.push_back(axiom);
        }
    }
    ~Pool() { std::filesystem::remove_all(root_); }
    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;

    void write(const std::string& file, const std::string& text) const { std::ofstream(root_ / "axioms" / file, std::ios::binary) << text; }

    AxiomSet set;
    std::vector<Axiom> axioms;

private:
    std::filesystem::path root_;
};

bool mentions(const std::string& text, AxiomSlot slot)
{
    return text.find("Title " + std::string(axiomSlotName(slot))) != std::string::npos;
}

struct Fixture {
    RecordingUiRenderer renderer;
    UiContext ui{renderer, 1280, 720};
    Fixture()
    {
        const std::filesystem::path fonts = std::filesystem::path(ICHOR_ART_DIR) / "fonts";
        REQUIRE(ui.ok());
        REQUIRE(ui.loadFontFace(fonts / "AlegreyaSans-Regular.ttf"));
        REQUIRE(ui.loadFontFace(fonts / "Cinzel.ttf"));
    }
    void frame(double time)
    {
        renderer.clearDraws();
        ui.update(time);
        ui.render();
    }
};

}

TEST_CASE("the new-game view shows slots A to C with their titles and never D or E, for every seed", "[new_game]")
{
    const Pool pool;
    for (std::uint64_t seed = 0; seed < 300; ++seed) {
        const SlotRollResult roll = rollSlots(seed, pool.axioms);
        REQUIRE(roll.ok());
        const NewGameView view = buildNewGameView(seed, roll, pool.set);
        REQUIRE(view.seed == seed);
        REQUIRE(view.rows.size() == 3);
        for (std::size_t i = 0; i < 3; ++i) {
            const SlotAssignment* assignment = roll.find(kSlots[i]);
            REQUIRE(assignment != nullptr);
            REQUIRE(view.rows[i].letter == axiomSlotLetter(kSlots[i]));
            REQUIRE(view.rows[i].title == pool.set.find(assignment->axiomId)->title);
            REQUIRE(mentions(view.rows[i].title, kSlots[i]));
        }
        // The roll did fill D (and often E), so a leak would have something to show.
        REQUIRE(roll.find(AxiomSlot::Pressure) != nullptr);
        for (const NewGameRow& row : view.rows) {
            REQUIRE_FALSE(mentions(row.title, AxiomSlot::Pressure));
            REQUIRE_FALSE(mentions(row.title, AxiomSlot::Anomaly));
            REQUIRE(row.slot != "Pressure");
            REQUIRE(row.slot != "Anomaly");
            REQUIRE(row.letter != 'D');
            REQUIRE(row.letter != 'E');
        }
    }
}

TEST_CASE("the new-game view hides D and E even when the roll marks them shown", "[new_game]")
{
    const Pool pool;
    SlotRollResult roll = rollSlots(7, pool.axioms);
    REQUIRE(roll.ok());
    for (SlotAssignment& assignment : roll.slots) {
        assignment.shown = true;
    }
    const NewGameView view = buildNewGameView(7, roll, pool.set);
    REQUIRE(view.rows.size() == 3);
    for (const NewGameRow& row : view.rows) {
        REQUIRE_FALSE(mentions(row.title, AxiomSlot::Pressure));
        REQUIRE_FALSE(mentions(row.title, AxiomSlot::Anomaly));
    }
}

TEST_CASE("a failed roll shows three placeholder rows and none of the roll's error text", "[new_game]")
{
    const Pool pool;
    const AxiomSet none;
    const SlotRollResult failed = rollSlots(1, {});
    REQUIRE_FALSE(failed.ok());
    const NewGameView view = buildNewGameView(1, failed, none);
    REQUIRE(view.rows.size() == 3);
    for (const NewGameRow& row : view.rows) {
        REQUIRE_FALSE(row.title.empty());
        REQUIRE(row.title.find(failed.message) == std::string::npos);
    }
    REQUIRE(view.rows[0].title == view.rows[2].title);

    // An axiom the data no longer has reads as a placeholder too.
    const SlotRollResult roll = rollSlots(1, pool.axioms);
    REQUIRE(buildNewGameView(1, roll, none).rows[1].title == view.rows[1].title);
}

TEST_CASE("the new-game screen draws nothing until shown, then lists the seed and slots A to C only", "[new_game]")
{
    const Pool pool;
    Fixture f;
    NewGameScreen screen(f.ui);
    REQUIRE(screen.ok());
    REQUIRE_FALSE(screen.visible());
    REQUIRE_FALSE(screen.beginCentre().has_value());

    const SlotRollResult roll = rollSlots(4242, pool.axioms);
    REQUIRE(roll.ok());
    screen.setView(buildNewGameView(4242, roll, pool.set));
    f.frame(0.0);
    REQUIRE(f.renderer.draws().empty());

    screen.setVisible(true);
    f.frame(0.1);
    const std::size_t withRows = f.renderer.draws().size();
    REQUIRE(withRows >= 5); // veil, card, rows, button, text

    screen.setView(NewGameView{});
    f.frame(0.15);
    REQUIRE(f.renderer.draws().size() < withRows); // the three rows and their text are gone
    screen.setView(buildNewGameView(4242, roll, pool.set));
    f.frame(0.18);
    REQUIRE(f.renderer.draws().size() == withRows);

    const std::string markup = screen.markup();
    REQUIRE(markup.find("4242") != std::string::npos);
    for (const AxiomSlot slot : {AxiomSlot::Cataclysm, AxiomSlot::Law, AxiomSlot::Powers}) {
        REQUIRE(markup.find(pool.set.find(roll.find(slot)->axiomId)->title) != std::string::npos);
    }
    REQUIRE_FALSE(mentions(markup, AxiomSlot::Pressure));
    REQUIRE_FALSE(mentions(markup, AxiomSlot::Anomaly));
    REQUIRE(markup.find("Slot D") == std::string::npos);
    REQUIRE(markup.find("Slot E") == std::string::npos);

    screen.setVisible(false);
    f.frame(0.2);
    REQUIRE(f.renderer.draws().empty());
}

TEST_CASE("an axiom title is shown as text, never as markup", "[new_game]")
{
    Fixture f;
    NewGameScreen screen(f.ui);
    NewGameView view;
    view.rows.push_back({'A', "Cataclysm", "<b>Flood</b> & \"ruin\""});
    screen.setView(view);
    const std::string markup = screen.markup();
    REQUIRE(markup.find("<b>") == std::string::npos);
    REQUIRE(markup.find("&lt;b&gt;") != std::string::npos);
}

TEST_CASE("the Begin button reports a click once, and a stale click is dropped on showing", "[new_game]")
{
    Fixture f;
    NewGameScreen screen(f.ui);
    screen.setView(NewGameView{});
    screen.setVisible(true);
    f.frame(0.0);
    const auto begin = screen.beginCentre();
    REQUIRE(begin.has_value());
    REQUIRE(std::abs(begin->first - 640.0f) < 2.0f); // centred

    const auto click = [&](double time) {
        f.ui.mouseMove(static_cast<int>(begin->first), static_cast<int>(begin->second));
        f.frame(time);
        f.ui.mouseButton(0, true);
        f.frame(time + 0.01);
        f.ui.mouseButton(0, false);
        f.frame(time + 0.02);
    };
    REQUIRE_FALSE(screen.takeBegin());
    click(0.1);
    REQUIRE(screen.takeBegin());
    REQUIRE_FALSE(screen.takeBegin());

    click(0.3);
    screen.setVisible(false);
    screen.setVisible(true);
    REQUIRE_FALSE(screen.takeBegin());
}
