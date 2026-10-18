#include "ichor/stage0/session_dashboard.h"

#include "ichor/stage0/session_ledger.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace ichor::stage0;

namespace {

const char* kHeader = "fight,seconds,gap_to_retry,loser,cause,p_hp,p_attacks,p_hits,p_whiffs,p_blocked,p_dodges,p_staggers,p_dealt,p_taken,"
                      "f_hp,f_attacks,f_hits,f_whiffs,f_blocked,f_dodges,f_staggers,f_dealt,f_taken\n";

std::string row(int n, const char* loser, const char* cause, const char* seconds)
{
    return std::to_string(n) + "," + seconds + ",2.50," + loser + "," + cause + ",0,5,2,3,1,4,0,60,100,40,6,3,2,0,0,0,100,60\n";
}

std::string table(const std::string& csv, const std::string& ledger)
{
    return formatDashboard(buildDashboard(parseFightLog(csv), ledger));
}

std::string withExtraColumns(const std::string& text, const char* extra)
{
    std::string out;
    std::size_t pos = 0;
    while (pos < text.size()) {
        const std::size_t end = text.find('\n', pos);
        out += text.substr(pos, end - pos) + extra + "\n";
        pos = end + 1;
    }
    return out;
}

struct Fixture {
    std::string csv = kHeader;
    std::string ledger = ledgerHeader();

    void fight(int n, const char* session, const char* variable, const char* loser, const char* cause, const char* seconds)
    {
        csv += row(n, loser, cause, seconds);
        ledger += ledgerRow(n, {session, variable});
    }
};

Fixture twoSessions()
{
    Fixture f;
    f.fight(5, "s0", "bug fix", "player", "unreadable", "9.00");
    for (int i = 21; i <= 30; ++i) {
        f.fight(i, "s1", "tell 0.15", "player", i % 2 ? "unreadable" : "offscreen", "10.00");
    }
    f.fight(31, "s1", "tell 0.15", "foe", "unknown", "30.00");
    f.fight(32, "s2", "stamina regen", "player", "stamina", "20.00");
    f.fight(33, "s2", "stamina regen", "player", "bleed", "10.00");
    f.fight(34, "s2", "stamina regen", "player", "cornered", "12.00");
    return f;
}

}

TEST_CASE("the golden table: sessions in fight order, deaths by cause, mean time to kill, diagnosis")
{
    const Fixture f = twoSessions();
    const std::string expected =
        "| # | Variable changed | Fights | Deaths by cause | Mean time to kill | Diagnosis |\n"
        "|---|---|---|---|---|---|\n"
        "| s1 | tell 0.15 | 11 | offscreen 5, unreadable 5 | 10.0 s | telegraph problem |\n"
        "| s2 | stamina regen | 3 | stamina 1, cornered 1, bleed 1 | 14.0 s | too few deaths |\n"
        "\n1 bug-fix window fights (1 to 20) are not counted.\n";
    CHECK(table(f.csv, f.ledger) == expected);
}

TEST_CASE("every death cause that occurs appears in the table")
{
    Fixture f;
    const char* causes[] = {"stamina", "whiff_punished", "cornered", "mistimed_block", "offscreen", "unreadable", "bleed", "unknown"};
    int n = 21;
    for (const char* cause : causes) {
        f.fight(n++, "s1", "v", "player", cause, "5.00");
    }
    const std::string out = table(f.csv, f.ledger);
    for (const char* cause : causes) {
        CHECK(out.find(std::string(cause) + " 1") != std::string::npos);
    }
}

TEST_CASE("the death cells add up to the fights the player lost")
{
    const Fixture f = twoSessions();
    const Dashboard d = buildDashboard(parseFightLog(f.csv), f.ledger);
    int total = 0;
    for (const SessionRow& s : d.sessions) {
        CHECK(s.deaths.total() == s.playerLosses);
        total += s.deaths.total();
    }
    CHECK(total == 13);
}

TEST_CASE("a missing sidecar groups every tuning fight into one unlabelled session")
{
    const Fixture f = twoSessions();
    const Dashboard d = buildDashboard(parseFightLog(f.csv), "");
    REQUIRE(d.sessions.size() == 1);
    CHECK(d.sessions[0].label == kUnlabelledSession);
    CHECK(d.sessions[0].fights == 14);
    CHECK(d.sessions[0].deaths.total() == 13);
}

TEST_CASE("extra columns in either file are tolerated")
{
    const Fixture f = twoSessions();
    CHECK(table(withExtraColumns(f.csv, ",extra,99"), withExtraColumns(f.ledger, ",x,y")) == table(f.csv, f.ledger));
}

TEST_CASE("hostile input never crashes and still yields a table")
{
    const std::string hostile[] = {
        "",
        "\n\n\n",
        ",,,,",
        std::string("\0\xff\xfe,,\n", 8),
        "1,nan,inf,player,stamina,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0\n",
        "99999999999999999999,1,1,player,stamina,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0\n",
        "garbage row without commas\n",
        std::string(100000, ',') + "\n",
        "1,2,3\n",
    };
    for (const std::string& csv : hostile) {
        for (const std::string& ledger : hostile) {
            CHECK(table(csv, ledger).starts_with("| # |"));
        }
    }
    const std::string mixed = std::string(kHeader) + "garbage\n" + row(30, "player", "stamina", "4.00");
    CHECK(table(mixed, "nonsense\n-1,x\n30,,\n").find("stamina 1") != std::string::npos);
}

TEST_CASE("repeated fight numbers count once and labels cannot break a row")
{
    Fixture f;
    f.fight(21, "s1", "v", "player", "stamina", "5.00");
    f.csv += row(21, "player", "stamina", "5.00");
    f.ledger += "21,s2,other,0\n";
    f.ledger += "22,a|b,x|y,0\n";
    f.csv += row(22, "foe", "unknown", "5.00");
    const Dashboard d = buildDashboard(parseFightLog(f.csv), f.ledger);
    int fights = 0;
    for (const SessionRow& s : d.sessions) {
        fights += s.fights;
    }
    CHECK(fights == 2);
    const std::string out = formatDashboard(d);
    CHECK(out.find("| a/b | x/y |") != std::string::npos);
}

TEST_CASE("an empty log still prints a table")
{
    CHECK(table("", "").find("no tuning fights") != std::string::npos);
}
