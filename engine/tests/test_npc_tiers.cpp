#include "ichor/data/npc_tiers.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <process.h>
#define ICHOR_PID _getpid()
#else
#include <unistd.h>
#define ICHOR_PID getpid()
#endif

using namespace ichor;

namespace {

class Fixture {
public:
    Fixture()
    {
        static int counter = 0;
        root_ = std::filesystem::temp_directory_path() / ("ichor_npc_tiers_test_" + std::to_string(ICHOR_PID) + "_" + std::to_string(counter++));
        std::filesystem::remove_all(root_);
        std::filesystem::create_directories(root_);
    }
    ~Fixture() { std::filesystem::remove_all(root_); }
    void write(const std::string& name, const std::string& text) const { std::ofstream(root_ / name, std::ios::binary) << text; }
    const std::filesystem::path& root() const { return root_; }

private:
    std::filesystem::path root_;
};

const std::string kPrincipal = R"({"id":"auditor","tier":"principal","name":"Teles the Auditor","voice":"auditor_dry","arc":"ledger_truth","vo":"auditor_vo","face":"auditor_face","cast_roles":["faction_leader","ledger_keeper"]})";
const std::string kRegular = R"({"id":"ilse","tier":"regular","name":"Ilse Vance","home":"saltmarsh","role":"salt_panner","traits":["bitter","proud"],
  "relationships":[{"kind":"family","target":"harl"},{"kind":"creditor","target":"auditor"},{"kind":"rival","target":"maren"}],
  "want":"Wants her brother off the dive crews.","fear":"Drowning like her father.","memory":"ledger_ilse"})";
const std::string kHarl = R"({"id":"harl","tier":"resident","name":"Harl Vance","location":"saltmarsh"})";
const std::string kMaren = R"({"id":"maren","tier":"resident","name":"Maren","location":"saltmarsh"})";
const std::string kCrowd = R"({"id":"market_murmur","tier":"crowd","ambience":"market_chatter"})";

std::string array(const std::vector<std::string>& records)
{
    std::string out = "[";
    for (std::size_t i = 0; i < records.size(); ++i) {
        out += (i == 0 ? "" : ",") + records[i];
    }
    return out + "]";
}

std::vector<std::string> parse(const std::string& text, std::vector<NpcRecord>* out = nullptr)
{
    std::vector<NpcRecord> records;
    std::vector<std::string> problems;
    const bool ok = parseNpcFile("npcs/t.json", text, records, problems);
    REQUIRE(ok == problems.empty());
    if (out != nullptr) {
        *out = records;
    }
    return problems;
}

bool mentions(const std::vector<std::string>& problems, const std::string& needle)
{
    return std::any_of(problems.begin(), problems.end(), [&](const std::string& p) { return p.find(needle) != std::string::npos; });
}

std::string replaced(std::string text, const std::string& from, const std::string& to)
{
    const auto at = text.find(from);
    REQUIRE(at != std::string::npos);
    return text.replace(at, from.size(), to);
}

}

TEST_CASE("tiers are named as in doc 10")
{
    CHECK(npcTierName(NpcTier::Principal) == "principal");
    CHECK(npcTierName(NpcTier::Regular) == "regular");
    CHECK(npcTierName(NpcTier::Resident) == "resident");
    CHECK(npcTierName(NpcTier::Crowd) == "crowd");
    CHECK(relationKindName(RelationKind::Creditor) == "creditor");
}

TEST_CASE("one record of each tier loads with exactly its fields")
{
    std::vector<NpcRecord> records;
    const auto problems = parse(array({kPrincipal, kRegular, kHarl, kCrowd}), &records);
    REQUIRE(problems.empty());
    REQUIRE(records.size() == 4);

    const auto& principal = std::get<Principal>(records[0]);
    CHECK(principal.name == "Teles the Auditor");
    CHECK(principal.face == "auditor_face");
    CHECK(principal.castRoles == std::vector<std::string> {"faction_leader", "ledger_keeper"});

    const auto& regular = std::get<Regular>(records[1]);
    CHECK(regular.home == "saltmarsh");
    CHECK(regular.role == "salt_panner");
    CHECK(regular.traits == std::vector<std::string> {"bitter", "proud"});
    REQUIRE(regular.relationships.size() == 3);
    CHECK(regular.relationships[1].kind == RelationKind::Creditor);
    CHECK(regular.relationships[1].target == "auditor");
    CHECK(regular.memory == "ledger_ilse");

    CHECK(std::get<Resident>(records[2]).location == "saltmarsh");
    CHECK(std::get<Crowd>(records[3]).ambience == "market_chatter");
    CHECK(npcTier(records[0]) == NpcTier::Principal);
    CHECK(npcTier(records[3]) == NpcTier::Crowd);
    CHECK(npcId(records[1]) == "ilse");
}

TEST_CASE("an unknown or missing tier is refused and listed")
{
    CHECK(mentions(parse(array({replaced(kHarl, "resident", "deity")})), "[0].tier: expected one of principal, regular, resident, crowd"));
    CHECK(mentions(parse(array({replaced(kHarl, R"("tier":"resident",)", "")})), "[0].tier: missing"));
    CHECK(mentions(parse(array({replaced(kHarl, R"("resident")", "3")})), "[0].tier: expected a string"));
}

TEST_CASE("a field another tier owns is refused")
{
    CHECK(mentions(parse(array({replaced(kCrowd, R"("ambience")", R"("name":"Bob","ambience")")})), "[0].name: not allowed for tier crowd"));
    CHECK(mentions(parse(array({replaced(kHarl, R"("location")", R"("want":"x","location")")})), "[0].want: not allowed for tier resident"));
    CHECK(mentions(parse(array({replaced(kPrincipal, R"("voice")", R"("traits":["bitter","proud"],"voice")")})), "[0].traits: not allowed for tier principal"));
    CHECK(mentions(parse(array({replaced(kRegular, R"("want")", R"("face":"f","want")")})), "[0].face: not allowed for tier regular"));
}

TEST_CASE("a field nobody has is an unknown key")
{
    CHECK(mentions(parse(array({replaced(kHarl, R"("location")", R"("mood":"x","location")")})), "[0].mood: unknown key"));
}

TEST_CASE("every required field of each tier is required")
{
    const std::vector<std::pair<std::string, std::string>> cases = {
        {kPrincipal, R"("voice":"auditor_dry",)"},
        {kPrincipal, R"("arc":"ledger_truth",)"},
        {kPrincipal, R"("vo":"auditor_vo",)"},
        {kPrincipal, R"("face":"auditor_face",)"},
        {kPrincipal, R"(,"cast_roles":["faction_leader","ledger_keeper"])"},
        {kRegular, R"("home":"saltmarsh",)"},
        {kRegular, R"("role":"salt_panner",)"},
        {kRegular, R"("fear":"Drowning like her father.",)"},
        {kRegular, R"(,"memory":"ledger_ilse")"},
        {kHarl, R"(,"location":"saltmarsh")"},
        {kHarl, R"("name":"Harl Vance",)"},
        {kCrowd, R"(,"ambience":"market_chatter")"},
    };
    for (const auto& [record, field] : cases) {
        INFO(field);
        CHECK(mentions(parse(array({replaced(record, field, "")})), ": missing"));
    }
}

TEST_CASE("traits and relationships keep to the doc's counts")
{
    CHECK(mentions(parse(array({replaced(kRegular, R"(["bitter","proud"])", R"(["bitter"])")})), "traits: expected 2 to 3"));
    CHECK(mentions(parse(array({replaced(kRegular, R"(["bitter","proud"])", R"(["bitter","proud","pious","greedy"])")})), "traits: expected 2 to 3"));
    CHECK(mentions(parse(array({replaced(kRegular, R"(["bitter","proud"])", R"(["bitter","bitter"])")})), "duplicate"));
    CHECK(mentions(parse(array({replaced(kRegular, R"(,{"kind":"rival","target":"maren"})", "")})), "relationships: expected 3 to 5"));
    const std::string six = R"("relationships":[{"kind":"family","target":"a"},{"kind":"family","target":"b"},{"kind":"family","target":"c"},{"kind":"family","target":"d"},{"kind":"family","target":"e"},{"kind":"family","target":"f"}],)";
    const std::string base = R"({"id":"x","tier":"regular","name":"X","home":"h","role":"r","traits":["a","b"],)" + six + R"("want":"w","fear":"f","memory":"m"})";
    CHECK(mentions(parse(array({base})), "relationships: expected 3 to 5"));
}

TEST_CASE("relationship entries are checked")
{
    CHECK(mentions(parse(array({replaced(kRegular, R"("kind":"family")", R"("kind":"cousin")")})), "relationships[0].kind: expected one of family, rival, creditor, lover, grudge"));
    CHECK(mentions(parse(array({replaced(kRegular, R"("target":"harl")", R"("target":3)")})), "relationships[0].target: expected a string"));
    CHECK(mentions(parse(array({replaced(kRegular, R"({"kind":"family","target":"harl"})", R"({"kind":"family"})")})), "relationships[0].target: missing"));
    CHECK(mentions(parse(array({replaced(kRegular, R"({"kind":"family","target":"harl"})", R"({"kind":"family","target":"harl","note":"x"})")})), "relationships[0].note: unknown key"));
    CHECK(mentions(parse(array({replaced(kRegular, R"({"kind":"family","target":"harl"})", R"({"kind":"rival","target":"maren"})")})), "relationships[2]: duplicate"));
    CHECK(mentions(parse(array({replaced(kRegular, R"({"kind":"family","target":"harl"})", "7")})), "relationships[0]: expected an object"));
}

TEST_CASE("wrong JSON types are named")
{
    CHECK(mentions(parse(array({replaced(kHarl, R"("Harl Vance")", "42")})), "[0].name: expected a string, got number"));
    CHECK(mentions(parse(array({replaced(kPrincipal, R"(["faction_leader","ledger_keeper"])", R"("faction_leader")")})), "cast_roles: expected an array"));
    CHECK(mentions(parse(array({replaced(kRegular, R"(["bitter","proud"])", R"([1,2])")})), "traits[0]: expected a string"));
    CHECK(mentions(parse(array({"5"})), "[0]: expected an object"));
    CHECK(mentions(parse(R"({"id":"a"})"), "expected an array"));
    CHECK(mentions(parse(array({replaced(kHarl, R"("id":"harl")", R"("id":null)")})), "[0].id: expected a string"));
}

TEST_CASE("ids, words and text follow the data conventions")
{
    CHECK(mentions(parse(array({replaced(kHarl, R"("id":"harl")", R"("id":"Harl V")")})), "[0].id: expected a lowercase ascii id"));
    CHECK(mentions(parse(array({replaced(kHarl, R"("id":"harl")", R"("id":"")")})), "[0].id"));
    CHECK(mentions(parse(array({replaced(kHarl, R"("saltmarsh")", R"("Salt Marsh")")})), "[0].location: expected a lowercase ascii id"));
    CHECK(mentions(parse(array({replaced(kHarl, R"("Harl Vance")", R"("")")})), "[0].name: must not be empty"));
    CHECK(mentions(parse(array({replaced(kHarl, R"("Harl Vance")", R"("Harl — Vance")")})), "[0].name: must not contain an em-dash"));
}

TEST_CASE("huge strings and huge lists are refused, not stored")
{
    const std::string huge(100000, 'a');
    CHECK(mentions(parse(array({replaced(kHarl, "Harl Vance", huge)})), "[0].name: longer than"));
    CHECK(mentions(parse(array({replaced(kHarl, R"("id":"harl")", R"("id":")" + huge + R"(")")})), "[0].id: longer than"));
    CHECK(mentions(parse(array({replaced(kRegular, "Wants her brother off the dive crews.", huge)})), "[0].want: longer than"));
    std::string many = "[";
    for (int i = 0; i < 6000; ++i) {
        many += std::string(i == 0 ? "" : ",") + R"({"id":"c)" + std::to_string(i) + R"(","tier":"crowd","ambience":"a"})";
    }
    CHECK(mentions(parse(many + "]"), "more than"));
    std::string roles = "[";
    for (int i = 0; i < 100; ++i) {
        roles += std::string(i == 0 ? "" : ",") + "\"r" + std::to_string(i) + "\"";
    }
    CHECK(mentions(parse(array({replaced(kPrincipal, R"(["faction_leader","ledger_keeper"])", roles + "]")})), "cast_roles: expected 1 to"));
}

TEST_CASE("empty, malformed and hostile files are refused")
{
    CHECK(mentions(parse(""), "empty file"));
    CHECK(mentions(parse("  \n\t "), "empty file"));
    CHECK(mentions(parse("[{"), "not valid JSON"));
    CHECK(mentions(parse(std::string(2'000'000, ' ')), "larger than"));
    CHECK(mentions(parse(std::string(100000, '[')), "nested deeper"));
    CHECK(mentions(parse(array({})), "no records"));
}

TEST_CASE("every problem is listed, with its path")
{
    const auto problems = parse(array({replaced(kHarl, R"("Harl Vance")", "1"), replaced(kCrowd, R"("market_chatter")", "true"), R"({"id":"z","tier":"nope"})"}));
    CHECK(problems.size() == 3);
    CHECK(mentions(problems, "npcs/t.json: [0].name"));
    CHECK(mentions(problems, "npcs/t.json: [1].ambience"));
    CHECK(mentions(problems, "npcs/t.json: [2].tier"));
}

TEST_CASE("duplicate ids inside a file are refused")
{
    CHECK(mentions(parse(array({kHarl, replaced(kCrowd, "market_murmur", "harl")})), "[1].id: duplicate of \"harl\""));
}

TEST_CASE("canonical form and digest do not depend on key order or whitespace")
{
    std::vector<NpcRecord> a;
    std::vector<NpcRecord> b;
    REQUIRE(parse(array({kRegular}), &a).empty());
    REQUIRE(parse(R"([ {"memory":"ledger_ilse","fear":"Drowning like her father.","want":"Wants her brother off the dive crews.",
      "relationships":[{"target":"harl","kind":"family"},{"target":"auditor","kind":"creditor"},{"target":"maren","kind":"rival"}],
      "traits":["bitter","proud"],"role":"salt_panner","home":"saltmarsh","name":"Ilse Vance","tier":"regular","id":"ilse"} ])", &b).empty());
    CHECK(npcCanonicalJson(a[0]) == npcCanonicalJson(b[0]));
    CHECK(npcDigest(a[0]) == npcDigest(b[0]));
    CHECK(npcCanonicalJson(a[0]).find(R"("tier":"regular")") != std::string::npos);

    std::vector<NpcRecord> c;
    REQUIRE(parse(array({replaced(kRegular, "Ilse Vance", "Ilse Vanse")}), &c).empty());
    CHECK(npcDigest(a[0]) != npcDigest(c[0]));
    std::vector<NpcRecord> d;
    REQUIRE(parse(array({replaced(kRegular, R"(["bitter","proud"])", R"(["proud","bitter"])")}), &d).empty());
    CHECK(npcDigest(a[0]) != npcDigest(d[0]));
}

TEST_CASE("duplicate keys are refused at every level")
{
    CHECK(mentions(parse(R"([{"id":"a","tier":"crowd","ambience":"x","ambience":"y"}])"), "duplicate key \"ambience\""));
    CHECK(mentions(parse(R"([{"id":"a","tier":"crowd","tier":"resident","ambience":"x"}])"), "duplicate key \"tier\""));
    CHECK(mentions(parse(array({replaced(kRegular, R"({"kind":"family","target":"harl"})", R"({"kind":"family","kind":"rival","target":"harl"})")})), "duplicate key \"kind\""));
}

TEST_CASE("a missing id and an em-dash in any text field are refused")
{
    CHECK(mentions(parse(array({replaced(kHarl, R"("id":"harl",)", "")})), "[0].id: missing"));
    CHECK(mentions(parse(array({replaced(kRegular, "Wants her brother", "Wants \xE2\x80\x94 her brother")})), "[0].want: must not contain an em-dash"));
    CHECK(mentions(parse(array({replaced(kRegular, "Drowning", "Drowning \xE2\x80\x94")})), "[0].fear: must not contain an em-dash"));
}

TEST_CASE("canonical form is key-sorted and pinned, and the roster digest is pinned")
{
    std::vector<NpcRecord> records;
    REQUIRE(parse(array({kPrincipal, kRegular, kHarl, kCrowd}), &records).empty());
    CHECK(npcCanonicalJson(records[0]) == R"({"arc":"ledger_truth","cast_roles":["faction_leader","ledger_keeper"],"face":"auditor_face","id":"auditor","name":"Teles the Auditor","tier":"principal","vo":"auditor_vo","voice":"auditor_dry"})");
    CHECK(npcCanonicalJson(records[1]) == R"({"fear":"Drowning like her father.","home":"saltmarsh","id":"ilse","memory":"ledger_ilse","name":"Ilse Vance","relationships":[{"kind":"family","target":"harl"},{"kind":"creditor","target":"auditor"},{"kind":"rival","target":"maren"}],"role":"salt_panner","tier":"regular","traits":["bitter","proud"],"want":"Wants her brother off the dive crews."})");
    CHECK(npcCanonicalJson(records[2]) == R"({"id":"harl","location":"saltmarsh","name":"Harl Vance","tier":"resident"})");
    CHECK(npcCanonicalJson(records[3]) == R"({"ambience":"market_chatter","id":"market_murmur","tier":"crowd"})");

    Fixture fixture;
    fixture.write("a.json", array({kPrincipal, kRegular, kHarl, kMaren, kCrowd}));
    NpcRoster roster;
    REQUIRE(roster.loadDirectory(fixture.root()));
    CHECK(roster.digest() == 0x88a428eb1c5ec5b2ULL);
}

TEST_CASE("a roster loads every file, counts tiers and digests independent of file layout")
{
    Fixture one;
    one.write("a.json", array({kPrincipal, kRegular}));
    one.write("b.json", array({kHarl, kMaren, kCrowd}));
    one.write("notes.txt", "ignored");
    NpcRoster roster;
    REQUIRE(roster.loadDirectory(one.root()));
    CHECK(roster.problems().empty());
    CHECK(roster.records().size() == 5);
    CHECK(roster.count(NpcTier::Principal) == 1);
    CHECK(roster.count(NpcTier::Regular) == 1);
    CHECK(roster.count(NpcTier::Resident) == 2);
    CHECK(roster.count(NpcTier::Crowd) == 1);
    REQUIRE(roster.find("ilse") != nullptr);
    CHECK(npcTier(*roster.find("ilse")) == NpcTier::Regular);
    CHECK(roster.find("nobody") == nullptr);

    Fixture other;
    other.write("z.json", array({kCrowd, kMaren, kHarl, kRegular}));
    other.write("y.json", array({kPrincipal}));
    NpcRoster reshuffled;
    REQUIRE(reshuffled.loadDirectory(other.root()));
    CHECK(reshuffled.digest() == roster.digest());
    CHECK(roster.digest() != 0);
}

TEST_CASE("duplicate ids across files and tiers are refused")
{
    Fixture fixture;
    fixture.write("a.json", array({kPrincipal, kHarl, kMaren}));
    fixture.write("b.json", array({replaced(kCrowd, "market_murmur", "auditor")}));
    NpcRoster roster;
    CHECK_FALSE(roster.loadDirectory(fixture.root()));
    CHECK(mentions(roster.problems(), "b.json: [0].id: duplicate of \"auditor\" in a.json"));
}

TEST_CASE("relationships must name another named NPC")
{
    Fixture fixture;
    fixture.write("a.json", array({kRegular, kHarl, kMaren}));
    NpcRoster roster;
    CHECK_FALSE(roster.loadDirectory(fixture.root()));
    CHECK(mentions(roster.problems(), "a.json: [0].relationships[1].target: unknown NPC \"auditor\""));

    Fixture self;
    self.write("a.json", array({replaced(kRegular, R"("target":"harl")", R"("target":"ilse")"), kHarl, kMaren, kPrincipal}));
    NpcRoster selfRoster;
    CHECK_FALSE(selfRoster.loadDirectory(self.root()));
    CHECK(mentions(selfRoster.problems(), "relationships[0].target: an NPC cannot be related to itself"));

    Fixture crowd;
    crowd.write("a.json", array({replaced(kRegular, R"("target":"harl")", R"("target":"market_murmur")"), kHarl, kMaren, kPrincipal, kCrowd}));
    NpcRoster crowdRoster;
    CHECK_FALSE(crowdRoster.loadDirectory(crowd.root()));
    CHECK(mentions(crowdRoster.problems(), "relationships[0].target: \"market_murmur\" is crowd and has no name"));
}

TEST_CASE("a failed load changes nothing and lists problems across all files")
{
    Fixture fixture;
    fixture.write("a.json", array({kHarl}));
    NpcRoster roster;
    REQUIRE(roster.loadDirectory(fixture.root()));
    const auto digest = roster.digest();

    fixture.write("a.json", array({replaced(kHarl, "resident", "deity")}));
    fixture.write("b.json", "");
    CHECK_FALSE(roster.loadDirectory(fixture.root()));
    CHECK(roster.problems().size() == 2);
    CHECK(roster.digest() == digest);
    CHECK(roster.records().size() == 1);

    CHECK_FALSE(roster.loadDirectory(fixture.root() / "missing"));
    CHECK(mentions(roster.problems(), "no NPC directory"));
}
