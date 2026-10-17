#include "ichor/graph/spine_cast_binding.h"
#include "ichor/ledger/quest_validation.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <string>

using namespace ichor;
namespace sp = ichor::graph::spine;

namespace {

struct MainSpine {
    sp::Spine spine;
    BoundQuest quest; // nodes carry the slot name in `entity` until bound
    std::uint64_t budget = 0;
    std::string error;
};

std::string str(const nlohmann::json& j, const char* key)
{
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

MainSpine parseSpine(const std::string& text)
{
    MainSpine out;
    const nlohmann::json doc = nlohmann::json::parse(text, nullptr, false);
    if (!doc.is_array()) {
        out.error = "not an array";
        return out;
    }
    std::map<std::string, std::string> beatIds;
    for (const auto& rec : doc) {
        const std::string type = str(rec, "type");
        if (type == "meta") {
            out.budget = rec.value("budget", 0ull);
        } else if (type == "slot") {
            sp::Slot s;
            s.id = str(rec, "slot");
            s.kind = str(rec, "kind") == "npc" ? sp::SlotKind::Npc : sp::SlotKind::Faction;
            s.needsTag = str(rec, "needs_tag");
            s.memberOf = str(rec, "member_of");
            s.hostileTo = str(rec, "hostile_to");
            out.spine.slots.push_back(s);
        } else if (type == "beat") {
            QuestNode n;
            n.id = str(rec, "id");
            n.entity = str(rec, "slot");
            n.cost = rec.value("cost", 0u);
            n.refusable = rec.value("refusable", false);
            for (const auto& a : rec.value("after", nlohmann::json::array()))
                n.after.push_back(a.get<std::string>());
            out.quest.nodes.push_back(n);
        } else {
            out.error = "unknown record type " + type;
        }
    }
    out.quest.id = "main_spine";
    return out;
}

MainSpine loadSpine()
{
    std::ifstream in(std::string(ICHOR_REPO_DATA) + "/quests/main_spine.json", std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return parseSpine(text);
}

sp::Cast sampleWorld()
{
    sp::Cast c;
    c.factions = {
        {"hellenotamiai", {}, {}},          {"isonomoi", {}, {"hellenotamiai"}}, {"archaists", {}, {"hellenotamiai"}},
        {"telestai", {}, {}},               {"mnemones", {}, {}},                {"refuseniks", {}, {}},
        {"periegetes", {}, {}},
    };
    c.npcs = {
        {"aristo", "hellenotamiai", {"envoy"}}, {"bion", "hellenotamiai", {}},
        {"chara", "isonomoi", {}},              {"damon", "archaists", {}},
        {"eudora", "telestai", {}},             {"ismene", "telestai", {"envoy"}},
        {"lykon", "mnemones", {"rememberer"}},  {"melia", "mnemones", {"rememberer"}},
        {"nikias", "periegetes", {"rememberer"}},
    };
    return c;
}

QuestVerdict validateBound(const MainSpine& s, const sp::Result& r, const sp::Cast& cast, std::uint64_t budget)
{
    std::map<std::string, std::string> bySlot;
    for (const sp::Binding& b : r.bindings)
        bySlot[b.slot] = b.entity;
    BoundQuest q = s.quest;
    for (QuestNode& n : q.nodes) {
        const auto it = bySlot.find(n.entity);
        n.entity = it == bySlot.end() ? std::string() : it->second;
    }
    std::set<std::string, std::less<>> known;
    for (const auto& f : cast.factions)
        known.insert(f.id);
    for (const auto& n : cast.npcs)
        known.insert(n.id);
    QuestValidationInputs in;
    in.knownEntities = &known;
    in.budget = budget;
    return validateQuest(q, in);
}

} // namespace

TEST_CASE("the authored spine file parses cleanly", "[spine][quests][data]")
{
    const MainSpine s = loadSpine();
    REQUIRE(s.error.empty());
    CHECK(s.spine.slots.size() >= 8);
    CHECK(s.quest.nodes.size() >= 8);
    CHECK(s.budget > 0);
}

TEST_CASE("every beat names a declared slot and a declared earlier beat", "[spine][quests][data]")
{
    const MainSpine s = loadSpine();
    REQUIRE(s.error.empty());
    std::set<std::string> slots;
    for (const auto& sl : s.spine.slots)
        slots.insert(sl.id);
    std::set<std::string> seen;
    for (const QuestNode& n : s.quest.nodes) {
        CHECK(slots.count(n.entity) == 1);
        for (const auto& a : n.after)
            CHECK(seen.count(a) == 1);
        seen.insert(n.id);
    }
    std::set<std::string> used;
    for (const QuestNode& n : s.quest.nodes)
        used.insert(n.entity);
    for (const auto& sl : slots)
        CHECK(used.count(sl) == 1);
}

TEST_CASE("the spine binds over a sample world and validates for many seeds", "[spine][quests]")
{
    const MainSpine s = loadSpine();
    REQUIRE(s.error.empty());
    const sp::Cast cast = sampleWorld();
    for (std::uint64_t seed = 0; seed < 64; ++seed) {
        const sp::Result r = sp::bind(s.spine, cast, seed);
        REQUIRE(r.ok);
        const QuestVerdict v = validateBound(s, r, cast, s.budget);
        INFO("seed " << seed);
        CHECK(v.valid);
        CHECK(v.issues.empty());
        CHECK(v.totalCost <= s.budget);
    }
}

TEST_CASE("the same beats reach different casts across seeds", "[spine][quests]")
{
    const MainSpine s = loadSpine();
    const sp::Cast cast = sampleWorld();
    std::set<std::string> casts;
    for (std::uint64_t seed = 0; seed < 64; ++seed) {
        const sp::Result r = sp::bind(s.spine, cast, seed);
        REQUIRE(r.ok);
        std::string key;
        for (const auto& b : r.bindings)
            key += b.slot + "=" + b.entity + ";";
        casts.insert(key);
    }
    CHECK(casts.size() > 1);
}

TEST_CASE("a world that cannot serve the spine fails instead of inventing a cast", "[spine][quests]")
{
    const MainSpine s = loadSpine();
    sp::Cast cast = sampleWorld();
    for (auto& f : cast.factions)
        f.hostileTo.clear();
    CHECK_FALSE(sp::bind(s.spine, cast, 1).ok);

    sp::Cast noWitness = sampleWorld();
    for (auto& n : noWitness.npcs)
        n.tags.erase(std::remove(n.tags.begin(), n.tags.end(), "rememberer"), n.tags.end());
    CHECK_FALSE(sp::bind(s.spine, noWitness, 1).ok);
}

TEST_CASE("quest validation rejects a broken spine", "[spine][quests]")
{
    const MainSpine s = loadSpine();
    const sp::Cast cast = sampleWorld();
    const sp::Result r = sp::bind(s.spine, cast, 3);
    REQUIRE(r.ok);

    CHECK_FALSE(validateBound(s, r, cast, 5).valid);

    MainSpine noExit = s;
    for (QuestNode& n : noExit.quest.nodes)
        n.refusable = false;
    CHECK_FALSE(validateBound(noExit, r, cast, s.budget).valid);

    MainSpine dangling = s;
    dangling.quest.nodes[1].after.push_back("spine.beat.nowhere");
    CHECK_FALSE(validateBound(dangling, r, cast, s.budget).valid);

    sp::Result unbound = r;
    unbound.bindings.pop_back();
    CHECK_FALSE(validateBound(s, unbound, cast, s.budget).valid);
}

TEST_CASE("hostile spine text never crashes the parser", "[spine][quests]")
{
    CHECK_FALSE(parseSpine("").error.empty());
    CHECK_FALSE(parseSpine("{").error.empty());
    CHECK_FALSE(parseSpine("{\"a\":1}").error.empty());
    CHECK_FALSE(parseSpine("[{\"type\":\"bogus\"}]").error.empty());
}
