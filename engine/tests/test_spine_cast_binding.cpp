#include "ichor/graph/spine_cast_binding.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <limits>
#include <set>
#include <string>

using namespace ichor::graph::spine;

namespace {

Spine mainSpine()
{
    Spine s;
    s.slots = {
        {"patron", SlotKind::Faction, "", "", ""},
        {"rival", SlotKind::Faction, "", "", "patron"},
        {"informant", SlotKind::Npc, "", "rival", ""},
        {"ally", SlotKind::Npc, "healer", "patron", ""},
    };
    return s;
}

Cast richCast()
{
    Cast c;
    c.factions = {
        {"league", {}, {"isonomoi"}},
        {"isonomoi", {}, {"league"}},
        {"telestai", {}, {"league"}},
        {"hermits", {}, {}},
    };
    c.npcs = {
        {"anna", "league", {"healer"}},   {"bram", "league", {}},
        {"cato", "isonomoi", {"healer"}}, {"dara", "isonomoi", {}},
        {"enzo", "telestai", {"healer"}}, {"fenn", "telestai", {}},
        {"gil", "", {"healer"}},
    };
    return c;
}

const std::string& entityFor(const Result& r, const std::string& slot)
{
    static const std::string none;
    for (const Binding& b : r.bindings)
        if (b.slot == slot)
            return b.entity;
    return none;
}

bool has(const Result& r, Code c)
{
    return std::any_of(r.findings.begin(), r.findings.end(), [c](const Finding& f) { return f.code == c; });
}

const Faction* faction(const Cast& c, const std::string& id)
{
    for (const Faction& f : c.factions)
        if (f.id == id)
            return &f;
    return nullptr;
}

const Npc* npc(const Cast& c, const std::string& id)
{
    for (const Npc& n : c.npcs)
        if (n.id == id)
            return &n;
    return nullptr;
}

} // namespace

TEST_CASE("every slot is bound and every constraint holds", "[spine][quests]")
{
    const Spine spine = mainSpine();
    const Cast cast = richCast();
    for (std::uint64_t seed = 0; seed < 64; ++seed) {
        const Result r = bind(spine, cast, seed);
        REQUIRE(r.ok);
        REQUIRE(r.bindings.size() == spine.slots.size());
        const std::string patron = entityFor(r, "patron");
        const std::string rival = entityFor(r, "rival");
        CHECK(patron != rival);
        const Faction* rf = faction(cast, rival);
        const Faction* pf = faction(cast, patron);
        REQUIRE((rf != nullptr && pf != nullptr));
        const bool hostile = std::find(rf->hostileTo.begin(), rf->hostileTo.end(), patron) != rf->hostileTo.end() ||
                             std::find(pf->hostileTo.begin(), pf->hostileTo.end(), rival) != pf->hostileTo.end();
        CHECK(hostile);
        const Npc* informant = npc(cast, entityFor(r, "informant"));
        const Npc* ally = npc(cast, entityFor(r, "ally"));
        REQUIRE((informant != nullptr && ally != nullptr));
        CHECK(informant->faction == rival);
        CHECK(ally->faction == patron);
        CHECK(std::find(ally->tags.begin(), ally->tags.end(), "healer") != ally->tags.end());
        CHECK(informant != ally);
    }
}

TEST_CASE("binding is deterministic and ignores cast vector order", "[spine][quests]")
{
    const Spine spine = mainSpine();
    const Cast cast = richCast();
    Cast reversed = cast;
    std::reverse(reversed.factions.begin(), reversed.factions.end());
    std::reverse(reversed.npcs.begin(), reversed.npcs.end());
    for (std::uint64_t seed = 0; seed < 32; ++seed) {
        const Result a = bind(spine, cast, seed);
        const Result b = bind(spine, cast, seed);
        const Result c = bind(spine, reversed, seed);
        REQUIRE(a.ok);
        for (std::size_t i = 0; i < a.bindings.size(); ++i) {
            CHECK(a.bindings[i].entity == b.bindings[i].entity);
            CHECK(a.bindings[i].entity == c.bindings[i].entity);
        }
    }
}

TEST_CASE("different seeds cast the spine differently", "[spine][quests]")
{
    const Spine spine = mainSpine();
    const Cast cast = richCast();
    std::set<std::string> patrons;
    std::set<std::string> whole;
    for (std::uint64_t seed = 0; seed < 64; ++seed) {
        const Result r = bind(spine, cast, seed);
        REQUIRE(r.ok);
        patrons.insert(entityFor(r, "patron"));
        std::string key;
        for (const Binding& b : r.bindings)
            key += b.entity + "|";
        whole.insert(key);
    }
    CHECK(patrons.size() >= 2);
    CHECK(whole.size() >= 4);
}

TEST_CASE("an unsatisfiable cast fails instead of inventing anyone", "[spine][quests]")
{
    Cast cast = richCast();
    cast.factions = {{"league", {}, {}}, {"hermits", {}, {}}}; // nobody is hostile
    const Result r = bind(mainSpine(), cast, 1);
    CHECK_FALSE(r.ok);
    CHECK(r.bindings.empty());
    CHECK(has(r, Code::Unsatisfiable));
}

TEST_CASE("a slot with no tagged candidate is reported by name", "[spine][quests]")
{
    Cast cast = richCast();
    for (Npc& n : cast.npcs)
        n.tags.clear();
    const Result r = bind(mainSpine(), cast, 1);
    CHECK_FALSE(r.ok);
    REQUIRE(has(r, Code::NoCandidate));
    CHECK(r.findings.front().slot == "ally");
}

TEST_CASE("backtracking recovers when the first pick dead-ends", "[spine][quests]")
{
    // Only the pair (b, c) is hostile, and only c has a member. Any patron
    // other than b or c dead-ends, so every seed must backtrack to b and c.
    Spine spine;
    spine.slots = {
        {"patron", SlotKind::Faction, "", "", ""},
        {"rival", SlotKind::Faction, "", "", "patron"},
        {"informant", SlotKind::Npc, "", "rival", ""},
    };
    Cast cast;
    cast.factions = {{"a", {}, {}}, {"b", {}, {"c"}}, {"c", {}, {}}, {"d", {}, {}}};
    cast.npcs = {{"n1", "b", {}}};
    for (std::uint64_t seed = 0; seed < 32; ++seed) {
        const Result r = bind(spine, cast, seed);
        REQUIRE(r.ok);
        CHECK(entityFor(r, "rival") == "b");
        CHECK(entityFor(r, "patron") == "c");
        CHECK(entityFor(r, "informant") == "n1");
    }
}

TEST_CASE("distinct slots never share an entity", "[spine][quests]")
{
    Spine spine;
    spine.slots = {{"x", SlotKind::Npc, "", "", ""}, {"y", SlotKind::Npc, "", "", ""}};
    Cast cast;
    cast.npcs = {{"only", "", {}}};
    CHECK_FALSE(bind(spine, cast, 3).ok);
    cast.npcs.push_back({"two", "", {}});
    const Result r = bind(spine, cast, 3);
    REQUIRE(r.ok);
    CHECK(entityFor(r, "x") != entityFor(r, "y"));
}

TEST_CASE("hostile spines are rejected without crashing", "[spine][quests]")
{
    const Cast cast = richCast();

    CHECK(has(bind(Spine{}, cast, 1), Code::Empty));

    Spine tooMany;
    tooMany.slots.assign(kMaxSlots + 1, {"s", SlotKind::Faction, "", "", ""});
    CHECK(has(bind(tooMany, cast, 1), Code::TooLarge));

    Spine dup;
    dup.slots = {{"a", SlotKind::Faction, "", "", ""}, {"a", SlotKind::Faction, "", "", ""}};
    CHECK(has(bind(dup, cast, 1), Code::DuplicateSlot));

    Spine noId;
    noId.slots = {{"", SlotKind::Faction, "", "", ""}};
    CHECK(has(bind(noId, cast, 1), Code::BadSlotId));

    Spine longId;
    longId.slots = {{std::string(kMaxIdLength + 1, 'x'), SlotKind::Faction, "", "", ""}};
    CHECK(has(bind(longId, cast, 1), Code::BadSlotId));

    Spine dangling;
    dangling.slots = {{"n", SlotKind::Npc, "", "ghost", ""}};
    CHECK(has(bind(dangling, cast, 1), Code::BadReference));

    Spine wrongKind;
    wrongKind.slots = {{"n", SlotKind::Npc, "", "", ""}, {"m", SlotKind::Npc, "", "n", ""}};
    CHECK(has(bind(wrongKind, cast, 1), Code::BadReference));

    Spine selfHostile;
    selfHostile.slots = {{"f", SlotKind::Faction, "", "", "f"}};
    CHECK(has(bind(selfHostile, cast, 1), Code::BadReference));

    Spine cycle;
    cycle.slots = {{"f", SlotKind::Faction, "", "", "g"}, {"g", SlotKind::Faction, "", "", "f"}};
    CHECK(has(bind(cycle, cast, 1), Code::ReferenceCycle));
}

TEST_CASE("hostile casts are rejected without crashing", "[spine][quests]")
{
    const Spine spine = mainSpine();

    Cast dup = richCast();
    dup.npcs.push_back({"anna", "", {}});
    CHECK(has(bind(spine, dup, 1), Code::DuplicateCast));

    Cast clash = richCast();
    clash.npcs.push_back({"league", "", {}}); // npc id equals a faction id
    CHECK(has(bind(spine, clash, 1), Code::DuplicateCast));

    Cast noId = richCast();
    noId.factions.push_back({"", {}, {}});
    CHECK(has(bind(spine, noId, 1), Code::BadCastId));

    Cast manyTags = richCast();
    manyTags.npcs[0].tags.assign(kMaxTags + 1, "t");
    CHECK(has(bind(spine, manyTags, 1), Code::BadCastId));

    Cast longHostile = richCast();
    longHostile.factions[0].hostileTo.assign(kMaxTags + 1, "x");
    CHECK(has(bind(spine, longHostile, 1), Code::BadCastId));

    Cast longString = richCast();
    longString.npcs[0].faction = std::string(kMaxIdLength + 1, 'x');
    CHECK(has(bind(spine, longString, 1), Code::BadCastId));
    longString = richCast();
    longString.npcs[0].tags = {std::string(kMaxIdLength + 1, 'x')};
    CHECK(has(bind(spine, longString, 1), Code::BadCastId));

    Spine longTag;
    longTag.slots = {{"f", SlotKind::Faction, std::string(kMaxIdLength + 1, 'x'), "", ""}};
    CHECK(has(bind(longTag, richCast(), 1), Code::BadSlotId));

    Cast huge;
    huge.npcs.resize(kMaxCastEntities + 1);
    CHECK(has(bind(spine, huge, 1), Code::TooLarge));

    CHECK_FALSE(bind(spine, Cast{}, 1).ok);
}

TEST_CASE("extreme seeds are fine", "[spine][quests]")
{
    const Spine spine = mainSpine();
    const Cast cast = richCast();
    CHECK(bind(spine, cast, 0).ok);
    CHECK(bind(spine, cast, std::numeric_limits<std::uint64_t>::max()).ok);
}

TEST_CASE("a pathological search stops at the budget", "[spine][quests]")
{
    // Two slots need the one rare faction, so the 12 free slots before them
    // would be re-shuffled forever without the cap.
    Spine spine;
    for (int i = 0; i < 12; ++i)
        spine.slots.push_back({"s" + std::to_string(i), SlotKind::Faction, "", "", ""});
    spine.slots.push_back({"rare1", SlotKind::Faction, "rare", "", ""});
    spine.slots.push_back({"rare2", SlotKind::Faction, "rare", "", ""});
    Cast cast;
    for (int i = 0; i < 40; ++i)
        cast.factions.push_back({"f" + std::to_string(i), {}, {}});
    cast.factions.push_back({"r", {"rare"}, {}});
    const Result r = bind(spine, cast, 5);
    CHECK_FALSE(r.ok);
    CHECK(has(r, Code::OverBudget));
    CHECK(r.bindings.empty());
}
