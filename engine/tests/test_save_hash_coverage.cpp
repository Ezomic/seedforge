// A component a snapshot saves but the state hash skips lets two runs that disagree report the same
// hash, so a replay desyncs silently; one hashed but not saved makes every loaded game disagree with
// the run that saved it. This probes every component storage of a scene that holds them all: each
// is removed (or changed, where the scene needs it on every entity) in a fresh copy, and the save
// bytes and the hash must both move or both stay put.

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
    void (*change)(entt::registry&); // for a component every entity must hold; null removes it instead
};

template <class T>
constexpr entt::id_type idOf()
{
    return entt::type_hash<T>::value();
}

// Every component the scene keeps. A storage missing from this table fails the test, so a new
// component has to be classified here before it can pass. SavedOnly and HashedOnly entries are the
// allowlist of intended differences, and each needs a comment saying why.
const std::vector<Expected>& sceneComponents()
{
    static const std::vector<Expected> table{
        {idOf<StableId>(), "StableId", Kept::Both, [](entt::registry& r) {
             for (auto [entity, id] : r.view<StableId>().each()) id.value ^= 1u;
         }},
        {idOf<Name>(), "Name", Kept::Both, [](entt::registry& r) {
             for (auto [entity, name] : r.view<Name>().each()) name.value += "!";
         }},
        {idOf<Transform>(), "Transform", Kept::Both, [](entt::registry& r) {
             for (auto [entity, transform] : r.view<Transform>().each()) transform.position.x += 1.0;
         }},
        {idOf<MeshRef>(), "MeshRef", Kept::Both, nullptr},
        {idOf<Spin>(), "Spin", Kept::Both, nullptr},
        {idOf<Player>(), "Player", Kept::Both, nullptr},
        {idOf<StaticBox>(), "StaticBox", Kept::Both, nullptr},
        {idOf<CharacterBody>(), "CharacterBody", Kept::Both, nullptr},
        {idOf<Animator>(), "Animator", Kept::Both, nullptr},
        {idOf<MontagePlayer>(), "MontagePlayer", Kept::Both, nullptr},
        // Stands for every registered game component: they all share the hooks registerComponent builds.
        {idOf<Mark>(), "registered game component", Kept::Both, nullptr},
        // Rendering interpolation only, overwritten at the start of every tick.
        {idOf<PreviousTransform>(), "PreviousTransform", Kept::Neither, nullptr},
        // Skinning matrices rebuilt from the Animator every tick.
        {idOf<SkinPose>(), "SkinPose", Kept::Neither, nullptr},
    };
    return table;
}

// What the probe needs from a scene: its registry, the bytes a save writes and the hash it reports.
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
    for (const Expected& expected : table) {
        const std::unique_ptr<Subject> subject = make();
        entt::sparse_set* storage = subject->registry().storage(expected.id);
        if (storage == nullptr || storage->empty()) {
            problems.push_back(std::format("the fixture holds no {}, so it cannot be probed", expected.name));
            continue;
        }
        if (expected.change != nullptr) {
            expected.change(subject->registry());
        } else {
            storage->clear();
        }
        const bool saved = subject->saved() != savedBefore;
        const bool hashed = subject->hashed() != hashBefore;
        const Kept kept = saved ? (hashed ? Kept::Both : Kept::SavedOnly) : (hashed ? Kept::HashedOnly : Kept::Neither);
        if (kept != expected.kept) {
            problems.push_back(std::format("{} is {}, expected {}", expected.name, describe(kept), describe(expected.kept)));
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
        wall.name = {"wall"};
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

class FixtureSubject final : public Subject {
public:
    FixtureSubject(bool saves, bool hashes, bool stray)
        : saves_(saves)
        , hashes_(hashes)
    {
        const entt::entity entity = registry_.create();
        registry_.emplace<Probe>(entity, 3u);
        if (stray) registry_.emplace<Stray>(entity, 4u);
    }

    entt::registry& registry() override { return registry_; }
    std::vector<std::uint8_t> saved() override { return {static_cast<std::uint8_t>(saves_ ? registry_.storage<Probe>().size() : 0u)}; }
    std::uint64_t hashed() override { return hashes_ ? registry_.storage<Probe>().size() : 0u; }

private:
    entt::registry registry_;
    bool saves_;
    bool hashes_;
};

MakeSubject fixture(bool saves, bool hashes, bool stray = false)
{
    return [=] { return std::make_unique<FixtureSubject>(saves, hashes, stray); };
}

std::vector<Expected> probeKept(Kept kept)
{
    return {{idOf<Probe>(), "Probe", kept, nullptr}};
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
    CHECK(coverageProblems(fixture(true, true), probeKept(Kept::Both)).empty());
}

TEST_CASE("the coverage probe goes red for a component saved but not hashed", "[save][hash]")
{
    const std::vector<std::string> problems = coverageProblems(fixture(true, false), probeKept(Kept::Both));
    REQUIRE(problems.size() == 1);
    CHECK(problems[0] == "Probe is saved but not hashed, expected saved and hashed");
}

TEST_CASE("the coverage probe goes red for a component hashed but not saved", "[save][hash]")
{
    const std::vector<std::string> problems = coverageProblems(fixture(false, true), probeKept(Kept::Both));
    REQUIRE(problems.size() == 1);
    CHECK(problems[0] == "Probe is hashed but not saved, expected saved and hashed");
}

TEST_CASE("an allowlisted difference passes only as the difference it was listed for", "[save][hash]")
{
    CHECK(coverageProblems(fixture(true, false), probeKept(Kept::SavedOnly)).empty());
    CHECK(coverageProblems(fixture(false, true), probeKept(Kept::HashedOnly)).empty());
    CHECK_FALSE(coverageProblems(fixture(true, true), probeKept(Kept::SavedOnly)).empty());
}

TEST_CASE("a component nobody classified fails by default, and so does one the fixture lacks", "[save][hash]")
{
    const std::vector<std::string> stray = coverageProblems(fixture(true, true, true), probeKept(Kept::Both));
    REQUIRE(stray.size() == 1);
    CHECK(stray[0].ends_with("is not classified: add it to the table"));

    std::vector<Expected> table = probeKept(Kept::Both);
    table.push_back({idOf<Stray>(), "Stray", Kept::Neither, nullptr});
    const std::vector<std::string> missing = coverageProblems(fixture(true, true), table);
    REQUIRE(missing.size() == 1);
    CHECK(missing[0] == "the fixture holds no Stray, so it cannot be probed");
}

}
