#include "ichor/data/axiom.h"
#include "ichor/data/data_set.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace ichor;

namespace {

const std::set<std::string> kSlots = {"weapon", "off_hand", "head", "body", "feet", "hands", "trinket", "tool"};
const std::set<std::string> kCostKinds = {"weight", "repair", "noise", "vitality", "charge", "standing", "stamina", "sight",
                                          "access", "time", "risk", "slot", "debt", "coin"};
constexpr double kMaxNotableWeight = 6.0;

std::string text(const DataRecord& record, const std::string& key)
{
    const auto it = record.fields.find(key);
    if (it == record.fields.end()) return {};
    const auto* value = std::get_if<std::string>(&it->second);
    return value ? *value : std::string();
}

std::vector<std::string> list(const DataRecord& record, const std::string& key)
{
    const auto it = record.fields.find(key);
    if (it == record.fields.end()) return {};
    const auto* value = std::get_if<std::vector<std::string>>(&it->second);
    return value ? *value : std::vector<std::string>();
}

std::vector<std::string> problems(const DataRecord& item, const AxiomSet& axioms)
{
    std::vector<std::string> found;
    for (const char* key : {"name", "description", "benefit", "cost"}) {
        const std::string value = text(item, key);
        if (value.empty()) found.push_back(std::string("missing ") + key);
        if (value.find("\xE2\x80\x94") != std::string::npos || value.find("--") != std::string::npos) {
            found.push_back(std::string("em-dash in ") + key);
        }
    }
    if (text(item, "tier") != "notable") found.push_back("tier is not notable");
    if (text(item, "class") != "gear") found.push_back("class is not gear");
    if (!kSlots.contains(text(item, "slot"))) found.push_back("unknown slot");

    const std::vector<std::string> kinds = list(item, "cost_kinds");
    if (kinds.empty()) found.push_back("no cost_kinds");
    for (const std::string& kind : kinds) {
        if (!kCostKinds.contains(kind)) found.push_back("unknown cost kind " + kind);
    }

    const auto weight = item.fields.find("weight");
    const double* value = weight == item.fields.end() ? nullptr : std::get_if<double>(&weight->second);
    if (!value || !std::isfinite(*value) || *value < 0.0 || *value > kMaxNotableWeight) found.push_back("weight missing or out of range");

    const std::vector<std::string> homes = list(item, "home");
    if (homes.empty()) found.push_back("no home axiom");
    for (const std::string& home : homes) {
        if (axioms.find(home) == nullptr) found.push_back("unknown home axiom " + home);
    }
    if (item.fields.contains("grants")) found.push_back("notable items grant no capability");
    return found;
}

const DataSet& shipped()
{
    static DataSet set;
    static const bool loaded = set.loadDirectory(ICHOR_REPO_DATA);
    REQUIRE(loaded);
    return set;
}

const DataTable& combined()
{
    const DataTable* table = shipped().table("notable_items");
    REQUIRE(table != nullptr);
    return *table;
}

// The second tranche alone: gear_2.json copied into a scratch data root.
const DataTable& secondTranche()
{
    static DataSet set;
    static const bool loaded = [] {
        const std::filesystem::path root = std::filesystem::temp_directory_path() / "ichor_notable_items_2_scratch";
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root / "notable_items");
        std::filesystem::copy_file(std::filesystem::path(ICHOR_REPO_DATA) / "notable_items" / "gear_2.json",
                                   root / "notable_items" / "gear_2.json");
        const bool ok = set.loadDirectory(root);
        std::filesystem::remove_all(root);
        return ok;
    }();
    REQUIRE(loaded);
    const DataTable* table = set.table("notable_items");
    REQUIRE(table != nullptr);
    return *table;
}

}

TEST_CASE("the second notable tranche adds at least forty items on top of the first")
{
    REQUIRE(secondTranche().records.size() >= 40);
    REQUIRE(combined().records.size() >= secondTranche().records.size() + 36);
}

TEST_CASE("every second-tranche item is a trade-off inside the notable weight cap")
{
    AxiomSet axioms;
    REQUIRE(axioms.loadDirectory(ICHOR_REPO_DATA));
    for (const auto& [id, item] : secondTranche().records) {
        INFO(id);
        const std::vector<std::string> found = problems(item, axioms);
        INFO((found.empty() ? std::string() : found.front()));
        REQUIRE(found.empty());
    }
}

TEST_CASE("second-tranche ids and names are unique across both files and the catalogue")
{
    const DataTable* catalogue = shipped().table("catalogue");
    REQUIRE(catalogue != nullptr);

    std::set<std::string> names;
    for (const auto& [id, item] : catalogue->records) names.insert(text(item, "name"));
    for (const auto& [id, item] : combined().records) {
        INFO(id);
        REQUIRE(!catalogue->records.contains(id));
        REQUIRE(names.insert(text(item, "name")).second);
    }
}

TEST_CASE("the second tranche covers every slot and a wide spread of home axioms")
{
    std::map<std::string, int> perSlot;
    std::set<std::string> homes;
    for (const auto& [id, item] : secondTranche().records) {
        ++perSlot[text(item, "slot")];
        for (const std::string& home : list(item, "home")) homes.insert(home);
    }
    for (const std::string& slot : kSlots) {
        INFO(slot);
        REQUIRE(perSlot[slot] >= 3);
    }
    REQUIRE(homes.size() >= 18);
}

TEST_CASE("the second-tranche checker goes red on a broken item")
{
    AxiomSet axioms;
    REQUIRE(axioms.loadDirectory(ICHOR_REPO_DATA));
    const DataRecord good = secondTranche().records.begin()->second;
    REQUIRE(problems(good, axioms).empty());

    DataRecord emDash = good;
    emDash.fields["benefit"] = std::string("Sharp \xE2\x80\x94 very.");
    REQUIRE(!problems(emDash, axioms).empty());

    DataRecord tooHeavy = good;
    tooHeavy.fields["weight"] = 40.0;
    REQUIRE(!problems(tooHeavy, axioms).empty());

    DataRecord unknownHome = good;
    unknownHome.fields["home"] = std::vector<std::string>{"law.nonexistent"};
    REQUIRE(!problems(unknownHome, axioms).empty());
}
