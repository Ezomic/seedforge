#include "ichor/sim/scene.h"
#include "ichor/stage0/combat.h"
#include "ichor/stage0/fight_review.h"
#include "ichor/stage0/recorder.h"
#include "../../../engine/tests/test_temp.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <vector>

using namespace ichor;
using namespace ichor::stage0;

namespace {

constexpr double kDt = 1.0 / 60.0;

void clean(const std::filesystem::path& csv)
{
    std::error_code ec;
    std::filesystem::remove(csv, ec);
    std::filesystem::remove(std::filesystem::path(csv).replace_extension(".sessions.csv"), ec);
    std::filesystem::remove_all(fightRecordDirectory(csv), ec);
}

void writeBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void finishFight(FightRecorder& recorder, double start, bool playerDied)
{
    recorder.noteAttack(true, start);
    recorder.setClock(start + 0.5);
    recorder.noteHit(true, 12.5f, false);
    recorder.noteDeath(playerDied, DeathCause::WhiffPunished, 0.0f, 0.0f, start + 1.0);
}

}

TEST_CASE("a fight is saved the moment it ends and replays to the same cause and outcome")
{
    const auto csv = ichor::test::tempPath("ichor_review.csv");
    clean(csv);
    Scene scene(6);
    Stage0 stage{Tuning{}};
    stage.install(scene, {0.0, 0.0, 3.0}, {0.0, 0.0, -2.0});
    scene.tick(kDt);
    stage.resetFight(scene);
    stage.recorder().open(csv);
    const entt::entity player = scene.find(stage.playerId());
    for (int i = 0; i < 60 * 60 && scene.registry().get<Combatant>(player).alive(); ++i) {
        scene.tick(kDt);
    }
    REQUIRE(stage.recorder().fightsWritten() == 1);

    // No clean exit happened: the record is already on disk.
    const auto path = fightRecordPath(fightRecordDirectory(csv), 1);
    const auto record = loadFightRecord(path);
    REQUIRE(record.has_value());
    CHECK(record->index == 1);
    CHECK(record->loser == FightSide::Player);
    CHECK(record->cause == stage.recorder().lastCause());
    CHECK(record->cause == static_cast<DeathCause>(scene.registry().get<Combatant>(player).deathCause));
    CHECK_FALSE(record->events.empty());
    const std::string text = describeFight(*record);
    CHECK(text.find(std::string("player died (") + name(record->cause) + ")") != std::string::npos);
    CHECK(reviewFight(path) == 0);
    CHECK_FALSE(std::filesystem::exists(std::filesystem::path(path) += ".tmp"));
    clean(csv);
}

TEST_CASE("only the first twenty fights are kept and an abandoned one is not saved")
{
    const auto csv = ichor::test::tempPath("ichor_review_cap.csv");
    clean(csv);
    FightRecorder recorder;
    recorder.open(csv);
    recorder.noteAttack(true, 0.0);
    recorder.noteRetry(1.0);
    for (int i = 0; i < 25; ++i) {
        finishFight(recorder, 10.0 * (i + 1), i % 2 == 0);
    }
    const auto dir = fightRecordDirectory(csv);
    for (std::uint32_t n = 1; n <= 25; ++n) {
        CHECK(std::filesystem::exists(fightRecordPath(dir, n)) == (n <= kMaxRecordedFights));
    }
    const auto first = loadFightRecord(fightRecordPath(dir, 1));
    REQUIRE(first.has_value());
    CHECK(first->loser == FightSide::Player);
    CHECK(first->cause == DeathCause::WhiffPunished);
    CHECK(summarize(*first).playerDamageTenths == 125);
    clean(csv);
}

TEST_CASE("a later session does not overwrite or exceed the first twenty")
{
    const auto csv = ichor::test::tempPath("ichor_review_resume.csv");
    clean(csv);
    {
        FightRecorder first;
        first.open(csv);
        for (int i = 0; i < 18; ++i) finishFight(first, 10.0 * (i + 1), true);
    }
    FightRecorder second;
    second.open(csv);
    for (int i = 0; i < 5; ++i) finishFight(second, 10.0 * (i + 1), false);
    const auto dir = fightRecordDirectory(csv);
    CHECK(loadFightRecord(fightRecordPath(dir, 18))->loser == FightSide::Player);
    CHECK(loadFightRecord(fightRecordPath(dir, 19))->loser == FightSide::Foe);
    CHECK(loadFightRecord(fightRecordPath(dir, 20))->index == 20);
    CHECK_FALSE(std::filesystem::exists(fightRecordPath(dir, 21)));
    clean(csv);
}

TEST_CASE("a truncated, corrupted, oversized or missing record is refused without a crash")
{
    FightRecord r;
    r.index = 2;
    r.durationMs = 1000;
    r.events = {{0, FightEventKind::Attack, FightSide::Player, 0}, {500, FightEventKind::Hit, FightSide::Player, 40}};
    const auto path = ichor::test::tempPath("ichor_review_bad.ifrd");
    REQUIRE(saveFightRecord(path, r));
    REQUIRE(loadFightRecord(path).has_value());

    const std::vector<std::uint8_t> bytes = encode(r);
    for (std::size_t n = 0; n < bytes.size(); ++n) {
        writeBytes(path, std::vector<std::uint8_t>(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(n)));
        CHECK_FALSE(loadFightRecord(path).has_value());
        CHECK(reviewFight(path) == 1);
    }
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        std::vector<std::uint8_t> bad = bytes;
        bad[i] = 0xFF;
        writeBytes(path, bad);
        const auto loaded = loadFightRecord(path);
        if (loaded) {
            CHECK(loaded->events.size() <= kMaxFightEvents); // a flip the codec cannot see is still bounded
        }
    }
    writeBytes(path, std::vector<std::uint8_t>(1 << 20, 0x41));
    CHECK_FALSE(loadFightRecord(path).has_value());
    std::filesystem::remove(path);
    CHECK_FALSE(loadFightRecord(path).has_value());
    CHECK(reviewFight(path) == 1);
    CHECK_FALSE(loadFightRecord(std::filesystem::temp_directory_path()).has_value());
}
