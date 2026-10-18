#include "ichor/data/axiom.h"
#include "ichor/data/data_set.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

using namespace ichor;

namespace {

const std::set<std::string> kClasses = {"consumable", "relic", "key"};
const std::set<std::string> kCostKinds = {"weight", "repair", "noise", "vitality", "charge", "standing", "stamina", "sight",
                                          "access", "time", "risk", "slot", "debt", "coin"};
const std::map<std::string, double> kMaxWeight = {{"consumable", 1.0}, {"relic", 2.0}, {"key", 1.0}};

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

const double* number(const DataRecord& record, const std::string& key)
{
    const auto it = record.fields.find(key);
    return it == record.fields.end() ? nullptr : std::get_if<double>(&it->second);
}

const bool* flag(const DataRecord& record, const std::string& key)
{
    const auto it = record.fields.find(key);
    return it == record.fields.end() ? nullptr : std::get_if<bool>(&it->second);
}

// Axiom id to the point-of-interest ids its poi.weights names.
const std::map<std::string, std::set<std::string>>& poisByAxiom()
{
    static const std::map<std::string, std::set<std::string>> pois = [] {
        std::map<std::string, std::set<std::string>> found;
        for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::path(ICHOR_REPO_DATA) / "axioms")) {
            if (entry.path().extension() != ".json") continue;
            std::ifstream in(entry.path(), std::ios::binary);
            const nlohmann::json root = nlohmann::json::parse(in, nullptr, false);
            if (!root.is_object() || !root.contains("id") || !root["id"].is_string()) continue;
            std::set<std::string>& ids = found[root["id"].get<std::string>()];
            if (root.contains("poi") && root["poi"].is_object() && root["poi"].contains("weights") && root["poi"]["weights"].is_object()) {
                for (const auto& [poi, weight] : root["poi"]["weights"].items()) ids.insert(poi);
            }
        }
        return found;
    }();
    return pois;
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

    const std::string cls = text(item, "class");
    if (!kClasses.contains(cls)) found.push_back("class is not consumable, relic or key");

    const std::vector<std::string> kinds = list(item, "cost_kinds");
    if (kinds.empty()) found.push_back("no cost_kinds");
    for (const std::string& kind : kinds) {
        if (!kCostKinds.contains(kind)) found.push_back("unknown cost kind " + kind);
    }

    const double* weight = number(item, "weight");
    const auto cap = kMaxWeight.find(cls);
    const double limit = cap == kMaxWeight.end() ? 0.0 : cap->second;
    if (!weight || !std::isfinite(*weight) || *weight < 0.0 || *weight > limit) found.push_back("weight missing or out of range");

    const std::vector<std::string> homes = list(item, "home");
    if (homes.empty()) found.push_back("no home axiom");
    for (const std::string& home : homes) {
        if (axioms.find(home) == nullptr) found.push_back("unknown home axiom " + home);
    }
    if (item.fields.contains("grants")) found.push_back("notable items grant no capability");
    if (item.fields.contains("slot")) found.push_back("only gear has a slot");

    const double* uses = number(item, "uses");
    const bool* passive = flag(item, "passive");
    const std::string opens = text(item, "opens");
    const double* perWorld = number(item, "per_world");

    if (cls == "consumable") {
        if (!uses || *uses != 1.0) found.push_back("a consumable is single use");
        if (passive) found.push_back("a consumable is not passive");
        if (!opens.empty() || perWorld) found.push_back("a consumable opens nothing");
    } else if (cls == "relic") {
        if (!passive || !*passive) found.push_back("a relic is a carried passive");
        if (uses) found.push_back("a relic has no uses");
        if (!opens.empty() || perWorld) found.push_back("a relic opens nothing");
        if (homes.size() != 1) found.push_back("a relic is tied to exactly one home axiom");
    } else if (cls == "key") {
        if (!perWorld || *perWorld != 1.0) found.push_back("a key is one per world");
        if (uses || passive) found.push_back("a key has no uses and is not passive");
        if (homes.size() != 1) found.push_back("a key is tied to exactly one home axiom");
        if (opens.empty()) {
            found.push_back("a key opens a point of interest");
        } else if (homes.size() == 1) {
            const auto axiomPois = poisByAxiom().find(homes.front());
            if (axiomPois == poisByAxiom().end() || !axiomPois->second.contains(opens)) {
                found.push_back("key opens " + opens + ", which is not a poi of its home axiom");
            }
        }
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

const DataTable& combined()
{
    const DataTable* table = shipped().table("notable_items");
    REQUIRE(table != nullptr);
    return *table;
}

// other_2.json alone, copied into a scratch data root unique to this process (ctest runs each case in parallel).
const DataTable& classTranche()
{
    static DataSet set;
    static const bool loaded = [] {
        const std::filesystem::path root = std::filesystem::temp_directory_path() / ("ichor_notable_items_classes_" + std::to_string((std::uint64_t{std::random_device{}()} << 32) | std::random_device{}()));
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root / "notable_items");
        std::filesystem::copy_file(std::filesystem::path(ICHOR_REPO_DATA) / "notable_items" / "other_2.json",
                                   root / "notable_items" / "other_2.json");
        const bool ok = set.loadDirectory(root);
        std::filesystem::remove_all(root);
        return ok;
    }();
    REQUIRE(loaded);
    const DataTable* table = set.table("notable_items");
    REQUIRE(table != nullptr);
    return *table;
}

DataRecord firstOfClass(const std::string& cls)
{
    for (const auto& [id, item] : classTranche().records) {
        if (text(item, "class") == cls) return item;
    }
    FAIL("no item of class " + cls);
    return {};
}

}

TEST_CASE("the class tranche has about thirty items spread over consumables, relics and keys")
{
    std::map<std::string, int> perClass;
    for (const auto& [id, item] : classTranche().records) ++perClass[text(item, "class")];
    REQUIRE(classTranche().records.size() >= 30);
    for (const char* cls : {"consumable", "relic", "key"}) {
        INFO(cls);
        REQUIRE(perClass[cls] >= 8);
    }
    REQUIRE(combined().records.size() >= classTranche().records.size() + 156);
}

TEST_CASE("every class item passes its per-class schema")
{
    AxiomSet axioms;
    REQUIRE(axioms.loadDirectory(ICHOR_REPO_DATA));
    for (const auto& [id, item] : classTranche().records) {
        INFO(id);
        const std::vector<std::string> found = problems(item, axioms);
        INFO((found.empty() ? std::string() : found.front()));
        REQUIRE(found.empty());
    }
}

TEST_CASE("class item ids and names are unique across every notable file and the catalogue")
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

TEST_CASE("the class tranche leans on home axioms the gear tranches use thinly")
{
    std::set<std::string> homes;
    for (const auto& [id, item] : classTranche().records) {
        for (const std::string& home : list(item, "home")) homes.insert(home);
    }
    REQUIRE(homes.size() >= 12);
}

TEST_CASE("gear files stay gear and carry none of the class fields")
{
    int gear = 0;
    for (const auto& [id, item] : combined().records) {
        if (text(item, "class") != "gear") continue;
        ++gear;
        INFO(id);
        REQUIRE(!item.fields.contains("uses"));
        REQUIRE(!item.fields.contains("passive"));
        REQUIRE(!item.fields.contains("opens"));
        REQUIRE(!item.fields.contains("per_world"));
    }
    REQUIRE(gear >= 120);
}

TEST_CASE("the class checker goes red when a class rule is removed")
{
    AxiomSet axioms;
    REQUIRE(axioms.loadDirectory(ICHOR_REPO_DATA));

    const DataRecord consumable = firstOfClass("consumable");
    const DataRecord relic = firstOfClass("relic");
    const DataRecord key = firstOfClass("key");
    for (const DataRecord* good : {&consumable, &relic, &key}) REQUIRE(problems(*good, axioms).empty());

    DataRecord reusable = consumable;
    reusable.fields["uses"] = 3.0;
    REQUIRE(!problems(reusable, axioms).empty());

    DataRecord noUses = consumable;
    noUses.fields.erase("uses");
    REQUIRE(!problems(noUses, axioms).empty());

    DataRecord activeRelic = relic;
    activeRelic.fields["passive"] = false;
    REQUIRE(!problems(activeRelic, axioms).empty());

    DataRecord twoHomeRelic = relic;
    twoHomeRelic.fields["home"] = std::vector<std::string>{"law.oath", "law.mysteries"};
    REQUIRE(!problems(twoHomeRelic, axioms).empty());

    DataRecord noLock = key;
    noLock.fields.erase("opens");
    REQUIRE(!problems(noLock, axioms).empty());

    DataRecord wrongLock = key;
    wrongLock.fields["opens"] = std::string("farmstead_that_is_not_in_its_axiom");
    REQUIRE(!problems(wrongLock, axioms).empty());

    DataRecord manyPerWorld = key;
    manyPerWorld.fields["per_world"] = 2.0;
    REQUIRE(!problems(manyPerWorld, axioms).empty());

    DataRecord gearClass = relic;
    gearClass.fields["class"] = std::string("gear");
    REQUIRE(!problems(gearClass, axioms).empty());

    DataRecord withSlot = consumable;
    withSlot.fields["slot"] = std::string("trinket");
    REQUIRE(!problems(withSlot, axioms).empty());

    DataRecord emDash = relic;
    emDash.fields["benefit"] = std::string("Sharp \xE2\x80\x94 very.");
    REQUIRE(!problems(emDash, axioms).empty());

    DataRecord heavyKey = key;
    heavyKey.fields["weight"] = 5.0;
    REQUIRE(!problems(heavyKey, axioms).empty());

    DataRecord notANumber = consumable;
    notANumber.fields["weight"] = std::nan("");
    REQUIRE(!problems(notANumber, axioms).empty());

    DataRecord unknownHome = key;
    unknownHome.fields["home"] = std::vector<std::string>{"law.nonexistent"};
    REQUIRE(!problems(unknownHome, axioms).empty());
}
