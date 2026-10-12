#include "ichor/script/scene_bindings.h"

#include "ichor/sim/scene.h"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace ichor {

namespace {

double numberAt(const std::vector<ScriptValue>& args, std::size_t index, const char* function)
{
    if (index >= args.size() || !std::holds_alternative<double>(args[index])) {
        throw std::runtime_error(std::string(function) + ": argument " + std::to_string(index + 1) + " must be a number");
    }
    const double value = std::get<double>(args[index]);
    // A NaN or an infinity would poison the state hash, physics and the command log.
    if (!std::isfinite(value) || std::abs(value) > 1e7) {
        throw std::runtime_error(std::string(function) + ": argument " + std::to_string(index + 1) + " must be finite and within 1e7");
    }
    return value;
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
        if (world->find(spawn.id) != entt::null) {
            throw std::runtime_error("spawn: '" + name + "' already exists");
        }
        spawn.name.value = name;
        spawn.transform.position = {numberAt(args, 1, "spawn"), numberAt(args, 2, "spawn"), numberAt(args, 3, "spawn")};
        if (args.size() > 4) {
            const double mesh = numberAt(args, 4, "spawn");
            if (mesh < 0.0 || mesh != std::floor(mesh) || mesh > static_cast<double>(std::numeric_limits<std::uint32_t>::max())) {
                throw std::runtime_error("spawn: mesh must be a whole, non-negative index");
            }
            spawn.mesh = MeshRef{static_cast<std::uint32_t>(mesh)};
        }
        world->enqueue(spawn);
        return std::monostate{};
    });
    host.bind("destroy", [world](const std::vector<ScriptValue>& args) -> ScriptValue {
        const std::string& name = nameAt(args, "destroy");
        if (world->find(StableId::of(name)) == entt::null) {
            throw std::runtime_error("destroy: '" + name + "' does not exist");
        }
        world->enqueue(DestroyCommand{StableId::of(name)});
        return std::monostate{};
    });
    host.bind("move", [world](const std::vector<ScriptValue>& args) -> ScriptValue {
        const std::string& name = nameAt(args, "move");
        const StableId id = StableId::of(name);
        if (world->find(id) == entt::null) {
            throw std::runtime_error("move: '" + name + "' does not exist");
        }
        world->enqueue(MoveCommand{id, {numberAt(args, 1, "move"), numberAt(args, 2, "move"), numberAt(args, 3, "move")}});
        return std::monostate{};
    });
    host.bind("exists", [world](const std::vector<ScriptValue>& args) -> ScriptValue {
        return world->find(StableId::of(nameAt(args, "exists"))) != entt::null;
    });
}

}
