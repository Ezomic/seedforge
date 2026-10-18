#include "ichor/lint/axiom_coverage.h"

#include "ichor/data/synergy_content.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

using namespace ichor;
using namespace ichor::lint;

namespace {

namespace fs = std::filesystem;

std::string readLf(const fs::path& file)
{
    std::ifstream in(file, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
    return text;
}

struct Shipped {
    AxiomSet axioms;
    std::vector<Synergy> synergies;

    Shipped()
    {
        std::vector<std::string> problems;
        REQUIRE(axioms.loadDirectory(ICHOR_REPO_DATA));
        REQUIRE(loadSynergyContentDirectory(fs::path(ICHOR_REPO_DATA) / "synergies", axiomPool(axioms), synergies, problems));
    }
};

std::size_t count(const std::string& text, const std::string& needle)
{
    std::size_t found = 0;
    for (std::size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + needle.size())) ++found;
    return found;
}

}

TEST_CASE("the checked-in axiom coverage report is current", "[axiom_coverage]")
{
    std::string generated;
    std::vector<std::string> problems;
    REQUIRE(generateAxiomCoverage(ICHOR_REPO_DATA, generated, problems));
    const fs::path file = fs::path(ICHOR_REPO_DATA) / ".." / "docs" / "axiom-coverage.md";
    INFO("regenerate with: ichor_lint --coverage-out docs/axiom-coverage.md");
    CHECK(readLf(file) == generated);
}

TEST_CASE("the report goes stale when a synergy is added or removed", "[axiom_coverage]")
{
    const Shipped shipped;
    const std::string baseline = renderAxiomCoverage(shipped.axioms, shipped.synergies);

    std::vector<Synergy> fewer = shipped.synergies;
    const auto flagship = std::find_if(fewer.begin(), fewer.end(), [](const Synergy& s) { return s.axioms.size() == 2 && s.contentTier == ContentTier::Flagship; });
    REQUIRE(flagship != fewer.end());
    fewer.erase(flagship);
    const std::string without = renderAxiomCoverage(shipped.axioms, fewer);
    CHECK(without != baseline);
    CHECK(count(without, " F |") + 2 == count(baseline, " F |"));
    CHECK(count(without, "- `") == count(baseline, "- `") + 1);
}

TEST_CASE("a text-only synergy shows as T and a flagship as F", "[axiom_coverage]")
{
    const Shipped shipped;
    const std::string text = renderAxiomCoverage(shipped.axioms, shipped.synergies);
    CHECK(text.find(" T |") != std::string::npos);
    CHECK(text.find(" F |") != std::string::npos);
    CHECK(text.find("`cataclysm.flood` + `law.orichalcum` + `powers.league`") != std::string::npos);
}

TEST_CASE("the report ignores the order of the synergies", "[axiom_coverage]")
{
    const Shipped shipped;
    std::vector<Synergy> reversed(shipped.synergies.rbegin(), shipped.synergies.rend());
    CHECK(renderAxiomCoverage(shipped.axioms, reversed) == renderAxiomCoverage(shipped.axioms, shipped.synergies));
}

TEST_CASE("pair totals add up and nothing is listed twice", "[axiom_coverage]")
{
    const Shipped shipped;
    const std::string text = renderAxiomCoverage(shipped.axioms, shipped.synergies);
    const std::size_t axioms = shipped.axioms.axioms().size();
    CHECK(text.find("| Total | " + std::to_string(axioms * (axioms - 1) / 2) + " |") != std::string::npos);
    CHECK(text.find('\r') == std::string::npos);
}

TEST_CASE("with no synergies every pair that can roll is listed as missing", "[axiom_coverage]")
{
    const Shipped shipped;
    const std::string text = renderAxiomCoverage(shipped.axioms, {});
    CHECK(text.find("| Flagship synergy (content tier 1) | 0 |") != std::string::npos);
    CHECK(text.find("No triple synergies yet.") != std::string::npos);
    CHECK(text.find(" F |") == std::string::npos);
}

TEST_CASE("an unloadable data root reports problems instead of throwing", "[axiom_coverage]")
{
    std::string markdown;
    std::vector<std::string> problems;
    CHECK_FALSE(generateAxiomCoverage(fs::path(ICHOR_TEST_OUTPUT) / "axiom_coverage_no_such_dir", markdown, problems));
    CHECK(markdown.empty());
}

namespace {

Synergy pairSynergy(std::string id, std::string a, std::string b, ContentTier tier)
{
    Synergy synergy;
    synergy.id = std::move(id);
    synergy.axioms = {std::move(a), std::move(b)};
    synergy.contentTier = tier;
    return synergy;
}

// Four shipped axioms (two cataclysms, one law, one powers) in a scratch data root.
struct SmallRoot {
    fs::path root = fs::path(ICHOR_TEST_OUTPUT) / "axiom_coverage_small";
    AxiomSet axioms;

    SmallRoot()
    {
        fs::remove_all(root);
        fs::create_directories(root / "axioms");
        for (const char* name : {"flood", "kronos", "orichalcum", "league"})
            fs::copy_file(fs::path(ICHOR_REPO_DATA) / "axioms" / (std::string(name) + ".json"), root / "axioms" / (std::string(name) + ".json"));
        REQUIRE(axioms.loadDirectory(root));
    }
    ~SmallRoot()
    {
        std::error_code code;
        fs::remove_all(root, code);
    }
};

}

TEST_CASE("each pair lands in the right matrix cell, and a flagship beats a duplicate text-only entry", "[axiom_coverage]")
{
    const SmallRoot small;
    const std::vector<Synergy> synergies = {
        pairSynergy("a_text", "law.orichalcum", "cataclysm.flood", ContentTier::TextOnly),
        pairSynergy("b_flagship", "cataclysm.flood", "law.orichalcum", ContentTier::Flagship),
        pairSynergy("c_text", "powers.league", "law.orichalcum", ContentTier::TextOnly),
    };
    for (const auto& order : {synergies, std::vector<Synergy>(synergies.rbegin(), synergies.rend())}) {
        const std::string text = renderAxiomCoverage(small.axioms, order);
        CHECK(text.find("\n| 1 | - | x | F | . |\n") != std::string::npos);
        CHECK(text.find("\n| 2 | x | - | . | . |\n") != std::string::npos);
        CHECK(text.find("\n| 3 | F | . | - | T |\n") != std::string::npos);
        CHECK(text.find("\n| 4 | . | . | T | - |\n") != std::string::npos);
        CHECK(text.find("| Flagship synergy (content tier 1) | 1 |") != std::string::npos);
        CHECK(text.find("| Text-only synergy (content tier 2) | 1 |") != std::string::npos);
        CHECK(text.find("| No synergy, can roll together | 3 |") != std::string::npos);
        CHECK(text.find("| Excluded (same slot or declared conflict) | 1 |") != std::string::npos);
        CHECK(text.find("- `cataclysm.flood` + `powers.league`\n") != std::string::npos);
    }
}

TEST_CASE("a synergy on an unknown axiom or a self pair is ignored", "[axiom_coverage]")
{
    const SmallRoot small;
    const std::vector<Synergy> synergies = {
        pairSynergy("ghost", "cataclysm.flood", "cataclysm.no_such", ContentTier::Flagship),
        pairSynergy("self", "law.orichalcum", "law.orichalcum", ContentTier::Flagship),
    };
    CHECK(renderAxiomCoverage(small.axioms, synergies) == renderAxiomCoverage(small.axioms, {}));
}

TEST_CASE("an empty axiom set renders an empty matrix", "[axiom_coverage]")
{
    const AxiomSet none;
    const std::string text = renderAxiomCoverage(none, {});
    CHECK(text.find("Axioms: 0.") != std::string::npos);
    CHECK(text.find("| Total | 0 |") != std::string::npos);
}
