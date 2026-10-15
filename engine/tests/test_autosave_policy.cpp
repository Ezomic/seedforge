#include "ichor/save/autosave_policy.h"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

using namespace ichor;

namespace {

AutosavePolicy policy()
{
    AutosavePolicy p;
    p.rules = {{"enter", "settlement_enter", 10}, {"quest", "quest_resolved", 0}, {"betray", "betrayed", 50}};
    p.minGapTicks = 100;
    p.timerTicks = 1000;
    p.slots = 2;
    return p;
}

AutosaveDecision step(const AutosavePolicy& p, const AutosaveState& s, const std::vector<AutosaveEvent>& events, std::uint64_t now)
{
    return decideAutosave(p, s, events, now);
}

}

TEST_CASE("an event with no rule never saves, including the moment before a choice", "[autosave_policy]")
{
    const AutosaveDecision d = step(policy(), {}, {{"choice_offered", 500}, {"betrayal_pending", 500}}, 500);
    CHECK_FALSE(d.save);
    CHECK(d.state.pending.empty());
}

TEST_CASE("a consequence saves only after its delay, not on the frame it happened", "[autosave_policy]")
{
    const AutosavePolicy p = policy();
    AutosaveDecision d = step(p, {}, {{"betrayed", 200}}, 200);
    CHECK_FALSE(d.save);
    CHECK(d.state.pending.size() == 1);
    d = step(p, d.state, {}, 249);
    CHECK_FALSE(d.save);
    d = step(p, d.state, {}, 250);
    REQUIRE(d.save);
    CHECK(d.reason == "betrayed");
    CHECK(d.slot == "autosave_01");
    CHECK(d.state.pending.empty());
    CHECK(d.state.lastSaveTick == 250);
}

TEST_CASE("a burst inside the minimum gap becomes one later save", "[autosave_policy]")
{
    const AutosavePolicy p = policy();
    AutosaveDecision d = step(p, {}, {{"quest_resolved", 300}}, 300);
    REQUIRE(d.save);
    d = step(p, d.state, {{"quest_resolved", 310}, {"betrayed", 320}}, 360);
    CHECK_FALSE(d.save);
    CHECK(d.state.pending.size() == 2);
    d = step(p, d.state, {}, 399);
    CHECK_FALSE(d.save);
    d = step(p, d.state, {}, 400);
    REQUIRE(d.save);
    CHECK(d.reason == "quest_resolved");
    CHECK(d.state.pending.empty());
    CHECK_FALSE(step(p, d.state, {}, 450).save);
}

TEST_CASE("events not yet due survive a save for an earlier one", "[autosave_policy]")
{
    const AutosavePolicy p = policy();
    const AutosaveDecision d = step(p, {}, {{"quest_resolved", 300}, {"betrayed", 300}}, 300);
    REQUIRE(d.save);
    CHECK(d.reason == "quest_resolved");
    REQUIRE(d.state.pending.size() == 1);
    CHECK(d.state.pending[0].reason == "betrayed");
}

TEST_CASE("slots rotate and wrap", "[autosave_policy]")
{
    const AutosavePolicy p = policy();
    AutosaveState s;
    std::vector<std::string> slots;
    for (std::uint64_t t = 1000; t < 1600; t += 200) {
        const AutosaveDecision d = step(p, s, {{"quest_resolved", t}}, t);
        REQUIRE(d.save);
        slots.push_back(d.slot);
        s = d.state;
    }
    CHECK(slots == std::vector<std::string>{"autosave_01", "autosave_02", "autosave_01"});
}

TEST_CASE("the timer saves after a quiet stretch and not before", "[autosave_policy]")
{
    const AutosavePolicy p = policy();
    CHECK_FALSE(step(p, {}, {}, 999).save);
    const AutosaveDecision d = step(p, {}, {}, 1000);
    REQUIRE(d.save);
    CHECK(d.reason == "timer");
    CHECK_FALSE(step(p, d.state, {}, 1999).save);
    CHECK(step(p, d.state, {}, 2000).save);
}

TEST_CASE("timerTicks of zero turns the timer off", "[autosave_policy]")
{
    AutosavePolicy p = policy();
    p.timerTicks = 0;
    CHECK_FALSE(step(p, {}, {}, 1ULL << 40).save);
}

TEST_CASE("a clock that went backwards after a load does not block saving forever", "[autosave_policy]")
{
    const AutosavePolicy p = policy();
    AutosaveState s;
    s.saved = true;
    s.lastSaveTick = 5000;
    CHECK(step(p, s, {{"quest_resolved", 10}}, 10).save);
}

TEST_CASE("extreme ticks and delays do not overflow", "[autosave_policy]")
{
    AutosavePolicy p = policy();
    p.rules.push_back({"far", "far_event", std::numeric_limits<std::uint64_t>::max()});
    const std::uint64_t big = std::numeric_limits<std::uint64_t>::max();
    const AutosaveDecision d = step(p, {}, {{"far_event", big - 1}, {"quest_resolved", big - 1}}, big - 1);
    REQUIRE(d.save);
    CHECK(d.reason == "quest_resolved");
    REQUIRE(d.state.pending.size() == 1);
    CHECK(d.state.pending[0].dueTick == big);

    AutosaveState s;
    s.saved = true;
    s.lastSaveTick = big;
    CHECK_FALSE(step(p, s, {{"quest_resolved", big}}, big).save);
}

TEST_CASE("pending events are bounded", "[autosave_policy]")
{
    AutosavePolicy p = policy();
    p.minGapTicks = 1ULL << 40;
    AutosaveState s;
    s.saved = true;
    s.lastSaveTick = 5;
    const std::vector<AutosaveEvent> flood(10000, AutosaveEvent{"quest_resolved", 6});
    const AutosaveDecision d = step(p, s, flood, 7);
    CHECK_FALSE(d.save);
    CHECK(d.state.pending.size() == AutosaveState::kMaxPending);
}

TEST_CASE("a hostile state is trimmed and its slot index wraps", "[autosave_policy]")
{
    AutosaveState s;
    s.pending.assign(5000, PendingAutosave{"x", std::numeric_limits<std::uint64_t>::max()});
    s.nextSlot = 4000000001u;
    const AutosaveDecision d = step(policy(), s, {}, 5000);
    REQUIRE(d.save);
    CHECK(d.state.pending.size() <= AutosaveState::kMaxPending);
    CHECK(d.slot == "autosave_02");
}

TEST_CASE("the same inputs always give the same decision", "[autosave_policy]")
{
    const AutosavePolicy p = policy();
    const std::vector<AutosaveEvent> events{{"settlement_enter", 5}, {"betrayed", 7}};
    CHECK(step(p, {}, events, 100).state == step(p, {}, events, 100).state);
}

TEST_CASE("ledger facts become events and consequential ones trigger a save", "[autosave_policy]")
{
    ConsequenceLedger ledger;
    REQUIRE(ledger.append({"f1", "saltmarsh", "burned", "true", 10}) == AppendResult::Ok);
    REQUIRE(ledger.append({"f2", "ilse", "betrayed", "harl", 20}) == AppendResult::Ok);
    const std::vector<AutosaveEvent> events = eventsFromLedger(ledger, 1);
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == "betrayed");
    CHECK(events[0].tick == 20);
    CHECK(eventsFromLedger(ledger, 0).size() == 2);
    CHECK(eventsFromLedger(ledger, 99).empty());
    const AutosaveDecision d = step(policy(), {}, events, 70);
    REQUIRE(d.save);
    CHECK(d.reason == "betrayed");
}

TEST_CASE("policy json parses and rejects bad input", "[autosave_policy]")
{
    const auto ok = AutosavePolicy::parse(R"([
        {"id":"timing","kind":"timing","minGapTicks":10,"timerTicks":0,"slots":4},
        {"id":"r1","kind":"rule","event":"quest_resolved","delayTicks":5}])");
    REQUIRE(ok);
    CHECK(ok->minGapTicks == 10);
    CHECK(ok->timerTicks == 0);
    CHECK(ok->slots == 4);
    REQUIRE(ok->rules.size() == 1);
    CHECK(ok->rules[0] == AutosaveRule{"r1", "quest_resolved", 5});

    for (const char* bad : {"", "{}", "[1]", "not json", R"([{"id":"a","kind":"rule"}])",
             R"([{"id":"a","kind":"rule","event":"Bad Event","delayTicks":1}])",
             R"([{"id":"a","kind":"rule","event":"x","delayTicks":-1}])",
             R"([{"id":"a","kind":"rule","event":"x","delayTicks":1.5}])",
             R"([{"id":"a","kind":"rule","event":"x","delayTicks":1e30}])",
             R"([{"id":"a","kind":"rule","event":"x","delayTicks":"1"}])",
             R"([{"id":"a","kind":"rule","event":"x"},{"id":"b","kind":"rule","event":"x"}])",
             R"([{"id":"a","kind":"rule","event":"x"},{"id":"a","kind":"rule","event":"y"}])",
             R"([{"id":"t","kind":"timing","slots":0}])",
             R"([{"id":"t","kind":"timing","slots":100}])",
             R"([{"id":"t","kind":"timing","slots":2},{"id":"u","kind":"timing","slots":2}])",
             R"([{"id":"a","kind":"other"}])"}) {
        INFO(bad);
        CHECK_FALSE(AutosavePolicy::parse(bad));
    }
    CHECK_FALSE(AutosavePolicy::parse(std::string(AutosavePolicy::kMaxBytes + 1, ' ')));
}

TEST_CASE("the shipped rules file parses and has no rule for the moment before a choice", "[autosave_policy]")
{
    std::ifstream in(std::string(ICHOR_REPO_DATA) + "/save/autosave_rules.json", std::ios::binary);
    REQUIRE(in);
    const std::string text((std::istreambuf_iterator<char>(in)), {});
    const auto p = AutosavePolicy::parse(text);
    REQUIRE(p);
    CHECK_FALSE(p->rules.empty());
    for (const AutosaveRule& rule : p->rules) {
        CHECK(rule.event.find("offered") == std::string::npos);
        CHECK(rule.event.find("pending") == std::string::npos);
    }
}

TEST_CASE("a load that rewinds the clock drops old pending entries and restarts the timer", "[autosave_policy]")
{
    const AutosavePolicy p = policy();
    AutosaveState s;
    s.saved = true;
    s.lastSaveTick = 5000;
    s.pending = {{"betrayed", 20}};
    const AutosaveDecision d = step(p, s, {}, 30);
    CHECK_FALSE(d.save);
    CHECK(d.state.pending.empty());
    CHECK_FALSE(step(p, d.state, {}, 1029).save);
    CHECK(step(p, d.state, {}, 1030).save);
}

TEST_CASE("far future entries cannot starve new events", "[autosave_policy]")
{
    AutosaveState s;
    s.pending.assign(AutosaveState::kMaxPending, PendingAutosave{"x", 1ULL << 50});
    const AutosaveDecision d = step(policy(), s, {{"quest_resolved", 10}}, 10);
    REQUIRE(d.save);
    CHECK(d.reason == "quest_resolved");
}

TEST_CASE("the earliest due event names the save", "[autosave_policy]")
{
    const AutosaveDecision d = step(policy(), {}, {{"betrayed", 100}, {"quest_resolved", 100}}, 200);
    REQUIRE(d.save);
    CHECK(d.reason == "quest_resolved");
}
