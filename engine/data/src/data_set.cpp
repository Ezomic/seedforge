#include "ichor/data/data_set.h"

#include "ichor/core/hash.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>

namespace ichor {

namespace {

bool parseValue(const nlohmann::json& json, DataValue& out)
{
    if (json.is_boolean()) {
        out = json.get<bool>();
        return true;
    }
    if (json.is_number()) {
        out = json.get<double>();
        return true;
    }
    if (json.is_string()) {
        out = json.get<std::string>();
        return true;
    }
    if (json.is_array()) {
        std::vector<std::string> list;
        for (const nlohmann::json& item : json) {
            if (!item.is_string()) {
                return false;
            }
            list.push_back(item.get<std::string>());
        }
        out = std::move(list);
        return true;
    }
    return false;
}

std::uint64_t hashValue(std::uint64_t seed, const DataValue& value)
{
    std::uint64_t h = hashCombine(seed, value.index());
    if (const bool* flag = std::get_if<bool>(&value)) {
        return hashCombine(h, *flag ? 1 : 0);
    }
    if (const double* number = std::get_if<double>(&value)) {
        return hashCombine(h, hash64(std::to_string(*number)));
    }
    if (const std::string* text = std::get_if<std::string>(&value)) {
        return hashCombine(h, hash64(*text));
    }
    for (const std::string& item : std::get<std::vector<std::string>>(value)) {
        h = hashCombine(h, hash64(item));
    }
    return h;
}

}

bool DataSet::loadDirectory(const std::filesystem::path& root)
{
    // Built aside and swapped in on success, so a half-edited file keeps the last good data.
    std::map<std::string, DataTable, std::less<>> tables;
    error_.clear();
    std::error_code code;
    if (!std::filesystem::is_directory(root, code)) {
        error_ = "no data directory at " + root.string();
        return false;
    }
    // Sorted so the same files always load in the same order and errors are reproducible.
    std::vector<std::filesystem::path> files;
    for (std::filesystem::recursive_directory_iterator it(root, code), end; !code && it != end; it.increment(code)) {
        if (it->is_regular_file(code) && it->path().extension() == ".json") {
            files.push_back(it->path());
        }
    }
    if (code) {
        error_ = "cannot read " + root.string() + ": " + code.message();
        return false;
    }
    std::sort(files.begin(), files.end());

    for (const std::filesystem::path& file : files) {
        const std::filesystem::path relative = std::filesystem::relative(file, root);
        const auto fail = [&](const std::string& message) {
            error_ = relative.generic_string() + ": " + message;
            return false;
        };
        const std::string tableName = relative.has_parent_path() ? relative.begin()->generic_string() : relative.stem().generic_string();

        std::ifstream stream(file, std::ios::binary);
        nlohmann::json json = nlohmann::json::parse(stream, nullptr, false);
        if (json.is_discarded()) {
            return fail("not valid JSON");
        }
        if ((tableName == "axioms" || tableName == "conflicts" || tableName == "ui") && json.is_object()) {
            continue; // nested documents that AxiomSet and ConflictList validate and load themselves
        }
        if (!json.is_array()) {
            return fail("expected an array of records");
        }
        DataTable& table = tables[tableName];
        table.name = tableName;
        for (std::size_t index = 0; index < json.size(); ++index) {
            const nlohmann::json& item = json[index];
            const std::string where = "record " + std::to_string(index);
            if (!item.is_object() || !item.contains("id") || !item["id"].is_string() || item["id"].get<std::string>().empty()) {
                return fail(where + " needs a non-empty string id");
            }
            DataRecord record;
            record.id = item["id"].get<std::string>();
            if (table.records.contains(record.id)) {
                return fail("duplicate id '" + record.id + "' in table '" + tableName + "'");
            }
            for (const auto& [key, value] : item.items()) {
                if (key == "id") {
                    continue;
                }
                DataValue parsed;
                if (!parseValue(value, parsed)) {
                    return fail("'" + record.id + "' field '" + key + "' must be a boolean, number, string or list of strings");
                }
                record.fields.emplace(key, std::move(parsed));
            }
            table.records.emplace(record.id, std::move(record));
        }
    }
    tables_ = std::move(tables);
    return true;
}

const DataTable* DataSet::table(std::string_view name) const
{
    const auto found = tables_.find(name);
    return found == tables_.end() ? nullptr : &found->second;
}

const DataRecord* DataSet::record(std::string_view tableName, std::string_view id) const
{
    const DataTable* found = table(tableName);
    if (found == nullptr) {
        return nullptr;
    }
    const auto it = found->records.find(id);
    return it == found->records.end() ? nullptr : &it->second;
}

const DataValue* DataSet::field(std::string_view tableName, std::string_view id, std::string_view fieldName) const
{
    const DataRecord* found = record(tableName, id);
    if (found == nullptr) {
        return nullptr;
    }
    const auto it = found->fields.find(fieldName);
    return it == found->fields.end() ? nullptr : &it->second;
}

std::uint64_t DataSet::digest() const
{
    std::uint64_t h = 0;
    for (const auto& [tableName, table] : tables_) {
        h = hashCombine(h, hash64(tableName));
        for (const auto& [id, record] : table.records) {
            h = hashCombine(h, hash64(id));
            for (const auto& [key, value] : record.fields) {
                h = hashValue(hashCombine(h, hash64(key)), value);
            }
        }
    }
    return h;
}

}
