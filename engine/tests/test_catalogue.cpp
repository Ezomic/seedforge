#include "ichor/data/axiom.h"
#include "ichor/data/data_set.h"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace ichor;

namespace {

const std::set<std::string> kClasses = {"consumable", "material", "information", "coin", "gear"};
const std::set<std::string> kCostKinds = {"weight", "repair", "noise", "vitality", "charge", "standing", "stamina", "sight",
                                          "access", "time", "risk", "slot", "debt", "coin"};

// Uniques are the named, permanent items, so the mix leans on gear and information; consumables and
// materials appear only as named vessels and relics, and coin has no unique at all.
const std::map<std::string, int> kExpectedMix = {{"gear", 30}, {"information", 12}, {"consumable", 10}, {"material", 8}};

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

std::set<std::string> capabilityIds()
{
    std::ifstream in(std::string(ICHOR_REPO_DATA) + "/progression/capabilities.json", std::ios::binary);
    const nlohmann::json root = nlohmann::json::parse(in, nullptr, false);
    std::set<std::string> ids;
    if (!root.is_array()) return ids;
    for (const auto& entry : root) ids.insert(entry.value("id", std::string()));
    return ids;
}

std::vector<std::string> problems(const DataRecord& item, const AxiomSet& axioms, const std::set<std::string>& capabilities)
{
    std::vector<std::string> found;
    for (const char* key : {"name", "description", "benefit", "cost"}) {
        if (text(item, key).empty()) found.push_back(std::string("missing ") + key);
    }
    if (text(item, "tier") != "unique") found.push_back("tier is not unique");
    if (!kClasses.contains(text(item, "class"))) found.push_back("unknown class");
    if (text(item, "class") == "coin") found.push_back("coin has no uniques");
    if (text(item, "slot").empty()) found.push_back("missing slot");

    const std::vector<std::string> kinds = list(item, "cost_kinds");
    if (kinds.empty()) found.push_back("no cost_kinds");
    for (const std::string& kind : kinds) {
        if (!kCostKinds.contains(kind)) found.push_back("unknown cost kind " + kind);
    }

    const auto weight = item.fields.find("weight");
    const double* value = weight == item.fields.end() ? nullptr : std::get_if<double>(&weight->second);
    if (!value || !std::isfinite(*value) || *value < 0.0) found.push_back("weight missing, negative or not finite");

    const std::vector<std::string> homes = list(item, "home");
    if (homes.empty()) found.push_back("no home axiom");
    for (const std::string& home : homes) {
        if (axioms.find(home) == nullptr) found.push_back("unknown home axiom " + home);
    }
    for (const std::string& grant : list(item, "grants")) {
        if (!capabilities.contains(grant)) found.push_back("unknown capability " + grant);
    }

    for (const char* key : {"name", "description", "benefit", "cost"}) {
        if (text(item, key).find("\xE2\x80\x94") != std::string::npos) found.push_back(std::string("em-dash in ") + key);
    }
    return found;
}

const DataSet& shipped()
{
    static DataSet set;
    static const bool loaded = set.loadDirectory(ICHOR_REPO_DATA);
    REQUIRE(loaded);
    return set;
}

}

TEST_CASE("the catalogue holds about sixty unique items with distinct ids and names")
{
    const DataTable* table = shipped().table("catalogue");
    REQUIRE(table != nullptr);
    REQUIRE(table->records.size() == 60);

    std::set<std::string> names;
    for (const auto& [id, item] : table->records) {
        REQUIRE(!id.empty());
        REQUIRE(std::all_of(id.begin(), id.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; }));
        REQUIRE(names.insert(text(item, "name")).second);
    }
}

TEST_CASE("every catalogue item is a trade-off with a real cost, never a pure upgrade")
{
    AxiomSet axioms;
    REQUIRE(axioms.loadDirectory(ICHOR_REPO_DATA));
    const std::set<std::string> capabilities = capabilityIds();
    REQUIRE(!capabilities.empty());

    for (const auto& [id, item] : shipped().table("catalogue")->records) {
        INFO(id);
        const std::vector<std::string> found = problems(item, axioms, capabilities);
        INFO((found.empty() ? std::string() : found.front()));
        REQUIRE(found.empty());
    }
}

TEST_CASE("the item checker goes red on an item with no cost")
{
    AxiomSet axioms;
    REQUIRE(axioms.loadDirectory(ICHOR_REPO_DATA));
    const std::set<std::string> capabilities = capabilityIds();

    DataRecord good = *shipped().record("catalogue", "ledger_knife");
    REQUIRE(problems(good, axioms, capabilities).empty());

    DataRecord noCost = good;
    noCost.fields.erase("cost");
    REQUIRE(!problems(noCost, axioms, capabilities).empty());

    DataRecord noKinds = good;
    noKinds.fields.erase("cost_kinds");
    REQUIRE(!problems(noKinds, axioms, capabilities).empty());

    DataRecord badGrant = good;
    badGrant.fields["grants"] = std::vector<std::string>{"fly"};
    REQUIRE(!problems(badGrant, axioms, capabilities).empty());

    DataRecord emDash = good;
    emDash.fields["cost"] = std::string("It costs \xE2\x80\x94 a lot.");
    REQUIRE(!problems(emDash, axioms, capabilities).empty());
}

TEST_CASE("the catalogue's class mix follows doc 06")
{
    std::map<std::string, int> mix;
    for (const auto& [id, item] : shipped().table("catalogue")->records) ++mix[text(item, "class")];
    REQUIRE(mix == kExpectedMix);
    REQUIRE(mix["gear"] > mix["information"]);
    REQUIRE(mix["information"] > mix["consumable"]);
    REQUIRE(mix["consumable"] > mix["material"]);
}

TEST_CASE("catalogue items cover the traversal and lock capabilities that gear can unlock")
{
    std::set<std::string> granted;
    for (const auto& [id, item] : shipped().table("catalogue")->records) {
        for (const std::string& grant : list(item, "grants")) granted.insert(grant);
    }
    for (const char* verb : {"swim", "dive", "climb", "descend", "boat", "pick_lock_simple", "pick_lock_complex", "read_old_script"}) {
        REQUIRE(granted.contains(verb));
    }
}

TEST_CASE("the parry shields in the catalogue read as parrying weapons")
{
    const DataValue* window = shipped().field("catalogue", "aegis_of_the_silent", "parry_window_ticks");
    REQUIRE(window != nullptr);
    REQUIRE(std::get<double>(*window) == 14.0);
    REQUIRE(shipped().field("catalogue", "ledger_knife", "parry_window_ticks") == nullptr);
}
