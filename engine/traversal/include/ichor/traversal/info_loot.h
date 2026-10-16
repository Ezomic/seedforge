#pragma once

#include "ichor/traversal/vantage_reveal.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ichor {

enum class InfoKind : std::uint8_t {
    Map,    // reveals cells
    Chart,  // marks routes
    Cipher, // decodes hidden texts
    Key,    // opens a lock by stable id
};

constexpr std::size_t kInfoMaxIdLength = 64;
constexpr std::size_t kInfoMaxMapCells = 4096;
constexpr std::size_t kInfoMaxEntries = 256;
constexpr std::size_t kInfoMaxTextLength = 1024;
constexpr std::size_t kInfoMaxItems = 4096;
constexpr std::size_t kInfoMaxStateRevealedCells = 1000000;
constexpr std::size_t kInfoMaxStateEntries = 65536;

struct InfoCipherText {
    std::string textId;
    std::string plain;
};

// One information item. Only the field matching `kind` is read. `singleUse` items are spent by the
// first successful use; failed uses never spend an item.
struct InfoItemDef {
    std::string id;
    InfoKind kind = InfoKind::Map;
    bool singleUse = false;
    std::vector<VantageCell> cells;    // Map
    std::vector<std::string> routeIds; // Chart
    std::vector<InfoCipherText> texts; // Cipher
    std::string lockId;                // Key
};

enum class InfoDefStatus : std::uint8_t {
    Ok,
    InvalidId, // empty, too long, or characters outside [a-z0-9_.]
    Duplicate,
    TooLarge,  // a list over its cap, a text over kInfoMaxTextLength, or too many items
    Malformed, // the payload for the kind is empty, or a Key without a lock id
};

// Ids are lower-case ASCII letters, digits, '_' and '.', 1..kInfoMaxIdLength long.
bool isValidInfoId(const std::string& id);

class InfoLootRegistry {
public:
    // Stores a normalised copy (cells sorted and unique, routes and texts sorted by id and unique).
    InfoDefStatus add(const InfoItemDef& def);
    const InfoItemDef* find(const std::string& id) const;
    std::size_t size() const { return items_.size(); }

private:
    std::vector<InfoItemDef> items_; // sorted by id
};

// Everything the holder has learned. All vectors are kept sorted and unique.
struct InfoLootState {
    std::vector<std::string> spentItems;
    std::vector<VantageCell> revealedCells;
    std::vector<std::string> markedRoutes;
    std::vector<std::string> decodedTexts;
    std::vector<std::string> openedLocks;
};

enum class InfoUseStatus : std::uint8_t {
    Ok,
    UnknownItem,
    InvalidId,     // the item id or the target id is not a valid id
    Spent,         // a single-use item that was already used
    WrongTarget,   // a cipher with no such text, or a key that does not fit the lock
    MissingTarget, // a cipher or key used without a target
    AlreadyDone,   // lock already open or text already decoded: nothing changes, nothing is spent
    StateFull,     // the state is at its cap: nothing changes, nothing is spent
};

struct InfoUseResult {
    InfoUseStatus status = InfoUseStatus::Ok;
    std::size_t newCells = 0;
    std::vector<std::string> newRoutes;
    std::string plain; // decoded text
    std::string openedLock;
    bool spent = false; // this use spent the item
};

// Pure and deterministic. `target` is the hidden text id for a cipher and the lock id for a key; it
// is ignored for maps and charts. A failed use leaves `state` untouched. A map or chart used again
// succeeds with nothing new.
InfoUseResult useInfoItem(const InfoLootRegistry& registry, InfoLootState& state, const std::string& itemId,
    const std::string& target = {});

}
