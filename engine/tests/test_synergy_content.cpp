#include "ichor/data/data_set.h"
#include "ichor/data/synergy.h"
#include "ichor/data/synergy_content.h"
#include "ichor/data/xref.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace ichor;

namespace {

AxiomPool pool()
{
    return {
        {"cataclysm.artemis_withdrew", AxiomSlot::Cataclysm},
        {"cataclysm.flood", AxiomSlot::Cataclysm},
        {"law.hecatomb", AxiomSlot::Law},
        {"law.oath", AxiomSlot::Law},
        {"pressure.unburied", AxiomSlot::Pressure},
        {"pressure.opening", AxiomSlot::Pressure},
    };
}

const std::string kTier1 = R"x("id":"a","tier":"cataclysm_law","axioms":["cataclysm.artemis_withdrew","law.hecatomb"],"content_tier":1,"faction":"hekatombaia","dungeon_type":"levy_house","mechanic":"human_levy","world_name":"The Levied Shore")x";
const std::string kTier2 = R"x("id":"b","tier":"pressure","axioms":["cataclysm.flood","pressure.opening"],"content_tier":2,"world_name":"The Rising Rift","flavour":{"shrine":"The water is still rising."})x";

std::string file(const std::string& body) { return "{\"synergies\":[{" + body + "}]}"; }

bool mentions(const std::vector<std::string>& problems, const std::string& needle)
{
    return std::any_of(problems.begin(), problems.end(), [&](const std::string& p) { return p.find(needle) != std::string::npos; });
}

// Parse problems first, then content problems; empty when the text is a valid record set.
std::vector<std::string> problemsOf(const std::string& text, std::vector<Synergy>* out = nullptr)
{
    std::vector<Synergy> defs;
    std::vector<std::string> problems;
    if (parseSynergies("synergies/t.json", text, pool(), defs, problems)) {
        validateSynergyContent("synergies/t.json", defs, problems);
    }
    if (out != nullptr) {
        *out = defs;
    }
    return problems;
}

// Removes one top-level `"field":value` member from a record body (the fields in the fixtures are never first).
std::string without(std::string body, const std::string& field)
{
    const std::string key = ",\"" + field + "\":";
    const auto at = body.find(key);
    REQUIRE(at != std::string::npos);
    std::size_t end = at + key.size();
    int depth = 0;
    bool inString = false;
    for (; end < body.size(); ++end) {
        const char c = body[end];
        if (c == '"') {
            inString = !inString;
        } else if (inString) {
            continue;
        } else if (c == '{' || c == '[') {
            ++depth;
        } else if (c == '}' || c == ']') {
            --depth;
        } else if (c == ',' && depth == 0) {
            break;
        }
    }
    return body.erase(at, end - at);
}

struct TempDir {
    std::filesystem::path path;
    explicit TempDir(const std::string& name)
        : path(std::filesystem::path(ICHOR_TEST_OUTPUT) / name)
    {
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~TempDir()
    {
        std::error_code code;
        std::filesystem::remove_all(path, code);
    }
    void write(const std::string& name, const std::string& text) const
    {
        std::filesystem::create_directories((path / name).parent_path());
        std::ofstream(path / name, std::ios::binary) << text;
    }
};

}

TEST_CASE("a tier 1 and a tier 2 record validate and carry their content tier", "[synergy_content]")
{
    std::vector<Synergy> defs;
    REQUIRE(problemsOf("{\"synergies\":[{" + kTier1 + "},{" + kTier2 + "}]}", &defs).empty());
    REQUIRE(defs.size() == 2);
    CHECK(defs[0].contentTier == ContentTier::Flagship);
    CHECK(defs[0].mechanic == "human_levy");
    CHECK(defs[0].faction == "hekatombaia");
    CHECK(defs[1].contentTier == ContentTier::TextOnly);
    CHECK(defs[1].mechanic.empty());
}

TEST_CASE("files without content_tier still parse (the existing format is unchanged)", "[synergy_content]")
{
    std::vector<Synergy> defs;
    std::vector<std::string> problems;
    const std::string text = file(R"x("id":"a","tier":"cataclysm_law","axioms":["cataclysm.flood","law.oath"])x");
    REQUIRE(parseSynergies("t", text, pool(), defs, problems));
    CHECK(defs[0].contentTier == ContentTier::Unset);
}

TEST_CASE("content validation requires a content tier", "[synergy_content]")
{
    CHECK(mentions(problemsOf(file(without(kTier1, "content_tier"))), "a.content_tier: missing"));
}

TEST_CASE("tier 1 requires faction, dungeon_type, mechanic and world_name", "[synergy_content]")
{
    for (const char* field : {"faction", "dungeon_type", "mechanic", "world_name"}) {
        INFO(field);
        CHECK(mentions(problemsOf(file(without(kTier1, field))), std::string("a.") + field + ": required for content tier 1"));
    }
}

TEST_CASE("tier 2 is text only: it must carry a world name and flavour and no faction, dungeon or mechanic", "[synergy_content]")
{
    CHECK(mentions(problemsOf(file(without(kTier2, "world_name"))), "b.world_name: required for content tier 2"));
    CHECK(mentions(problemsOf(file(without(kTier2, "flavour"))), "b.flavour: required for content tier 2"));
    CHECK(mentions(problemsOf(file(kTier2 + ",\"faction\":\"hekatombaia\"")), "b.faction: not allowed for content tier 2"));
    CHECK(mentions(problemsOf(file(kTier2 + ",\"dungeon_type\":\"levy_house\"")), "b.dungeon_type: not allowed for content tier 2"));
    CHECK(mentions(problemsOf(file(kTier2 + ",\"mechanic\":\"human_levy\"")), "b.mechanic: not allowed for content tier 2"));
}

TEST_CASE("content_tier and mechanic reject malformed values", "[synergy_content]")
{
    for (const char* bad : {"0", "3", "-1", "1.5", "\"1\"", "true", "null", "[]", "{}", "18446744073709551616"}) {
        INFO(bad);
        std::string body = kTier1;
        body.replace(body.find("\"content_tier\":1"), 16, std::string("\"content_tier\":") + bad);
        CHECK(mentions(problemsOf(file(body)), "synergies[0].content_tier"));
    }
    CHECK_FALSE(problemsOf(file(kTier1 + ",\"x\":1e999")).empty());
    for (const char* bad : {"\"Bad Id\"", "\"\"", "7", "null", "\"a\\u0000b\"", "\"\\u00e9\""}) {
        INFO(bad);
        std::string body = kTier1;
        body.replace(body.find("\"mechanic\":\"human_levy\""), 23, std::string("\"mechanic\":") + bad);
        CHECK(mentions(problemsOf(file(body)), "synergies[0].mechanic"));
    }
    CHECK(mentions(problemsOf(file(without(kTier1, "mechanic") + ",\"mechanic\":\"" + std::string(65, 'x') + "\"")), "synergies[0].mechanic"));
}

TEST_CASE("hostile text in a tier 2 record is rejected, not stored", "[synergy_content]")
{
    CHECK_FALSE(problemsOf(file(without(kTier2, "world_name") + ",\"world_name\":\"a\\u0007b\"")).empty());
    CHECK_FALSE(problemsOf(file(without(kTier2, "flavour") + ",\"flavour\":{\"shrine\":\"" + std::string(1001, 'x') + "\"}")).empty());
    CHECK_FALSE(problemsOf(file(without(kTier2, "flavour") + ",\"flavour\":{}")).empty());
    CHECK_FALSE(problemsOf(std::string("{\"synergies\":[{") + kTier2 + std::string("\0\0", 2) + "}]}").empty());
    CHECK_FALSE(problemsOf(std::string(300, '[')).empty());
    CHECK_FALSE(problemsOf("").empty());
    CHECK_FALSE(problemsOf("\xff\xfe{").empty());
}

TEST_CASE("a mixed file reports every content problem with the record id", "[synergy_content]")
{
    const auto problems = problemsOf("{\"synergies\":[{" + without(without(kTier1, "faction"), "mechanic") + "},{" + kTier2 + ",\"mechanic\":\"x\"" + "}]}");
    CHECK(mentions(problems, "a.faction"));
    CHECK(mentions(problems, "a.mechanic"));
    CHECK(mentions(problems, "b.mechanic"));
}

TEST_CASE("loading a directory merges files and rejects clashes across them", "[synergy_content]")
{
    TempDir dir("synergy_content_dir");
    dir.write("one.json", file(kTier1));
    dir.write("two.json", file(kTier2));
    dir.write("notes.txt", "not json");
    std::vector<Synergy> defs;
    std::vector<std::string> problems;
    REQUIRE(loadSynergyContentDirectory(dir.path, pool(), defs, problems));
    REQUIRE(defs.size() == 2);
    CHECK(defs[0].id == "a");

    dir.write("three.json", file(kTier1));
    defs.clear();
    CHECK_FALSE(loadSynergyContentDirectory(dir.path, pool(), defs, problems));
    CHECK(mentions(problems, "duplicate id \"a\""));
    CHECK(mentions(problems, "same axioms"));
    CHECK(defs.empty());
}

TEST_CASE("a bad file in a directory fails the load with its own label", "[synergy_content]")
{
    TempDir dir("synergy_content_bad");
    dir.write("good.json", file(kTier1));
    dir.write("bad.json", "{not json");
    std::vector<Synergy> defs;
    std::vector<std::string> problems;
    CHECK_FALSE(loadSynergyContentDirectory(dir.path, pool(), defs, problems));
    CHECK(mentions(problems, "bad.json"));
    problems.clear();
    CHECK_FALSE(loadSynergyContentDirectory(dir.path / "missing", pool(), defs, problems));
    CHECK(mentions(problems, "cannot read directory"));
}

TEST_CASE("a directory with too many files is refused", "[synergy_content]")
{
    TempDir dir("synergy_content_many");
    for (int i = 0; i < 300; ++i) {
        dir.write("f" + std::to_string(i) + ".json", "{}");
    }
    std::vector<Synergy> defs;
    std::vector<std::string> problems;
    CHECK_FALSE(loadSynergyContentDirectory(dir.path, pool(), defs, problems));
    CHECK(mentions(problems, "more than"));
}

TEST_CASE("the shipped synergies load against the shipped axioms and have no dangling reference", "[synergy_content]")
{
    const std::filesystem::path root = ICHOR_GAME_DATA_DIR;
    DataSet data;
    AxiomSet axioms;
    REQUIRE(data.loadDirectory(root));
    REQUIRE(axioms.loadDirectory(root));
    std::vector<Synergy> defs;
    std::vector<std::string> problems;
    const bool ok = loadSynergyContentDirectory(root / "synergies", axiomPool(axioms), defs, problems);
    for (const std::string& problem : problems) {
        UNSCOPED_INFO(problem);
    }
    REQUIRE(ok);
    const auto tier = [&](ContentTier t) { return std::count_if(defs.begin(), defs.end(), [&](const Synergy& s) { return s.contentTier == t; }); };
    CHECK(tier(ContentTier::Flagship) >= 1);
    CHECK(tier(ContentTier::TextOnly) >= 1);
    const XrefReport report = checkReferences(data, axioms, root);
    for (const DanglingRef& ref : report.dangling) {
        UNSCOPED_INFO(describe(ref));
    }
    CHECK(report.ok());
}

TEST_CASE("a synergy naming a faction that does not exist is a dangling reference", "[synergy_content]")
{
    TempDir dir("synergy_content_xref");
    const std::filesystem::path real = ICHOR_GAME_DATA_DIR;
    std::filesystem::copy(real, dir.path, std::filesystem::copy_options::recursive);
    dir.write("synergies/ghost.json", file(R"x("id":"ghost","tier":"cataclysm_law","axioms":["cataclysm.flood","law.orichalcum"],"content_tier":1,"faction":"no_such_faction","dungeon_type":"x","mechanic":"y","world_name":"Z")x"));
    DataSet data;
    AxiomSet axioms;
    REQUIRE(data.loadDirectory(dir.path));
    REQUIRE(axioms.loadDirectory(dir.path));
    const XrefReport report = checkReferences(data, axioms, dir.path);
    REQUIRE_FALSE(report.ok());
    REQUIRE(report.dangling.size() == 1);
    CHECK(report.dangling[0].record == "ghost");
    CHECK(report.dangling[0].field == "faction");
    CHECK(report.dangling[0].target == "factions");
}

TEST_CASE("the shipped flagship fires from its axioms and injects its faction", "[synergy_content]")
{
    const std::filesystem::path root = ICHOR_GAME_DATA_DIR;
    AxiomSet axioms;
    REQUIRE(axioms.loadDirectory(root));
    std::vector<Synergy> defs;
    std::vector<std::string> problems;
    REQUIRE(loadSynergyContentDirectory(root / "synergies", axiomPool(axioms), defs, problems));
    const auto fired = detectSynergies({"cataclysm.artemis_withdrew", "law.hecatomb"}, defs);
    REQUIRE_FALSE(fired.empty());
    const SynergyOverride result = applyOverrides(fired, defs, {});
    CHECK(result.factions == std::vector<std::string>{"hekatombaia"});
    CHECK_FALSE(result.dungeonTypes.empty());
    CHECK_FALSE(result.worldName.empty());
}

TEST_CASE("the invaders' animals synergy loads, passes xref and fires for its axiom pair", "[synergy_content]")
{
    const std::filesystem::path root = ICHOR_GAME_DATA_DIR;
    DataSet data;
    AxiomSet axioms;
    REQUIRE(data.loadDirectory(root));
    REQUIRE(axioms.loadDirectory(root));
    std::vector<Synergy> defs;
    std::vector<std::string> problems;
    REQUIRE(loadSynergyContentDirectory(root / "synergies", axiomPool(axioms), defs, problems));
    const auto found = std::find_if(defs.begin(), defs.end(), [](const Synergy& s) { return s.id == "artemis_satrapy"; });
    REQUIRE(found != defs.end());
    CHECK(found->contentTier == ContentTier::Flagship);
    CHECK(checkReferences(data, axioms, root).ok());

    const auto fired = detectSynergies({"cataclysm.artemis_withdrew", "powers.satrapy"}, defs);
    const SynergyOverride result = applyOverrides(fired, defs, {});
    CHECK(result.factions == std::vector<std::string>{"diadochoi"});
    CHECK(result.worldName == "The Stabled Occupation");
    CHECK_FALSE(result.dungeonTypes.empty());

    const auto unrelated = detectSynergies({"cataclysm.artemis_withdrew", "powers.hegemony"}, defs);
    CHECK(applyOverrides(unrelated, defs, {}).worldName != "The Stabled Occupation");
}
