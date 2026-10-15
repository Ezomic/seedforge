#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace ichor {

// A field of a data record: a scalar, or a list of strings (tags, drops).
using DataValue = std::variant<bool, double, std::string, std::vector<std::string>>;

struct DataRecord {
    std::string id;
    std::string file; // data-root-relative path of the file that defined it
    std::map<std::string, DataValue, std::less<>> fields;
};

struct DataTable {
    std::string name;
    std::map<std::string, DataRecord, std::less<>> records; // by id, so iteration order is fixed
};

// The RPG data layer: data/<table>/*.json, shared with the generator, loaded
// into id-keyed tables. A file is a JSON array of objects, each with a string
// "id" and fields that are booleans, numbers, strings or lists of strings.
// The file's directory names the table; ids are unique within a table across
// all its files. Loading is all or nothing: any error names the file and
// leaves whatever was loaded before untouched, so a half-edited file during
// a reload keeps the last good data.
class DataSet {
public:
    bool loadDirectory(const std::filesystem::path& root);
    void clear() { tables_.clear(); }

    const std::string& lastError() const { return error_; }
    const DataTable* table(std::string_view name) const;
    const DataRecord* record(std::string_view table, std::string_view id) const;
    const DataValue* field(std::string_view table, std::string_view id, std::string_view field) const;
    std::size_t tableCount() const { return tables_.size(); }

    // A stable digest of every table, record and field, independent of file
    // layout and load order. The save ledger records it, so a save made
    // against different data can be told apart.
    std::uint64_t digest() const;

private:
    std::map<std::string, DataTable, std::less<>> tables_;
    std::string error_;
};

}
