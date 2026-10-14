#include "ichor/data/axiom.h"
#include "ichor/data/data_set.h"
#include "ichor/data/xref.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
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
        root_ = std::filesystem::temp_directory_path() / ("ichor_xref_test_" + std::to_string(ICHOR_PID) + "_" + std::to_string(counter++));
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

std::string axiomText(const std::string& id, const std::string& conflicts, const std::string& inject, const std::string& suppress = "[]")
{
    return R"({"id": ")" + id + R"(", "slot": "cataclysm", "title": "T", "tags": ["WATER"], "conflicts": )" + conflicts + R"(,
        "terrain": {"coastal_erosion": 2.4},
        "settlement": {"density_mult": 0.7},
        "biomes": {"inject": )" + inject + R"(, "suppress": )" + suppress + R"(},
        "poi": {"weights": {"farmstead": 0.2}}})";
}

const std::string kBiomes = R"([{"id": "salt_blight"}, {"id": "tidal_ruin"}, {"id": "lowland_farm"}])";

struct Loaded {
    DataSet data;
    AxiomSet axioms;
    XrefReport report;
};

Loaded run(const Fixture& fixture)
{
    Loaded loaded;
    REQUIRE(loaded.data.loadDirectory(fixture.root()));
    REQUIRE(loaded.axioms.loadDirectory(fixture.root()));
    loaded.report = checkReferences(loaded.data, loaded.axioms, fixture.root());
    return loaded;
}

std::vector<std::string> lines(const XrefReport& report)
{
    std::vector<std::string> out;
    for (const DanglingRef& ref : report.dangling) {
        out.push_back(describe(ref));
    }
    return out;
}

bool contains(const std::vector<std::string>& all, const std::string& wanted)
{
    return std::find(all.begin(), all.end(), wanted) != all.end();
}

void cleanSet(const Fixture& fixture)
{
    fixture.write("biomes/biomes.json", kBiomes);
    fixture.write("axioms/a.json", axiomText("cataclysm.a", R"(["cataclysm.b"])", R"(["salt_blight"])", R"(["lowland_farm"])"));
    fixture.write("axioms/b.json", axiomText("cataclysm.b", R"(["cataclysm.a"])", R"(["tidal_ruin"])"));
}

}

TEST_CASE("a set with no dangling reference passes and counts what it checked", "[xref]")
{
    Fixture fixture;
    cleanSet(fixture);
    const Loaded loaded = run(fixture);
    CHECK(loaded.report.ok());
    CHECK(loaded.report.checked == 5);
    CHECK(contains(loaded.report.unchecked, "axioms.poi.weights -> pois"));
}

TEST_CASE("a dangling biome is reported with its file, record, field and value", "[xref]")
{
    Fixture fixture;
    cleanSet(fixture);
    fixture.write("axioms/a.json", axiomText("cataclysm.a", R"(["cataclysm.b"])", R"(["salt_blight", "no_such_biome"])", R"(["lowland_farm"])"));
    const Loaded loaded = run(fixture);
    REQUIRE(loaded.report.dangling.size() == 1);
    const DanglingRef& ref = loaded.report.dangling.front();
    CHECK(ref.file == "axioms/a.json");
    CHECK(ref.record == "cataclysm.a");
    CHECK(ref.field == "biomes.inject");
    CHECK(ref.value == "no_such_biome");
    CHECK(ref.target == "biomes");
    CHECK(describe(ref) == "axioms/a.json: cataclysm.a: biomes.inject: no biomes record \"no_such_biome\"");
}

TEST_CASE("every dangling reference is reported, in a fixed order, not just the first", "[xref]")
{
    Fixture fixture;
    cleanSet(fixture);
    fixture.write("axioms/b.json", axiomText("cataclysm.b", R"(["cataclysm.ghost", "cataclysm.a"])", R"(["gone_one", "tidal_ruin"])", R"(["gone_two"])"));
    fixture.write("axioms/a.json", axiomText("cataclysm.a", R"(["cataclysm.b"])", R"(["gone_three"])"));
    fixture.write("conflicts/conflicts.json", R"({"pairs": [["cataclysm.a", "cataclysm.void"]]})");
    fixture.write("synergies/s.json", R"({"synergies": [{"id": "syn", "tier": "cataclysm_law", "axioms": ["cataclysm.a", "law.missing"]}]})");
    fixture.write("npcs/traits.json", R"([{"id": "bitter", "incompatible": ["cheerful"]}])");
    const Loaded loaded = run(fixture);
    const std::vector<std::string> found = lines(loaded.report);
    CHECK(found == std::vector<std::string>{
                       "axioms/a.json: cataclysm.a: biomes.inject: no biomes record \"gone_three\"",
                       "axioms/b.json: cataclysm.b: biomes.inject: no biomes record \"gone_one\"",
                       "axioms/b.json: cataclysm.b: biomes.suppress: no biomes record \"gone_two\"",
                       "axioms/b.json: cataclysm.b: conflicts: no axioms record \"cataclysm.ghost\"",
                       "conflicts/conflicts.json: pairs[0]: pairs[0][1]: no axioms record \"cataclysm.void\"",
                       "npcs/traits.json: bitter: incompatible: no npcs record \"cheerful\"",
                       "synergies/s.json: syn: axioms[1]: no axioms record \"law.missing\"",
                   });
}

TEST_CASE("removing a referenced biome turns a clean set red", "[xref]")
{
    Fixture fixture;
    cleanSet(fixture);
    REQUIRE(run(fixture).report.ok());

    fixture.write("biomes/biomes.json", R"([{"id": "tidal_ruin"}, {"id": "lowland_farm"}])");
    const Loaded loaded = run(fixture);
    REQUIRE(loaded.report.dangling.size() == 1);
    CHECK(loaded.report.dangling.front().value == "salt_blight");
    CHECK(loaded.report.dangling.front().record == "cataclysm.a");
}

TEST_CASE("a rule whose target table does not exist yet is listed, not failed, and bites once the table appears", "[xref]")
{
    Fixture fixture;
    fixture.write("axioms/a.json", axiomText("cataclysm.a", "[]", R"(["salt_blight"])"));
    const Loaded without = run(fixture);
    CHECK(without.report.ok());
    CHECK(contains(without.report.unchecked, "axioms.biomes.inject -> biomes"));

    fixture.write("biomes/biomes.json", R"([{"id": "other"}])");
    const Loaded with = run(fixture);
    REQUIRE(with.report.dangling.size() == 1);
    CHECK(with.report.dangling.front().value == "salt_blight");
    CHECK_FALSE(contains(with.report.unchecked, "axioms.biomes.inject -> biomes"));
}

TEST_CASE("custom rules read weight-map keys and string fields", "[xref]")
{
    Fixture fixture;
    fixture.write("axioms/a.json", axiomText("cataclysm.a", "[]", "[]"));
    fixture.write("pois/pois.json", R"([{"id": "pharos"}])");
    fixture.write("quests/q.json", R"([{"id": "q1", "giver": "pharos"}, {"id": "q2", "giver": "nowhere"}])");
    DataSet data;
    AxiomSet axioms;
    REQUIRE(data.loadDirectory(fixture.root()));
    REQUIRE(axioms.loadDirectory(fixture.root()));
    const std::vector<RefRule> rules = {{"axioms", "poi.weights", "pois"}, {"quests", "giver", "pois"}};
    const XrefReport report = checkReferences(data, axioms, fixture.root(), rules);
    CHECK(lines(report) == std::vector<std::string>{
                               "axioms/a.json: cataclysm.a: poi.weights: no pois record \"farmstead\"",
                               "quests/q.json: q2: giver: no pois record \"nowhere\"",
                           });
}

TEST_CASE("a nested document that cannot be read is reported, not skipped", "[xref]")
{
    Fixture fixture;
    cleanSet(fixture);
    fixture.write("synergies/broken.json", R"({"synergies": 5})");
    fixture.write("conflicts/conflicts.json", R"({"pairs": "no"})");
    const Loaded loaded = run(fixture);
    CHECK_FALSE(loaded.report.ok());
    CHECK(loaded.report.dangling.empty());
    CHECK(loaded.report.unreadable == std::vector<std::string>{"conflicts/conflicts.json", "synergies/broken.json"});
}

TEST_CASE("the shipped data has no dangling reference", "[xref]")
{
    const std::filesystem::path root = ICHOR_GAME_DATA_DIR;
    DataSet data;
    AxiomSet axioms;
    REQUIRE(data.loadDirectory(root));
    REQUIRE(axioms.loadDirectory(root));
    const XrefReport report = checkReferences(data, axioms, root);
    INFO("run ichor_tests [xref] to list them");
    for (const DanglingRef& ref : report.dangling) {
        UNSCOPED_INFO(describe(ref));
    }
    CHECK(report.ok());
    CHECK(report.checked > 0);
}

TEST_CASE("removing a real record from the shipped data turns the check red", "[xref]")
{
    Fixture fixture;
    const std::filesystem::path real = ICHOR_GAME_DATA_DIR;
    std::filesystem::copy(real, fixture.root(), std::filesystem::copy_options::recursive);

    // npcs/traits.json: "bitter" names "cheerful" as incompatible, so dropping "cheerful" strands it.
    const std::filesystem::path file = fixture.root() / "npcs" / "traits.json";
    std::ifstream in(file, std::ios::binary);
    nlohmann::json traits = nlohmann::json::parse(in);
    in.close();
    const auto before = traits.size();
    traits.erase(std::remove_if(traits.begin(), traits.end(), [](const nlohmann::json& trait) { return trait["id"] == "cheerful"; }), traits.end());
    REQUIRE(traits.size() == before - 1);
    fixture.write("npcs/traits.json", traits.dump());

    DataSet data;
    AxiomSet axioms;
    REQUIRE(data.loadDirectory(fixture.root()));
    REQUIRE(data.record("npcs", "cheerful") == nullptr);
    REQUIRE(axioms.loadDirectory(fixture.root()));
    const std::vector<std::string> found = lines(checkReferences(data, axioms, fixture.root()));
    CHECK(contains(found, "npcs/traits.json: bitter: incompatible: no npcs record \"cheerful\""));
}
