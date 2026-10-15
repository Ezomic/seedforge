#include "ichor/stage0/death_cause.h"
#include "ichor/stage0/fight_report.h"
#include "ichor/stage0/recorder.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <string>

using namespace ichor::stage0;

namespace {

DeathTally tallyOf(int unreadable, int offscreen, int stamina, int whiff, int block, int cornered)
{
    DeathTally t;
    const auto add = [&t](DeathCause c, int n) {
        for (int i = 0; i < n; ++i) t.add(c);
    };
    add(DeathCause::Unreadable, unreadable);
    add(DeathCause::OffScreen, offscreen);
    add(DeathCause::OutOfStamina, stamina);
    add(DeathCause::WhiffPunished, whiff);
    add(DeathCause::MistimedBlock, block);
    add(DeathCause::Cornered, cornered);
    return t;
}

const char* kHeader = "fight,seconds,gap_to_retry,loser,cause,p_hp,p_attacks,p_hits,p_whiffs,p_blocked,p_dodges,p_staggers,p_dealt,p_taken,"
                      "f_hp,f_attacks,f_hits,f_whiffs,f_blocked,f_dodges,f_staggers,f_dealt,f_taken\n";

std::string row(long long n, const char* loser, const char* cause, const char* gap = "2.50")
{
    return std::to_string(n) + ",12.00," + gap + "," + loser + "," + cause + ",0,5,2,3,1,4,0,60,100,40,6,3,2,0,0,0,100,60\n";
}

}

TEST_CASE("classifier keeps the doc 18 precedence")
{
    DeathFacts all;
    all.victimRecovering = all.blocked = all.outOfStamina = all.againstWall = true;
    CHECK(classifyDeath(all) == DeathCause::WhiffPunished);
    all.attackerOnScreen = false;
    CHECK(classifyDeath(all) == DeathCause::OffScreen);

    DeathFacts f;
    CHECK(classifyDeath(f) == DeathCause::Unreadable);
    f.againstWall = true;
    CHECK(classifyDeath(f) == DeathCause::Cornered);
    f.outOfStamina = true;
    CHECK(classifyDeath(f) == DeathCause::OutOfStamina);
    f.blocked = true;
    CHECK(classifyDeath(f) == DeathCause::MistimedBlock);
    f.victimRecovering = true;
    CHECK(classifyDeath(f) == DeathCause::WhiffPunished);
}

TEST_CASE("tally ignores out-of-range causes and reports shares")
{
    DeathTally t;
    CHECK(t.share(DeathCause::Unreadable) == 0.0);
    t.add(DeathCause::Unreadable);
    t.add(DeathCause::Cornered);
    t.add(static_cast<DeathCause>(200));
    CHECK(t.total() == 2);
    CHECK(t.count(static_cast<DeathCause>(200)) == 0);
    CHECK(t.share(DeathCause::Unreadable) == 0.5);
}

TEST_CASE("the distribution separates a telegraph failure from a design question")
{
    CHECK(diagnose(tallyOf(3, 2, 1, 0, 0, 0)) == Diagnosis::TooFewDeaths);
    CHECK(diagnose(tallyOf(6, 4, 1, 1, 0, 0)) == Diagnosis::Telegraph);
    CHECK(diagnose(tallyOf(1, 1, 8, 1, 0, 0)) == Diagnosis::Economy);
    CHECK(diagnose(tallyOf(1, 0, 1, 5, 3, 2)) == Diagnosis::Readable);
    CHECK(diagnose(tallyOf(3, 2, 3, 2, 1, 1)) == Diagnosis::Mixed);
    // Exactly half is not a majority.
    CHECK(diagnose(tallyOf(5, 0, 0, 5, 0, 0)) == Diagnosis::Mixed);
}

TEST_CASE("a removed classifier would go red: all-unreadable and all-readable differ")
{
    CHECK(diagnose(tallyOf(12, 0, 0, 0, 0, 0)) != diagnose(tallyOf(0, 0, 0, 12, 0, 0)));
}

TEST_CASE("parsing reads the recorder's own rows")
{
    FightRecorder recorder;
    recorder.noteAttack(true, 1.0);
    recorder.noteHit(false, 100.0f, false);
    recorder.noteDeath(true, DeathCause::OffScreen, 0.0f, 60.0f, 9.0);
    const auto rows = parseFightLog(std::string(kHeader) + recorder.lastRow());
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].fight == 1);
    CHECK(rows[0].playerLost);
    CHECK(rows[0].cause == DeathCause::OffScreen);
    CHECK(rows[0].seconds == 8.0);
    CHECK(rows[0].playerAttacks == 1);
    CHECK(rows[0].playerDamageTaken == 100.0);
}

TEST_CASE("hostile logs yield fewer rows, never a crash")
{
    CHECK(parseFightLog("").empty());
    CHECK(parseFightLog("\n\n\r\n").empty());
    CHECK(parseFightLog(kHeader).empty());
    CHECK(parseFightLog(std::string(1000, ',')).empty());
    CHECK(parseFightLog(row(1, "nobody", "stamina")).empty());
    CHECK(parseFightLog(row(1, "player", "gremlins")).empty());
    CHECK(parseFightLog(row(1, "player", "stamina", "nan")).empty());
    CHECK(parseFightLog(row(1, "player", "stamina", "inf")).empty());
    CHECK(parseFightLog(row(-1, "player", "stamina")).empty());
    CHECK(parseFightLog(row(99999999999, "player", "stamina")).empty());
    CHECK(parseFightLog(std::string("\xff\xfe\x00garbage,1,2", 17)).empty());
    std::string truncated = row(1, "player", "stamina");
    truncated.resize(truncated.size() / 2);
    CHECK(parseFightLog(truncated).empty());
    std::string good = row(1, "player", "stamina");
    CHECK(parseFightLog(good + "junk\n" + row(2, "foe", "unreadable")).size() == 2);
}

TEST_CASE("CRLF line endings parse")
{
    std::string text = std::string(kHeader) + row(1, "player", "unreadable");
    std::string crlf;
    for (const char c : text) {
        if (c == '\n') crlf += '\r';
        crlf += c;
    }
    CHECK(parseFightLog(crlf).size() == 1);
}

TEST_CASE("summary counts only player deaths in the distribution")
{
    std::string text = kHeader;
    for (int i = 0; i < 6; ++i) text += row(i + 1, "player", "unreadable");
    for (int i = 0; i < 5; ++i) text += row(i + 7, "player", "offscreen", "0.00");
    text += row(12, "foe", "stamina");
    text += row(13, "foe", "stamina");
    const FightSummary s = summarise(parseFightLog(text));
    CHECK(s.fights == 13);
    CHECK(s.playerLosses == 11);
    CHECK(s.playerWins == 2);
    CHECK(s.playerDeaths.total() == 11);
    CHECK(s.playerDeaths.count(DeathCause::OutOfStamina) == 0);
    CHECK(s.medianSeconds == 12.0);
    CHECK(s.medianGapToRetry == 2.5);
    CHECK(s.diagnosis == Diagnosis::Telegraph);
    const std::string report = formatReport(s);
    CHECK(report.find("unreadable") != std::string::npos);
    CHECK(report.find("telegraph problem") != std::string::npos);
    CHECK(report.find("stamina") == std::string::npos);
}

TEST_CASE("empty summary is quiet")
{
    const FightSummary s = summarise({});
    CHECK(s.fights == 0);
    CHECK(s.medianSeconds == 0.0);
    CHECK(s.diagnosis == Diagnosis::TooFewDeaths);
}

TEST_CASE("the recorder tallies player deaths only, and drops abandoned fights")
{
    FightRecorder recorder;
    recorder.noteAttack(true, 0.0);
    recorder.noteDeath(true, DeathCause::Unreadable, 0.0f, 50.0f, 5.0);
    recorder.noteAttack(true, 6.0);
    recorder.noteDeath(false, DeathCause::Unreadable, 50.0f, 0.0f, 9.0);
    recorder.noteAttack(true, 10.0);
    recorder.noteRetry(11.0);
    recorder.noteDeath(true, DeathCause::Cornered, 0.0f, 50.0f, 12.0);
    CHECK(recorder.playerDeaths().total() == 1);
    CHECK(recorder.playerDeaths().count(DeathCause::Unreadable) == 1);
}

TEST_CASE("classifier matches the original inline chain for every combination")
{
    for (int bits = 0; bits < 32; ++bits) {
        DeathFacts f;
        f.attackerOnScreen = (bits & 1) != 0;
        f.victimRecovering = (bits & 2) != 0;
        f.blocked = (bits & 4) != 0;
        f.outOfStamina = (bits & 8) != 0;
        f.againstWall = (bits & 16) != 0;
        DeathCause legacy = DeathCause::Unreadable;
        if (!f.attackerOnScreen) legacy = DeathCause::OffScreen;
        else if (f.victimRecovering) legacy = DeathCause::WhiffPunished;
        else if (f.blocked) legacy = DeathCause::MistimedBlock;
        else if (f.outOfStamina) legacy = DeathCause::OutOfStamina;
        else if (f.againstWall) legacy = DeathCause::Cornered;
        CHECK(classifyDeath(f) == legacy);
    }
}
