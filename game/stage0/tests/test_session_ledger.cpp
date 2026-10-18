#include "ichor/stage0/session_ledger.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace ichor::stage0;

namespace {

std::string ledger(int from, int to, const char* session)
{
    std::string text = ledgerHeader();
    for (int i = from; i <= to; ++i) {
        text += ledgerRow(i, {session, "stagger"});
    }
    return text;
}

}

TEST_CASE("the bug-fix window is exactly fights 1 to 20")
{
    CHECK_FALSE(isBugFixFight(0));
    CHECK_FALSE(isBugFixFight(-3));
    CHECK(isBugFixFight(1));
    CHECK(isBugFixFight(20));
    CHECK_FALSE(isBugFixFight(21));
}

TEST_CASE("ledger rows carry the marker and the window flag")
{
    CHECK(ledgerRow(20, {"s1", "poise"}) == "20,s1,poise,1\n");
    CHECK(ledgerRow(21, {"s2", "poise"}) == "21,s2,poise,0\n");
}

TEST_CASE("hostile labels cannot break a row")
{
    CHECK(cleanField("a,b\"c\nd\r\x01") == "abcd");
    CHECK(cleanField(std::string(500, 'x')).size() == kMaxLabelLength);
    const std::string row = ledgerRow(5, {"x,1,1\n99,evil", ",,"});
    CHECK(readProgress(row).recorded == 1);
}

TEST_CASE("progress counts toward 200 and splits the bug-fix window")
{
    const GateProgress none = readProgress(ledgerHeader());
    CHECK(none.recorded == 0);
    CHECK(none.remaining == 200);
    CHECK_FALSE(none.complete);

    const GateProgress some = readProgress(ledger(1, 50, "s1"));
    CHECK(some.recorded == 50);
    CHECK(some.bugFix == 20);
    CHECK(some.tuned == 30);
    CHECK(some.remaining == 150);

    const GateProgress early = readProgress(ledger(1, 7, "s1"));
    CHECK(early.bugFix == 7);
    CHECK(early.tuned == 0);
}

TEST_CASE("progress completes at 200 and never goes negative")
{
    const GateProgress done = readProgress(ledger(1, 200, "s1"));
    CHECK(done.complete);
    CHECK(done.remaining == 0);
    const GateProgress over = readProgress(ledger(1, 230, "s1"));
    CHECK(over.remaining == 0);
    CHECK(over.recorded == 230);
}

TEST_CASE("progress ignores junk, repeats and a forged flag column")
{
    std::string text = ledger(1, 3, "s1");
    text += "2,s9,other,0\n";
    text += "abc,s1,x,0\n";
    text += "0,s1,x,0\n";
    text += "-4,s1,x,0\n";
    text += "no commas\n";
    text += "\n";
    text += "30,s1,x,1\r\n";
    const GateProgress p = readProgress(text);
    CHECK(p.recorded == 4);
    CHECK(p.bugFix == 3);
    CHECK(p.tuned == 1);
}

TEST_CASE("progress lists the distinct sessions")
{
    const GateProgress p = readProgress(ledger(1, 5, "s1") + ledger(6, 9, "s2"));
    CHECK(p.sessions.size() == 2);
    CHECK(p.sessions.contains("s2"));
    CHECK(progressLine(p) == "9/200 fights (9 bug-fix window, 0 tuning), 191 to go, 2 sessions");
}
