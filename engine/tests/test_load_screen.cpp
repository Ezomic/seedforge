#include "ichor/save/save_slots.h"
#include "ichor/sim/scene.h"
#include "ichor/ui/load_screen.h"
#include "ichor/ui/recording_renderer.h"
#include "ichor/ui/ui_context.h"
#include "test_temp.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

using namespace ichor;

namespace {

constexpr double kDt = 1.0 / 60.0;
const SaveContext kContext{77, 1, 1234};

class Dir {
public:
    Dir()
    {
        static int counter = 0;
        path_ = test::tempPath("ichor_load_screen_test_" + std::to_string(counter++));
        std::filesystem::remove_all(path_);
    }
    ~Dir() { std::filesystem::remove_all(path_); }
    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

void saveAt(SaveSlots& slots, const char* name, int ticks)
{
    Scene scene(5);
    for (int i = 0; i < ticks; ++i) {
        scene.tick(kDt);
    }
    REQUIRE(slots.save(name, scene, kContext));
}

void writeFile(const std::filesystem::path& path, const std::string& bytes)
{
    std::ofstream(path, std::ios::binary) << bytes;
}

LoadView viewOf(const SaveSlots& slots)
{
    return buildLoadView(slots.listAll(), "The Drowned Reach", kDt, [&slots](const std::string& name) {
        std::error_code ec;
        const std::filesystem::path picture = slots.thumbnailPath(name);
        return std::filesystem::is_regular_file(picture, ec) ? picture : std::filesystem::path();
    });
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
    void click(std::pair<float, float> at, double time)
    {
        ui.mouseMove(static_cast<int>(at.first), static_cast<int>(at.second));
        frame(time);
        ui.mouseButton(0, true);
        frame(time + 0.01);
        ui.mouseButton(0, false);
        frame(time + 0.02);
    }
};

}

TEST_CASE("playtime is hours, minutes and seconds of simulated play", "[load_screen]")
{
    CHECK(playtimeText(0, kDt) == "0:00:00");
    CHECK(playtimeText(60 * 3725, kDt) == "1:02:05");
    CHECK(playtimeText(100, 0.0) == "0:00:00");
}

TEST_CASE("the listing shows each slot with the world title, playtime and a thumbnail, a slot without one gets the placeholder", "[load_screen]")
{
    const Dir dir;
    SaveSlots slots(dir.path());
    saveAt(slots, "autosave", 60 * 90);
    saveAt(slots, "quicksave", 60 * 3700);
    writeFile(slots.thumbnailPath("quicksave"), "png");

    const LoadView view = viewOf(slots);
    REQUIRE(view.rows.size() == 2);
    CHECK(view.rows[0].slot == "autosave");
    CHECK(view.rows[0].title == "The Drowned Reach");
    CHECK(view.rows[0].playtime == "0:01:30");
    CHECK(view.rows[0].thumbnail.empty());
    CHECK(view.rows[1].slot == "quicksave");
    CHECK(view.rows[1].playtime == "1:01:40");
    CHECK(view.rows[1].thumbnail.ends_with("quicksave.png"));
    CHECK_FALSE(view.rows[0].damaged);
}

TEST_CASE("the picture lives beside the slot and the slot file is the same with or without it", "[load_screen]")
{
    const Dir dir;
    SaveSlots slots(dir.path());
    saveAt(slots, "quicksave", 30);
    const auto sizeBefore = std::filesystem::file_size(dir.path() / "quicksave.sav");
    writeFile(slots.thumbnailPath("quicksave"), "png");
    CHECK(slots.thumbnailPath("quicksave").parent_path() == dir.path());
    CHECK(slots.thumbnailPath("quicksave") != dir.path() / "quicksave.sav");
    CHECK(std::filesystem::file_size(dir.path() / "quicksave.sav") == sizeBefore);
    CHECK(slots.list().size() == 1);
    CHECK(slots.listAll().size() == 1);
}

TEST_CASE("a damaged slot is listed as damaged, not hidden, and one with a good backup says which backup loads", "[load_screen]")
{
    const Dir dir;
    SaveSlots slots(dir.path());
    saveAt(slots, "good", 10);
    saveAt(slots, "half", 10);
    saveAt(slots, "half", 20);
    std::filesystem::resize_file(dir.path() / "half.sav", std::filesystem::file_size(dir.path() / "half.sav") / 2);
    std::filesystem::create_directories(dir.path());
    writeFile(dir.path() / "broken.sav", "not a save at all");

    CHECK(slots.list().size() == 2); // the plain listing still leaves the damaged one out
    const LoadView view = viewOf(slots);
    REQUIRE(view.rows.size() == 3);
    CHECK(view.rows[0].slot == "broken");
    CHECK(view.rows[0].damaged);
    CHECK(view.rows[0].playtime.empty());
    CHECK(view.rows[0].thumbnail.empty());
    CHECK(view.rows[1].slot == "good");
    CHECK(view.rows[1].note.empty());
    CHECK(view.rows[2].slot == "half");
    CHECK_FALSE(view.rows[2].damaged);
    CHECK(view.rows[2].note.find("backup 1") != std::string::npos);
}

TEST_CASE("the load screen draws nothing until shown, lists the rows escaped, and reports a chosen slot once", "[load_screen]")
{
    Fixture f;
    LoadScreen screen(f.ui);
    REQUIRE(screen.ok());
    LoadView view;
    view.rows.push_back({"one", "The <b>Reach</b>", "0:10:00", "", "", false});
    view.rows.push_back({"two", "Damaged save", "", "", "This slot cannot be read and cannot be loaded.", true});
    screen.setView(view);
    f.frame(0.0);
    CHECK(f.renderer.draws().empty());
    CHECK_FALSE(screen.rowCentre(0).has_value());

    screen.setVisible(true);
    f.frame(0.1);
    CHECK(f.renderer.draws().size() >= 5);
    const std::string markup = screen.markup();
    CHECK(markup.find("<b>") == std::string::npos);
    CHECK(markup.find("No picture") != std::string::npos);
    CHECK(markup.find("Played 0:10:00") != std::string::npos);
    CHECK(markup.find("cannot be loaded") != std::string::npos);

    REQUIRE(screen.rowCentre(1).has_value());
    f.click(*screen.rowCentre(1), 0.2);
    CHECK_FALSE(screen.takeChosen().has_value()); // a damaged slot cannot be chosen

    REQUIRE(screen.rowCentre(0).has_value());
    f.click(*screen.rowCentre(0), 0.3);
    CHECK(screen.takeChosen() == std::optional<std::string>("one"));
    CHECK_FALSE(screen.takeChosen().has_value());

    REQUIRE(screen.backCentre().has_value());
    f.click(*screen.backCentre(), 0.4);
    CHECK(screen.takeBack());
    CHECK_FALSE(screen.takeBack());

    screen.setVisible(false);
    f.frame(0.5);
    CHECK(f.renderer.draws().empty());
}

TEST_CASE("a thumbnail row asks for the picture and an empty list says so", "[load_screen]")
{
    Fixture f;
    LoadScreen screen(f.ui);
    LoadView view;
    view.rows.push_back({"one", "Reach", "0:00:01", "C:/saves/one.png", "", false});
    screen.setView(view);
    CHECK(screen.markup().find("<img") != std::string::npos);
    CHECK(screen.markup().find("C:/saves/one.png") != std::string::npos);
    screen.setView(LoadView{});
    CHECK(screen.markup().find("No saved games yet.") != std::string::npos);
}
