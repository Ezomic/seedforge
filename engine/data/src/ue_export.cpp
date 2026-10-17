#include "ichor/data/ue_export.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstddef>
#include <limits>
#include <set>

namespace ichor {

namespace {

constexpr std::size_t kMaxNameLength = 1023;
constexpr std::size_t kMaxAssetNameLength = 200;

// UE asset names are case-insensitive, and so is the Windows filesystem they cook to.
std::string lowered(std::string text)
{
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return text;
}

bool isAlnumOrUnderscore(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

bool isIdentifier(std::string_view text)
{
    if (text.empty() || text.size() > kMaxNameLength) {
        return false;
    }
    for (const char c : text) {
        if (!isAlnumOrUnderscore(c)) {
            return false;
        }
    }
    return true;
}

bool isNameValue(std::string_view text)
{
    if (text.empty() || text.size() > kMaxNameLength) {
        return false;
    }
    for (const char c : text) {
        if (!isAlnumOrUnderscore(c) && c != '.' && c != '-') {
            return false;
        }
    }
    return true;
}

const char* typeName(UeFieldType type)
{
    switch (type) {
    case UeFieldType::Bool: return "bool";
    case UeFieldType::Int32: return "int32";
    case UeFieldType::Float: return "float";
    case UeFieldType::String: return "string";
    case UeFieldType::Name: return "name";
    case UeFieldType::StringArray: return "string_array";
    }
    return "string";
}

bool parseType(std::string_view text, UeFieldType& out)
{
    for (const UeFieldType type : {UeFieldType::Bool, UeFieldType::Int32, UeFieldType::Float, UeFieldType::String, UeFieldType::Name, UeFieldType::StringArray}) {
        if (text == typeName(type)) {
            out = type;
            return true;
        }
    }
    return false;
}

std::string assetNameFor(const std::string& table, const std::string& id)
{
    std::string name = "DA_" + table + "_";
    for (const char c : id) {
        name.push_back(isAlnumOrUnderscore(c) ? c : '_');
    }
    return name;
}

// Returns an empty string on success, else why the value does not fit the type.
std::string convertValue(const UeFieldSpec& spec, const DataValue& value, UeValue& out)
{
    switch (spec.type) {
    case UeFieldType::Bool:
        if (const bool* flag = std::get_if<bool>(&value)) {
            out = *flag;
            return {};
        }
        return "must be a boolean";
    case UeFieldType::Int32: {
        const double* number = std::get_if<double>(&value);
        if (number == nullptr) {
            return "must be a number";
        }
        const double lo = static_cast<double>(std::numeric_limits<std::int32_t>::min());
        const double hi = static_cast<double>(std::numeric_limits<std::int32_t>::max());
        if (!std::isfinite(*number) || *number < lo || *number > hi || *number != std::floor(*number)) {
            return "must be a whole number within the int32 range";
        }
        out = static_cast<std::int32_t>(*number);
        return {};
    }
    case UeFieldType::Float: {
        const double* number = std::get_if<double>(&value);
        if (number == nullptr) {
            return "must be a number";
        }
        const double limit = static_cast<double>(std::numeric_limits<float>::max());
        if (!std::isfinite(*number) || std::fabs(*number) > limit) {
            return "must be a finite number within the float range";
        }
        out = static_cast<float>(*number);
        return {};
    }
    case UeFieldType::String: {
        const std::string* text = std::get_if<std::string>(&value);
        if (text == nullptr) {
            return "must be a string";
        }
        out = *text;
        return {};
    }
    case UeFieldType::Name: {
        const std::string* text = std::get_if<std::string>(&value);
        if (text == nullptr || !isNameValue(*text)) {
            return "must be a non-empty string of letters, digits, '_', '.' or '-'";
        }
        out = *text;
        return {};
    }
    case UeFieldType::StringArray: {
        const auto* list = std::get_if<std::vector<std::string>>(&value);
        if (list == nullptr) {
            return "must be a list of strings";
        }
        out = *list;
        return {};
    }
    }
    return "unsupported type";
}

}

bool parseUeSchema(std::string_view json, UeSchema& out, std::string& error)
{
    error.clear();
    const nlohmann::json doc = nlohmann::json::parse(json.begin(), json.end(), nullptr, false);
    if (doc.is_discarded() || !doc.is_object() || !doc.contains("tables") || !doc["tables"].is_array()) {
        error = "schema must be a JSON object with a 'tables' array";
        return false;
    }
    UeSchema schema;
    std::set<std::string> seenTables;
    for (const nlohmann::json& item : doc["tables"]) {
        if (!item.is_object() || !item.contains("table") || !item["table"].is_string() || !item.contains("assetClass") || !item["assetClass"].is_string() || !item.contains("fields") || !item["fields"].is_array()) {
            error = "each table needs string 'table' and 'assetClass' and a 'fields' array";
            return false;
        }
        UeTableSchema table;
        table.table = item["table"].get<std::string>();
        table.assetClass = item["assetClass"].get<std::string>();
        if (!isIdentifier(table.table) || !isIdentifier(table.assetClass)) {
            error = "table '" + table.table + "': table and assetClass must be identifiers";
            return false;
        }
        if (!seenTables.insert(table.table).second) {
            error = "duplicate table '" + table.table + "'";
            return false;
        }
        std::set<std::string> seenFields;
        for (const nlohmann::json& field : item["fields"]) {
            if (!field.is_object() || !field.contains("name") || !field["name"].is_string() || !field.contains("type") || !field["type"].is_string()) {
                error = "table '" + table.table + "': each field needs string 'name' and 'type'";
                return false;
            }
            UeFieldSpec spec;
            spec.name = field["name"].get<std::string>();
            if (!isIdentifier(spec.name) || spec.name == "id") {
                error = "table '" + table.table + "': bad field name '" + spec.name + "'";
                return false;
            }
            if (!parseType(field["type"].get<std::string>(), spec.type)) {
                error = "table '" + table.table + "': field '" + spec.name + "' has unknown type '" + field["type"].get<std::string>() + "'";
                return false;
            }
            if (field.contains("required")) {
                if (!field["required"].is_boolean()) {
                    error = "table '" + table.table + "': field '" + spec.name + "' required must be a boolean";
                    return false;
                }
                spec.required = field["required"].get<bool>();
            }
            if (!seenFields.insert(spec.name).second) {
                error = "table '" + table.table + "': duplicate field '" + spec.name + "'";
                return false;
            }
            table.fields.push_back(std::move(spec));
        }
        schema.tables.push_back(std::move(table));
    }
    out = std::move(schema);
    return true;
}

UeConversion convertToUeAssets(const DataSet& data, const UeSchema& schema)
{
    UeConversion result;
    std::set<std::string> assetNames;
    for (const UeTableSchema& tableSchema : schema.tables) {
        const DataTable* table = data.table(tableSchema.table);
        if (table == nullptr) {
            result.errors.push_back("table '" + tableSchema.table + "' is not in the data");
            continue;
        }
        std::set<std::string, std::less<>> known;
        for (const UeFieldSpec& spec : tableSchema.fields) {
            known.insert(spec.name);
        }
        for (const auto& [id, record] : table->records) {
            const std::string where = tableSchema.table + "/" + id;
            UeAsset asset;
            asset.assetName = assetNameFor(tableSchema.table, id);
            asset.assetClass = tableSchema.assetClass;
            asset.table = tableSchema.table;
            asset.sourceId = id;
            bool good = true;
            if (asset.assetName.size() > kMaxAssetNameLength) {
                result.errors.push_back(where + ": asset name is longer than " + std::to_string(kMaxAssetNameLength) + " characters");
                good = false;
            } else if (!assetNames.insert(lowered(asset.assetName)).second) {
                result.errors.push_back(where + ": asset name '" + asset.assetName + "' collides with another record");
                good = false;
            }
            for (const UeFieldSpec& spec : tableSchema.fields) {
                const auto found = record.fields.find(spec.name);
                if (found == record.fields.end()) {
                    if (spec.required) {
                        result.errors.push_back(where + ": missing required field '" + spec.name + "'");
                        good = false;
                    }
                    continue;
                }
                UeField field;
                field.name = spec.name;
                field.type = spec.type;
                const std::string why = convertValue(spec, found->second, field.value);
                if (!why.empty()) {
                    result.errors.push_back(where + ": field '" + spec.name + "' " + why);
                    good = false;
                    continue;
                }
                asset.fields.push_back(std::move(field));
            }
            for (const auto& entry : record.fields) {
                if (!known.contains(entry.first)) {
                    result.errors.push_back(where + ": field '" + entry.first + "' is not in the schema");
                    good = false;
                }
            }
            if (good) {
                result.assets.push_back(std::move(asset));
            }
        }
    }
    return result;
}

std::string toUeIntermediateJson(const UeConversion& conversion)
{
    nlohmann::ordered_json assets = nlohmann::ordered_json::array();
    for (const UeAsset& asset : conversion.assets) {
        nlohmann::ordered_json fields = nlohmann::ordered_json::array();
        for (const UeField& field : asset.fields) {
            nlohmann::ordered_json value;
            if (const bool* flag = std::get_if<bool>(&field.value)) {
                value = *flag;
            } else if (const std::int32_t* whole = std::get_if<std::int32_t>(&field.value)) {
                value = *whole;
            } else if (const float* real = std::get_if<float>(&field.value)) {
                value = *real;
            } else if (const std::string* text = std::get_if<std::string>(&field.value)) {
                value = *text;
            } else {
                value = std::get<std::vector<std::string>>(field.value);
            }
            nlohmann::ordered_json entry = nlohmann::ordered_json::object();
            entry["name"] = field.name;
            entry["type"] = typeName(field.type);
            entry["value"] = std::move(value);
            fields.push_back(std::move(entry));
        }
        nlohmann::ordered_json out = nlohmann::ordered_json::object();
        out["assetName"] = asset.assetName;
        out["assetClass"] = asset.assetClass;
        out["table"] = asset.table;
        out["sourceId"] = asset.sourceId;
        out["fields"] = std::move(fields);
        assets.push_back(std::move(out));
    }
    nlohmann::ordered_json doc = nlohmann::ordered_json::object();
    doc["version"] = 1;
    doc["assets"] = std::move(assets);
    return doc.dump(2, ' ', false, nlohmann::ordered_json::error_handler_t::replace) + "\n";
}

}
