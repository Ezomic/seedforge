#include "ichor/world/carry.h"

#include "ichor/economy/loot_use.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace ichor {

namespace {

constexpr const char* kPickup = "carry.pickup";
constexpr const char* kUse = "carry.use";
constexpr const char* kDrop = "carry.drop";
constexpr const char* kGrant = "carry.grant";

std::string textOf(const DataRecord& record, const char* key)
{
    const auto it = record.fields.find(key);
    return it != record.fields.end() && std::holds_alternative<std::string>(it->second) ? std::get<std::string>(it->second) : std::string{};
}

// relic and key are not classes the inventory knows; both are carried one at a time, as information is.
std::string inventoryClass(const std::string& dataClass)
{
    return dataClass == "relic" || dataClass == "key" ? "information" : dataClass;
}

bool finitePoint(double x, double z, float y)
{
    return std::isfinite(x) && std::isfinite(z) && std::isfinite(y);
}

}

CarryCatalogue CarryCatalogue::fromData(const DataSet& data, std::vector<std::string>& problems)
{
    std::vector<const DataTable*> tables;
    for (const char* tableName : {"notable_items", "catalogue"}) {
        if (const DataTable* table = data.table(tableName)) {
            tables.push_back(table);
        } else {
            problems.push_back(std::string("there is no ") + tableName + " table, so those items cannot be carried");
        }
    }
    return fromTables(tables, problems);
}

CarryCatalogue CarryCatalogue::fromTables(const std::vector<const DataTable*>& tables, std::vector<std::string>& problems)
{
    DataTable merged;
    merged.name = "carry";
    std::map<std::string, CarryItem, std::less<>> info;
    for (const DataTable* table : tables) {
        for (const auto& [id, record] : table->records) {
            if (merged.records.contains(id)) {
                continue;
            }
            DataRecord copy = record;
            const std::string dataClass = textOf(record, "class");
            copy.fields["class"] = inventoryClass(dataClass);
            DataTable probe;
            probe.records.emplace(id, copy);
            std::string error;
            if (!ItemCatalogue::fromTable(probe, StackRules{}, error)) {
                problems.push_back("item " + id + " cannot be carried: " + error);
                continue;
            }
            merged.records.emplace(id, std::move(copy));
            const std::string name = textOf(record, "name");
            info.emplace(id, CarryItem{id, name.empty() ? id : name, dataClass, textOf(record, "benefit")});
        }
    }
    std::string error;
    std::optional<ItemCatalogue> items = ItemCatalogue::fromTable(merged, StackRules{}, error);
    if (!items) {
        // Every record passed alone, so this cannot happen; an empty catalogue is the safe answer if it does.
        problems.push_back("the carried catalogue could not be built: " + error);
        items = ItemCatalogue::fromTable(DataTable{}, StackRules{}, error);
        info.clear();
    }
    CarryCatalogue out(std::move(*items));
    out.info_ = std::move(info);
    return out;
}

const CarryItem* CarryCatalogue::info(std::string_view id) const
{
    const auto it = info_.find(id);
    return it == info_.end() ? nullptr : &it->second;
}

std::string formatWeightUnits(std::uint64_t units)
{
    const std::uint64_t tenths = units / (kWeightUnitsPerWeight / 10) + (units % (kWeightUnitsPerWeight / 10) >= kWeightUnitsPerWeight / 20 ? 1 : 0);
    return std::format("{}.{}", tenths / 10, tenths % 10);
}

CarrySystem::CarrySystem(std::shared_ptr<const PlacedLootSet> loot, CarryCatalogue catalogue, std::uint64_t capacityUnits)
    : loot_(std::move(loot))
    , catalogue_(std::move(catalogue))
    , capacity_(capacityUnits)
{
}

void CarrySystem::install(Scene& scene)
{
    scene.registerComponent<Carried>(kCarriedComponent);
    scene.registerGameCommand(kPickup, [this](Scene& s, const GameCommand& c) { pickup(s, c); });
    scene.registerGameCommand(kUse, [this](Scene& s, const GameCommand& c) { use(s, c); });
    scene.registerGameCommand(kDrop, [this](Scene& s, const GameCommand& c) { drop(s, c); });
    scene.registerGameCommand(kGrant, [this](Scene& s, const GameCommand& c) { grant(s, c); });
    scene.addSystem("carry", [this](Scene& s, double, const InputFrame& input) { step(s, input); });
}

StableId CarrySystem::playerOf(const Scene& scene) const
{
    for (auto [entity, id, player] : scene.registry().view<const StableId, const Player>().each()) {
        return id;
    }
    return StableId::of("player"); // not spawned yet: the id the app gives the player
}

const Carried* CarrySystem::carried(const Scene& scene) const
{
    for (auto [entity, player, held] : scene.registry().view<const Player, const Carried>().each()) {
        return &held;
    }
    return nullptr;
}

bool CarrySystem::isTaken(const Carried* carried, std::string_view itemId)
{
    return carried != nullptr && std::ranges::find(carried->taken, itemId) != carried->taken.end();
}

Inventory CarrySystem::inventoryOf(const Carried& carried) const
{
    Inventory inventory(catalogue_.items(), capacity_);
    for (const CarryStack& stack : carried.stacks) {
        // A saved stack the data no longer knows, or past its limit, is not carried.
        (void)inventory.addForced(stack.itemId, stack.count);
    }
    return inventory;
}

void CarrySystem::store(Carried& carried, const Inventory& inventory) const
{
    carried.stacks.clear();
    for (const InventoryRow& row : inventory.rows()) {
        carried.stacks.push_back({row.id, row.count});
    }
}

void CarrySystem::tidy(Carried& carried) const
{
    if (carried.taken.size() > kMaxTakenItems) {
        carried.taken.resize(kMaxTakenItems);
    }
    std::erase_if(carried.dropped, [&](const CarryDrop& drop) {
        const ItemDef* def = catalogue_.items().find(drop.itemId);
        return def == nullptr || drop.count == 0 || drop.count > def->stackLimit || !finitePoint(drop.x, drop.z, drop.groundY);
    });
    if (carried.dropped.size() > kMaxDroppedItems) {
        carried.dropped.resize(kMaxDroppedItems);
    }
}

std::string CarrySystem::nameOf(std::string_view itemId) const
{
    const CarryItem* item = catalogue_.info(itemId);
    return item != nullptr ? item->name : std::string(itemId);
}

void CarrySystem::say(const Scene& scene, std::string text)
{
    notice_ = std::move(text);
    noticeAt_ = scene.time();
}

CarrySystem::Target CarrySystem::nearest(const Carried* carried, double x, double z) const
{
    Target best;
    double bestSquared = kCarryReachM * kCarryReachM;
    const auto consider = [&](double itemX, double itemZ, auto&& fill) {
        const double dx = itemX - x;
        const double dz = itemZ - z;
        const double squared = dx * dx + dz * dz;
        if (!(squared <= bestSquared) || (best.found && !(squared < bestSquared))) {
            return;
        }
        if (fill()) {
            bestSquared = squared;
        }
    };
    if (loot_) {
        for (const PlacedLoot& item : loot_->all()) {
            consider(item.x, item.z, [&] {
                if (isTaken(carried, item.itemId)) {
                    return false;
                }
                best = {true, false, 0, item.itemId, 1};
                return true;
            });
        }
    }
    if (carried != nullptr) {
        for (std::size_t i = 0; i < carried->dropped.size(); ++i) {
            const CarryDrop& drop = carried->dropped[i];
            consider(drop.x, drop.z, [&] {
                best = {true, true, i, drop.itemId, drop.count};
                return true;
            });
        }
    }
    return best;
}

std::string CarrySystem::notice(const Scene& scene) const
{
    return scene.time() - noticeAt_ < kCarryNoticeSeconds ? notice_ : std::string();
}

std::string CarrySystem::prompt(const Scene& scene) const
{
    if (std::string recent = notice(scene); !recent.empty()) {
        return recent;
    }
    for (auto [entity, player, transform] : scene.registry().view<const Player, const Transform>().each()) {
        const Target target = nearest(carried(scene), transform.position.x, transform.position.z);
        if (!target.found) {
            return {};
        }
        const ItemDef* def = catalogue_.items().find(target.itemId);
        const std::uint64_t units = def != nullptr ? def->weightUnits * target.count : 0;
        return std::format("E: pick up {} ({})", nameOf(target.itemId), formatWeightUnits(units));
    }
    return {};
}

CarryView CarrySystem::view(const Scene& scene) const
{
    CarryView out;
    out.capacityUnits = capacity_;
    const Carried* held = carried(scene);
    if (held == nullptr) {
        return out;
    }
    const Inventory inventory = inventoryOf(*held);
    out.totalUnits = inventory.totalWeightUnits();
    out.level = inventory.level();
    for (const InventoryRow& row : inventory.rows()) {
        const CarryItem* info = catalogue_.info(row.id);
        out.rows.push_back({row.id, info != nullptr ? info->name : row.id, info != nullptr ? info->itemClass : std::string(), info != nullptr ? info->benefit : std::string(), row.count,
            row.unitWeightUnits, info != nullptr && info->itemClass == "consumable"});
    }
    return out;
}

void CarrySystem::requestUse(Scene& scene, const std::string& itemId) const
{
    scene.enqueue(GameCommand{playerOf(scene), kUse, itemId, 1});
}

void CarrySystem::requestDrop(Scene& scene, const std::string& itemId, std::uint32_t count) const
{
    scene.enqueue(GameCommand{playerOf(scene), kDrop, itemId, count});
}

void CarrySystem::requestGrant(Scene& scene, const std::string& itemId, std::uint32_t count) const
{
    scene.enqueue(GameCommand{playerOf(scene), kGrant, itemId, count});
}

void CarrySystem::step(Scene& scene, const InputFrame& input)
{
    const bool interact = (input.buttons & (InputFrame::Interact | InputFrame::Confirm)) != 0;
    const bool pressed = interact && !interactHeld_;
    interactHeld_ = interact;
    for (auto [entity, id, player, transform] : scene.registry().view<const StableId, Player, const Transform>().each()) {
        Carried& held = scene.registry().get_or_emplace<Carried>(entity);
        // The load slows the walk. Sprinting and the stamina cost are not here: there is no stamina in the app yet.
        const EncumbrancePenalty penalty = encumbrancePenalty(inventoryOf(held).level());
        player.metresPerSecond = Player{}.metresPerSecond * static_cast<float>(penalty.movePermille) / 1000.0f;
        if (pressed && nearest(&held, transform.position.x, transform.position.z).found) {
            scene.enqueue(GameCommand{id, kPickup, {}, 0});
        }
        break;
    }
}

void CarrySystem::pickup(Scene& scene, const GameCommand& command)
{
    const entt::entity entity = scene.find(command.actor);
    const Transform* transform = entity == entt::null ? nullptr : scene.registry().try_get<Transform>(entity);
    if (transform == nullptr) {
        return;
    }
    Carried& held = scene.registry().get_or_emplace<Carried>(entity);
    tidy(held);
    const Target target = nearest(&held, transform->position.x, transform->position.z);
    if (!target.found) {
        return;
    }
    Inventory inventory = inventoryOf(held);
    const std::string name = nameOf(target.itemId);
    const InventoryResult result = inventory.add(target.itemId, target.count);
    switch (result.status) {
    case InventoryStatus::Ok:
        break;
    case InventoryStatus::OverCapacity: {
        const std::uint64_t free = capacity_ > inventory.totalWeightUnits() ? capacity_ - inventory.totalWeightUnits() : 0;
        say(scene, std::format("{} is too heavy to carry now ({} free of {})", name, formatWeightUnits(free), formatWeightUnits(capacity_)));
        return;
    }
    case InventoryStatus::StackFull:
        say(scene, std::format("You can carry no more {}", name));
        return;
    default:
        say(scene, std::format("{} cannot be taken", name));
        return;
    }
    store(held, inventory);
    if (target.dropped) {
        held.dropped.erase(held.dropped.begin() + static_cast<std::ptrdiff_t>(target.index));
    } else if (held.taken.size() < kMaxTakenItems) {
        held.taken.push_back(target.itemId);
    }
    say(scene, std::format("Picked up {}", name));
}

void CarrySystem::use(Scene& scene, const GameCommand& command)
{
    const entt::entity entity = scene.find(command.actor);
    if (entity == entt::null) {
        return;
    }
    Carried& held = scene.registry().get_or_emplace<Carried>(entity);
    const CarryItem* info = catalogue_.info(command.subject);
    if (info == nullptr || info->itemClass != "consumable") {
        say(scene, std::format("{} cannot be used", nameOf(command.subject)));
        return;
    }
    Inventory inventory = inventoryOf(held);
    ChargeLedger ledger;
    // Every consumable in the data is a single dose.
    const UseResult result = useConsumable(inventory, ledger, ConsumableDef{info->id, 1, info->benefit, 0});
    if (result.status != UseStatus::Ok) {
        say(scene, std::format("You have no {}", info->name));
        return;
    }
    store(held, inventory);
    say(scene, info->benefit.empty() ? std::format("Used {}", info->name) : std::format("Used {}: {}", info->name, info->benefit));
}

void CarrySystem::drop(Scene& scene, const GameCommand& command)
{
    const entt::entity entity = scene.find(command.actor);
    const Transform* transform = entity == entt::null ? nullptr : scene.registry().try_get<Transform>(entity);
    if (transform == nullptr) {
        return;
    }
    Carried& held = scene.registry().get_or_emplace<Carried>(entity);
    tidy(held);
    const std::uint32_t count = command.count == 0 ? 1 : command.count;
    if (held.dropped.size() >= kMaxDroppedItems) {
        say(scene, "There is no room to put anything more down");
        return;
    }
    Inventory inventory = inventoryOf(held);
    if (inventory.remove(command.subject, count).status != InventoryStatus::Ok) {
        say(scene, std::format("You do not have {} of {}", count, nameOf(command.subject)));
        return;
    }
    store(held, inventory);
    held.dropped.push_back({command.subject, count, transform->position.x, transform->position.z, static_cast<float>(transform->position.y)});
    say(scene, std::format("Dropped {}", nameOf(command.subject)));
}

void CarrySystem::grant(Scene& scene, const GameCommand& command)
{
    const entt::entity entity = scene.find(command.actor);
    if (entity == entt::null) {
        return;
    }
    Carried& held = scene.registry().get_or_emplace<Carried>(entity);
    Inventory inventory = inventoryOf(held);
    if (inventory.addForced(command.subject, command.count).status != InventoryStatus::Ok) {
        say(scene, std::format("{} could not be added", nameOf(command.subject)));
        return;
    }
    store(held, inventory);
    say(scene, std::format("Received {}", nameOf(command.subject)));
}

}
