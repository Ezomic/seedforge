#include "ichor/script/data_bindings.h"

#include "ichor/data/data_set.h"

#include <stdexcept>
#include <string>

namespace ichor {

namespace {

const std::string& textAt(const std::vector<ScriptValue>& args, std::size_t index, const char* function)
{
    if (index >= args.size() || !std::holds_alternative<std::string>(args[index])) {
        throw std::runtime_error(std::string(function) + ": argument " + std::to_string(index + 1) + " must be a string");
    }
    return std::get<std::string>(args[index]);
}

}

void bindData(ScriptHost& host, const DataSet& data)
{
    const DataSet* set = &data;
    host.bind("data", [set](const std::vector<ScriptValue>& args) -> ScriptValue {
        const DataValue* value = set->field(textAt(args, 0, "data"), textAt(args, 1, "data"), textAt(args, 2, "data"));
        if (value == nullptr) {
            return std::monostate{};
        }
        if (const bool* flag = std::get_if<bool>(value)) {
            return *flag;
        }
        if (const double* number = std::get_if<double>(value)) {
            return *number;
        }
        if (const std::string* text = std::get_if<std::string>(value)) {
            return *text;
        }
        std::string joined;
        for (const std::string& item : std::get<std::vector<std::string>>(*value)) {
            joined += joined.empty() ? "" : ",";
            joined += item;
        }
        return joined;
    });
    host.bind("data_has", [set](const std::vector<ScriptValue>& args) -> ScriptValue {
        return set->record(textAt(args, 0, "data_has"), textAt(args, 1, "data_has")) != nullptr;
    });
}

}
