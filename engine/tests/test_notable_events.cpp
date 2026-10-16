#include "ichor/save/notable_events.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace ichor;

namespace {

void add(ConsequenceLedger& l, std::string id, std::string subject, std::string kind, std::string value, std::uint64_t tick)
{
    REQUIRE(l.append(Fact{std::move(id), std::move(subject), std::move(kind), std::move(value), tick}) == AppendResult::Ok);
}

NotableRule rule(std::string kind, std::string phrase)
{
    NotableRule r;
    r.kind = std::move(kind);
    r.category = "world";
    r.phrase = std::move(phrase);
    return r;
}

}

TEST_CASE("a rule needs a plain category and a phrase with known placeholders only", "[notable]")
{
    CHECK(isValidNotableRule(rule("k", "{subject} did {value}.")));
    CHECK_FALSE(isValidNotableRule(rule("", "x")));
    CHECK_FALSE(isValidNotableRule(rule("k", "")));
    CHECK_FALSE(isValidNotableRule(rule("k", "{nope}")));
    CHECK_FALSE(isValidNotableRule(rule("k", "stray } brace")));
    CHECK_FALSE(isValidNotableRule(rule("k", "open { brace")));
    CHECK_FALSE(isValidNotableRule(rule("k", std::string(301, 'a'))));
    NotableRule bad = rule("k", "x");
    bad.category = "Bad Cat";
    CHECK_FALSE(isValidNotableRule(bad));
    for (const NotableRule& r : defaultNotableRules())
        CHECK(isValidNotableRule(r));
}

TEST_CASE("a numeric rule keeps only values at or over the threshold", "[notable]")
{
    NotableRule r = rule("starving", "{subject} starves ({value}).");
    r.numeric = true;
    r.minValue = 10;
    ConsequenceLedger l;
    add(l, "a", "reach", "starving", "9", 1);
    add(l, "b", "reach", "starving", "10", 2);
    add(l, "c", "harl", "starving", "many", 3);
    add(l, "d", "vale", "starving", "-50", 4);
    add(l, "e", "mere", "starving", "99999999999999999999", 5);
    add(l, "f", "oak", "starving", "11", 6);
    const auto out = selectNotableEvents(l, {r});
    REQUIRE(out.size() == 2);
    CHECK(out[0].text == "reach starves (10).");
    CHECK(out[1].text == "oak starves (11).");
}

TEST_CASE("unknown kinds and invalid rules produce nothing", "[notable]")
{
    ConsequenceLedger l;
    add(l, "a", "reach", "mystery", "1", 1);
    add(l, "b", "reach", "killed", "1", 2);
    CHECK(selectNotableEvents(l, defaultNotableRules()).size() == 1);
    CHECK(selectNotableEvents(l, {}).empty());
    CHECK(selectNotableEvents(l, {rule("killed", "{bogus}")}).empty());
    CHECK(selectNotableEvents(ConsequenceLedger{}, defaultNotableRules()).empty());
}

TEST_CASE("duplicates and cooldown repeats are dropped", "[notable]")
{
    NotableRule r = rule("tithe", "{subject} paid.");
    r.cooldownTicks = 100;
    ConsequenceLedger l;
    add(l, "a", "reach", "tithe", "1", 0);
    add(l, "b", "reach", "tithe", "1", 50);
    add(l, "c", "vale", "tithe", "1", 60);
    add(l, "d", "reach", "tithe", "1", 150);
    const auto out = selectNotableEvents(l, {r});
    REQUIRE(out.size() == 2);
    CHECK(out[0].tick == 0);
    CHECK(out[1].text == "vale paid.");

    NotableRule once = rule("killed", "{subject} died.");
    ConsequenceLedger l2;
    add(l2, "a", "harl", "killed", "1", 1);
    add(l2, "b", "harl", "killed", "2", 2);
    CHECK(selectNotableEvents(l2, {once}).size() == 1);
}

TEST_CASE("the first valid rule for a kind wins", "[notable]")
{
    ConsequenceLedger l;
    add(l, "a", "x", "killed", "1", 1);
    const auto out = selectNotableEvents(l, {rule("killed", "{bad}"), rule("killed", "first"), rule("killed", "second")});
    REQUIRE(out.size() == 1);
    CHECK(out[0].text == "first");
}

TEST_CASE("hostile strings are made safe and never re-expanded", "[notable]")
{
    NotableRule r = rule("quest-resolved", "{subject}: {value}");
    Fact f{"a", "reach", "quest-resolved", "line\nbreak \xE2\x80\xAE" "rtl \xC0\xAF bad \xED\xA0\x80 \x01\t{subject}", 1};
    const std::string text = composeNotablePhrase(r, f);
    CHECK(text.find('\n') == std::string::npos);
    CHECK(text.find('\t') == std::string::npos);
    CHECK(text.find("\xE2\x80\xAE") == std::string::npos);
    CHECK(text.find("\xEF\xBF\xBD") != std::string::npos);
    CHECK(text.find("{subject}") != std::string::npos);
    CHECK(text.rfind("reach: ", 0) == 0);

    Fact big{"a", "reach", "quest-resolved", std::string(1000, '\xC3'), 1};
    CHECK(composeNotablePhrase(r, big).size() <= 7 + kMaxNotableFieldBytes);

    Fact multi{"a", "reach", "quest-resolved", std::string(79, 'x') + "\xE2\x82\xAC", 1};
    const std::string cut = composeNotablePhrase(r, multi);
    CHECK(cut == "reach: " + std::string(79, 'x'));
}

TEST_CASE("the same ledger and rules give identical entries, and they enter the chronicle", "[notable]")
{
    ConsequenceLedger l;
    add(l, "a", "reach", "settlement-fell", "1", 5);
    add(l, "b", "harl", "killed", "1", 9);
    const auto one = selectNotableEvents(l, defaultNotableRules());
    CHECK(one == selectNotableEvents(l, defaultNotableRules()));
    REQUIRE(one.size() == 2);
    CHECK(one[0].text == "reach fell.");
    CHECK(one[0].weight == 230);

    Chronicle c;
    CHECK(appendNotableEvents(c, one) == 2);
    CHECK(c.entries()[1].text == "harl was killed.");
    CHECK(c.entries()[1].weight == 200);
    Chronicle again;
    appendNotableEvents(again, one);
    CHECK(c.serialize() == again.serialize());
}

TEST_CASE("maxEntries caps the output", "[notable]")
{
    ConsequenceLedger l;
    for (int i = 0; i < 10; ++i)
        add(l, "k" + std::to_string(i), "s" + std::to_string(i), "killed", "1", static_cast<std::uint64_t>(i));
    CHECK(selectNotableEvents(l, defaultNotableRules(), 3).size() == 3);
    CHECK(selectNotableEvents(l, defaultNotableRules(), 0).empty());
}
