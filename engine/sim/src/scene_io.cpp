#include "ichor/sim/scene_io.h"

#include "ichor/core/binary.h"
#include "ichor/core/file_io.h"
#include "ichor/core/log.h"
#include "ichor/sim/scene.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>

namespace ichor {

namespace {

constexpr std::array<char, 4> kMagic{'I', 'S', 'A', 'V'};
constexpr std::uint32_t kVersion = 1;

enum ComponentBits : std::uint32_t {
    HasMesh = 1u << 0,
    HasSpin = 1u << 1,
    HasPlayer = 1u << 2,
    HasStaticBox = 1u << 3,
    HasCharacter = 1u << 4,
};

struct RngState {
    std::string system;
    std::uint64_t state = 0;
    std::uint64_t inc = 0;
    std::uint64_t seed = 0;
};

}

template <>
struct Reflect<RngState> {
    static constexpr auto fields()
    {
        return std::make_tuple(field("system", &RngState::system), field("state", &RngState::state), field("inc", &RngState::inc), field("seed", &RngState::seed));
    }
};

bool saveScene(const std::filesystem::path& path, const Scene& scene)
{
    BinaryWriter w;
    w.write(kMagic);
    w.write(kVersion);
    w.write(scene.seed());
    w.write(scene.clock().tick);
    w.write(scene.clock().time);

    std::vector<RngState> rngs;
    for (const auto& [name, rng] : scene.rngs()) {
        rngs.push_back({name, rng.state(), rng.increment(), rng.seed()});
    }
    w.write(rngs);

    const entt::registry& registry = scene.registry();
    std::vector<entt::entity> entities;
    for (auto entity : registry.view<const StableId>()) {
        entities.push_back(entity);
    }
    std::sort(entities.begin(), entities.end(), [&](entt::entity a, entt::entity b) {
        return registry.get<StableId>(a) < registry.get<StableId>(b);
    });
    w.write(static_cast<std::uint32_t>(entities.size()));
    for (entt::entity entity : entities) {
        std::uint32_t bits = 0;
        const auto* mesh = registry.try_get<MeshRef>(entity);
        const auto* spin = registry.try_get<Spin>(entity);
        const auto* player = registry.try_get<Player>(entity);
        const auto* box = registry.try_get<StaticBox>(entity);
        const auto* body = registry.try_get<CharacterBody>(entity);
        if (mesh) bits |= HasMesh;
        if (spin) bits |= HasSpin;
        if (player) bits |= HasPlayer;
        if (box) bits |= HasStaticBox;
        if (body) bits |= HasCharacter;
        w.write(registry.get<StableId>(entity));
        w.write(registry.get<Name>(entity));
        w.write(registry.get<Transform>(entity));
        w.write(bits);
        if (mesh) w.write(*mesh);
        if (spin) w.write(*spin);
        if (player) w.write(*player);
        if (box) w.write(*box);
        if (body) w.write(*body);
    }

    return writeFileAtomically(path, w.bytes());
}

bool loadScene(const std::filesystem::path& path, Scene& scene)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        logf(LogLevel::Error, "cannot open {}", path.string());
        return false;
    }
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    BinaryReader r(bytes);

    const auto bad = [&](const char* why) {
        logf(LogLevel::Error, "{}: {}", path.string(), why);
        return false;
    };
    std::array<char, 4> magic{};
    std::uint32_t version = 0;
    std::uint64_t seed = 0;
    Scene::Clock clock;
    if (!r.read(magic) || magic != kMagic || !r.read(version) || version != kVersion) return bad("not a scene snapshot");
    if (!r.read(seed) || seed != scene.seed()) return bad("seed does not match the scene");
    if (!r.read(clock.tick) || !r.read(clock.time)) return bad("truncated clock");

    std::vector<RngState> rngs;
    if (!r.read(rngs)) return bad("truncated generators");

    std::uint32_t count = 0;
    if (!r.read(count) || count > r.remaining()) return bad("bad entity count");
    for (const RngState& state : rngs) {
        if ((state.inc & 1u) == 0) return bad("generator increment must be odd");
    }

    // Everything is parsed before the scene is touched, so a bad file leaves it intact.
    std::vector<SpawnCommand> spawns;
    for (std::uint32_t i = 0; i < count; ++i) {
        SpawnCommand spawn;
        std::uint32_t bits = 0;
        if (!r.read(spawn.id) || !r.read(spawn.name) || !r.read(spawn.transform) || !r.read(bits)) return bad("truncated entity");
        if ((bits & HasMesh) != 0) {
            MeshRef mesh;
            if (!r.read(mesh)) return bad("truncated mesh");
            spawn.mesh = mesh;
        }
        if ((bits & HasSpin) != 0) {
            Spin spin;
            if (!r.read(spin)) return bad("truncated spin");
            spawn.spin = spin;
        }
        if ((bits & HasPlayer) != 0) {
            Player player;
            if (!r.read(player)) return bad("truncated player");
            spawn.player = player;
        }
        if ((bits & HasStaticBox) != 0) {
            StaticBox box;
            if (!r.read(box)) return bad("truncated static box");
            spawn.staticBox = box;
        }
        if ((bits & HasCharacter) != 0) {
            CharacterBody body;
            if (!r.read(body)) return bad("truncated character");
            spawn.character = body;
        }
        spawns.push_back(std::move(spawn));
    }
    if (!r.atEnd()) return bad("trailing bytes");

    scene.clearForRestore();
    scene.restoreClock(clock);
    for (const RngState& state : rngs) {
        scene.restoreRng(state.system, Rng::fromState(state.seed, state.state, state.inc));
    }
    // Static bodies first, so characters find their ground whatever the id order.
    for (const SpawnCommand& spawn : spawns) {
        if (spawn.staticBox) scene.restore(spawn);
    }
    for (const SpawnCommand& spawn : spawns) {
        if (!spawn.staticBox) scene.restore(spawn);
    }
    return true;
}

}
