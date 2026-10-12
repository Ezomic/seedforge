#include "ichor/sim/scene_io.h"

#include "ichor/core/binary.h"
#include "ichor/core/file_io.h"
#include "ichor/core/log.h"
#include "ichor/sim/scene.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <span>
#include <iterator>

namespace ichor {

namespace {

constexpr std::array<char, 4> kMagic{'I', 'S', 'A', 'V'};
constexpr std::uint32_t kVersion = 3; // 2: montage cues; 3: registered game components

enum ComponentBits : std::uint32_t {
    HasMesh = 1u << 0,
    HasSpin = 1u << 1,
    HasPlayer = 1u << 2,
    HasStaticBox = 1u << 3,
    HasCharacter = 1u << 4,
    HasAnimator = 1u << 5,
    HasMontage = 1u << 6,
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

    std::vector<std::string> hookNames;
    for (const Scene::ComponentHooks& hooks : scene.componentHooks()) {
        hookNames.push_back(hooks.name);
    }
    w.write(hookNames);

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
        const auto* animator = registry.try_get<Animator>(entity);
        const auto* montage = registry.try_get<MontagePlayer>(entity);
        if (animator) bits |= HasAnimator;
        if (montage) bits |= HasMontage;
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
        if (animator) w.write(*animator);
        if (montage) w.write(*montage);
        for (const Scene::ComponentHooks& hooks : scene.componentHooks()) {
            const bool has = hooks.has(entity);
            w.write(static_cast<std::uint8_t>(has ? 1 : 0));
            if (has) hooks.write(entity, w);
        }
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
    std::vector<std::string> hookNames;
    if (!r.read(hookNames)) return bad("truncated component list");
    const auto& hooks = scene.componentHooks();
    if (hookNames.size() != hooks.size()) return bad("registered components differ from the snapshot");
    for (std::size_t i = 0; i < hooks.size(); ++i) {
        if (hookNames[i] != hooks[i].name) return bad("registered components differ from the snapshot");
    }

    std::uint32_t count = 0;
    if (!r.read(count) || count > r.remaining()) return bad("bad entity count");
    for (const RngState& state : rngs) {
        if ((state.inc & 1u) == 0) return bad("generator increment must be odd");
    }

    // Everything is parsed before the scene is touched, so a bad file leaves it intact.
    std::vector<SpawnCommand> spawns;
    std::vector<std::optional<MontagePlayer>> montages;
    std::vector<std::size_t> hookOffsets; // where each entity's game components start in the file
    const std::size_t total = bytes.size();
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
        if ((bits & HasAnimator) != 0) {
            Animator animator;
            if (!r.read(animator)) return bad("truncated animator");
            spawn.animator = animator;
        }
        std::optional<MontagePlayer> montage;
        if ((bits & HasMontage) != 0) {
            MontagePlayer player;
            if (!r.read(player)) return bad("truncated montage");
            montage = player;
        }
        spawns.push_back(std::move(spawn));
        montages.push_back(montage);
        // Game components are validated now by skipping, and applied once the entity exists.
        hookOffsets.push_back(total - r.remaining());
        for (const Scene::ComponentHooks& hook : hooks) {
            std::uint8_t has = 0;
            if (!r.read(has)) return bad("truncated component");
            if (has != 0 && !hook.skip(r)) return bad("truncated component");
        }
    }
    if (!r.atEnd()) return bad("trailing bytes");

    scene.clearForRestore();
    scene.restoreClock(clock);
    for (const RngState& state : rngs) {
        scene.restoreRng(state.system, Rng::fromState(state.seed, state.state, state.inc));
    }
    // Static bodies first, so characters find their ground whatever the id order.
    const auto applyHooks = [&](std::size_t i) {
        BinaryReader again(std::span(bytes.data() + hookOffsets[i], bytes.size() - hookOffsets[i]));
        const entt::entity entity = scene.find(spawns[i].id);
        for (const Scene::ComponentHooks& hook : hooks) {
            std::uint8_t has = 0;
            again.read(has);
            if (has != 0) hook.read(entity, again);
        }
    };
    for (std::size_t i = 0; i < spawns.size(); ++i) {
        if (spawns[i].staticBox) {
            scene.restore(spawns[i]);
            applyHooks(i);
        }
    }
    for (std::size_t i = 0; i < spawns.size(); ++i) {
        if (!spawns[i].staticBox) {
            scene.restore(spawns[i], montages[i]);
            applyHooks(i);
        }
    }
    return true;
}

}
