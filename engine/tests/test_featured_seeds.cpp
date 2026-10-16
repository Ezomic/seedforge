#include "ichor/data/featured_seeds.h"

#include <catch2/catch_test_macros.hpp>

#include "ichor/core/hash.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <vector>

using namespace ichor;

namespace {

Axiom make(AxiomSlot slot, const std::string& name)
{
    Axiom axiom;
    axiom.slot = slot;
    axiom.id = std::string(axiomSlotName(slot)) + "." + name;
    axiom.title = name;
    return axiom;
}

std::vector<Axiom> pool(int perSlot = 3)
{
    std::vector<Axiom> out;
    for (const AxiomSlot slot : {AxiomSlot::Cataclysm, AxiomSlot::Law, AxiomSlot::Powers, AxiomSlot::Pressure, AxiomSlot::Anomaly}) {
        for (int i = 0; i < perSlot; ++i) {
            out.push_back(make(slot, "n" + std::to_string(i)));
        }
    }
    return out;
}

std::vector<std::string> rolledIds(std::uint64_t seed, const std::vector<Axiom>& axioms)
{
    std::vector<std::string> ids;
    for (const SlotAssignment& slot : rollSlots(seed, axioms).slots) {
        ids.push_back(slot.axiomId);
    }
    return ids;
}

std::vector<FeaturedSeed> sampleList(std::size_t n)
{
    std::vector<FeaturedSeed> list;
    for (std::size_t i = 0; i < n; ++i) {
        list.push_back({std::uint64_t{1000} + i, "World " + std::to_string(i), {}});
    }
    return list;
}

}

TEST_CASE("featured rotation holds for a week and walks the list in order", "[featured_seeds]")
{
    const std::vector<FeaturedSeed> list = sampleList(3);
    for (std::int64_t week = 0; week < 9; ++week) {
        const FeaturedSeed* first = featuredForDay(list, week * kFeaturedPeriodDays);
        REQUIRE(first != nullptr);
        REQUIRE(first->seed == list[static_cast<std::size_t>(week % 3)].seed);
        for (std::int64_t d = 0; d < kFeaturedPeriodDays; ++d) {
            REQUIRE(featuredForDay(list, week * kFeaturedPeriodDays + d) == first);
        }
    }
    REQUIRE(featuredForDay(list, 6) != featuredForDay(list, 7));
}

TEST_CASE("featured rotation covers every entry over a cycle", "[featured_seeds]")
{
    const std::vector<FeaturedSeed> list = sampleList(5);
    std::set<std::size_t> seen;
    for (std::int64_t day = 0; day < 5 * kFeaturedPeriodDays; ++day) {
        seen.insert(*featuredIndexForDay(list.size(), day));
    }
    REQUIRE(seen.size() == 5);
}

TEST_CASE("featured rotation handles negative days without a seam at zero", "[featured_seeds]")
{
    REQUIRE(featuredWeekOf(-1) == -1);
    REQUIRE(featuredWeekOf(-7) == -1);
    REQUIRE(featuredWeekOf(-8) == -2);
    REQUIRE(*featuredIndexForDay(3, -1) == 2);
    REQUIRE(*featuredIndexForDay(3, -7) == 2);
    REQUIRE(*featuredIndexForDay(3, -8) == 1);
    REQUIRE(*featuredIndexForDay(3, -21) == 0);
    REQUIRE(*featuredIndexForDay(3, -22) == 2);
    REQUIRE(*featuredIndexForDay(3, -28) == 2);
    REQUIRE(*featuredIndexForDay(3, -29) == 1);
    for (std::int64_t week = -20; week < 20; ++week) {
        const std::size_t a = *featuredIndexForDay(4, week * kFeaturedPeriodDays);
        const std::size_t b = *featuredIndexForDay(4, (week + 1) * kFeaturedPeriodDays);
        REQUIRE(b == (a + 1) % 4);
    }
}

TEST_CASE("featured rotation survives extreme day indices", "[featured_seeds]")
{
    const std::vector<FeaturedSeed> list = sampleList(7);
    for (const std::int64_t day : {std::numeric_limits<std::int64_t>::min(), std::numeric_limits<std::int64_t>::max(), std::int64_t{0}}) {
        const std::optional<std::size_t> index = featuredIndexForDay(list.size(), day);
        REQUIRE(index.has_value());
        REQUIRE(*index < list.size());
        REQUIRE(featuredForDay(list, day) == &list[*index]);
    }
    constexpr std::int64_t lo = std::numeric_limits<std::int64_t>::min();
    constexpr std::int64_t hi = std::numeric_limits<std::int64_t>::max();
    REQUIRE(*featuredIndexForDay(7, hi) == 0);
    REQUIRE(*featuredIndexForDay(7, lo) == 6);
    REQUIRE(*featuredIndexForDay(3, hi) == 1);
    REQUIRE(*featuredIndexForDay(3, lo) == 1);
    REQUIRE(*featuredIndexForDay(7, lo + kFeaturedPeriodDays) == 0);
}

TEST_CASE("featured rotation of an empty list is nothing", "[featured_seeds]")
{
    REQUIRE_FALSE(featuredIndexForDay(0, 100).has_value());
    REQUIRE(featuredForDay({}, 100) == nullptr);
}

TEST_CASE("a single featured entry is always featured", "[featured_seeds]")
{
    const std::vector<FeaturedSeed> list = sampleList(1);
    for (const std::int64_t day : {-100, -1, 0, 6, 7, 1000}) {
        REQUIRE(featuredForDay(list, day) == &list[0]);
    }
}

TEST_CASE("an unpinned entry validates when its seed rolls", "[featured_seeds]")
{
    const std::vector<Axiom> axioms = pool();
    REQUIRE(validateFeaturedSeed({42, "Flooded vale", {}}, axioms).ok());
}

TEST_CASE("a pinned entry validates only against the axioms it really rolls", "[featured_seeds]")
{
    const std::vector<Axiom> axioms = pool();
    const std::vector<std::string> rolled = rolledIds(42, axioms);
    REQUIRE(validateFeaturedSeed({42, "Flooded vale", rolled}, axioms).ok());

    std::vector<std::string> reversed(rolled.rbegin(), rolled.rend());
    REQUIRE(validateFeaturedSeed({42, "Flooded vale", reversed}, axioms).ok());

    std::vector<std::string> short_ = rolled;
    short_.pop_back();
    REQUIRE(validateFeaturedSeed({42, "Flooded vale", short_}, axioms).problem == FeaturedProblem::RolledMismatch);

    std::vector<std::string> swapped = rolled;
    swapped[0] = swapped[0] == "cataclysm.n0" ? "cataclysm.n1" : "cataclysm.n0";
    REQUIRE(validateFeaturedSeed({42, "Flooded vale", swapped}, axioms).problem == FeaturedProblem::RolledMismatch);
}

TEST_CASE("a pin that goes stale when the pool changes is caught", "[featured_seeds]")
{
    const std::vector<Axiom> axioms = pool();
    std::uint64_t seed = 0;
    while (rolledIds(seed, axioms)[0] != "cataclysm.n2") {
        ++seed;
    }
    const FeaturedSeed entry{seed, "Flooded vale", rolledIds(seed, axioms)};
    REQUIRE(validateFeaturedSeed(entry, axioms).ok());

    std::vector<Axiom> smaller = pool(2);
    REQUIRE(validateFeaturedSeed(entry, smaller).problem == FeaturedProblem::UnknownExpected);

    std::vector<std::string> kept = rolledIds(seed, smaller);
    const FeaturedSeed other{seed, "Flooded vale", kept};
    REQUIRE(validateFeaturedSeed(other, smaller).ok());
    REQUIRE(validateFeaturedSeed(other, axioms).problem == FeaturedProblem::RolledMismatch);
}

TEST_CASE("a pin naming an axiom outside the pool is unknown", "[featured_seeds]")
{
    const FeaturedCheck check = validateFeaturedSeed({42, "x", {"cataclysm.nope"}}, pool());
    REQUIRE(check.problem == FeaturedProblem::UnknownExpected);
    REQUIRE(check.message.find("cataclysm.nope") != std::string::npos);
}

TEST_CASE("validation honours the coexist predicate", "[featured_seeds]")
{
    const std::vector<Axiom> axioms = pool();
    const std::vector<std::string> plain = rolledIds(42, axioms);
    const std::string banned = plain[1];
    const AxiomCoexist noBanned = [&](const Axiom&, const Axiom& candidate) { return candidate.id != banned; };
    std::vector<std::string> constrained;
    for (const SlotAssignment& slot : rollSlots(42, axioms, noBanned).slots) {
        constrained.push_back(slot.axiomId);
    }
    REQUIRE(std::find(constrained.begin(), constrained.end(), banned) == constrained.end());
    REQUIRE(validateFeaturedSeed({42, "x", plain}, axioms, noBanned).problem == FeaturedProblem::RolledMismatch);
    REQUIRE(validateFeaturedSeed({42, "x", constrained}, axioms, noBanned).ok());
    REQUIRE(validateFeaturedSeed({42, "x", {}}, axioms, noBanned).ok());
}

TEST_CASE("a seed that cannot roll on the pool fails validation", "[featured_seeds]")
{
    std::vector<Axiom> axioms = pool();
    std::erase_if(axioms, [](const Axiom& a) { return a.slot == AxiomSlot::Law; });
    const FeaturedCheck check = validateFeaturedSeed({42, "x", {}}, axioms);
    REQUIRE(check.problem == FeaturedProblem::NoSlotRoll);
    REQUIRE_FALSE(check.message.empty());
    REQUIRE(validateFeaturedSeed({42, "x", {}}, {}).problem == FeaturedProblem::NoSlotRoll);
}

TEST_CASE("bad titles and pin ids are rejected", "[featured_seeds]")
{
    const std::vector<Axiom> axioms = pool();
    REQUIRE(validateFeaturedSeed({1, "", {}}, axioms).problem == FeaturedProblem::BadTitle);
    REQUIRE(validateFeaturedSeed({1, std::string(kMaxFeaturedTitleBytes + 1, 'a'), {}}, axioms).problem == FeaturedProblem::BadTitle);
    REQUIRE(validateFeaturedSeed({1, std::string(kMaxFeaturedTitleBytes, 'a'), {}}, axioms).ok());
    REQUIRE(validateFeaturedSeed({1, "tab\there", {}}, axioms).problem == FeaturedProblem::BadTitle);
    REQUIRE(validateFeaturedSeed({1, "caf\xc3\xa9", {}}, axioms).problem == FeaturedProblem::BadTitle);
    REQUIRE(validateFeaturedSeed({1, std::string("nul\0byte", 8), {}}, axioms).problem == FeaturedProblem::BadTitle);
    REQUIRE(validateFeaturedSeed({1, "x", {""}}, axioms).problem == FeaturedProblem::BadAxiomId);
    REQUIRE(validateFeaturedSeed({1, "x", {std::string(kMaxFeaturedAxiomIdBytes + 1, 'a')}}, axioms).problem == FeaturedProblem::BadAxiomId);
    REQUIRE(validateFeaturedSeed({1, "x", std::vector<std::string>(kMaxFeaturedAxioms + 1, "cataclysm.n0")}, axioms).problem == FeaturedProblem::TooManyAxioms);
}

TEST_CASE("list validation names the failing entry", "[featured_seeds]")
{
    const std::vector<Axiom> axioms = pool();
    std::vector<FeaturedSeed> list = sampleList(4);
    REQUIRE(validateFeaturedList(list, axioms).ok());

    list[2].title.clear();
    const FeaturedCheck check = validateFeaturedList(list, axioms);
    REQUIRE(check.problem == FeaturedProblem::BadTitle);
    REQUIRE(check.index == 2);
}

TEST_CASE("list validation rejects empty, duplicate seeds and oversize lists", "[featured_seeds]")
{
    const std::vector<Axiom> axioms = pool();
    REQUIRE(validateFeaturedList({}, axioms).problem == FeaturedProblem::Empty);

    std::vector<FeaturedSeed> dup = sampleList(3);
    dup[2].seed = dup[0].seed;
    const FeaturedCheck check = validateFeaturedList(dup, axioms);
    REQUIRE(check.problem == FeaturedProblem::DuplicateSeed);
    REQUIRE(check.index == 2);

    REQUIRE(validateFeaturedList(sampleList(kMaxFeaturedSeeds), axioms).ok());
    REQUIRE(validateFeaturedList(sampleList(kMaxFeaturedSeeds + 1), axioms).problem == FeaturedProblem::TooManyEntries);
}

TEST_CASE("the codec round-trips lists exactly", "[featured_seeds]")
{
    const std::vector<Axiom> axioms = pool();
    std::vector<FeaturedSeed> list = sampleList(5);
    list[1].expectedAxioms = rolledIds(list[1].seed, axioms);
    list[3].seed = std::numeric_limits<std::uint64_t>::max();
    list[4].seed = 0;

    const std::optional<std::string> text = encodeFeaturedList(list);
    REQUIRE(text.has_value());
    REQUIRE(text->rfind("ichorf1.", 0) == 0);
    const std::optional<std::vector<FeaturedSeed>> back = decodeFeaturedList(*text);
    REQUIRE(back.has_value());
    REQUIRE(*back == list);
    REQUIRE(encodeFeaturedList(*back) == text);
}

TEST_CASE("the codec round-trips an empty list and the size limits", "[featured_seeds]")
{
    const std::optional<std::string> empty = encodeFeaturedList({});
    REQUIRE(empty.has_value());
    REQUIRE(decodeFeaturedList(*empty) == std::vector<FeaturedSeed>{});

    std::vector<FeaturedSeed> full;
    for (std::size_t i = 0; i < kMaxFeaturedSeeds; ++i) {
        FeaturedSeed entry{std::uint64_t{i}, std::string(kMaxFeaturedTitleBytes, 'z'), {}};
        for (std::size_t a = 0; a < kMaxFeaturedAxioms; ++a) {
            entry.expectedAxioms.push_back(std::string(kMaxFeaturedAxiomIdBytes, 'a' + static_cast<char>(a)));
        }
        full.push_back(entry);
    }
    const std::optional<std::string> text = encodeFeaturedList(full);
    REQUIRE(text.has_value());
    REQUIRE(decodeFeaturedList(*text) == full);
}

TEST_CASE("the codec refuses lists that break the shape limits", "[featured_seeds]")
{
    REQUIRE_FALSE(encodeFeaturedList(sampleList(kMaxFeaturedSeeds + 1)).has_value());
    REQUIRE_FALSE(encodeFeaturedList(std::vector<FeaturedSeed>{{1, "", {}}}).has_value());
    REQUIRE_FALSE(encodeFeaturedList(std::vector<FeaturedSeed>{{1, "x", {""}}}).has_value());
    REQUIRE_FALSE(encodeFeaturedList(std::vector<FeaturedSeed>{{1, "caf\xc3\xa9", {}}}).has_value());
}

TEST_CASE("any single changed character in a code is rejected", "[featured_seeds]")
{
    const std::vector<FeaturedSeed> list = sampleList(3);
    const std::string text = *encodeFeaturedList(list);
    for (std::size_t i = 0; i < text.size(); ++i) {
        std::string bad = text;
        bad[i] = bad[i] == '0' ? '1' : '0';
        if (bad == text) {
            continue;
        }
        REQUIRE_FALSE(decodeFeaturedList(bad).has_value());
    }
}

TEST_CASE("every truncation and extension of a code is rejected", "[featured_seeds]")
{
    const std::string text = *encodeFeaturedList(sampleList(3));
    for (std::size_t n = 0; n < text.size(); ++n) {
        REQUIRE_FALSE(decodeFeaturedList(text.substr(0, n)).has_value());
    }
    REQUIRE_FALSE(decodeFeaturedList(text + "0").has_value());
    REQUIRE_FALSE(decodeFeaturedList(text + text).has_value());
}

TEST_CASE("the codec rejects uppercase, wrong prefix and hostile text", "[featured_seeds]")
{
    const std::string text = *encodeFeaturedList(sampleList(2));
    std::string upper = text;
    for (char& c : upper) {
        if (c >= 'a' && c <= 'f') {
            c = static_cast<char>(c - 32);
        }
    }
    upper.replace(0, 7, "ichorf1");
    REQUIRE_FALSE(decodeFeaturedList(upper).has_value());
    REQUIRE_FALSE(decodeFeaturedList("ichorf2" + text.substr(7)).has_value());
    REQUIRE_FALSE(decodeFeaturedList("").has_value());
    REQUIRE_FALSE(decodeFeaturedList("ichorf1.").has_value());
    REQUIRE_FALSE(decodeFeaturedList("ichorf1..0000").has_value());
    REQUIRE_FALSE(decodeFeaturedList(std::string("ichorf1.\0\0.0000", 15)).has_value());
    REQUIRE_FALSE(decodeFeaturedList(std::string(10u * 1024 * 1024, 'f')).has_value());
    REQUIRE_FALSE(decodeFeaturedList("ichorf1." + std::string(1u << 20, 'f') + ".0000").has_value());
}

namespace {

std::string codeFor(const std::string& payload)
{
    constexpr char kDigits[] = "0123456789abcdef";
    std::string out = "ichorf1.";
    for (const char c : payload) {
        out.push_back(kDigits[(static_cast<unsigned char>(c) >> 4) & 0xf]);
        out.push_back(kDigits[static_cast<unsigned char>(c) & 0xf]);
    }
    out.push_back('.');
    const std::uint16_t check = static_cast<std::uint16_t>(mix64(hash64(payload)) >> 48);
    for (int shift = 12; shift >= 0; shift -= 4) {
        out.push_back(kDigits[(check >> shift) & 0xf]);
    }
    return out;
}

std::string entryBytes(std::uint8_t titleLength, const std::string& title, std::uint8_t axiomCount, const std::string& axiomBytes = {})
{
    std::string out(8, '\x01');
    out.push_back(static_cast<char>(titleLength));
    out += title;
    out.push_back(static_cast<char>(axiomCount));
    out += axiomBytes;
    return out;
}

std::string withCount(std::uint16_t count, const std::string& rest)
{
    std::string out;
    out.push_back(static_cast<char>(count & 0xff));
    out.push_back(static_cast<char>(count >> 8));
    return out + rest;
}

}

TEST_CASE("a code with a valid check but a hostile payload is rejected", "[featured_seeds]")
{
    const std::string good = entryBytes(1, "a", 0);
    REQUIRE(decodeFeaturedList(codeFor(withCount(1, good))).has_value());

    REQUIRE_FALSE(decodeFeaturedList(codeFor("")).has_value());
    REQUIRE_FALSE(decodeFeaturedList(codeFor("\x01")).has_value());
    REQUIRE_FALSE(decodeFeaturedList(codeFor(withCount(65, ""))).has_value());
    REQUIRE_FALSE(decodeFeaturedList(codeFor(withCount(0xffff, ""))).has_value());
    REQUIRE_FALSE(decodeFeaturedList(codeFor(withCount(2, good))).has_value());
    REQUIRE_FALSE(decodeFeaturedList(codeFor(withCount(1, good + "x"))).has_value());
    REQUIRE_FALSE(decodeFeaturedList(codeFor(withCount(1, entryBytes(200, "a", 0)))).has_value());
    REQUIRE_FALSE(decodeFeaturedList(codeFor(withCount(1, entryBytes(0, "", 0)))).has_value());
    REQUIRE_FALSE(decodeFeaturedList(codeFor(withCount(1, entryBytes(1, "\x01", 0)))).has_value());
    REQUIRE_FALSE(decodeFeaturedList(codeFor(withCount(1, entryBytes(1, "a", 9)))).has_value());
    REQUIRE_FALSE(decodeFeaturedList(codeFor(withCount(1, entryBytes(1, "a", 1)))).has_value());
    REQUIRE_FALSE(decodeFeaturedList(codeFor(withCount(1, entryBytes(1, "a", 1, std::string("\x05") + "ab")))).has_value());
    REQUIRE_FALSE(decodeFeaturedList(codeFor(withCount(1, entryBytes(1, "a", 1, std::string("\x00", 1))))).has_value());
    REQUIRE(decodeFeaturedList(codeFor(withCount(1, entryBytes(1, "a", 1, std::string("\x02") + "ab")))).has_value());
}
