#pragma once

#include "ichor/net/coop_message.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ichor::net {

// The transport-independent half of doc 15 tier 2. A transport (not built yet)
// hands the session connection ids, raw bytes and a tick counter, and carries
// out the sends and drops the session returns. Nothing here reads a clock or
// touches a socket, so the whole state machine is testable and deterministic.
//
// Two things are deliberately not here. The Welcome carries the host's delta
// revision, but replaying deltas 1..N to a late joiner belongs to whoever owns
// the delta log (the save layer), so a guest must be brought up to the
// Welcome revision before it treats the world as live. And the inventory a
// guest declares in its Hello is a claim: the session only bounds-checks
// against it, the host simulation owns real item counts.
using ConnId = std::uint32_t;
using Tick = std::uint64_t;

struct SessionLimits {
    std::size_t maxPending = 8;
    Tick helloTimeoutTicks = 300;
    Tick idleTimeoutTicks = 1800;
    Tick rateWindowTicks = 60;
    std::uint32_t maxMessagesPerWindow = 120;
    std::uint32_t maxStrikes = 5;
    Tick maxFutureTicks = 10;
    Tick maxPastTicks = 120;
};

struct Outbound {
    ConnId to = 0;
    std::vector<std::uint8_t> bytes;
};

enum class SessionEventKind : std::uint8_t { PlayerJoined, PlayerLeft };

struct SessionEvent {
    SessionEventKind kind = SessionEventKind::PlayerJoined;
    PlayerId player = 0;
    CloseReason reason = CloseReason::Left;
};

// A guest request that passed validation. The host simulation decides whether
// it happens; the session never applies one.
struct AcceptedCommand {
    PlayerId player = 0;
    CommandMsg command;
};

struct HostOutput {
    std::vector<Outbound> sends;
    std::vector<ConnId> drops;
    std::vector<AcceptedCommand> commands;
    std::vector<SessionEvent> events;
};

struct PlayerInfo {
    PlayerId id = 0;
    std::string name;
    // What the guest brought. The host only ever reads this; guests keep no world progress.
    std::vector<InventoryEntry> inventory;
    std::optional<InputMsg> lastInput;
    bool isHost = false;
};

struct HostConfig {
    std::uint64_t worldSeed = 0;
    std::uint32_t genVersion = 0;
    std::string hostName = "Host";
};

class HostSession {
public:
    // The host is player 0 and has no connection: single player is a one-player listen server.
    explicit HostSession(HostConfig config, SessionLimits limits = {});

    HostOutput onConnect(ConnId conn, Tick now);
    HostOutput onMessage(ConnId conn, std::span<const std::uint8_t> bytes, Tick now);
    HostOutput onDisconnect(ConnId conn, Tick now);
    HostOutput advance(Tick now);
    // The only way the world changes on a guest: the host appends to its delta log and broadcasts.
    HostOutput publishDelta(std::uint64_t entity, std::uint16_t kind, std::int32_t value);
    HostOutput closeAll(CloseReason reason);

    std::vector<PlayerInfo> players() const;
    const PlayerInfo* player(PlayerId id) const;
    std::size_t playerCount() const;
    std::size_t pendingCount() const;
    std::uint32_t revision() const { return revision_; }
    Tick hostTick() const { return hostTick_; }

private:
    enum class State : std::uint8_t { AwaitingHello, Active };
    struct Conn {
        ConnId id = 0;
        State state = State::AwaitingHello;
        PlayerId player = 0;
        Tick connectedAt = 0;
        Tick lastHeard = 0;
        Tick windowStart = 0;
        std::uint32_t windowCount = 0;
        std::uint32_t strikes = 0;
    };

    Conn* find(ConnId conn);
    void observe(Tick now);
    void send(HostOutput& out, ConnId to, const HostMessage& message) const;
    void broadcast(HostOutput& out, const HostMessage& message, std::optional<ConnId> except) const;
    void close(HostOutput& out, ConnId conn, CloseReason reason, bool notify);
    void strike(HostOutput& out, ConnId conn, CloseReason reason);
    void handleHello(HostOutput& out, Conn& conn, const HelloMsg& hello);
    void handleActive(HostOutput& out, Conn& conn, const ClientMessage& message);
    bool takeRateToken(Conn& conn, Tick now) const;

    HostConfig config_;
    SessionLimits limits_;
    std::array<std::optional<PlayerInfo>, kMaxPlayers> players_;
    std::vector<Conn> conns_;
    std::uint32_t revision_ = 0;
    Tick hostTick_ = 0;
};

enum class ClientState : std::uint8_t { Idle, AwaitingWelcome, Active, Closed };

struct ClientOutput {
    std::vector<WorldDeltaMsg> deltas;
    std::vector<SessionEvent> events;
};

// The guest's view. It accepts host messages only in the order the protocol
// allows and refuses a delta log with a gap, so a hostile or buggy host cannot
// leave a guest silently diverged.
class ClientSession {
public:
    explicit ClientSession(SessionLimits limits = {});

    std::optional<std::vector<std::uint8_t>> begin(const HelloMsg& hello, Tick now);
    ClientOutput onMessage(std::span<const std::uint8_t> bytes, Tick now);
    void advance(Tick now);

    std::optional<std::vector<std::uint8_t>> makeInput(const InputMsg& input) const;
    std::optional<std::vector<std::uint8_t>> makeCommand(const CommandMsg& command) const;
    std::optional<std::vector<std::uint8_t>> makePing(std::uint32_t token) const;
    std::optional<std::vector<std::uint8_t>> leave();

    ClientState state() const { return state_; }
    std::optional<CloseReason> closeReason() const { return closeReason_; }
    PlayerId playerId() const { return playerId_; }
    std::uint32_t revision() const { return revision_; }
    std::uint32_t hostTick() const { return hostTick_; }
    const std::vector<RosterEntry>& roster() const { return roster_; }

private:
    void closeWith(CloseReason reason);
    RosterEntry* findRoster(PlayerId id);

    SessionLimits limits_;
    ClientState state_ = ClientState::Idle;
    std::optional<CloseReason> closeReason_;
    HelloMsg hello_;
    PlayerId playerId_ = 0;
    std::uint32_t revision_ = 0;
    std::uint32_t hostTick_ = 0;
    std::vector<RosterEntry> roster_;
    Tick startedAt_ = 0;
    Tick lastHeard_ = 0;
};

}
