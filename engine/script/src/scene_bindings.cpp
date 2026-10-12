#include "ichor/script/scene_bindings.h"

#include "ichor/sim/scene.h"

#include <stdexcept>
#include <string>

namespace ichor {

namespace {

double numberAt(const std::vector<ScriptValue>& args, std::size_t index, const char* function)
{
    if (index >= args.size() || !std::holds_alternative<double>(args[index])) {
        throw std::runtime_error(std::string(function) + ": argument " + std::to_string(index + 1) + " must be a number");
    }
    return std::get<double>(args[index]);
}

const std::string& nameAt(const std::vector<ScriptValue>& args, const char* function)
{
    if (args.empty() || !std::holds_alternative<std::string>(args[0]) || std::get<std::string>(args[0]).empty()) {
        throw std::runtime_error(std::string(function) + ": argument 1 must be a non-empty name");
    }
    return std::get<std::string>(args[0]);
}

}

void bindScene(ScriptHost& host, Scene& scene)
{
    Scene* world = &scene;
    host.bind("spawn", [world](const std::vector<ScriptValue>& args) -> ScriptValue {
        const std::string& name = nameAt(args, "spawn");
        SpawnCommand spawn;
        spawn.id = StableId::of(name);
        spawn.name.value = name;
        spawn.transform.position = {numberAt(args, 1, "spawn"), numberAt(args, 2, "spawn"), numberAt(args, 3, "spawn")};
        if (args.size() > 4) {
            const double mesh = numberAt(args, 4, "spawn");
            if (mesh < 0.0) {
                throw std::runtime_error("spawn: mesh must not be negative");
            }
            spawn.mesh = MeshRef{static_cast<std::uint32_t>(mesh)};
        }
        world->enqueue(spawn);
        return std::monostate{};
    });
    host.bind("destroy", [world](const std::vector<ScriptValue>& args) -> ScriptValue {
        world->enqueue(DestroyCommand{StableId::of(nameAt(args, "destroy"))});
        return std::monostate{};
    });
    host.bind("move", [world](const std::vector<ScriptValue>& args) -> ScriptValue {
        const StableId id = StableId::of(nameAt(args, "move"));
        world->enqueue(MoveCommand{id, {numberAt(args, 1, "move"), numberAt(args, 2, "move"), numberAt(args, 3, "move")}});
        return std::monostate{};
    });
    host.bind("exists", [world](const std::vector<ScriptValue>& args) -> ScriptValue {
        return world->find(StableId::of(nameAt(args, "exists"))) != entt::null;
    });
}

}
