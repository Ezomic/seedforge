#include "ichor/data/axiom.h"
#include "ichor/data/data_set.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

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
        root_ = std::filesystem::temp_directory_path() / ("ichor_axiom_test_" + std::to_string(ICHOR_PID) + "_" + std::to_string(counter++));
        std::filesystem::remove_all(root_);
        std::filesystem::create_directories(root_);
    }
    ~Fixture() { std::filesystem::remove_all(root_); }
    void write(const std::string& relative, const std::string& text) const
    {
        const std::filesystem::path path = root_ / relative;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream(path, std::ios::binary) << text;
    }
    const std::filesystem::path& root() const { return root_; }

private:
    std::filesystem::path root_;
};

const std::string kFlood = R"({
  "id": "cataclysm.flood",
  "slot": "cataclysm",
  "title": "Deukalion's Flood",
  "tags": ["WATER", "RUIN", "SCARCITY", "VERTICAL_LIMIT"],
  "conflicts": ["cataclysm.demeters_grief"],
  "terrain":    { "sea_level_offset_m": 200, "coastal_erosion": 2.4 },
  "settlement": { "min_elevation_m": 210, "prefer_slope": "gentle", "density_mult": 0.7 },
  "biomes":     { "inject": ["salt_blight", "tidal_ruin"], "suppress": ["lowland_farm"] },
  "poi":        { "weights": { "drowned_polis": 3.0, "pharos": 2.0, "farmstead": 0.2 } },
  "enemies":    { "roster_add": ["drowned", "salt_kin"], "roster_remove": ["bandit_farmer"] },
  "traversal":  { "unlock": ["swim_deep", "boat"] },
  "economy":    { "scarce": ["grain", "timber"], "abundant": ["salvage", "salt"] },
  "palette":    { "grade": "flood", "fog_density": 1.4 },
  "audio":      { "bed_overrides": ["surf_distant"], "music_palette": "flood" }
})";

std::vector<std::string> check(const std::string& text, Axiom* out = nullptr)
{
    Axiom axiom;
    std::vector<std::string> problems;
    const bool ok = parseAxiom("axioms/t.json", text, axiom, problems);
    REQUIRE(ok == problems.empty());
    if (out != nullptr) {
        *out = axiom;
    }
    return problems;
}

bool mentions(const std::vector<std::string>& problems, const std::string& needle)
{
    return std::any_of(problems.begin(), problems.end(), [&](const std::string& p) { return p.find(needle) != std::string::npos; });
}

// kFlood with one textual substitution.
std::string with(const std::string& from, const std::string& to)
{
    std::string text = kFlood;
    const std::size_t at = text.find(from);
    REQUIRE(at != std::string::npos);
    text.replace(at, from.size(), to);
    return text;
}

}

TEST_CASE("the flood axiom from doc 01 validates and loads")
{
    Axiom axiom;
    REQUIRE(check(kFlood, &axiom).empty());
    REQUIRE(axiom.id == "cataclysm.flood");
    REQUIRE(axiom.slot == AxiomSlot::Cataclysm);
    REQUIRE(axiomSlotLetter(axiom.slot) == 'A');
    REQUIRE(axiom.tags.size() == 4);
    REQUIRE(axiom.blocks.size() == 9);
    REQUIRE(std::get<double>(axiom.blocks.at("terrain").at("sea_level_offset_m")) == 200.0);
    REQUIRE(std::get<std::map<std::string, double, std::less<>>>(axiom.blocks.at("poi").at("weights")).at("pharos") == 2.0);
}

TEST_CASE("the canonical form and digest ignore key order and number spelling")
{
    Axiom a;
    Axiom b;
    REQUIRE(check(kFlood, &a).empty());
    REQUIRE(check(with("\"sea_level_offset_m\": 200", "\"sea_level_offset_m\": 200.0"), &b).empty());
    REQUIRE(axiomCanonicalJson(a) == axiomCanonicalJson(b));
    REQUIRE(axiomDigest(a) == axiomDigest(b));

    Axiom reordered;
    REQUIRE(check(R"({"audio": {"music_palette": "flood", "bed_overrides": ["surf_distant"]}, "title": "Deukalion's Flood", "slot": "cataclysm", "id": "cataclysm.flood",
        "tags": ["WATER", "RUIN", "SCARCITY", "VERTICAL_LIMIT"], "conflicts": ["cataclysm.demeters_grief"],
        "palette": {"fog_density": 1.4, "grade": "flood"}, "economy": {"abundant": ["salvage", "salt"], "scarce": ["grain", "timber"]},
        "traversal": {"unlock": ["swim_deep", "boat"]}, "enemies": {"roster_remove": ["bandit_farmer"], "roster_add": ["drowned", "salt_kin"]},
        "poi": {"weights": {"farmstead": 0.2, "pharos": 2.0, "drowned_polis": 3.0}}, "biomes": {"suppress": ["lowland_farm"], "inject": ["salt_blight", "tidal_ruin"]},
        "settlement": {"density_mult": 0.7, "prefer_slope": "gentle", "min_elevation_m": 210}, "terrain": {"coastal_erosion": 2.4, "sea_level_offset_m": 200}})",
        &reordered).empty());
    REQUIRE(axiomDigest(a) == axiomDigest(reordered));

    Axiom changed;
    REQUIRE(check(with("\"density_mult\": 0.7", "\"density_mult\": 0.8"), &changed).empty());
    REQUIRE(axiomDigest(a) != axiomDigest(changed));
    REQUIRE(check(with("\"salt_kin\"", "\"salt_kin\", \"x\""), &changed).empty());
    REQUIRE(axiomDigest(a) != axiomDigest(changed));
    REQUIRE(check(with("[\"drowned\", \"salt_kin\"]", "[\"salt_kin\", \"drowned\"]"), &changed).empty());
    REQUIRE(axiomDigest(a) != axiomDigest(changed));
}

TEST_CASE("an axiom must touch at least four directive blocks")
{
    const std::string three = R"({"id": "cataclysm.thin", "slot": "cataclysm", "title": "Thin", "tags": [], "conflicts": [],
        "terrain": {"coastal_erosion": 1}, "palette": {"grade": "x"}, "audio": {"music_palette": "x"}})";
    REQUIRE(mentions(check(three), "axioms/t.json: blocks: an axiom must touch at least 4 directive blocks, found 3"));
    REQUIRE(mentions(check(with("\"terrain\":    { \"sea_level_offset_m\": 200, \"coastal_erosion\": 2.4 },", "\"terrain\": {},")), "terrain: must not be empty"));
}

TEST_CASE("slot errors name the offender and the accepted values")
{
    REQUIRE(mentions(check(with("\"slot\": \"cataclysm\"", "\"slot\": \"F\"")), "axioms/t.json: slot: expected one of cataclysm, law, powers, pressure, anomaly (slots A to E), got \"F\""));
    REQUIRE(mentions(check(with("\"slot\": \"cataclysm\"", "\"slot\": \"Cataclysm\"")), "slot: expected one of"));
    REQUIRE(mentions(check(with("\"slot\": \"cataclysm\"", "\"slot\": \"a\"")), "slot: expected one of"));
    REQUIRE(mentions(check(with("\"slot\": \"cataclysm\"", "\"slot\": \"law\"")), "id: must start with the slot name \"law.\""));
    REQUIRE(mentions(check(with("\"slot\": \"cataclysm\"", "\"slot\": 3")), "slot: expected a string, got number"));
    REQUIRE(mentions(check(with("\"slot\": \"cataclysm\"", "\"slot\": null")), "slot: expected a string, got null"));
}

TEST_CASE("ids are lowercase ascii, slot-prefixed and bounded")
{
    REQUIRE(mentions(check(with("\"cataclysm.flood\"", "\"cataclysm.Flood\"")), "id:"));
    REQUIRE(mentions(check(with("\"cataclysm.flood\"", "\"cataclysm.fl\\u00f6od\"")), "id:"));
    REQUIRE(mentions(check(with("\"cataclysm.flood\"", "\"cataclysm.\"")), "id:"));
    REQUIRE(mentions(check(with("\"cataclysm.flood\"", "\"cataclysm.a b\"")), "id:"));
    REQUIRE(mentions(check(with("\"cataclysm.flood\"", "\"\"")), "id:"));
    REQUIRE(mentions(check(with("\"cataclysm.flood\"", "\"cataclysm." + std::string(500, 'a') + "\"")), "id: longer than 64"));
    REQUIRE(mentions(check(with("\"id\": \"cataclysm.flood\"", "\"id\": 7")), "id: expected a string, got number"));
}

TEST_CASE("title text must be non-empty, bounded and free of em-dashes")
{
    REQUIRE(mentions(check(with("\"Deukalion's Flood\"", "\"\"")), "title: must not be empty"));
    REQUIRE(mentions(check(with("\"Deukalion's Flood\"", "\"   \"")), "title: must not be empty"));
    REQUIRE(mentions(check(with("\"Deukalion's Flood\"", "\"Flood \\u2014 again\"")), "title: must not contain an em-dash"));
    REQUIRE(mentions(check(with("\"Deukalion's Flood\"", "\"" + std::string(100000, 'x') + "\"")), "title: longer than 120"));
    REQUIRE(check(with("\"Deukalion's Flood\"", "\"\\u039a\\u03b1\\u03c4\\u03b1\\u03ba\\u03bb\\u03c5\\u03c3\\u03bc\\u03cc\\u03c2\"")).empty());
    REQUIRE(mentions(check(with("\"grade\": \"flood\"", "\"grade\": \"a\\u2014b\"")), "palette.grade: must not contain an em-dash"));
}

TEST_CASE("tags and conflicts are validated entry by entry")
{
    REQUIRE(mentions(check(with("\"RUIN\"", "\"ruin\"")), "tags[1]:"));
    REQUIRE(mentions(check(with("\"RUIN\"", "\"WATER\"")), "tags[1]: duplicate"));
    REQUIRE(mentions(check(with("\"RUIN\"", "7")), "tags[1]: expected a string, got number"));
    REQUIRE(mentions(check(with("\"RUIN\"", "null")), "tags[1]: expected a string, got null"));
    REQUIRE(mentions(check(with("[\"WATER\", \"RUIN\", \"SCARCITY\", \"VERTICAL_LIMIT\"]", "\"WATER\"")), "tags: expected an array, got string"));
    REQUIRE(mentions(check(with("\"cataclysm.demeters_grief\"", "\"cataclysm.flood\"")), "conflicts[0]: an axiom cannot conflict with itself"));
    REQUIRE(mentions(check(with("\"cataclysm.demeters_grief\"", "\"Nope\"")), "conflicts[0]:"));
    REQUIRE(mentions(check(with("\"cataclysm.demeters_grief\"]", "\"cataclysm.demeters_grief\", \"cataclysm.demeters_grief\"]")), "conflicts[1]: duplicate"));
}

TEST_CASE("unknown keys are errors at every level")
{
    REQUIRE(mentions(check(with("\"title\"", "\"flavour\": \"x\", \"title\"")), "axioms/t.json: flavour: unknown key"));
    REQUIRE(mentions(check(with("\"coastal_erosion\"", "\"erosion\"")), "terrain.erosion: unknown key"));
    REQUIRE(mentions(check(with("\"palette\":", "\"weather\": {\"a\": 1}, \"palette\":")), "weather: unknown key"));
}

TEST_CASE("wrong types inside blocks are errors, not crashes")
{
    REQUIRE(mentions(check(with("\"coastal_erosion\": 2.4", "\"coastal_erosion\": \"high\"")), "terrain.coastal_erosion: expected a number, got string"));
    REQUIRE(mentions(check(with("\"prefer_slope\": \"gentle\"", "\"prefer_slope\": 4")), "settlement.prefer_slope: expected a string, got number"));
    REQUIRE(mentions(check(with("\"inject\": [\"salt_blight\", \"tidal_ruin\"]", "\"inject\": \"salt_blight\"")), "biomes.inject: expected an array, got string"));
    REQUIRE(mentions(check(with("\"salt_blight\"", "3")), "biomes.inject[0]: expected a string, got number"));
    REQUIRE(mentions(check(with("\"pharos\": 2.0", "\"pharos\": \"2\"")), "poi.weights.pharos: expected a number, got string"));
    REQUIRE(mentions(check(with("\"weights\": {", "\"weights\": [], \"x\": {")), "poi.weights: expected an object, got array"));
    REQUIRE(mentions(check(with("\"terrain\":    {", "\"terrain\": 5, \"zz\": {")), "terrain: expected an object, got number"));
    REQUIRE(mentions(check(with("\"coastal_erosion\": 2.4", "\"coastal_erosion\": null")), "terrain.coastal_erosion: expected a number, got null"));
    REQUIRE(mentions(check(with("\"coastal_erosion\": 2.4", "\"coastal_erosion\": true")), "terrain.coastal_erosion: expected a number, got boolean"));
    REQUIRE(mentions(check(with("\"density_mult\": 0.7", "\"density_mult\": -1")), "settlement.density_mult: must not be negative"));
    REQUIRE(mentions(check(with("\"pharos\": 2.0", "\"pharos\": -2")), "poi.weights.pharos: must not be negative"));
    REQUIRE(mentions(check(with("\"sea_level_offset_m\": 200", "\"sea_level_offset_m\": 1e999")), "not valid JSON"));
}

TEST_CASE("list entries inside blocks are ids and unique")
{
    REQUIRE(mentions(check(with("\"salt_kin\"", "\"Salt Kin\"")), "enemies.roster_add[1]:"));
    REQUIRE(mentions(check(with("\"salt_kin\"", "\"drowned\"")), "enemies.roster_add[1]: duplicate"));
    REQUIRE(mentions(check(with("\"boat\"", "\"\"")), "traversal.unlock[1]:"));
    REQUIRE(mentions(check(with("\"drowned_polis\"", "\"Drowned\"")), "poi.weights: key"));
}

TEST_CASE("every field missing and every field mistyped reports every problem")
{
    const auto missing = check("{}");
    for (const char* field : {"id", "slot", "title", "tags", "conflicts"}) {
        REQUIRE(mentions(missing, std::string("axioms/t.json: ") + field + ": missing"));
    }
    REQUIRE(mentions(missing, "blocks: an axiom must touch at least 4 directive blocks, found 0"));
    REQUIRE(missing.size() == 6);

    const auto wrong = check(R"({"id": 1, "slot": 2, "title": 3, "tags": 4, "conflicts": 5, "terrain": 6, "settlement": [], "biomes": "x", "poi": null})");
    REQUIRE(wrong.size() >= 9);
    REQUIRE(mentions(wrong, "id: expected a string, got number"));
    REQUIRE(mentions(wrong, "conflicts: expected an array, got number"));
    REQUIRE(mentions(wrong, "settlement: expected an object, got array"));
    REQUIRE(mentions(wrong, "poi: expected an object, got null"));
}

TEST_CASE("files that are not an axiom object are rejected cleanly")
{
    REQUIRE(mentions(check(""), "axioms/t.json: empty file"));
    REQUIRE(mentions(check("   \n"), "axioms/t.json: empty file"));
    REQUIRE(mentions(check("not json at all"), "axioms/t.json: not valid JSON"));
    REQUIRE(mentions(check("{\"id\": "), "axioms/t.json: not valid JSON"));
    REQUIRE(mentions(check("[]"), "axioms/t.json: expected an object, got array"));
    REQUIRE(mentions(check("[" + kFlood + "]"), "axioms/t.json: expected an object, got array"));
    REQUIRE(mentions(check("null"), "expected an object, got null"));
    REQUIRE(mentions(check("42"), "expected an object, got number"));
    REQUIRE(mentions(check("\"x\""), "expected an object, got string"));
    REQUIRE(mentions(check(std::string("\xff\xfe{}")), "not valid JSON"));
}

TEST_CASE("pathological nesting and size are refused without recursing")
{
    REQUIRE(mentions(check(std::string(200000, '[') + std::string(200000, ']')), "nested deeper than"));
    std::string deepObject;
    for (int i = 0; i < 100000; ++i) {
        deepObject += "{\"a\":";
    }
    REQUIRE(mentions(check(deepObject + "1" + std::string(100000, '}')), "nested deeper than"));
    REQUIRE(mentions(check(with("\"terrain\":    {", "\"terrain\": {\"a\": " + std::string(100, '[') + std::string(100, ']') + ", \"zz\": {")), "nested deeper than"));
    REQUIRE(mentions(check(std::string(2 * 1024 * 1024, ' ') + "{}"), "larger than"));
    REQUIRE(check(with("\"Deukalion's Flood\"", "\"" + std::string(100, '[') + "\"")).empty());
}

TEST_CASE("an axiom directory loads and digests deterministically")
{
    const Fixture fixture;
    fixture.write("axioms/flood.json", kFlood);
    AxiomSet set;
    REQUIRE(set.loadDirectory(fixture.root()));
    REQUIRE(set.axioms().size() == 1);
    REQUIRE(set.find("cataclysm.flood") != nullptr);
    REQUIRE(set.find("cataclysm.nope") == nullptr);

    AxiomSet again;
    REQUIRE(again.loadDirectory(fixture.root()));
    REQUIRE(again.digest() == set.digest());

    const Fixture renamed;
    renamed.write("axioms/zzz/other_name.json", kFlood);
    AxiomSet moved;
    REQUIRE(moved.loadDirectory(renamed.root()));
    REQUIRE(moved.digest() == set.digest());

    const Fixture empty;
    std::filesystem::create_directories(empty.root() / "axioms");
    AxiomSet none;
    REQUIRE(none.loadDirectory(empty.root()));
    REQUIRE(none.digest() != set.digest());
}

TEST_CASE("a failed directory load keeps the previous axioms and lists every problem")
{
    const Fixture good;
    good.write("axioms/flood.json", kFlood);
    AxiomSet set;
    REQUIRE(set.loadDirectory(good.root()));
    const std::uint64_t before = set.digest();

    const Fixture bad;
    bad.write("axioms/a.json", kFlood);
    bad.write("axioms/b.json", kFlood);
    bad.write("axioms/c.json", with("\"slot\": \"cataclysm\"", "\"slot\": \"F\""));
    bad.write("axioms/d.json", "");
    bad.write("axioms/notes.txt", "ignored");
    REQUIRE_FALSE(set.loadDirectory(bad.root()));
    REQUIRE(mentions(set.problems(), "axioms/b.json: id: duplicate of \"cataclysm.flood\" in axioms/a.json"));
    REQUIRE(mentions(set.problems(), "axioms/c.json: slot: expected one of"));
    REQUIRE(mentions(set.problems(), "axioms/d.json: empty file"));
    REQUIRE(set.digest() == before);
    REQUIRE(set.axioms().size() == 1);

    AxiomSet missing;
    REQUIRE_FALSE(missing.loadDirectory(bad.root() / "nowhere"));
    REQUIRE(mentions(missing.problems(), "no axioms directory"));
}

TEST_CASE("the data layer leaves the axioms directory to the axiom loader")
{
    const Fixture fixture;
    fixture.write("axioms/flood.json", kFlood);
    fixture.write("catalogue/weapons.json", R"([{"id": "spear", "damage": 4}])");
    DataSet data;
    REQUIRE(data.loadDirectory(fixture.root()));
    REQUIRE(data.table("axioms") == nullptr);
    REQUIRE(data.table("catalogue") != nullptr);
}

TEST_CASE("the shipped axioms directory is valid")
{
    AxiomSet set;
    const bool ok = set.loadDirectory(ICHOR_REPO_DATA);
    INFO((set.problems().empty() ? std::string() : set.problems().front()));
    REQUIRE(ok);
    REQUIRE(set.find("cataclysm.flood") != nullptr);
}
