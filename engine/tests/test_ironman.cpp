#include "ichor/save/ironman.h"
#include "ichor/save/save_slots.h"
#include "ichor/sim/scene.h"
#include "test_temp.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>

using namespace ichor;

namespace {

class Dir {
public:
    Dir() : path_(test::tempPath("ironman_slots")) { std::filesystem::remove_all(path_); }
    ~Dir() { std::error_code ec; std::filesystem::remove_all(path_, ec); }
    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

}
using Action = IronmanRun::Action;
using Verdict = IronmanRun::Verdict;

TEST_CASE("a living ironman run refuses manual saves and mid-run reloads but autosaves")
{
    const IronmanRun run;
    REQUIRE(run.alive());
    REQUIRE(run.check(Action::ManualSave) == Verdict::NoManualSaves);
    REQUIRE(run.check(Action::LoadMidRun) == Verdict::NoReloadMidRun);
    REQUIRE(run.check(Action::Autosave) == Verdict::Allowed);
    REQUIRE(run.check(Action::Continue) == Verdict::Allowed);
    REQUIRE(run.check(Action::NewRun) == Verdict::Allowed);
}

TEST_CASE("death ends the run: no autosave over it, no reload, no continue")
{
    IronmanRun run;
    run.recordDeath(500);
    REQUIRE_FALSE(run.alive());
    REQUIRE(run.deathTick() == 500);
    REQUIRE(run.check(Action::Autosave) == Verdict::RunEnded);
    REQUIRE(run.check(Action::LoadMidRun) == Verdict::RunEnded);
    REQUIRE(run.check(Action::Continue) == Verdict::RunEnded);
    REQUIRE(run.check(Action::ManualSave) == Verdict::NoManualSaves);
    REQUIRE(run.check(Action::NewRun) == Verdict::Allowed);
}

TEST_CASE("the first death stands and a new run starts alive")
{
    IronmanRun run;
    run.recordDeath(10);
    run.recordDeath(99);
    REQUIRE(run.deathTick() == 10);
    run.startNewRun();
    REQUIRE(run.alive());
    REQUIRE(run.check(Action::Autosave) == Verdict::Allowed);
}

TEST_CASE("the death marker survives a round trip, including tick zero and the maximum")
{
    for (const std::uint64_t tick : {std::uint64_t{0}, std::uint64_t{42}, std::numeric_limits<std::uint64_t>::max()}) {
        IronmanRun run;
        run.recordDeath(tick);
        const auto back = IronmanRun::decode(run.encode());
        REQUIRE(back.has_value());
        REQUIRE(back->deathTick() == tick);
    }
    const auto alive = IronmanRun::decode(IronmanRun{}.encode() + "\r\n");
    REQUIRE(alive.has_value());
    REQUIRE(alive->alive());
}

TEST_CASE("hostile death markers are refused, never read as alive")
{
    const char* bad[] = {
        "",
        "dead",
        "ichor-ironman 1 alive extra",
        "ichor-ironman 2 alive",
        "ichor-ironman 1 dead",
        "ichor-ironman 1 dead ",
        "ichor-ironman 1 dead -1",
        "ichor-ironman 1 dead +5",
        "ichor-ironman 1 dead 007",
        "ichor-ironman 1 dead 5x",
        "ichor-ironman 1 dead 18446744073709551616",
        "ichor-ironman 1 dead 99999999999999999999999999",
        "ichor-ironman 1 dead 1.5",
    };
    for (const char* text : bad) {
        INFO(text);
        REQUIRE_FALSE(IronmanRun::decode(text).has_value());
    }
    REQUIRE_FALSE(IronmanRun::decode(std::string("ichor-ironman 1 dead 5\0x", 24)).has_value());
}

TEST_CASE("the rolling autosave keeps backups while manual slots stay refused")
{
    const Dir dir;
    Scene scene(5);
    SaveSlots slots(dir.path(), {true, 2});
    const SaveContext context{7, 1, 1};
    IronmanRun run;
    REQUIRE(run.check(Action::ManualSave) == Verdict::NoManualSaves);
    REQUIRE_FALSE(slots.save("quicksave", scene, context));
    for (int i = 0; i < 4; ++i) {
        REQUIRE(run.check(Action::Autosave) == Verdict::Allowed);
        REQUIRE(slots.save(SaveSlots::kIronmanSlot, scene, context));
    }
    REQUIRE(slots.list().size() == 1);
}
