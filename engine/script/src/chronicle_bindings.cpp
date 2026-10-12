#include "ichor/script/chronicle_bindings.h"

#include "ichor/save/chronicle.h"
#include "ichor/sim/scene.h"

#include <stdexcept>
#include <string>

namespace ichor {

void bindChronicle(ScriptHost& host, Chronicle& chronicle, const Scene& scene)
{
    Chronicle* story = &chronicle;
    const Scene* world = &scene;
    host.bind("chronicle", [story, world](const std::vector<ScriptValue>& args) -> ScriptValue {
        if (args.size() < 2 || !std::holds_alternative<std::string>(args[0]) || !std::holds_alternative<std::string>(args[1])) {
            throw std::runtime_error("chronicle: expected a category and a text, both strings");
        }
        if (!story->add(world->tickCount(), std::get<std::string>(args[0]), std::get<std::string>(args[1]))) {
            throw std::runtime_error("chronicle: category must be lowercase letters, digits, dash or underscore, and the chronicle must not be full");
        }
        return std::monostate{};
    });
}

}
