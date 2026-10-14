#include "ichor/script/world_bindings.h"

#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace ichor {

namespace {

double coordinate(const std::vector<ScriptValue>& args, std::size_t index, const char* what)
{
    if (index >= args.size() || !std::holds_alternative<double>(args[index])) {
        throw std::runtime_error(std::string("water_depth: expected the ") + what + " as a number");
    }
    const double value = std::get<double>(args[index]);
    if (!std::isfinite(value) || std::abs(value) > 1e8) {
        throw std::runtime_error(std::string("water_depth: the ") + what + " is out of range");
    }
    return value;
}

double finite(double answer, const char* name)
{
    if (!std::isfinite(answer)) {
        throw std::runtime_error(std::string(name) + ": the world gave no sensible answer");
    }
    return answer;
}

}

void bindWorld(ScriptHost& host, WorldQueries queries)
{
    const auto shared = std::make_shared<WorldQueries>(std::move(queries));
    host.bind("water_depth", [shared](const std::vector<ScriptValue>& args) -> ScriptValue {
        const double x = coordinate(args, 0, "x");
        const double z = coordinate(args, 1, "z");
        return shared->waterDepth ? finite(shared->waterDepth(x, z), "water_depth") : 0.0;
    });
    host.bind("hour", [shared](const std::vector<ScriptValue>&) -> ScriptValue {
        return shared->hour ? finite(shared->hour(), "hour") : 0.0;
    });
    host.bind("overcast", [shared](const std::vector<ScriptValue>&) -> ScriptValue {
        return shared->overcast ? finite(shared->overcast(), "overcast") : 0.0;
    });
    host.bind("rain", [shared](const std::vector<ScriptValue>&) -> ScriptValue {
        return shared->rain ? finite(shared->rain(), "rain") : 0.0;
    });
}

}
