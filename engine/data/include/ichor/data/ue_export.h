#pragma once

#include "ichor/data/data_set.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace ichor {

enum class UeFieldType { Bool, Int32, Float, String, Name, StringArray };

struct UeFieldSpec {
    std::string name;
    UeFieldType type = UeFieldType::String;
    bool required = false;
};

struct UeTableSchema {
    std::string table;      // data/<table>/
    std::string assetClass; // the UE UDataAsset subclass the importer instantiates
    std::vector<UeFieldSpec> fields;
};

struct UeSchema {
    std::vector<UeTableSchema> tables;
};

using UeValue = std::variant<bool, std::int32_t, float, std::string, std::vector<std::string>>;

struct UeField {
    std::string name;
    UeFieldType type = UeFieldType::String;
    UeValue value;
};

// One DataAsset a UE-side importer would create: a flat, typed field list with no JSON left in it.
struct UeAsset {
    std::string assetName; // DA_<Table>_<id>, safe as a UE asset name
    std::string assetClass;
    std::string table;
    std::string sourceId;
    std::vector<UeField> fields; // in schema order
};

struct UeConversion {
    std::vector<UeAsset> assets; // table in schema order, then id order
    std::vector<std::string> errors;
    bool ok() const { return errors.empty(); }
};

// Schema document: {"tables":[{"table":"currency","assetClass":"UCurrencyData",
// "fields":[{"name":"cap","type":"int32","required":true}]}]}. Types: bool,
// int32, float, string, name, string_array.
bool parseUeSchema(std::string_view json, UeSchema& out, std::string& error);

// Validates every record of every schema table against its schema and
// converts it. Reports all problems rather than the first, and never returns
// a partial asset for a record that failed.
UeConversion convertToUeAssets(const DataSet& data, const UeSchema& schema);

// Deterministic text of the intermediate description, byte-identical across platforms.
std::string toUeIntermediateJson(const UeConversion& conversion);

}
