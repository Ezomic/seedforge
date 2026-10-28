// A component a snapshot saves but the state hash skips lets two runs that disagree report the same
// hash, so a replay desyncs silently; one hashed but not saved makes every loaded game disagree with
// the run that saved it. This probes every component storage of a scene that holds them all: in a
// fresh copy each is removed (unless every entity must hold it), and in another one of its fields is
// changed, and each time the save bytes and the hash must both move or both stay put. Removal alone
// would only check the presence bits both outputs keep, so a write of the data dropped from one of
// them would pass; the change catches that for the field it touches.

#include "ichor/core/file_io.h"
#include "ichor/sim/scene.h"
#include "ichor/sim/scene_io.h"
#include "test_temp.h"

#include <catch2/catch_test_macros.hpp>
#include <entt/core/type_info.hpp>

#include <algorithm>
#include <filesystem>
#include <format>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ichor {

namespace {

struct Mark {
    std::uint32_t value = 0;
};

}

template <>
struct Reflect<Mark> {
    static constexpr auto fields() { return std::make_tuple(field("value", &Mark::value)); }
};

namespace {

enum class Kept { Both, Neither, SavedOnly, HashedOnly };

struct Expected {
    entt::id_type id;
    const char* name;
    Kept kept;
    bool onEveryEntity; // the scene reads it from every entity, so it is changed but never removed
    void (*change)(entt::registry&);
};

template <class T>
constexpr entt::id_type idOf()
{
    return entt::type_hash<T>::value();
}

// Every component the scene keeps. A storage missing from this table fails the test, so a new
// component has to be classified here before it can pass. Only storages the fixture fills can be
// seen, so a component a system or command adds later must be put on the fixture too.
// SavedOnly and HashedOnly entries are the allowlist of intended differences, and each needs a
// comment saying why.
const std::vector<Expected>& sceneComponents()
{
    static const std::vector<Expected> table{
        {idOf<StableId>(), "StableId", Kept::Both, true, [](entt::registry& r) {
             for (auto [entity, id] : r.view<StableId>().each()) id.value ^= 1u;
         }},
        {idOf<Name>(), "Name", Kept::Both, true, [](entt::registry& r) {
             for (auto [entity, name] : r.view<Name>().each()) name.value += "!";
         }},
        {idOf<Transform>(), "Transform", Kept::Both, true, [](entt::registry& r) {
             for (auto [entity, transform] : r.view<Transform>().each()) transform.position.x += 1.0;
         }},
        {idOf<MeshRef>(), "MeshRef", Kept::Both, false, [](entt::registry& r) {
             for (auto [entity, mesh] : r.view<MeshRef>().each()) mesh.mesh += 1u;
         }},
        {idOf<Spin>(), "Spin", Kept::Both, false, [](entt::registry& r) {
             for (auto [entity, spin] : r.view<Spin>().each()) spin.radiansPerSecond += 1.0f;
         }},
        {idOf<Player>(), "Player", Kept::Both, false, [](entt::registry& r) {
             for (auto [entity, player] : r.view<Player>().each()) player.metresPerSecond += 1.0f;
         }},
        {idOf<StaticBox>(), "StaticBox", Kept::Both, false, [](entt::registry& r) {
             for (auto [entity, box] : r.view<StaticBox>().each()) box.halfExtents.x += 1.0f;
         }},
        {idOf<CharacterBody>(), "CharacterBody", Kept::Both, false, [](entt::registry& r) {
             for (auto [entity, body] : r.view<CharacterBody>().each()) body.fallSpeed += 1.0f;
         }},
        {idOf<Animator>(), "Animator", Kept::Both, false, [](entt::registry& r) {
             for (auto [entity, animator] : r.view<Animator>().each()) animator.time += 0.5f;
         }},
        {idOf<MontagePlayer>(), "MontagePlayer", Kept::Both, false, [](entt::registry& r) {
             for (auto [entity, montage] : r.view<MontagePlayer>().each()) montage.time += 0.25f;
         }},
        // Stands for every registered game component: they all share the hooks registerComponent builds.
        {idOf<Mark>(), "registered game component", Kept::Both, false, [](entt::registry& r) {
             for (auto [entity, mark] : r.view<Mark>().each()) mark.value += 1u;
         }},
        // Rendering interpolation only, overwritten at the start of every tick.
        {idOf<PreviousTransform>(), "PreviousTransform", Kept::Neither, false, [](entt::registry& r) {
             for (auto [entity, previous] : r.view<PreviousTransform>().each()) previous.position.x += 1.0;
         }},
        // Skinning matrices rebuilt from the Animator every tick.
        {idOf<SkinPose>(), "SkinPose", Kept::Neither, false, [](entt::registry& r) {
             for (auto [entity, pose] : r.view<SkinPose>().each()) pose.matrices.emplace_back(1.0f);
         }},
    };
    return table;
}

class Subject {
public:
    virtual ~Subject() = default;
    virtual entt::registry& registry() = 0;
    virtual std::vector<std::uint8_t> saved() = 0;
    virtual std::uint64_t hashed() = 0;
};

using MakeSubject = std::function<std::unique_ptr<Subject>()>;

const char* describe(Kept kept)
{
    switch (kept) {
    case Kept::Both: return "saved and hashed";
    case Kept::Neither: return "neither saved nor hashed";
    case Kept::SavedOnly: return "saved but not hashed";
    case Kept::HashedOnly: return "hashed but not saved";
    }
    return "?";
}

std::vector<std::string> coverageProblems(const MakeSubject& make, const std::vector<Expected>& table)
{
    std::vector<std::string> problems;
    const std::unique_ptr<Subject> baseline = make();
    const std::vector<std::uint8_t> savedBefore = baseline->saved();
    const std::uint64_t hashBefore = baseline->hashed();
    for (auto [id, storage] : baseline->registry().storage()) {
        if (std::ranges::find(table, id, &Expected::id) == table.end()) {
            problems.push_back(std::format("{} is not classified: add it to the table", storage.info().name()));
        }
    }
    const auto probe = [&](const Expected& expected, const char* how, const std::function<void(Subject&, entt::sparse_set&)>& alter) {
        const std::unique_ptr<Subject> subject = make();
        entt::sparse_set* storage = subject->registry().storage(expected.id);
        if (storage == nullptr || storage->empty()) {
            problems.push_back(std::format("the fixture holds no {}, so it cannot be probed", expected.name));
            return false;
        }
        alter(*subject, *storage);
        const bool saved = subject->saved() != savedBefore;
        const bool hashed = subject->hashed() != hashBefore;
        const Kept kept = saved ? (hashed ? Kept::Both : Kept::SavedOnly) : (hashed ? Kept::HashedOnly : Kept::Neither);
        if (kept != expected.kept) {
            problems.push_back(std::format("{} {} is {}, expected {}", expected.name, how, describe(kept), describe(expected.kept)));
        }
        return true;
    };
    for (const Expected& expected : table) {
        if (!probe(expected, "changed", [&](Subject& subject, entt::sparse_set&) { expected.change(subject.registry()); })) {
            continue;
        }
        if (!expected.onEveryEntity) {
            probe(expected, "removed", [](Subject&, entt::sparse_set& storage) { storage.clear(); });
        }
    }
    return problems;
}

class SceneSubject final : public Subject {
public:
    SceneSubject()
        : scene_(22)
    {
        scene_.registerComponent<Mark>("test.mark");

        SpawnCommand hero;
        // Binding every field makes a new SpawnCommand member a compile error here, so the fixture
        // learns to spawn it before the probe can miss it.
        [[maybe_unused]] auto& [id, name, transform, mesh, spin, player, staticBox, character, animator] = hero;
        id = StableId::of("hero");
        name = Name{"hero"};
        transform.position = {0.0, 1.0, 0.0};
        mesh = MeshRef{2};
        spin = Spin{};
        player = Player{};
        character = CharacterBody{};
        animator = Animator{};
        MontagePlayer montage;
        montage.recoveryEnd = 1.0f;
        montage.phase = 1;
        scene_.restore(hero, montage);
        scene_.registry().emplace<Mark>(scene_.find(hero.id), 5u);

        SpawnCommand wall;
        wall.id = StableId::of("wall");
        wall.name = Name{"wall"};
        wall.staticBox = StaticBox{{4.0f, 0.5f, 4.0f}};
        scene_.restore(wall);
    }

    entt::registry& registry() override { return scene_.registry(); }

    std::vector<std::uint8_t> saved() override
    {
        const std::filesystem::path path = test::tempPath("save_hash_coverage.isav");
        REQUIRE(saveScene(path, scene_));
        std::vector<std::uint8_t> bytes;
        REQUIRE(readFileBytes(path, bytes));
        std::filesystem::remove(path);
        return bytes;
    }

    std::uint64_t hashed() override { return scene_.stateHash(); }

private:
    Scene scene_;
};

// A stand-in scene whose save and hash each see a test-only component or not, to prove the probe
// catches the mismatches it exists for.
struct Probe {
    std::uint32_t value = 0;
};
struct Stray {
    std::uint32_t value = 0;
};

struct Wiring {
    bool saves = true;
    bool hashes = true;
    bool hashesValue = true; // false: the hash sees that the component is there, not what it holds
    bool stray = false;
};

class FixtureSubject final : public Subject {
public:
    explicit FixtureSubject(Wiring wiring)
        : wiring_(wiring)
    {
        const entt::entity entity = registry_.create();
        registry_.emplace<Probe>(entity, 3u);
        if (wiring.stray) registry_.emplace<Stray>(entity, 4u);
    }

    entt::registry& registry() override { return registry_; }

    std::vector<std::uint8_t> saved() override
    {
        if (!wiring_.saves) return {};
        std::vector<std::uint8_t> bytes;
        for (auto [entity, probe] : registry_.view<Probe>().each()) bytes.push_back(static_cast<std::uint8_t>(probe.value));
        return bytes;
    }

    std::uint64_t hashed() override
    {
        if (!wiring_.hashes) return 0;
        std::uint64_t h = registry_.storage<Probe>().size();
        if (wiring_.hashesValue) {
            for (auto [entity, probe] : registry_.view<Probe>().each()) h = hashCombine(h, probe.value);
        }
        return h;
    }

private:
    entt::registry registry_;
    Wiring wiring_;
};

MakeSubject fixture(Wiring wiring)
{
    return [=] { return std::make_unique<FixtureSubject>(wiring); };
}

std::vector<Expected> probeKept(Kept kept)
{
    return {{idOf<Probe>(), "Probe", kept, false, [](entt::registry& r) {
                 for (auto [entity, probe] : r.view<Probe>().each()) probe.value += 1u;
             }}};
}

}

TEST_CASE("every component the scene saves takes part in its state hash, and the reverse", "[save][hash]")
{
    const std::vector<std::string> problems = coverageProblems([] { return std::make_unique<SceneSubject>(); }, sceneComponents());
    for (const std::string& problem : problems) UNSCOPED_INFO(problem);
    CHECK(problems.empty());
}

TEST_CASE("the coverage probe passes a component that is both saved and hashed", "[save][hash]")
{
    CHECK(coverageProblems(fixture({}), probeKept(Kept::Both)).empty());
}

TEST_CASE("the coverage probe goes red for a component saved but not hashed", "[save][hash]")
{
    const std::vector<std::string> problems = coverageProblems(fixture({.hashes = false}), probeKept(Kept::Both));
    REQUIRE(problems.size() == 2);
    CHECK(problems[0] == "Probe changed is saved but not hashed, expected saved and hashed");
    CHECK(problems[1] == "Probe removed is saved but not hashed, expected saved and hashed");
}

TEST_CASE("the coverage probe goes red for a component hashed but not saved", "[save][hash]")
{
    const std::vector<std::string> problems = coverageProblems(fixture({.saves = false}), probeKept(Kept::Both));
    REQUIRE(problems.size() == 2);
    CHECK(problems[0] == "Probe changed is hashed but not saved, expected saved and hashed");
    CHECK(problems[1] == "Probe removed is hashed but not saved, expected saved and hashed");
}

TEST_CASE("the coverage probe goes red when the hash sees a component but not its data", "[save][hash]")
{
    const std::vector<std::string> problems = coverageProblems(fixture({.hashesValue = false}), probeKept(Kept::Both));
    REQUIRE(problems.size() == 1);
    CHECK(problems[0] == "Probe changed is saved but not hashed, expected saved and hashed");
}

TEST_CASE("an allowlisted difference passes only as the difference it was listed for", "[save][hash]")
{
    CHECK(coverageProblems(fixture({.hashes = false}), probeKept(Kept::SavedOnly)).empty());
    CHECK(coverageProblems(fixture({.saves = false}), probeKept(Kept::HashedOnly)).empty());
    CHECK_FALSE(coverageProblems(fixture({}), probeKept(Kept::SavedOnly)).empty());
}

TEST_CASE("a component nobody classified fails by default, and so does one the fixture lacks", "[save][hash]")
{
    const std::vector<std::string> stray = coverageProblems(fixture({.stray = true}), probeKept(Kept::Both));
    REQUIRE(stray.size() == 1);
    CHECK(stray[0].ends_with("is not classified: add it to the table"));

    std::vector<Expected> table = probeKept(Kept::Both);
    table.push_back({idOf<Stray>(), "Stray", Kept::Neither, false, [](entt::registry&) {}});
    const std::vector<std::string> missing = coverageProblems(fixture({}), table);
    REQUIRE(missing.size() == 1);
    CHECK(missing[0] == "the fixture holds no Stray, so it cannot be probed");
}

}
