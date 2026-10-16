#include "ichor/economy/stash.h"

#include <algorithm>
#include <iterator>
#include <utility>

namespace ichor {

namespace {

bool validName(std::string_view name)
{
    return !name.empty() && name.size() <= kMaxStashIdLength;
}

bool hiddenFrom(const StashContainer& c, std::string_view viewer)
{
    return c.kind == ContainerKind::Cache && !c.discovered && c.owner != viewer;
}

}

StashContainer* StashRegistry::lookup(std::string_view id, std::string_view viewer)
{
    const auto it = containers_.find(id);
    if (it == containers_.end() || hiddenFrom(it->second, viewer)) {
        return nullptr;
    }
    return &it->second;
}

StashStatus StashRegistry::create(ContainerKind kind, std::string_view id, std::string_view owner, std::string_view place, std::uint32_t capacity)
{
    if (!validName(id) || !validName(owner) || !validName(place) || capacity == 0 || capacity > kMaxStashCapacity) {
        return StashStatus::InvalidInput;
    }
    if (containers_.find(id) != containers_.end()) {
        return StashStatus::Duplicate;
    }
    if (containers_.size() >= kMaxContainers) {
        return StashStatus::TooMany;
    }
    StashContainer c;
    c.kind = kind;
    c.owner = std::string(owner);
    c.place = std::string(place);
    c.capacity = capacity;
    c.discovered = kind != ContainerKind::Cache;
    containers_.emplace(std::string(id), std::move(c));
    return StashStatus::Ok;
}

DepositResult StashRegistry::deposit(std::string_view id, std::string_view actor, std::string_view item, std::uint32_t quantity)
{
    StashContainer* c = lookup(id, actor);
    if (c == nullptr) {
        return {StashStatus::UnknownContainer, 0};
    }
    if (!validName(item)) {
        return {StashStatus::InvalidInput, 0};
    }
    if (c->state != ContainerState::Active) {
        return {StashStatus::Unavailable, 0};
    }
    if (c->owner != actor) {
        return {StashStatus::NotAllowed, 0};
    }
    if (quantity == 0) {
        return {StashStatus::Ok, 0};
    }
    const auto existing = c->items.find(item);
    if (existing == c->items.end() && c->items.size() >= kMaxItemKinds) {
        return {StashStatus::Full, 0};
    }
    const std::uint32_t stored = std::min(quantity, c->capacity - c->used);
    if (stored == 0) {
        return {StashStatus::Full, 0};
    }
    c->items[std::string(item)] += stored;
    c->used += stored;
    return {StashStatus::Ok, stored};
}

StashStatus StashRegistry::withdraw(std::string_view id, std::string_view actor, std::string_view item, std::uint32_t quantity)
{
    StashContainer* c = lookup(id, actor);
    if (c == nullptr) {
        return StashStatus::UnknownContainer;
    }
    if (!validName(item)) {
        return StashStatus::InvalidInput;
    }
    if (c->state != ContainerState::Active) {
        return StashStatus::Unavailable;
    }
    const bool open = c->kind == ContainerKind::Cache && c->discovered;
    if (c->owner != actor && !open) {
        return StashStatus::NotAllowed;
    }
    if (quantity == 0) {
        return StashStatus::Ok;
    }
    const auto it = c->items.find(item);
    if (it == c->items.end() || it->second < quantity) {
        return StashStatus::Insufficient;
    }
    it->second -= quantity;
    c->used -= quantity;
    if (it->second == 0) {
        c->items.erase(it);
    }
    return StashStatus::Ok;
}

StashStatus StashRegistry::moveCart(std::string_view id, std::string_view actor, std::string_view place)
{
    StashContainer* c = lookup(id, actor);
    if (c == nullptr) {
        return StashStatus::UnknownContainer;
    }
    if (c->kind != ContainerKind::Cart) {
        return StashStatus::NotMobile;
    }
    if (c->state != ContainerState::Active) {
        return StashStatus::Unavailable;
    }
    if (c->owner != actor) {
        return StashStatus::NotAllowed;
    }
    if (!validName(place)) {
        return StashStatus::InvalidInput;
    }
    c->place = std::string(place);
    return StashStatus::Ok;
}

StashStatus StashRegistry::markLost(std::string_view id)
{
    const auto it = containers_.find(id);
    if (it == containers_.end()) {
        return StashStatus::UnknownContainer;
    }
    if (it->second.kind != ContainerKind::Cart) {
        return StashStatus::NotMobile;
    }
    if (it->second.state != ContainerState::Active) {
        return StashStatus::Unavailable;
    }
    it->second.state = ContainerState::Lost;
    return StashStatus::Ok;
}

StashStatus StashRegistry::recover(std::string_view id)
{
    const auto it = containers_.find(id);
    if (it == containers_.end()) {
        return StashStatus::UnknownContainer;
    }
    if (it->second.kind != ContainerKind::Cart) {
        return StashStatus::NotMobile;
    }
    if (it->second.state != ContainerState::Lost) {
        return StashStatus::Unavailable;
    }
    it->second.state = ContainerState::Active;
    return StashStatus::Ok;
}

StashStatus StashRegistry::discover(std::string_view id)
{
    const auto it = containers_.find(id);
    if (it == containers_.end()) {
        return StashStatus::UnknownContainer;
    }
    if (it->second.kind != ContainerKind::Cache) {
        return StashStatus::NotAllowed;
    }
    if (it->second.state == ContainerState::Destroyed) {
        return StashStatus::Unavailable;
    }
    it->second.discovered = true;
    return StashStatus::Ok;
}

RaidResult StashRegistry::raid(std::string_view id, std::uint32_t permille)
{
    if (permille > kPermilleAll) {
        return {StashStatus::InvalidInput, {}};
    }
    const auto found = containers_.find(id);
    if (found == containers_.end() || !found->second.discovered) {
        return {StashStatus::UnknownContainer, {}};
    }
    StashContainer& c = found->second;
    if (c.state != ContainerState::Active) {
        return {StashStatus::Unavailable, {}};
    }
    RaidResult result;
    for (auto it = c.items.begin(); it != c.items.end();) {
        const auto share = static_cast<std::uint32_t>(static_cast<std::uint64_t>(it->second) * permille / kPermilleAll);
        if (share > 0) {
            result.taken.emplace(it->first, share);
            it->second -= share;
            c.used -= share;
        }
        it = it->second == 0 ? c.items.erase(it) : std::next(it);
    }
    ++c.raids;
    return result;
}

StashStatus StashRegistry::destroy(std::string_view id)
{
    const auto it = containers_.find(id);
    if (it == containers_.end()) {
        return StashStatus::UnknownContainer;
    }
    if (it->second.state == ContainerState::Destroyed) {
        return StashStatus::Unavailable;
    }
    it->second.state = ContainerState::Destroyed;
    it->second.items.clear();
    it->second.used = 0;
    return StashStatus::Ok;
}

const StashContainer* StashRegistry::find(std::string_view id, std::string_view viewer) const
{
    const auto it = containers_.find(id);
    if (it == containers_.end() || hiddenFrom(it->second, viewer)) {
        return nullptr;
    }
    return &it->second;
}

std::uint32_t StashRegistry::count(std::string_view id, std::string_view viewer, std::string_view item) const
{
    const StashContainer* c = find(id, viewer);
    if (c == nullptr) {
        return 0;
    }
    const auto it = c->items.find(item);
    return it == c->items.end() ? 0 : it->second;
}

std::vector<std::string> StashRegistry::visibleTo(std::string_view viewer) const
{
    std::vector<std::string> ids;
    for (const auto& [id, c] : containers_) {
        if (!hiddenFrom(c, viewer)) {
            ids.push_back(id);
        }
    }
    return ids;
}

}
