#include "ichor/world/world_title.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <set>
#include <string>
#include <string_view>
#include <vector>

using namespace ichor;

namespace {

const std::vector<std::string_view> kCataclysms = {"cataclysm.flood", "cataclysm.artemis_withdrew", "cataclysm.silence_of_the_oracles"};
const std::vector<std::string_view> kLaws = {"law.orichalcum", "law.hecatomb", "law.last_favour"};
const std::vector<std::string_view> kPowers = {"powers.hegemony", "powers.league", "powers.satrapy"};

bool hasDash(const std::string& text)
{
    return text.find("\xE2\x80\x94") != std::string::npos || text.find("--") != std::string::npos || text.find("\xE2\x80\x93") != std::string::npos;
}

bool plainAscii(const std::string& text)
{
    for (const char c : text) {
        if (static_cast<unsigned char>(c) >= 0x80 || static_cast<unsigned char>(c) < 0x20) {
            return false;
        }
    }
    return true;
}

}

TEST_CASE("the same axioms and seed compose the same blurb")
{
    const std::array<std::string_view, 3> ids = {"cataclysm.flood", "law.hecatomb", "powers.league"};
    const WorldBlurb a = composeWorldBlurb(ids, 77);
    REQUIRE_FALSE(a.title.empty());
    REQUIRE_FALSE(a.summary.empty());
    REQUIRE(composeWorldBlurb(ids, 77) == a);
}

TEST_CASE("the order the axioms are given in does not matter")
{
    const std::array<std::string_view, 3> forward = {"cataclysm.flood", "law.hecatomb", "powers.league"};
    const std::array<std::string_view, 3> backward = {"powers.league", "law.hecatomb", "cataclysm.flood"};
    REQUIRE(composeWorldBlurb(forward, 5) == composeWorldBlurb(backward, 5));
}

TEST_CASE("different axiom sets give different summaries and per-cataclysm titles")
{
    std::set<std::string> summaries;
    for (const auto c : kCataclysms) {
        for (const auto l : kLaws) {
            for (const auto p : kPowers) {
                const std::array<std::string_view, 3> ids = {c, l, p};
                summaries.insert(composeWorldBlurb(ids, 1).summary);
            }
        }
    }
    REQUIRE(summaries.size() == 27);

    std::set<std::string> titles;
    for (const auto c : kCataclysms) {
        const std::array<std::string_view, 1> ids = {c};
        titles.insert(composeWorldBlurb(ids, 1).title);
    }
    REQUIRE(titles.size() == kCataclysms.size());
}

TEST_CASE("without a cataclysm the law and the powers still name the world")
{
    std::set<std::string> titles;
    for (const auto l : kLaws) {
        const std::array<std::string_view, 1> ids = {l};
        titles.insert(composeWorldBlurb(ids, 1).title);
    }
    for (const auto p : kPowers) {
        const std::array<std::string_view, 1> ids = {p};
        titles.insert(composeWorldBlurb(ids, 1).title);
    }
    REQUIRE(titles.size() == 6);
}

TEST_CASE("the seed picks between phrasings but never leaves the pool")
{
    const std::array<std::string_view, 1> ids = {"cataclysm.flood"};
    std::set<std::string> titles;
    for (std::uint64_t seed = 0; seed < 64; ++seed) {
        titles.insert(composeWorldBlurb(ids, seed).title);
    }
    REQUIRE(titles.size() >= 2);
    REQUIRE(titles.size() <= 4);
}

TEST_CASE("the summary speaks of the axioms it was given")
{
    const std::array<std::string_view, 3> ids = {"cataclysm.flood", "law.hecatomb", "powers.league"};
    const std::string summary = composeWorldBlurb(ids, 0).summary;
    REQUIRE(summary.find("flood") != std::string::npos);
    REQUIRE(summary.find("League") != std::string::npos);
    REQUIRE(summary.find("sacrifice") != std::string::npos);
}

TEST_CASE("hidden slots never reach the blurb")
{
    const std::array<std::string_view, 3> shown = {"cataclysm.flood", "law.hecatomb", "powers.league"};
    const std::array<std::string_view, 5> withHidden = {"cataclysm.flood", "law.hecatomb", "powers.league", "pressure.unburied", "anomaly.labyrinth"};
    REQUIRE(composeWorldBlurb(shown, 9) == composeWorldBlurb(withHidden, 9));
}

TEST_CASE("missing slots leave their sentence out")
{
    const std::array<std::string_view, 3> full = {"cataclysm.flood", "law.hecatomb", "powers.league"};
    const std::array<std::string_view, 2> noLaw = {"cataclysm.flood", "powers.league"};
    const std::array<std::string_view, 1> onlyLaw = {"law.hecatomb"};
    const WorldBlurb fullBlurb = composeWorldBlurb(full, 3);
    const WorldBlurb partial = composeWorldBlurb(noLaw, 3);
    REQUIRE(partial.summary.size() < fullBlurb.summary.size());
    REQUIRE(partial.summary.find("sacrifice") == std::string::npos);
    REQUIRE(partial.title == fullBlurb.title);
    const WorldBlurb lawOnly = composeWorldBlurb(onlyLaw, 3);
    REQUIRE_FALSE(lawOnly.title.empty());
    REQUIRE_FALSE(lawOnly.summary.empty());
}

TEST_CASE("no axioms or only unknown ones still give a usable blurb")
{
    const WorldBlurb none = composeWorldBlurb({}, 4);
    REQUIRE_FALSE(none.title.empty());
    REQUIRE_FALSE(none.summary.empty());
    const std::array<std::string_view, 4> junk = {"cataclysm.nope", "law.", "", "bogus"};
    REQUIRE(composeWorldBlurb(junk, 4) == none);
}

TEST_CASE("an unknown id does not displace a known one")
{
    const std::array<std::string_view, 1> known = {"cataclysm.flood"};
    const std::array<std::string_view, 2> mixed = {"cataclysm.aaa_unknown", "cataclysm.flood"};
    REQUIRE(composeWorldBlurb(mixed, 2) == composeWorldBlurb(known, 2));
}

TEST_CASE("duplicate ids and two ids in one slot are tolerated")
{
    const std::array<std::string_view, 1> one = {"cataclysm.flood"};
    const std::array<std::string_view, 3> dup = {"cataclysm.flood", "cataclysm.flood", "cataclysm.flood"};
    REQUIRE(composeWorldBlurb(dup, 2) == composeWorldBlurb(one, 2));
    const std::array<std::string_view, 2> two = {"cataclysm.flood", "cataclysm.artemis_withdrew"};
    const std::array<std::string_view, 2> twoReversed = {"cataclysm.artemis_withdrew", "cataclysm.flood"};
    REQUIRE(composeWorldBlurb(two, 2) == composeWorldBlurb(twoReversed, 2));
}

TEST_CASE("hostile ids never leak into the text or crash")
{
    const std::string huge(1 << 20, 'A');
    const std::string nuls("cataclysm.flood\0law.hecatomb", 28);
    const std::string markup = "cataclysm.flood<script>%s%n{}\xE2\x80\x94";
    const std::array<std::string_view, 4> ids = {huge, nuls, markup, "\xff\xfe\x80"};
    const WorldBlurb blurb = composeWorldBlurb(ids, ~0ull);
    REQUIRE(blurb == composeWorldBlurb({}, ~0ull));
    REQUIRE(blurb.title.size() < 200);
    REQUIRE(blurb.summary.size() < 1000);
}

TEST_CASE("output is plain text without dashes for every combination and several seeds")
{
    for (const auto c : kCataclysms) {
        for (const auto l : kLaws) {
            for (const auto p : kPowers) {
                for (std::uint64_t seed = 0; seed < 8; ++seed) {
                    const std::array<std::string_view, 3> ids = {c, l, p};
                    const WorldBlurb blurb = composeWorldBlurb(ids, seed);
                    for (const std::string* text : {&blurb.title, &blurb.summary}) {
                        REQUIRE_FALSE(text->empty());
                        REQUIRE(plainAscii(*text));
                        REQUIRE_FALSE(hasDash(*text));
                        REQUIRE(text->front() != ' ');
                        REQUIRE(text->back() != ' ');
                        REQUIRE(text->find("  ") == std::string::npos);
                    }
                    REQUIRE(blurb.summary.back() == '.');
                }
            }
        }
    }
}

TEST_CASE("titles of different cataclysms never collide whatever the seed")
{
    const std::array<std::string_view, 1> flood = {"cataclysm.flood"};
    const std::array<std::string_view, 1> silence = {"cataclysm.silence_of_the_oracles"};
    for (std::uint64_t seed = 0; seed < 32; ++seed) {
        REQUIRE(composeWorldBlurb(flood, seed).title != composeWorldBlurb(silence, seed).title);
    }
}
