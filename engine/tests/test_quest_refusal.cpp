#include "ichor/graph/quest_refusal.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace ichor::graph::questrefusal;

namespace {

Option accept(std::uint32_t next)
{
    return {Choice::Accept, next, ""};
}

// Offer (1) -> obtain (2, rival 7) -> deliver (3).
Quest bareQuest()
{
    Quest q;
    q.start = 1;
    q.steps = {
        {1, 0, {accept(2)}},
        {2, 7, {accept(3)}},
        {3, 7, {accept(0)}},
    };
    return q;
}

bool has(const std::vector<Finding>& f, Code c, std::uint32_t step = 0)
{
    return std::any_of(f.begin(), f.end(), [&](const Finding& x) { return x.code == c && (step == 0 || x.step == step); });
}

const Option* find(const Quest& q, std::uint32_t step, Choice c)
{
    for (const Step& s : q.steps)
        if (s.id == step)
            for (const Option& o : s.options)
                if (o.choice == c)
                    return &o;
    return nullptr;
}

} // namespace

TEST_CASE("a quest with no refusal options fails on every count", "[questrefusal]")
{
    const auto f = validate(bareQuest());
    CHECK(has(f, Code::NoDecline, 1));
    CHECK(has(f, Code::NoWalkAway, 2));
    CHECK(has(f, Code::NoWalkAway, 3));
    CHECK(has(f, Code::NoTellOtherSide, 2));
    CHECK(has(f, Code::NoTellOtherSide, 3));
}

TEST_CASE("ensureRefusals gives every step a way out and the result validates", "[questrefusal]")
{
    const Result r = ensureRefusals(bareQuest());
    CHECK(r.findings.empty());
    CHECK(validate(r.quest).empty());

    const Option* decline = find(r.quest, 1, Choice::Decline);
    REQUIRE(decline != nullptr);
    CHECK(decline->next == 0);
    CHECK(decline->consequence == "declined:1");
    CHECK(find(r.quest, 1, Choice::WalkAway) == nullptr);
    CHECK(find(r.quest, 1, Choice::TellOtherSide) == nullptr);

    for (std::uint32_t id : {2u, 3u}) {
        const Option* walk = find(r.quest, id, Choice::WalkAway);
        REQUIRE(walk != nullptr);
        CHECK(walk->next == 0);
        CHECK(!walk->consequence.empty());
        const Option* tell = find(r.quest, id, Choice::TellOtherSide);
        REQUIRE(tell != nullptr);
        CHECK(tell->consequence == "told:7");
    }
}

TEST_CASE("ensureRefusals leaves the accept path untouched and is idempotent", "[questrefusal]")
{
    const Quest original = bareQuest();
    const Result once = ensureRefusals(original);
    for (std::size_t i = 0; i < original.steps.size(); ++i) {
        REQUIRE(once.quest.steps[i].options.front().choice == Choice::Accept);
        CHECK(once.quest.steps[i].options.front().next == original.steps[i].options.front().next);
    }
    const Result twice = ensureRefusals(once.quest);
    CHECK(twice.findings.empty());
    for (std::size_t i = 0; i < once.quest.steps.size(); ++i)
        CHECK(twice.quest.steps[i].options.size() == once.quest.steps[i].options.size());
}

TEST_CASE("a quest with no other side cannot be repaired", "[questrefusal]")
{
    Quest q = bareQuest();
    for (Step& s : q.steps)
        s.otherSide = 0;
    const Result r = ensureRefusals(q);
    CHECK(has(r.findings, Code::NoOtherSide));
    CHECK(find(r.quest, 1, Choice::Decline) != nullptr);
}

TEST_CASE("refusals must end the quest and leave a consequence", "[questrefusal]")
{
    Quest q = ensureRefusals(bareQuest()).quest;
    q.steps[1].options[1].next = 3; // walk away that continues
    q.steps[0].options[1].consequence.clear();
    const auto f = validate(q);
    CHECK(has(f, Code::RefusalContinues, 2));
    CHECK(has(f, Code::RefusalNoConsequence, 1));
}

TEST_CASE("decline is only valid at the start; tell needs a party", "[questrefusal]")
{
    Quest q = ensureRefusals(bareQuest()).quest;
    q.steps[1].options.push_back({Choice::Decline, 0, "x"});
    q.steps[0].options.push_back({Choice::TellOtherSide, 0, "y"});
    const auto f = validate(q);
    CHECK(has(f, Code::DeclineAfterStart, 2));
    CHECK(has(f, Code::TellWithoutOtherSide, 1));
}

TEST_CASE("unreachable steps are reported, not repaired", "[questrefusal]")
{
    Quest q = bareQuest();
    q.steps.push_back({9, 7, {accept(0)}});
    const Result r = ensureRefusals(q);
    CHECK(has(r.findings, Code::Unreachable, 9));
    CHECK(find(r.quest, 9, Choice::WalkAway) == nullptr);
}

TEST_CASE("structural problems are reported and nothing is changed", "[questrefusal]")
{
    SECTION("empty")
    {
        CHECK(has(validate(Quest{}), Code::Empty));
    }
    SECTION("duplicate and zero ids")
    {
        Quest q = bareQuest();
        q.steps.push_back({2, 0, {}});
        q.steps.push_back({0, 0, {}});
        const auto f = validate(q);
        CHECK(has(f, Code::DuplicateId));
        CHECK(has(f, Code::BadId));
    }
    SECTION("dangling next and missing start")
    {
        Quest q = bareQuest();
        q.steps[1].options[0].next = 99;
        q.start = 42;
        const Result r = ensureRefusals(q);
        CHECK(has(r.findings, Code::DanglingNext, 2));
        CHECK(has(r.findings, Code::NoStart));
        CHECK(r.quest.steps[0].options.size() == 1);
    }
    SECTION("duplicate choice")
    {
        Quest q = bareQuest();
        q.steps[0].options.push_back(accept(3));
        CHECK(has(validate(q), Code::DuplicateChoice, 1));
    }
}

TEST_CASE("hostile sizes are rejected before traversal", "[questrefusal]")
{
    SECTION("too many steps")
    {
        Quest q;
        q.start = 1;
        for (std::uint32_t i = 1; i <= kMaxSteps + 1; ++i)
            q.steps.push_back({i, 0, {accept(i + 1 <= kMaxSteps + 1 ? i + 1 : 0)}});
        CHECK(has(validate(q), Code::TooLarge));
        CHECK(has(ensureRefusals(q).findings, Code::TooLarge));
    }
    SECTION("too many options")
    {
        Quest q = bareQuest();
        q.steps[0].options.assign(kMaxOptionsPerStep + 1, accept(2));
        CHECK(has(validate(q), Code::TooLarge, 1));
    }
    SECTION("oversized consequence")
    {
        Quest q = ensureRefusals(bareQuest()).quest;
        q.steps[0].options[1].consequence.assign(kMaxConsequenceLength + 1, 'a');
        CHECK(has(validate(q), Code::TooLarge, 1));
    }
}

TEST_CASE("a cycle in the accept graph terminates", "[questrefusal]")
{
    Quest q = bareQuest();
    q.steps[2].options = {accept(1)};
    const Result r = ensureRefusals(q);
    CHECK(r.findings.empty());
}
