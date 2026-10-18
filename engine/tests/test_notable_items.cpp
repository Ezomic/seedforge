#include "ichor/data/axiom.h"
#include "ichor/data/data_set.h"
#include "ichor/data/id_audit.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
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

const std::set<std::string> kOtherClasses = {"consumable", "relic", "key"};

const DataSet& shipped()
{
    static DataSet set;
    static const bool loaded = set.loadDirectory(ICHOR_REPO_DATA);
    REQUIRE(loaded);
    return set;
}

const DataTable& notable()
{
    const DataTable* table = shipped().table("notable_items");
    REQUIRE(table != nullptr);
    return *table;
}

}

TEST_CASE("the first notable tranche is a coherent set of at least thirty gear items")
{
    std::set<std::string> slots;
    std::size_t gear = 0;
    for (const auto& [id, item] : notable().records) {
        if (kOtherClasses.contains(text(item, "class"))) continue;
        ++gear;
        slots.insert(text(item, "slot"));
    }
    REQUIRE(gear >= 30);
    REQUIRE(slots == kSlots);
}

TEST_CASE("every notable item is a trade-off inside the notable weight cap")
{
    AxiomSet axioms;
    REQUIRE(axioms.loadDirectory(ICHOR_REPO_DATA));
    for (const auto& [id, item] : notable().records) {
        if (kOtherClasses.contains(text(item, "class"))) continue;
        INFO(id);
        const std::vector<std::string> found = problems(item, axioms);
        INFO((found.empty() ? std::string() : found.front()));
        REQUIRE(found.empty());
    }
}

TEST_CASE("notable ids and names never collide with an existing catalogue item or each other")
{
    const DataTable* unique = shipped().table("catalogue");
    REQUIRE(unique != nullptr);

    std::set<std::string> names;
    for (const auto& [id, item] : unique->records) names.insert(text(item, "name"));
    for (const auto& [id, item] : notable().records) {
        INFO(id);
        REQUIRE(!unique->records.contains(id));
        REQUIRE(names.insert(text(item, "name")).second);
    }
}

TEST_CASE("notable ids pass the stable id audit")
{
    const IdAuditReport report = auditIds(ICHOR_REPO_DATA);
    INFO((report.violations.empty() ? std::string() : describe(report.violations.front())));
    REQUIRE(report.ok());
    for (const auto& [id, item] : notable().records) {
        INFO(id);
        REQUIRE(idProblem(id).empty());
    }
}

TEST_CASE("the notable checker goes red on a broken item")
{
    AxiomSet axioms;
    REQUIRE(axioms.loadDirectory(ICHOR_REPO_DATA));
    const DataRecord good = notable().records.begin()->second;
    REQUIRE(problems(good, axioms).empty());

    DataRecord noCost = good;
    noCost.fields.erase("cost");
    REQUIRE(!problems(noCost, axioms).empty());

    DataRecord emDash = good;
    emDash.fields["description"] = std::string("A blade \xE2\x80\x94 sharp.");
    REQUIRE(!problems(emDash, axioms).empty());

    DataRecord tooHeavy = good;
    tooHeavy.fields["weight"] = 40.0;
    REQUIRE(!problems(tooHeavy, axioms).empty());

    DataRecord nanWeight = good;
    nanWeight.fields["weight"] = std::nan("");
    REQUIRE(!problems(nanWeight, axioms).empty());

    DataRecord uniqueTier = good;
    uniqueTier.fields["tier"] = std::string("unique");
    REQUIRE(!problems(uniqueTier, axioms).empty());

    DataRecord grants = good;
    grants.fields["grants"] = std::vector<std::string>{"swim"};
    REQUIRE(!problems(grants, axioms).empty());
}
