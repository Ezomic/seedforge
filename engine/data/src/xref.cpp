#include "ichor/data/xref.h"

#include "ichor/core/file_io.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <tuple>

namespace ichor {

namespace {

using Json = nlohmann::json;

constexpr std::string_view kAxioms = "axioms";
constexpr std::uintmax_t kMaxFileBytes = 1024 * 1024;

const std::vector<RefRule> kRules = {
    {"axioms", "conflicts", "axioms"},
    {"axioms", "biomes.inject", "biomes"},
    {"axioms", "biomes.suppress", "biomes"},
    {"axioms", "poi.weights", "pois"},
    {"axioms", "enemies.roster_add", "enemies"},
    {"axioms", "enemies.roster_remove", "enemies"},
    {"axioms", "economy.scarce", "goods"},
    {"axioms", "economy.abundant", "goods"},
    {"npcs", "incompatible", "npcs"},
};

void wordsOf(const AxiomValue& value, std::vector<std::string>& out)
{
    if (const std::string* word = std::get_if<std::string>(&value)) {
        out.push_back(*word);
    } else if (const auto* list = std::get_if<std::vector<std::string>>(&value)) {
        out.insert(out.end(), list->begin(), list->end());
    } else if (const auto* weights = std::get_if<std::map<std::string, double, std::less<>>>(&value)) {
        for (const auto& [word, weight] : *weights) {
            out.push_back(word);
        }
    }
}

std::vector<std::string> axiomWords(const Axiom& axiom, std::string_view field)
{
    std::vector<std::string> words;
    if (field == "conflicts") {
        return axiom.conflicts;
    }
    const std::size_t dot = field.find('.');
    if (dot == std::string_view::npos) {
        return words;
    }
    const auto block = axiom.blocks.find(field.substr(0, dot));
    if (block == axiom.blocks.end()) {
        return words;
    }
    const auto key = block->second.find(field.substr(dot + 1));
    if (key != block->second.end()) {
        wordsOf(key->second, words);
    }
    return words;
}

std::vector<std::string> recordWords(const DataRecord& record, std::string_view field)
{
    std::vector<std::string> words;
    const auto it = record.fields.find(field);
    if (it == record.fields.end()) {
        return words;
    }
    if (const std::string* word = std::get_if<std::string>(&it->second)) {
        words.push_back(*word);
    } else if (const auto* list = std::get_if<std::vector<std::string>>(&it->second)) {
        words = *list;
    }
    return words;
}

class Checker {
public:
    Checker(const DataSet& data, const AxiomSet& axioms) : data_(data), axioms_(axioms) {}

    bool hasRecords(std::string_view target) const
    {
        if (target == kAxioms) {
            return true;
        }
        const DataTable* table = data_.table(target);
        return table != nullptr && !table->records.empty();
    }

    void check(std::string file, std::string record, std::string field, std::string_view target, const std::string& value)
    {
        ++checked;
        const bool found = target == kAxioms ? axioms_.find(value) != nullptr : data_.record(target, value) != nullptr;
        if (!found) {
            dangling.push_back({std::move(file), std::move(record), std::move(field), value, std::string(target)});
        }
    }

    std::size_t checked = 0;
    std::vector<DanglingRef> dangling;

private:
    const DataSet& data_;
    const AxiomSet& axioms_;
};

std::vector<std::filesystem::path> jsonFiles(const std::filesystem::path& directory)
{
    std::vector<std::filesystem::path> files;
    std::error_code code;
    for (std::filesystem::directory_iterator it(directory, code), end; !code && it != end; it.increment(code)) {
        if (it->is_regular_file(code) && it->path().extension() == ".json") {
            files.push_back(it->path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

bool readJson(const std::filesystem::path& file, Json& out)
{
    std::vector<std::uint8_t> bytes;
    if (!readFileBytes(file, bytes, kMaxFileBytes)) {
        return false;
    }
    out = Json::parse(bytes.begin(), bytes.end(), nullptr, false);
    return !out.is_discarded();
}

void checkConflictFiles(Checker& checker, const std::filesystem::path& dataRoot, std::vector<std::string>& unreadable)
{
    for (const std::filesystem::path& file : jsonFiles(dataRoot / "conflicts")) {
        Json json;
        const std::string label = std::filesystem::relative(file, dataRoot).generic_string();
        if (!readJson(file, json) || !json.is_object() || !json.contains("pairs") || !json["pairs"].is_array()) {
            unreadable.push_back(label);
            continue;
        }
        const Json& pairs = json["pairs"];
        for (std::size_t i = 0; i < pairs.size(); ++i) {
            if (!pairs[i].is_array()) {
                continue;
            }
            const std::string record = "pairs[" + std::to_string(i) + "]";
            for (std::size_t side = 0; side < pairs[i].size(); ++side) {
                if (pairs[i][side].is_string()) {
                    checker.check(label, record, record + "[" + std::to_string(side) + "]", kAxioms, pairs[i][side].get<std::string>());
                }
            }
        }
    }
}

void checkSynergyFiles(Checker& checker, const std::filesystem::path& dataRoot, std::vector<std::string>& unreadable)
{
    for (const std::filesystem::path& file : jsonFiles(dataRoot / "synergies")) {
        Json json;
        const std::string label = std::filesystem::relative(file, dataRoot).generic_string();
        if (!readJson(file, json) || !json.is_object() || !json.contains("synergies") || !json["synergies"].is_array()) {
            unreadable.push_back(label);
            continue;
        }
        const Json& synergies = json["synergies"];
        for (std::size_t i = 0; i < synergies.size(); ++i) {
            const Json& synergy = synergies[i];
            if (!synergy.is_object() || !synergy.contains("axioms") || !synergy["axioms"].is_array()) {
                continue;
            }
            const std::string record = synergy.contains("id") && synergy["id"].is_string() ? synergy["id"].get<std::string>() : "synergies[" + std::to_string(i) + "]";
            for (std::size_t j = 0; j < synergy["axioms"].size(); ++j) {
                if (synergy["axioms"][j].is_string()) {
                    checker.check(label, record, "axioms[" + std::to_string(j) + "]", kAxioms, synergy["axioms"][j].get<std::string>());
                }
            }
        }
    }
}

}

const std::vector<RefRule>& referenceRules()
{
    return kRules;
}

std::string describe(const DanglingRef& ref)
{
    return ref.file + ": " + ref.record + ": " + ref.field + ": no " + ref.target + " record \"" + ref.value + "\"";
}

XrefReport checkReferences(const DataSet& data, const AxiomSet& axioms, const std::filesystem::path& dataRoot)
{
    return checkReferences(data, axioms, dataRoot, referenceRules());
}

XrefReport checkReferences(const DataSet& data, const AxiomSet& axioms, const std::filesystem::path& dataRoot, const std::vector<RefRule>& rules)
{
    Checker checker(data, axioms);
    XrefReport report;
    for (const RefRule& rule : rules) {
        if (!checker.hasRecords(rule.target)) {
            report.unchecked.push_back(std::string(rule.source) + "." + std::string(rule.field) + " -> " + std::string(rule.target));
            continue;
        }
        if (rule.source == kAxioms) {
            for (const auto& [id, axiom] : axioms.axioms()) {
                for (const std::string& word : axiomWords(axiom, rule.field)) {
                    checker.check(axiom.file, id, std::string(rule.field), rule.target, word);
                }
            }
        } else if (const DataTable* table = data.table(rule.source); table == nullptr) {
            report.unchecked.push_back(std::string(rule.source) + "." + std::string(rule.field) + " (no such source table)");
        } else {
            for (const auto& [id, record] : table->records) {
                for (const std::string& word : recordWords(record, rule.field)) {
                    checker.check(record.file, id, std::string(rule.field), rule.target, word);
                }
            }
        }
    }
    checkConflictFiles(checker, dataRoot, report.unreadable);
    checkSynergyFiles(checker, dataRoot, report.unreadable);

    std::sort(checker.dangling.begin(), checker.dangling.end(), [](const DanglingRef& a, const DanglingRef& b) {
        return std::tie(a.file, a.record, a.field, a.value) < std::tie(b.file, b.record, b.field, b.value);
    });
    report.dangling = std::move(checker.dangling);
    report.checked = checker.checked;
    std::sort(report.unchecked.begin(), report.unchecked.end());
    std::sort(report.unreadable.begin(), report.unreadable.end());
    return report;
}

}
