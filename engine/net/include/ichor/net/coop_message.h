#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace ichor::net {

// doc 15 tier 2: the wire messages of a host-authoritative co-op session. The
// codec is transport independent and treats every byte as hostile: sizes are
// capped before parsing, every field is range checked, floats must be finite,
// and a message must be consumed exactly. Guests can only ask (input,
// commands); no guest message type can change world state.
constexpr std::uint16_t kMagic = 0x4349;
constexpr std::uint8_t kProtocolVersion = 1;
constexpr std::size_t kMaxMessageBytes = 2048;
constexpr std::size_t kMaxPlayers = 4;
constexpr std::size_t kMaxNameLength = 24;
constexpr std::size_t kMaxItemIdLength = 32;
constexpr std::size_t kMaxInventoryEntries = 32;
constexpr std::uint16_t kMaxItemCount = 9999;
constexpr std::uint8_t kMaxEmote = 15;

using PlayerId = std::uint8_t;

enum class MessageType : std::uint8_t {
    Hello = 1,
    Leave = 2,
    Input = 3,
    Command = 4,
    Ping = 5,
    Welcome = 16,
    Close = 17,
    PlayerJoined = 18,
    PlayerLeft = 19,
    WorldDelta = 20,
    Pong = 21,
};

enum class DecodeError : std::uint8_t { None, TooLarge, Truncated, BadMagic, BadVersion, BadType, BadField, Trailing };

enum class CloseReason : std::uint8_t {
    ServerFull = 1,
    WorldMismatch,
    NameTaken,
    VersionMismatch,
    Malformed,
    ProtocolViolation,
    RateLimited,
    Timeout,
    HostClosed,
    Left,
    Desync,
};

enum class CommandKind : std::uint8_t { Interact = 1, Attack, UseItem, Revive, Emote };

struct InventoryEntry {
    std::string itemId;
    std::uint16_t count = 0;
    bool operator==(const InventoryEntry&) const = default;
};

struct HelloMsg {
    std::uint64_t worldSeed = 0;
    std::uint32_t genVersion = 0;
    std::string name;
    std::vector<InventoryEntry> inventory;
    bool operator==(const HelloMsg&) const = default;
};

struct LeaveMsg {
    bool operator==(const LeaveMsg&) const = default;
};

struct InputMsg {
    std::uint32_t tick = 0;
    float moveX = 0.0f;
    float moveY = 0.0f;
    float facing = 0.0f;
    bool operator==(const InputMsg&) const = default;
};

struct CommandMsg {
    CommandKind kind = CommandKind::Interact;
    std::uint32_t tick = 0;
    std::uint64_t target = 0;
    std::int32_t arg = 0;
    bool operator==(const CommandMsg&) const = default;
};

struct PingMsg {
    std::uint32_t token = 0;
    bool operator==(const PingMsg&) const = default;
};

using ClientMessage = std::variant<HelloMsg, LeaveMsg, InputMsg, CommandMsg, PingMsg>;

struct RosterEntry {
    PlayerId id = 0;
    std::string name;
    bool operator==(const RosterEntry&) const = default;
};

struct WelcomeMsg {
    PlayerId playerId = 0;
    std::uint64_t worldSeed = 0;
    std::uint32_t genVersion = 0;
    std::uint32_t revision = 0;
    std::uint32_t hostTick = 0;
    std::vector<RosterEntry> roster;
    bool operator==(const WelcomeMsg&) const = default;
};

struct CloseMsg {
    CloseReason reason = CloseReason::HostClosed;
    bool operator==(const CloseMsg&) const = default;
};

struct PlayerJoinedMsg {
    RosterEntry player;
    bool operator==(const PlayerJoinedMsg&) const = default;
};

struct PlayerLeftMsg {
    PlayerId id = 0;
    bool operator==(const PlayerLeftMsg&) const = default;
};

// One entry of the host's delta log: revisions count up from 1 without gaps.
struct WorldDeltaMsg {
    std::uint32_t revision = 0;
    std::uint64_t entity = 0;
    std::uint16_t kind = 0;
    std::int32_t value = 0;
    bool operator==(const WorldDeltaMsg&) const = default;
};

struct PongMsg {
    std::uint32_t token = 0;
    std::uint32_t hostTick = 0;
    bool operator==(const PongMsg&) const = default;
};

using HostMessage = std::variant<WelcomeMsg, CloseMsg, PlayerJoinedMsg, PlayerLeftMsg, WorldDeltaMsg, PongMsg>;

template <class M>
struct Decoded {
    std::optional<M> message;
    DecodeError error = DecodeError::None;
};

bool isValidName(const std::string& name);
bool isValidItemId(const std::string& id);
bool isValidInventory(const std::vector<InventoryEntry>& inventory);
bool isValid(const ClientMessage& message);
bool isValid(const HostMessage& message);

// Encoding refuses a message that decoding would refuse, so a bug on the
// sending side cannot put an invalid message on the wire.
std::optional<std::vector<std::uint8_t>> encodeClient(const ClientMessage& message);
std::optional<std::vector<std::uint8_t>> encodeHost(const HostMessage& message);

Decoded<ClientMessage> decodeClient(std::span<const std::uint8_t> bytes);
Decoded<HostMessage> decodeHost(std::span<const std::uint8_t> bytes);

}
