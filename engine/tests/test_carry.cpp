#include "ichor/core/input.h"
#include "ichor/data/data_set.h"
#include "ichor/save/save_slots.h"
#include "ichor/sim/scene.h"
#include "ichor/sim/scene_io.h"
#include "ichor/world/carry.h"
#include "ichor/world/forged_heightfield.h"
#include "ichor/world/placed_loot.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>

using namespace ichor;

namespace {

constexpr double kDt = 1.0 / 60.0;

struct Shared {
    DataSet data;
    std::shared_ptr<const PlacedLootSet> loot;
    std::vector<std::string> problems;
};

// A weighty item with no other loot within reach, so standing on it picks up exactly that one.
const PlacedLoot* isolatedTarget(const PlacedLootSet& set, const CarryCatalogue& c)
{
    for (const PlacedLoot& item : set.all()) {
        const ItemDef* def = c.items().find(item.itemId);
        if (def == nullptr || def->weightUnits < 100) continue;
        const bool alone = std::ranges::none_of(set.all(), [&](const PlacedLoot& other) {
            return &other != &item && std::hypot(other.x - item.x, other.z - item.z) <= 2.0 * kCarryReachM;
        });
        if (alone) return &item;
    }
    return nullptr;
}

const Shared& shared()
{
    static const Shared s = [] {
        Shared out;
        REQUIRE(out.data.loadDirectory(ICHOR_GAME_DATA_DIR));
        const CarryCatalogue catalogue = CarryCatalogue::fromData(out.data, out.problems);
        std::vector<LootItemInfo> infos;
        worldforge::ForgeConfig config;
        config.dungeonArchetypes = {"crypt", "cave", "mine"};
        for (const char* tableName : {"notable_items", "catalogue"}) {
            for (const auto& [id, record] : out.data.table(tableName)->records) {
                const auto text = [&](const char* key) {
                    const auto it = record.fields.find(key);
                    return it != record.fields.end() && std::holds_alternative<std::string>(it->second) ? std::get<std::string>(it->second) : std::string{};
                };
                infos.push_back({id, text("tier"), text("class")});
                config.itemIds.push_back(id);
            }
        }
        for (std::uint64_t seed = 1; seed < 40; ++seed) {
            WorldDesc desc;
            desc.seed = seed;
            desc.sizeMetres = 2048;
            desc.metresPerSample = 8;
            desc.cellMetres = 512;
            desc = forgedWorldDesc(desc);
            worldforge::ForgedWorld world = worldforge::forgeWorld(seed, 128, config);
            ForgedHeightfield adapted = heightfieldFromForgedWorld(world, desc);
            if (!adapted.ok()) continue;
            PlacedPois pois = placeForgedPois(world, adapted.field);
            if (!pois.ok()) continue;
            PlacedLootResult placed = placeForgedLoot(world, pois.set, infos, adapted.field);
            if (placed.ok() && !placed.set.empty() && isolatedTarget(placed.set, catalogue)) {
                out.loot = std::make_shared<const PlacedLootSet>(std::move(placed.set));
                return out;
            }
        }
        FAIL("no seed under 40 forged a world with loot");
        return out;
    }();
    return s;
}

CarryCatalogue catalogue()
{
    std::vector<std::string> problems;
    return CarryCatalogue::fromData(shared().data, problems);
}

const PlacedLoot& target()
{
    static const PlacedLoot* const found = isolatedTarget(*shared().loot, catalogue());
    REQUIRE(found != nullptr);
    return *found;
}

std::uint64_t unitsOf(const char* id)
{
    const CarryCatalogue c = catalogue();
    const ItemDef* def = c.items().find(id);
    REQUIRE(def != nullptr);
    return def->weightUnits;
}

struct Rig {
    Scene scene{7};
    CarrySystem carry;

    explicit Rig(std::uint64_t capacity = kStartingCarryUnits)
        : carry(shared().loot, catalogue(), capacity)
    {
        carry.install(scene);
        SpawnCommand player;
        player.id = StableId::of("player");
        player.name.value = "player";
        player.transform.position = {target().x, static_cast<double>(target().groundY), target().z};
        player.player = Player{};
        scene.enqueue(player);
        tick();
    }

    void tick(std::uint32_t buttons = 0, float moveY = 0.0f)
    {
        InputFrame frame;
        frame.buttons = buttons;
        frame.moveY = moveY;
        scene.tick(kDt, frame);
    }

    // A press takes one tick to be seen and the next to be applied.
    void press(std::uint32_t button = InputFrame::Interact)
    {
        tick(button);
        tick();
        tick();
    }

    void settle()
    {
        tick();
        tick();
    }

    const Carried& held() const
    {
        const Carried* c = carry.carried(scene);
        REQUIRE(c != nullptr);
        return *c;
    }

    std::uint32_t count(const std::string& id) const
    {
        for (const CarryStack& stack : held().stacks) {
            if (stack.itemId == id) return stack.count;
        }
        return 0;
    }

    float speed() const { return static_cast<float>(carry.speedFactor(scene)); }

    float walkSpeed() const
    {
        for (auto [entity, player] : scene.registry().view<const Player>().each()) return player.metresPerSecond;
        return 0.0f;
    }
};

std::filesystem::path scratch(const char* name)
{
    const std::filesystem::path dir = std::filesystem::path(ICHOR_TEST_OUTPUT) / "carry" / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

}

TEST_CASE("every item of the data can be carried and relics and keys are carried one at a time", "[carry]")
{
    REQUIRE(shared().problems.empty());
    const CarryCatalogue c = catalogue();
    REQUIRE(c.size() >= 250);
    const CarryItem* relic = c.info("seers_censer_shard");
    REQUIRE(relic != nullptr);
    REQUIRE(relic->itemClass == "relic");
    REQUIRE(c.items().find("seers_censer_shard")->stackLimit == 1);
    REQUIRE(c.items().find("hunters_hush_tallow")->stackLimit == 5);
}

TEST_CASE("a record with no usable weight or class is left out and named, never fatal", "[carry]")
{
    DataTable table;
    const auto put = [&](const char* id, std::optional<const char*> itemClass, std::optional<double> weight) {
        DataRecord& record = table.records[id];
        record.id = id;
        if (itemClass) record.fields["class"] = std::string(*itemClass);
        if (weight) record.fields["weight"] = *weight;
    };
    put("good", "gear", 1.5);
    put("no_weight", "gear", std::nullopt);
    put("no_class", std::nullopt, 1.0);
    put("odd_class", "spirit", 1.0);
    put("nan_weight", "gear", std::numeric_limits<double>::quiet_NaN());
    put("negative", "gear", -1.0);
    put("huge", "gear", 1.0e9);
    put("a_key", "key", 0.1);

    std::vector<std::string> problems;
    const CarryCatalogue c = CarryCatalogue::fromTables({&table}, problems);
    REQUIRE(c.size() == 2);
    REQUIRE(c.items().find("good") != nullptr);
    REQUIRE(c.items().find("a_key")->stackLimit == 1);
    REQUIRE(problems.size() == 6);
    for (const char* id : {"no_weight", "no_class", "odd_class", "nan_weight", "negative", "huge"}) {
        INFO(id);
        REQUIRE(std::ranges::any_of(problems, [&](const std::string& p) { return p.find(id) != std::string::npos; }));
    }

    std::vector<std::string> none;
    REQUIRE(CarryCatalogue::fromTables({}, none).size() == 0);
}

TEST_CASE("standing on loot and pressing F picks it up and the world shows it gone", "[carry]")
{
    Rig rig;
    const PlacedLoot& item = target();
    REQUIRE(rig.carry.prompt(rig.scene).find("F: pick up") == 0);
    REQUIRE(rig.held().stacks.empty());

    rig.press();
    REQUIRE(rig.count(item.itemId) == 1);
    REQUIRE(CarrySystem::isTaken(&rig.held(), item.itemId));
    REQUIRE(rig.carry.notice(rig.scene).find("Picked up") == 0);

    rig.press();
    REQUIRE(rig.count(item.itemId) == 1);
    REQUIRE(rig.held().taken.size() == 1);
}

TEST_CASE("Enter picks up too, and a press with nothing in reach does nothing at all", "[carry]")
{
    Rig rig;
    rig.press(InputFrame::Confirm);
    REQUIRE(rig.held().taken.size() == 1);

    Rig far;
    far.scene.enqueue(MoveCommand{StableId::of("player"), {400.0, 0.0, 400.0}});
    far.settle();
    const std::size_t before = far.scene.log().entries.size();
    far.tick(InputFrame::Interact);
    far.tick();
    REQUIRE(far.scene.log().entries.size() == before);
    REQUIRE(far.held().taken.empty());
    REQUIRE(far.carry.prompt(far.scene).empty());
}

TEST_CASE("a held key picks up once, not every tick", "[carry]")
{
    Rig rig;
    const std::size_t before = rig.scene.log().entries.size();
    for (int i = 0; i < 30; ++i) rig.tick(InputFrame::Interact);
    rig.tick();
    std::size_t pickups = 0;
    for (std::size_t i = before; i < rig.scene.log().entries.size(); ++i) {
        if (const auto* command = std::get_if<GameCommand>(&rig.scene.log().entries[i].command); command != nullptr && command->kind == "carry.pickup") ++pickups;
    }
    REQUIRE(pickups == 1);
}

TEST_CASE("encumbrance: the level follows the load at the thresholds, and the walk slows with it", "[carry]")
{
    const std::uint64_t tallow = unitsOf("hunters_hush_tallow"); // 0.5
    const std::uint64_t loaf = unitsOf("grave_bread_loaf");      // 0.8
    REQUIRE(tallow == 500);
    REQUIRE(loaf == 800);
    Rig rig(1000);
    const float walk = rig.speed();
    REQUIRE(rig.carry.view(rig.scene).level == EncumbranceLevel::Light);

    rig.carry.requestGrant(rig.scene, "ichor_cruet", 1); // 0.4: still light
    rig.settle();
    REQUIRE(rig.carry.view(rig.scene).level == EncumbranceLevel::Light);
    REQUIRE(rig.speed() == walk);

    rig.carry.requestGrant(rig.scene, "hunters_hush_tallow", 1); // 0.9 in all
    rig.settle();
    REQUIRE(rig.carry.view(rig.scene).totalUnits == 900);
    REQUIRE(rig.carry.view(rig.scene).level == EncumbranceLevel::Heavy);
    REQUIRE(rig.speed() < walk);

    Rig loaded(1000);
    loaded.carry.requestGrant(loaded.scene, "hunters_hush_tallow", 1);
    loaded.settle();
    REQUIRE(loaded.carry.view(loaded.scene).level == EncumbranceLevel::Loaded);
    REQUIRE(loaded.speed() < walk);
    REQUIRE(loaded.speed() > rig.speed());

    Rig exact(1000);
    exact.carry.requestGrant(exact.scene, "ichor_cruet", 1);
    exact.carry.requestGrant(exact.scene, "grave_bread_loaf", 1); // 1.2: past capacity, a grant cannot be refused
    exact.settle();
    REQUIRE(exact.carry.view(exact.scene).level == EncumbranceLevel::Over);
    REQUIRE(exact.speed() < rig.speed());
}

TEST_CASE("a pickup that would pass capacity is refused and the item stays where it lies", "[carry]")
{
    const PlacedLoot& item = target();
    const std::uint64_t weight = unitsOf(item.itemId.c_str());
    REQUIRE(weight > 0);

    Rig tight(weight - 1);
    tight.press();
    REQUIRE(tight.held().stacks.empty());
    REQUIRE_FALSE(CarrySystem::isTaken(&tight.held(), item.itemId));
    REQUIRE(tight.carry.notice(tight.scene).find("too heavy") != std::string::npos);

    Rig exact(weight);
    exact.press();
    REQUIRE(exact.count(item.itemId) == 1);
    REQUIRE(exact.carry.view(exact.scene).level == EncumbranceLevel::Heavy);
}

TEST_CASE("using a consumable spends one, a gear item cannot be used and nothing held changes nothing", "[carry]")
{
    Rig rig;
    rig.carry.requestGrant(rig.scene, "hunters_hush_tallow", 2);
    rig.carry.requestGrant(rig.scene, "eleventh_spear", 1);
    rig.settle();
    REQUIRE(rig.count("hunters_hush_tallow") == 2);

    rig.carry.requestUse(rig.scene, "hunters_hush_tallow");
    rig.settle();
    REQUIRE(rig.count("hunters_hush_tallow") == 1);
    REQUIRE(rig.carry.notice(rig.scene).find("Used Huntress's Hush Tallow") == 0);

    rig.carry.requestUse(rig.scene, "eleventh_spear");
    rig.settle();
    REQUIRE(rig.count("eleventh_spear") == 1);
    REQUIRE(rig.carry.notice(rig.scene).find("cannot be used") != std::string::npos);

    const std::vector<CarryStack> stacks = rig.held().stacks;
    rig.carry.requestUse(rig.scene, "styx_oath_water");
    rig.carry.requestUse(rig.scene, "no_such_item");
    rig.settle();
    REQUIRE(rig.held().stacks.size() == stacks.size());
    for (std::size_t i = 0; i < stacks.size(); ++i) {
        REQUIRE(rig.held().stacks[i].itemId == stacks[i].itemId);
        REQUIRE(rig.held().stacks[i].count == stacks[i].count);
    }

    rig.carry.requestUse(rig.scene, "hunters_hush_tallow");
    rig.settle();
    REQUIRE(rig.count("hunters_hush_tallow") == 0);
}

TEST_CASE("a dropped item lies where it was dropped and can be picked up again, lighter load meanwhile", "[carry]")
{
    Rig rig;
    rig.carry.requestGrant(rig.scene, "hunters_hush_tallow", 2);
    rig.settle();
    const std::uint64_t loaded = rig.carry.view(rig.scene).totalUnits;

    rig.scene.enqueue(MoveCommand{StableId::of("player"), {300.0, 0.0, 300.0}});
    rig.carry.requestDrop(rig.scene, "hunters_hush_tallow");
    rig.settle();
    REQUIRE(rig.count("hunters_hush_tallow") == 1);
    REQUIRE(rig.carry.view(rig.scene).totalUnits == loaded - 500);
    REQUIRE(rig.held().dropped.size() == 1);
    const CarryDrop drop = rig.held().dropped.front();
    REQUIRE(drop.x == target().x + 300.0);
    REQUIRE(drop.z == target().z + 300.0);

    rig.carry.requestDrop(rig.scene, "hunters_hush_tallow", 5); // more than held
    rig.settle();
    REQUIRE(rig.held().dropped.size() == 1);
    REQUIRE(rig.count("hunters_hush_tallow") == 1);

    rig.scene.enqueue(MoveCommand{StableId::of("player"), {-600.0, 0.0, -600.0}});
    rig.settle();
    REQUIRE(rig.carry.prompt(rig.scene).find("F: pick up") != 0);
    rig.scene.enqueue(MoveCommand{StableId::of("player"), {600.0, 0.0, 600.0}});
    for (int i = 0; i < 300; ++i) rig.tick(); // the notice of the drop runs out
    REQUIRE(rig.carry.prompt(rig.scene).find("F: pick up Huntress's Hush Tallow") == 0);
    rig.press();
    REQUIRE(rig.count("hunters_hush_tallow") == 2);
    REQUIRE(rig.held().dropped.empty());
}

TEST_CASE("dropping a picked-up item puts it back in the world to be taken again", "[carry]")
{
    Rig rig;
    rig.press();
    const std::string id = target().itemId;
    REQUIRE(rig.count(id) == 1);
    rig.carry.requestDrop(rig.scene, id);
    rig.settle();
    REQUIRE(rig.count(id) == 0);
    REQUIRE(rig.held().dropped.size() == 1);
    REQUIRE(CarrySystem::isTaken(&rig.held(), id));
    rig.press();
    REQUIRE(rig.count(id) == 1);
    REQUIRE(rig.held().dropped.empty());
}

TEST_CASE("the carried state is hashed with the scene", "[carry]")
{
    Rig a;
    Rig b;
    REQUIRE(a.scene.stateHash() == b.scene.stateHash());
    a.press();
    b.tick();
    b.tick();
    b.tick();
    REQUIRE(a.scene.stateHash() != b.scene.stateHash());
}

TEST_CASE("the same inputs replayed into a fresh scene give the same state hash", "[carry][replay]")
{
    const auto run = [](Rig& rig) {
        rig.tick();
        rig.press();
        for (int i = 0; i < 30; ++i) rig.tick(0, 1.0f);
        rig.press();
        for (int i = 0; i < 10; ++i) rig.tick(0, -1.0f);
        rig.press(InputFrame::Confirm);
    };
    Rig a;
    Rig b;
    run(a);
    run(b);
    REQUIRE(a.scene.stateHash() == b.scene.stateHash());
    REQUIRE(a.scene.log().entries.size() == b.scene.log().entries.size());
}

TEST_CASE("the command log replayed into a fresh scene reproduces a pickup, a use, a drop and a grant", "[carry][replay]")
{
    Rig a;
    a.press();
    a.carry.requestGrant(a.scene, "hunters_hush_tallow", 3);
    a.settle();
    a.carry.requestUse(a.scene, "hunters_hush_tallow");
    a.carry.requestDrop(a.scene, "hunters_hush_tallow", 1);
    a.settle();
    a.tick();
    REQUIRE(a.count("hunters_hush_tallow") == 1);

    // No inputs here: the pickup in the log is what picks it up.
    Scene replay(7);
    CarrySystem carry(shared().loot, catalogue());
    carry.install(replay);
    for (std::uint64_t tick = 0; tick < a.scene.tickCount(); ++tick) {
        for (const CommandLog::Entry& entry : a.scene.log().entries) {
            if (entry.tick == tick) replay.enqueue(entry.command);
        }
        replay.tick(kDt, {});
    }
    REQUIRE(replay.stateHash() == a.scene.stateHash());
    REQUIRE(carry.carried(replay) != nullptr);
    REQUIRE(carry.carried(replay)->taken == a.held().taken);
}

TEST_CASE("a campaign slot saves and loads what is carried, and the world still shows it taken", "[carry][save]")
{
    Rig a;
    a.press();
    a.carry.requestGrant(a.scene, "hunters_hush_tallow", 2);
    a.carry.requestGrant(a.scene, "styx_oath_water", 1);
    a.settle();
    a.carry.requestDrop(a.scene, "styx_oath_water");
    a.settle();
    const std::uint64_t hash = a.scene.stateHash();

    SaveSlots slots(scratch("slot"), SavePolicy{});
    const SaveContext context{7, 1, 42};
    REQUIRE(slots.save("quicksave", a.scene, context));

    Rig b;
    b.scene.tick(kDt, {}); // somewhere else in time
    const LoadResult loaded = slots.load("quicksave", b.scene, context);
    REQUIRE(loaded.status == LoadStatus::Loaded);
    REQUIRE(b.scene.stateHash() == hash);
    REQUIRE(b.held().taken == a.held().taken);
    REQUIRE(b.count("hunters_hush_tallow") == 2);
    REQUIRE(b.held().dropped.size() == 1);
    REQUIRE(b.carry.view(b.scene).totalUnits == a.carry.view(a.scene).totalUnits);
    REQUIRE(CarrySystem::isTaken(b.carry.carried(b.scene), target().itemId));

    // The loaded game carries on exactly as the saved one would have.
    a.press();
    b.press();
    a.tick();
    b.tick();
    REQUIRE(a.scene.stateHash() == b.scene.stateHash());
}

TEST_CASE("a save made before the carried component existed still loads, and the player gains it on the next tick", "[carry][save]")
{
    Scene old(7);
    SpawnCommand player;
    player.id = StableId::of("player");
    player.name.value = "player";
    player.player = Player{};
    old.enqueue(player);
    old.tick(kDt, {});
    const std::filesystem::path path = scratch("old") / "old.isav";
    REQUIRE(saveScene(path, old));

    Rig now;
    REQUIRE(loadScene(path, now.scene));
    REQUIRE(now.carry.carried(now.scene) == nullptr);
    now.tick();
    REQUIRE(now.carry.carried(now.scene) != nullptr);
    REQUIRE(now.held().stacks.empty());
}

TEST_CASE("a snapshot that holds a component this scene lacks is still refused", "[carry][save]")
{
    Rig with;
    with.press();
    const std::filesystem::path path = scratch("strict") / "with.isav";
    REQUIRE(saveScene(path, with.scene));

    Scene without(7);
    REQUIRE_FALSE(loadScene(path, without));
}

TEST_CASE("hostile carried state is shrugged off: unknown ids, huge counts, duplicates and non-finite drops", "[carry]")
{
    Rig rig;
    rig.tick();
    Carried bad;
    bad.stacks = {{"no_such_item", 5}, {"hunters_hush_tallow", 4000000000u}, {"hunters_hush_tallow", 0}, {"ichor_cruet", 2}, {"ichor_cruet", 2}, {"", 1}};
    bad.taken = {"", "no_such_item", target().itemId, target().itemId};
    bad.dropped = {{"ichor_cruet", 1, std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0f},
        {"ichor_cruet", 1, 0.0, std::numeric_limits<double>::infinity(), 0.0f},
        {"no_such_item", 1, target().x, target().z, 0.0f},
        {"ichor_cruet", 0, target().x, target().z, 0.0f},
        {"ichor_cruet", 1, target().x, target().z, std::numeric_limits<float>::quiet_NaN()}};
    for (auto [entity, player] : rig.scene.registry().view<Player>().each()) {
        rig.scene.registry().emplace_or_replace<Carried>(entity, bad);
    }
    const CarryView view = rig.carry.view(rig.scene);
    for (const CarryViewRow& row : view.rows) {
        REQUIRE(row.id != "no_such_item");
        REQUIRE(row.count > 0);
        REQUIRE(row.count <= 5);
    }
    REQUIRE(view.totalUnits < kStartingCarryUnits * 100);
    rig.carry.requestDrop(rig.scene, "ichor_cruet");
    rig.carry.requestUse(rig.scene, "ichor_cruet");
    rig.press();
    for (const CarryDrop& drop : rig.held().dropped) {
        REQUIRE(std::isfinite(drop.x));
        REQUIRE(std::isfinite(drop.z));
        REQUIRE(std::isfinite(drop.groundY));
        REQUIRE(drop.count > 0);
    }
}

TEST_CASE("a stack past its limit cannot be picked up and is told so", "[carry]")
{
    Rig rig;
    rig.carry.requestGrant(rig.scene, "hunters_hush_tallow", 5);
    rig.carry.requestGrant(rig.scene, "hunters_hush_tallow", 1); // a grant cannot pass the stack limit either
    rig.settle();
    REQUIRE(rig.count("hunters_hush_tallow") == 5);
    REQUIRE(rig.carry.notice(rig.scene).find("could not be added") != std::string::npos);
}

TEST_CASE("a full taken list refuses a pickup rather than letting the item be taken again", "[carry]")
{
    Rig rig;
    rig.tick();
    for (auto [entity, player] : rig.scene.registry().view<Player>().each()) {
        Carried padded;
        padded.taken.assign(kMaxTakenItems, "padding");
        rig.scene.registry().emplace_or_replace<Carried>(entity, padded);
    }
    rig.press();
    REQUIRE(rig.held().stacks.empty());
    REQUIRE(rig.held().taken.size() == kMaxTakenItems);
    REQUIRE(rig.carry.notice(rig.scene).find("cannot take") != std::string::npos);
}

TEST_CASE("a hostile number of stacks is cut to a bound", "[carry]")
{
    Rig rig;
    rig.tick();
    for (auto [entity, player] : rig.scene.registry().view<Player>().each()) {
        Carried many;
        many.stacks.assign(kMaxCarryStacks * 3, CarryStack{"ichor_cruet", 1});
        rig.scene.registry().emplace_or_replace<Carried>(entity, many);
    }
    rig.tick();
    REQUIRE(rig.held().stacks.size() <= kMaxCarryStacks);
}

TEST_CASE("the load scales whatever speed another system gave the player and never overwrites it", "[carry]")
{
    const auto walked = [](bool loaded) {
        Rig rig(1000);
        rig.scene.setSpeedModifier([&rig](const glm::dvec3&) { return rig.carry.speedFactor(rig.scene); });
        for (auto [entity, player] : rig.scene.registry().view<Player>().each()) player.metresPerSecond = 5.0f; // as the combat bridge does
        if (loaded) {
            rig.carry.requestGrant(rig.scene, "ichor_cruet", 1);
            rig.carry.requestGrant(rig.scene, "hunters_hush_tallow", 1);
        }
        rig.settle();
        REQUIRE(rig.walkSpeed() == 5.0f);
        const double from = rig.scene.registry().get<Transform>(rig.scene.find(StableId::of("player"))).position.z;
        for (int i = 0; i < 60; ++i) rig.tick(0, 1.0f);
        REQUIRE(rig.walkSpeed() == 5.0f);
        return from - rig.scene.registry().get<Transform>(rig.scene.find(StableId::of("player"))).position.z;
    };
    const double free = walked(false);
    const double heavy = walked(true);
    REQUIRE(free > 4.5);
    REQUIRE(heavy > 0.0);
    REQUIRE(heavy < free * 0.75);
    REQUIRE(heavy > free * 0.65);
}
