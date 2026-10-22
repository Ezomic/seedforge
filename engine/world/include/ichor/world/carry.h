#pragma once

#include "ichor/core/reflect.h"
#include "ichor/data/data_set.h"
#include "ichor/economy/inventory_model.h"
#include "ichor/sim/scene.h"
#include "ichor/world/placed_loot.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// What the player carries in the forged world (doc 06): pick up the loot that lies in the world, see it
// as a list with weights, feel the load, use a consumable, put something down. The rules are the economy
// module's (Inventory, encumbrance levels, useConsumable); this ties them to the scene.
//
// The state is one reflected component on the player, `Carried`, registered with the scene, so it is
// hashed with the scene and saved and loaded inside every slot of a campaign with nothing else to keep in
// step. A pickup, use, drop or grant is a GameCommand of kind carry.pickup, carry.use, carry.drop or
// carry.grant: logged, applied at the start of a tick, deterministic. A log replayed into a fresh scene
// with the same system installed reproduces the same state hash.
//
// "Loot lives in places": a placed item is never duplicated or rolled. Picking one up records its item id
// in `taken`, so the world shows it gone and a load brings it back gone; the placed set itself never changes.

constexpr std::uint64_t kStartingCarryUnits = 40 * kWeightUnitsPerWeight; // the progression table's carry at the start
constexpr double kCarryReachM = 2.5;
constexpr std::size_t kMaxDroppedItems = 1024;
constexpr std::size_t kMaxTakenItems = kMaxLootItems;
constexpr std::size_t kMaxCarryStacks = 4096;
constexpr double kCarryNoticeSeconds = 4.0;

constexpr const char* kCarriedComponent = "world.carried";

struct CarryStack {
    std::string itemId;
    std::uint32_t count = 0;
};

struct CarryDrop {
    std::string itemId;
    std::uint32_t count = 0;
    double x = 0.0; // world metres
    double z = 0.0;
    float groundY = 0.0f;
};

struct Carried {
    std::vector<CarryStack> stacks;
    std::vector<std::string> taken; // placed loot picked up, by item id
    std::vector<CarryDrop> dropped; // what the player put down, lying where it was dropped
};

template <>
struct Reflect<CarryStack> {
    static constexpr auto fields() { return std::make_tuple(field("itemId", &CarryStack::itemId), field("count", &CarryStack::count)); }
};
template <>
struct Reflect<CarryDrop> {
    static constexpr auto fields()
    {
        return std::make_tuple(field("itemId", &CarryDrop::itemId), field("count", &CarryDrop::count), field("x", &CarryDrop::x), field("z", &CarryDrop::z),
            field("groundY", &CarryDrop::groundY));
    }
};
template <>
struct Reflect<Carried> {
    static constexpr auto fields()
    {
        return std::make_tuple(field("stacks", &Carried::stacks), field("taken", &Carried::taken), field("dropped", &Carried::dropped));
    }
};

// What the player may know of an item: its name and what it does, beside the weights the economy module holds.
struct CarryItem {
    std::string id;
    std::string name;
    std::string itemClass; // as the data says: gear, consumable, information, material, relic, key
    std::string benefit;
};

// The notable_items and catalogue tables as the inventory sees them. A relic or a key is carried one at a time,
// as information is. A record with no usable class or weight is left out and named in `problems`, never fatal.
class CarryCatalogue {
public:
    static CarryCatalogue fromData(const DataSet& data, std::vector<std::string>& problems);
    // The first table to hold an id wins. A missing table is the caller's to report.
    static CarryCatalogue fromTables(const std::vector<const DataTable*>& tables, std::vector<std::string>& problems);

    const ItemCatalogue& items() const { return items_; }
    const CarryItem* info(std::string_view id) const;
    std::size_t size() const { return info_.size(); }

private:
    explicit CarryCatalogue(ItemCatalogue items)
        : items_(std::move(items))
    {
    }

    ItemCatalogue items_;
    std::map<std::string, CarryItem, std::less<>> info_;
};

// "1.2": units in tenths of a catalogue weight unit, rounded half up.
std::string formatWeightUnits(std::uint64_t units);

struct CarryViewRow {
    std::string id;
    std::string name;
    std::string itemClass;
    std::string benefit;
    std::uint32_t count = 0;
    std::uint64_t unitWeightUnits = 0;
    bool usable = false;
};

struct CarryView {
    std::vector<CarryViewRow> rows; // by item id
    std::uint64_t totalUnits = 0;
    std::uint64_t capacityUnits = 0;
    EncumbranceLevel level = EncumbranceLevel::Light;
};

class CarrySystem {
public:
    // `loot` may be null: then nothing lies in the world and only drops can be picked up.
    CarrySystem(std::shared_ptr<const PlacedLootSet> loot, CarryCatalogue catalogue, std::uint64_t capacityUnits = kStartingCarryUnits);
    CarrySystem(const CarrySystem&) = delete;
    CarrySystem& operator=(const CarrySystem&) = delete;

    // Registers the component, the four command kinds and the "carry" system. Call before any snapshot is loaded.
    // The scene must not outlive this.
    void install(Scene& scene);

    const CarryCatalogue& catalogue() const { return catalogue_; }
    std::uint64_t capacityUnits() const { return capacity_; }

    // The player's carried state; null before the player has one.
    const Carried* carried(const Scene& scene) const;
    CarryView view(const Scene& scene) const;

    // The line to show: what happened lately, else what E would pick up, else empty.
    std::string prompt(const Scene& scene) const;
    // What happened lately (picked up, too heavy, used), for as long as it should show; empty after.
    std::string notice(const Scene& scene) const;

    // The UI's requests: each queues a command for the next tick. A drop of count 0 drops one.
    void requestUse(Scene& scene, const std::string& itemId) const;
    void requestDrop(Scene& scene, const std::string& itemId, std::uint32_t count = 1) const;
    // A reward the player cannot refuse: it may take the load past capacity.
    void requestGrant(Scene& scene, const std::string& itemId, std::uint32_t count) const;

    static bool isTaken(const Carried* carried, std::string_view itemId);

private:
    struct Target {
        bool found = false;
        bool dropped = false;
        std::size_t index = 0;
        std::string itemId;
        std::uint32_t count = 1;
    };

    void step(Scene& scene, const InputFrame& input);
    void pickup(Scene& scene, const GameCommand& command);
    void use(Scene& scene, const GameCommand& command);
    void drop(Scene& scene, const GameCommand& command);
    void grant(Scene& scene, const GameCommand& command);

    Target nearest(const Carried* carried, double x, double z) const;
    Inventory inventoryOf(const Carried& carried) const;
    void store(Carried& carried, const Inventory& inventory) const;
    void tidy(Carried& carried) const;
    std::string nameOf(std::string_view itemId) const;
    void say(const Scene& scene, std::string text);
    StableId playerOf(const Scene& scene) const;

    std::shared_ptr<const PlacedLootSet> loot_;
    CarryCatalogue catalogue_;
    std::uint64_t capacity_;
    bool interactHeld_ = false;
    std::string notice_;
    double noticeAt_ = -1.0e9;
};

}
