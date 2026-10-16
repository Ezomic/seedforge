#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// Doc 06, encumbrance: the answer is never "walk back", it is "what do I leave
// behind". A stash is fixed at a place, a cart moves and can be lost, a cache
// is hidden until found. Capacity counts units; weight belongs to the
// inventory ticket and is not modelled here.
enum class ContainerKind : std::uint8_t { Stash, Cart, Cache };

enum class ContainerState : std::uint8_t { Active, Lost, Destroyed };

enum class StashStatus : std::uint8_t {
    Ok,
    UnknownContainer, // no such id, or a hidden cache seen by someone who may not know of it
    InvalidInput,     // empty or oversized id, a zero capacity, a permille over 1000
    Duplicate,
    TooMany,
    NotAllowed,   // not the owner and no right to this container
    NotMobile,    // only a cart moves or can be lost
    Unavailable,  // lost or destroyed
    Full,         // a deposit that fitted nothing
    Insufficient, // a withdrawal of more than is stored
};

constexpr std::size_t kMaxStashIdLength = 64;
constexpr std::size_t kMaxContainers = 4096;
constexpr std::size_t kMaxItemKinds = 256;
constexpr std::uint32_t kMaxStashCapacity = 1u << 24;
constexpr std::uint32_t kPermilleAll = 1000;

struct StashContainer {
    ContainerKind kind = ContainerKind::Stash;
    std::string owner;
    std::string place; // the settlement or position id; a cart's changes, the others' never do
    std::uint32_t capacity = 0;
    std::uint32_t used = 0;
    ContainerState state = ContainerState::Active;
    bool discovered = true; // false only for a cache nobody has found yet
    std::uint32_t raids = 0;
    std::map<std::string, std::uint32_t, std::less<>> items; // ordered, so iteration is deterministic
};

struct DepositResult {
    StashStatus status = StashStatus::Ok;
    std::uint32_t stored = 0; // less than asked when the container ran out of room
};

struct RaidResult {
    StashStatus status = StashStatus::Ok;
    std::map<std::string, std::uint32_t> taken; // only items that lost at least one unit
};

// markLost, recover, discover, raid and destroy take no actor: they are world
// events the caller triggers, not player requests, so they do not hide a
// cache's existence. Ids share one namespace, destroyed containers keep their
// slot (there is no pruning yet), and create reports Duplicate for any id.
//
// Every count is checked against the capacity before it is added, so nothing
// wraps, and every refusal leaves the registry unchanged.
class StashRegistry {
public:
    // A cache starts hidden; a stash or cart starts known.
    [[nodiscard]] StashStatus create(ContainerKind kind, std::string_view id, std::string_view owner, std::string_view place, std::uint32_t capacity);

    // The owner fills it as far as it will go: `stored` says how much landed.
    // Only an active container takes a deposit.
    [[nodiscard]] DepositResult deposit(std::string_view id, std::string_view actor, std::string_view item, std::uint32_t quantity);
    // All or nothing. The owner may always withdraw; anyone may from a cache
    // that has been discovered.
    [[nodiscard]] StashStatus withdraw(std::string_view id, std::string_view actor, std::string_view item, std::uint32_t quantity);

    // A cart goes where its owner goes.
    [[nodiscard]] StashStatus moveCart(std::string_view id, std::string_view actor, std::string_view place);
    // A cart can be lost and found again; its contents are out of reach meanwhile.
    [[nodiscard]] StashStatus markLost(std::string_view id);
    [[nodiscard]] StashStatus recover(std::string_view id);

    // Anyone can find a hidden cache; finding it opens its withdrawals.
    [[nodiscard]] StashStatus discover(std::string_view id);

    // Takes `permille`/1000 of every item, rounded down, in item id order. A
    // raid cannot touch a hidden cache or a lost or destroyed container.
    [[nodiscard]] RaidResult raid(std::string_view id, std::uint32_t permille);
    // Everything inside is gone and the container refuses all further use.
    [[nodiscard]] StashStatus destroy(std::string_view id);

    // Nullptr for an unknown id. A hidden cache is visible to its owner only.
    [[nodiscard]] const StashContainer* find(std::string_view id, std::string_view viewer) const;
    // How many of an item the container holds; zero when unknown or not visible.
    [[nodiscard]] std::uint32_t count(std::string_view id, std::string_view viewer, std::string_view item) const;
    // Ids the viewer may see, in id order.
    [[nodiscard]] std::vector<std::string> visibleTo(std::string_view viewer) const;
    [[nodiscard]] std::size_t size() const { return containers_.size(); }

private:
    StashContainer* lookup(std::string_view id, std::string_view viewer);

    std::map<std::string, StashContainer, std::less<>> containers_;
};

}
