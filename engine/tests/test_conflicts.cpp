#include "ichor/core/hash.h"
#include "ichor/data/axiom.h"
#include "ichor/data/conflicts.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
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
        root_ = std::filesystem::temp_directory_path() / ("ichor_conflicts_test_" + std::to_string(ICHOR_PID) + "_" + std::to_string(counter++));
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

std::string axiomText(const std::string& id)
{
    return R"({
  "id": ")" + id + R"(",
  "slot": "cataclysm",
  "title": "Test axiom",
  "tags": ["WATER"],
  "conflicts": [],
  "terrain":    { "sea_level_offset_m": 200 },
  "settlement": { "density_mult": 0.7 },
  "biomes":     { "inject": ["salt_blight"] },
  "poi":        { "weights": { "pharos": 2.0 } }
})";
}

AxiomSet poolOf(const Fixture& fixture, const std::vector<std::string>& ids)
{
    for (const std::string& id : ids) {
        fixture.write("axioms/" + id + ".json", axiomText(id));
    }
    AxiomSet pool;
    const bool ok = pool.loadDirectory(fixture.root());
    INFO((pool.problems().empty() ? std::string() : pool.problems().front()));
    REQUIRE(ok);
    return pool;
}

const std::vector<std::string> kIds = {"cataclysm.flood", "cataclysm.demeters_grief", "cataclysm.plague", "cataclysm.fire"};

bool mentions(const std::vector<std::string>& problems, const std::string& needle)
{
    return std::any_of(problems.begin(), problems.end(), [&](const std::string& p) { return p.find(needle) != std::string::npos; });
}

bool load(ConflictList& list, const AxiomSet& pool, const std::string& text)
{
    return list.loadText("conflicts/conflicts.json", text, pool);
}

const std::string kDoc = R"({ "pairs": [["cataclysm.flood", "cataclysm.demeters_grief"]] })";

}

TEST_CASE("the doc 01 pair blocks in both directions")
{
    Fixture fixture;
    const AxiomSet pool = poolOf(fixture, kIds);
    ConflictList list;
    REQUIRE(load(list, pool, kDoc));
    REQUIRE(list.size() == 1);
    REQUIRE_FALSE(list.mayCoexist("cataclysm.flood", "cataclysm.demeters_grief"));
    REQUIRE_FALSE(list.mayCoexist("cataclysm.demeters_grief", "cataclysm.flood"));
    REQUIRE(list.mayCoexist("cataclysm.flood", "cataclysm.plague"));
    REQUIRE(list.mayCoexist("cataclysm.plague", "cataclysm.flood"));
    REQUIRE(list.mayCoexist("cataclysm.plague", "cataclysm.fire"));
}

TEST_CASE("mayCoexist on axioms matches the id form and fits a bool(Axiom, Axiom) slot")
{
    Fixture fixture;
    const AxiomSet pool = poolOf(fixture, kIds);
    ConflictList list;
    REQUIRE(load(list, pool, kDoc));
    const Axiom& flood = *pool.find("cataclysm.flood");
    const Axiom& grief = *pool.find("cataclysm.demeters_grief");
    const Axiom& plague = *pool.find("cataclysm.plague");
    const std::function<bool(const Axiom&, const Axiom&)> predicate = [&](const Axiom& a, const Axiom& b) { return list.mayCoexist(a, b); };
    REQUIRE_FALSE(predicate(flood, grief));
    REQUIRE_FALSE(predicate(grief, flood));
    REQUIRE(predicate(flood, plague));
}

TEST_CASE("an axiom never coexists with itself")
{
    Fixture fixture;
    const AxiomSet pool = poolOf(fixture, kIds);
    ConflictList list;
    REQUIRE(load(list, pool, R"({ "pairs": [] })"));
    REQUIRE_FALSE(list.mayCoexist("cataclysm.flood", "cataclysm.flood"));
}

TEST_CASE("an empty list is valid and blocks nothing")
{
    Fixture fixture;
    const AxiomSet pool = poolOf(fixture, kIds);
    ConflictList list;
    REQUIRE(load(list, pool, R"({ "pairs": [] })"));
    REQUIRE(list.size() == 0);
    REQUIRE(list.mayCoexist("cataclysm.flood", "cataclysm.demeters_grief"));
    REQUIRE(list.checkSet({"cataclysm.flood", "cataclysm.plague"}).empty());
}

TEST_CASE("every problem is reported with its path and nothing is loaded")
{
    Fixture fixture;
    const AxiomSet pool = poolOf(fixture, kIds);
    ConflictList list;
    REQUIRE(load(list, pool, kDoc));

    const std::string bad = R"({ "pairs": [
        ["cataclysm.flood", "cataclysm.nowhere"],
        ["cataclysm.fire", "cataclysm.fire"],
        ["cataclysm.flood", "cataclysm.plague"],
        ["cataclysm.plague", "cataclysm.flood"],
        ["cataclysm.fire", "cataclysm.plague"],
        ["cataclysm.fire", "cataclysm.plague"]
    ] })";
    REQUIRE_FALSE(load(list, pool, bad));
    REQUIRE(mentions(list.problems(), "pairs[0][1]: unknown axiom \"cataclysm.nowhere\""));
    REQUIRE(mentions(list.problems(), "pairs[1]: an axiom cannot conflict with itself"));
    REQUIRE(mentions(list.problems(), "pairs[3]: duplicate of pairs[2]"));
    REQUIRE(mentions(list.problems(), "pairs[5]: duplicate of pairs[4]"));
    REQUIRE(list.problems().size() == 4);
    REQUIRE(mentions(list.problems(), "conflicts/conflicts.json"));
    REQUIRE(list.size() == 1);
    REQUIRE_FALSE(list.mayCoexist("cataclysm.flood", "cataclysm.demeters_grief"));
}

TEST_CASE("wrong JSON types and shapes are problems, not crashes")
{
    Fixture fixture;
    const AxiomSet pool = poolOf(fixture, kIds);
    const auto problemsOf = [&](const std::string& text) {
        ConflictList list;
        REQUIRE_FALSE(load(list, pool, text));
        REQUIRE_FALSE(list.problems().empty());
        return list.problems();
    };
    REQUIRE(mentions(problemsOf(""), "not valid JSON"));
    REQUIRE(mentions(problemsOf("{"), "not valid JSON"));
    REQUIRE(mentions(problemsOf("[]"), "must be an object"));
    REQUIRE(mentions(problemsOf("{}"), "pairs: missing"));
    REQUIRE(mentions(problemsOf(R"({ "pairs": {} })"), "pairs: must be an array"));
    REQUIRE(mentions(problemsOf(R"({ "pairs": [], "extra": 1 })"), "extra: unknown key"));
    REQUIRE(mentions(problemsOf(R"({ "pairs": ["x"] })"), "pairs[0]: must be an array of two ids"));
    REQUIRE(mentions(problemsOf(R"({ "pairs": [["cataclysm.flood"]] })"), "pairs[0]: must be an array of two ids"));
    REQUIRE(mentions(problemsOf(R"({ "pairs": [["cataclysm.flood", "cataclysm.fire", "cataclysm.plague"]] })"), "pairs[0]: must be an array of two ids"));
    REQUIRE(mentions(problemsOf(R"({ "pairs": [["cataclysm.flood", 3]] })"), "pairs[0][1]: must be a string"));
    REQUIRE(mentions(problemsOf(R"({ "pairs": [[null, "cataclysm.flood"]] })"), "pairs[0][0]: must be a string"));
    REQUIRE(mentions(problemsOf(R"({ "pairs": [["", "cataclysm.flood"]] })"), "pairs[0][0]: unknown axiom"));
}

TEST_CASE("a missing file is a problem and keeps the previous list")
{
    Fixture fixture;
    const AxiomSet pool = poolOf(fixture, kIds);
    fixture.write("conflicts/conflicts.json", kDoc);
    ConflictList list;
    REQUIRE(list.loadDataRoot(fixture.root(), pool));
    REQUIRE(list.size() == 1);

    Fixture empty;
    REQUIRE_FALSE(list.loadDataRoot(empty.root(), pool));
    REQUIRE(mentions(list.problems(), "conflicts/conflicts.json"));
    REQUIRE(mentions(list.problems(), "cannot read"));
    REQUIRE(list.size() == 1);
    REQUIRE_FALSE(list.mayCoexist("cataclysm.flood", "cataclysm.demeters_grief"));
}

TEST_CASE("pair order, listing order and layout do not change the digest")
{
    Fixture fixture;
    const AxiomSet pool = poolOf(fixture, kIds);
    ConflictList a;
    ConflictList b;
    ConflictList c;
    REQUIRE(load(a, pool, R"({ "pairs": [["cataclysm.flood", "cataclysm.demeters_grief"], ["cataclysm.fire", "cataclysm.plague"]] })"));
    REQUIRE(load(b, pool, "{\n \"pairs\" : [ [\"cataclysm.plague\", \"cataclysm.fire\"],\n [\"cataclysm.demeters_grief\", \"cataclysm.flood\"] ]\n}"));
    REQUIRE(load(c, pool, R"({ "pairs": [["cataclysm.flood", "cataclysm.demeters_grief"]] })"));
    REQUIRE(a.digest() == b.digest());
    REQUIRE(a.canonicalJson() == b.canonicalJson());
    REQUIRE(a.canonicalJson() == R"({"pairs":[["cataclysm.demeters_grief","cataclysm.flood"],["cataclysm.fire","cataclysm.plague"]]})");
    REQUIRE(a.digest() == hash64(a.canonicalJson()));
    REQUIRE(a.digest() != c.digest());
    ConflictList none;
    REQUIRE(load(none, pool, R"({ "pairs": [] })"));
    REQUIRE(none.digest() != c.digest());
}

TEST_CASE("checkSet reports blocked pairs and repeated axioms in set order")
{
    Fixture fixture;
    const AxiomSet pool = poolOf(fixture, kIds);
    ConflictList list;
    REQUIRE(load(list, pool, kDoc));

    REQUIRE(list.checkSet({"cataclysm.flood", "cataclysm.plague", "cataclysm.fire"}).empty());

    const auto blocked = list.checkSet({"cataclysm.demeters_grief", "cataclysm.plague", "cataclysm.flood"});
    REQUIRE(blocked.size() == 1);
    REQUIRE(blocked[0].first == 0);
    REQUIRE(blocked[0].second == 2);
    REQUIRE(blocked[0].kind == SetViolation::Kind::Blocked);

    const auto repeated = list.checkSet({"cataclysm.fire", "cataclysm.plague", "cataclysm.fire"});
    REQUIRE(repeated.size() == 1);
    REQUIRE(repeated[0].first == 0);
    REQUIRE(repeated[0].second == 2);
    REQUIRE(repeated[0].kind == SetViolation::Kind::Repeated);

    REQUIRE(list.checkSet(std::vector<std::string>{}).empty());
    REQUIRE(list.checkSet({"cataclysm.flood"}).empty());

    const Axiom& flood = *pool.find("cataclysm.flood");
    const Axiom& grief = *pool.find("cataclysm.demeters_grief");
    REQUIRE(list.checkAxioms(std::vector<const Axiom*>{&flood, &grief}).size() == 1);
    REQUIRE(list.checkAxioms(std::vector<const Axiom*>{&flood, &flood}).size() == 1);
}

TEST_CASE("a huge list loads and answers symmetrically")
{
    Fixture fixture;
    std::vector<std::string> ids;
    for (int i = 0; i < 400; ++i) {
        ids.push_back("cataclysm.a" + std::to_string(1000 + i));
    }
    const AxiomSet pool = poolOf(fixture, ids);
    std::string text = R"({ "pairs": [)";
    std::size_t count = 0;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        for (std::size_t j = i + 1; j < ids.size(); j += 2) {
            text += (count++ == 0 ? "" : ",");
            text += "[\"" + ids[j] + "\",\"" + ids[i] + "\"]";
        }
    }
    text += "] }";
    ConflictList list;
    const bool loaded = load(list, pool, text);
    INFO((list.problems().empty() ? std::string() : list.problems().front()));
    REQUIRE(loaded);
    REQUIRE(list.size() == count);
    for (std::size_t i = 0; i < ids.size(); ++i) {
        for (std::size_t j = i + 1; j < ids.size(); ++j) {
            const bool blocked = (j - i) % 2 == 1;
            REQUIRE(list.mayCoexist(ids[i], ids[j]) == !blocked);
            REQUIRE(list.mayCoexist(ids[j], ids[i]) == !blocked);
        }
    }
}

TEST_CASE("an oversized file is refused")
{
    Fixture fixture;
    const AxiomSet pool = poolOf(fixture, kIds);
    fixture.write("conflicts/conflicts.json", std::string(5 * 1024 * 1024, ' '));
    ConflictList list;
    REQUIRE_FALSE(list.loadDataRoot(fixture.root(), pool));
    REQUIRE(mentions(list.problems(), "larger than"));
}

TEST_CASE("the shipped data/conflicts file validates against the shipped axioms")
{
    const std::filesystem::path root = ICHOR_REPO_DATA;
    AxiomSet pool;
    REQUIRE(pool.loadDirectory(root));
    ConflictList list;
    const bool ok = list.loadDataRoot(root, pool);
    INFO((list.problems().empty() ? std::string() : list.problems().front()));
    REQUIRE(ok);
}

TEST_CASE("a later good load replaces the list and clears the problems")
{
    Fixture fixture;
    const AxiomSet pool = poolOf(fixture, kIds);
    ConflictList list;
    REQUIRE_FALSE(load(list, pool, "{"));
    REQUIRE(list.size() == 0);
    REQUIRE(list.mayCoexist("cataclysm.flood", "cataclysm.demeters_grief"));
    REQUIRE(load(list, pool, kDoc));
    REQUIRE(list.problems().empty());
    REQUIRE(load(list, pool, R"({ "pairs": [["cataclysm.fire", "cataclysm.plague"]] })"));
    REQUIRE(list.size() == 1);
    REQUIRE(list.mayCoexist("cataclysm.flood", "cataclysm.demeters_grief"));
    REQUIRE_FALSE(list.mayCoexist("cataclysm.fire", "cataclysm.plague"));
}
