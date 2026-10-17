#include "ichor/net/coop_session.h"

#include <algorithm>
#include <limits>

namespace ichor::net {

namespace {

std::uint32_t clampTick(Tick t)
{
    return t > std::numeric_limits<std::uint32_t>::max() ? std::numeric_limits<std::uint32_t>::max() : static_cast<std::uint32_t>(t);
}

bool sameName(const std::string& a, const std::string& b)
{
    return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
        const auto fold = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
        return fold(x) == fold(y);
    });
}

bool elapsed(Tick now, Tick since, Tick limit)
{
    return now > since && now - since > limit;
}

}

HostSession::HostSession(HostConfig config, SessionLimits limits)
    : config_(std::move(config))
    , limits_(limits)
{
    if (!isValidName(config_.hostName)) config_.hostName = "Host";
    players_[0] = PlayerInfo{.id = 0, .name = config_.hostName, .inventory = {}, .lastInput = std::nullopt, .isHost = true};
}

HostSession::Conn* HostSession::find(ConnId conn)
{
    const auto it = std::find_if(conns_.begin(), conns_.end(), [&](const Conn& c) { return c.id == conn; });
    return it == conns_.end() ? nullptr : &*it;
}

void HostSession::observe(Tick now)
{
    hostTick_ = std::max(hostTick_, now);
}

void HostSession::send(HostOutput& out, ConnId to, const HostMessage& message) const
{
    if (auto bytes = encodeHost(message)) out.sends.push_back({to, std::move(*bytes)});
}

void HostSession::broadcast(HostOutput& out, const HostMessage& message, std::optional<ConnId> except) const
{
    const auto bytes = encodeHost(message);
    if (!bytes) return;
    for (const Conn& c : conns_) {
        if (c.state == State::Active && c.id != except) out.sends.push_back({c.id, *bytes});
    }
}

void HostSession::close(HostOutput& out, ConnId conn, CloseReason reason, bool notify)
{
    const auto it = std::find_if(conns_.begin(), conns_.end(), [&](const Conn& c) { return c.id == conn; });
    if (it == conns_.end()) return;
    const Conn closing = *it;
    conns_.erase(it);
    if (notify) send(out, conn, CloseMsg{reason});
    out.drops.push_back(conn);
    if (closing.state != State::Active) return;
    players_[closing.player].reset();
    out.events.push_back({SessionEventKind::PlayerLeft, closing.player, reason});
    broadcast(out, PlayerLeftMsg{closing.player}, std::nullopt);
}

void HostSession::strike(HostOutput& out, ConnId conn, CloseReason reason)
{
    Conn* c = find(conn);
    if (c == nullptr) return;
    if (++c->strikes >= limits_.maxStrikes) close(out, conn, reason, true);
}

bool HostSession::takeRateToken(Conn& conn, Tick now) const
{
    if (now < conn.windowStart || now - conn.windowStart >= limits_.rateWindowTicks) {
        conn.windowStart = now;
        conn.windowCount = 0;
    }
    return ++conn.windowCount <= limits_.maxMessagesPerWindow;
}

std::size_t HostSession::pendingCount() const
{
    return static_cast<std::size_t>(std::count_if(conns_.begin(), conns_.end(), [](const Conn& c) { return c.state == State::AwaitingHello; }));
}

HostOutput HostSession::onConnect(ConnId conn, Tick now)
{
    HostOutput out;
    observe(now);
    if (find(conn) != nullptr) return out;
    if (pendingCount() >= limits_.maxPending) {
        send(out, conn, CloseMsg{CloseReason::ServerFull});
        out.drops.push_back(conn);
        return out;
    }
    conns_.push_back(Conn{.id = conn, .connectedAt = now, .lastHeard = now, .windowStart = now});
    return out;
}

HostOutput HostSession::onDisconnect(ConnId conn, Tick now)
{
    HostOutput out;
    observe(now);
    close(out, conn, CloseReason::Left, false);
    out.drops.clear();
    return out;
}

HostOutput HostSession::onMessage(ConnId conn, std::span<const std::uint8_t> bytes, Tick now)
{
    HostOutput out;
    observe(now);
    Conn* c = find(conn);
    if (c == nullptr) {
        out.drops.push_back(conn);
        return out;
    }
    c->lastHeard = now;
    if (!takeRateToken(*c, now)) {
        strike(out, conn, CloseReason::RateLimited);
        return out;
    }
    const auto decoded = decodeClient(bytes);
    if (!decoded.message) {
        if (c->state == State::AwaitingHello) {
            close(out, conn, decoded.error == DecodeError::BadVersion ? CloseReason::VersionMismatch : CloseReason::Malformed, true);
        } else {
            strike(out, conn, CloseReason::Malformed);
        }
        return out;
    }
    if (c->state == State::AwaitingHello) {
        if (const auto* hello = std::get_if<HelloMsg>(&*decoded.message)) {
            handleHello(out, *c, *hello);
        } else if (std::holds_alternative<LeaveMsg>(*decoded.message)) {
            close(out, conn, CloseReason::Left, false);
        } else {
            close(out, conn, CloseReason::ProtocolViolation, true);
        }
        return out;
    }
    handleActive(out, *c, *decoded.message);
    return out;
}

void HostSession::handleHello(HostOutput& out, Conn& conn, const HelloMsg& hello)
{
    const ConnId id = conn.id;
    if (hello.worldSeed != config_.worldSeed || hello.genVersion != config_.genVersion) {
        close(out, id, CloseReason::WorldMismatch, true);
        return;
    }
    std::optional<PlayerId> slot;
    for (PlayerId p = 0; p < kMaxPlayers; ++p) {
        if (players_[p] && sameName(players_[p]->name, hello.name)) {
            close(out, id, CloseReason::NameTaken, true);
            return;
        }
        if (!slot && !players_[p]) slot = p;
    }
    if (!slot) {
        close(out, id, CloseReason::ServerFull, true);
        return;
    }
    WelcomeMsg welcome{.playerId = *slot, .worldSeed = config_.worldSeed, .genVersion = config_.genVersion, .revision = revision_, .hostTick = clampTick(hostTick_), .roster = {}};
    for (const auto& p : players_) {
        if (p) welcome.roster.push_back({p->id, p->name});
    }
    players_[*slot] = PlayerInfo{.id = *slot, .name = hello.name, .inventory = hello.inventory, .lastInput = std::nullopt, .isHost = false};
    welcome.roster.push_back({*slot, hello.name});
    std::sort(welcome.roster.begin(), welcome.roster.end(), [](const RosterEntry& a, const RosterEntry& b) { return a.id < b.id; });
    conn.state = State::Active;
    conn.player = *slot;
    send(out, id, welcome);
    broadcast(out, PlayerJoinedMsg{{*slot, hello.name}}, id);
    out.events.push_back({SessionEventKind::PlayerJoined, *slot, CloseReason::Left});
}

void HostSession::handleActive(HostOutput& out, Conn& conn, const ClientMessage& message)
{
    const ConnId id = conn.id;
    const PlayerId pid = conn.player;
    if (std::holds_alternative<LeaveMsg>(message)) {
        close(out, id, CloseReason::Left, false);
    } else if (const auto* ping = std::get_if<PingMsg>(&message)) {
        send(out, id, PongMsg{ping->token, clampTick(hostTick_)});
    } else if (const auto* input = std::get_if<InputMsg>(&message)) {
        if (input->tick > hostTick_ + limits_.maxFutureTicks) {
            strike(out, id, CloseReason::ProtocolViolation);
            return;
        }
        auto& last = players_[pid]->lastInput;
        if (!last || input->tick > last->tick) last = *input;
    } else if (const auto* command = std::get_if<CommandMsg>(&message)) {
        if (command->tick > hostTick_ + limits_.maxFutureTicks) {
            strike(out, id, CloseReason::ProtocolViolation);
            return;
        }
        if (hostTick_ > static_cast<Tick>(command->tick) + limits_.maxPastTicks) return;
        if (command->kind == CommandKind::UseItem && static_cast<std::size_t>(command->arg) >= players_[pid]->inventory.size()) {
            strike(out, id, CloseReason::ProtocolViolation);
            return;
        }
        out.commands.push_back({pid, *command});
    } else {
        strike(out, id, CloseReason::ProtocolViolation);
    }
}

HostOutput HostSession::advance(Tick now)
{
    HostOutput out;
    observe(now);
    std::vector<ConnId> expired;
    for (const Conn& c : conns_) {
        const bool pending = c.state == State::AwaitingHello;
        if (pending ? elapsed(now, c.connectedAt, limits_.helloTimeoutTicks) : elapsed(now, c.lastHeard, limits_.idleTimeoutTicks)) {
            expired.push_back(c.id);
        }
    }
    for (const ConnId id : expired) close(out, id, CloseReason::Timeout, true);
    return out;
}

HostOutput HostSession::publishDelta(std::uint64_t entity, std::uint16_t kind, std::int32_t value)
{
    HostOutput out;
    if (revision_ == std::numeric_limits<std::uint32_t>::max()) return closeAll(CloseReason::Desync);
    ++revision_;
    broadcast(out, WorldDeltaMsg{revision_, entity, kind, value}, std::nullopt);
    return out;
}

HostOutput HostSession::closeAll(CloseReason reason)
{
    HostOutput out;
    std::vector<ConnId> ids;
    ids.reserve(conns_.size());
    for (const Conn& c : conns_) ids.push_back(c.id);
    for (const ConnId id : ids) close(out, id, reason, true);
    return out;
}

std::vector<PlayerInfo> HostSession::players() const
{
    std::vector<PlayerInfo> result;
    for (const auto& p : players_) {
        if (p) result.push_back(*p);
    }
    return result;
}

const PlayerInfo* HostSession::player(PlayerId id) const
{
    return id < kMaxPlayers && players_[id] ? &*players_[id] : nullptr;
}

std::size_t HostSession::playerCount() const
{
    return static_cast<std::size_t>(std::count_if(players_.begin(), players_.end(), [](const auto& p) { return p.has_value(); }));
}

ClientSession::ClientSession(SessionLimits limits)
    : limits_(limits)
{
}

void ClientSession::closeWith(CloseReason reason)
{
    state_ = ClientState::Closed;
    if (!closeReason_) closeReason_ = reason;
}

RosterEntry* ClientSession::findRoster(PlayerId id)
{
    const auto it = std::find_if(roster_.begin(), roster_.end(), [&](const RosterEntry& e) { return e.id == id; });
    return it == roster_.end() ? nullptr : &*it;
}

std::optional<std::vector<std::uint8_t>> ClientSession::begin(const HelloMsg& hello, Tick now)
{
    if (state_ != ClientState::Idle) return std::nullopt;
    auto bytes = encodeClient(hello);
    if (!bytes) return std::nullopt;
    hello_ = hello;
    state_ = ClientState::AwaitingWelcome;
    startedAt_ = now;
    lastHeard_ = now;
    return bytes;
}

ClientOutput ClientSession::onMessage(std::span<const std::uint8_t> bytes, Tick now)
{
    ClientOutput out;
    if (state_ != ClientState::AwaitingWelcome && state_ != ClientState::Active) return out;
    const auto decoded = decodeHost(bytes);
    if (!decoded.message) {
        closeWith(CloseReason::Malformed);
        return out;
    }
    lastHeard_ = now;
    const HostMessage& message = *decoded.message;
    if (const auto* close = std::get_if<CloseMsg>(&message)) {
        closeWith(close->reason);
        return out;
    }
    if (state_ == ClientState::AwaitingWelcome) {
        const auto* welcome = std::get_if<WelcomeMsg>(&message);
        bool ok = welcome != nullptr && welcome->playerId != 0 && welcome->worldSeed == hello_.worldSeed
            && welcome->genVersion == hello_.genVersion;
        if (ok) {
            ok = std::any_of(welcome->roster.begin(), welcome->roster.end(),
                     [&](const RosterEntry& e) { return e.id == welcome->playerId && e.name == hello_.name; })
                && std::any_of(welcome->roster.begin(), welcome->roster.end(), [](const RosterEntry& e) { return e.id == 0; });
        }
        if (!ok) {
            closeWith(CloseReason::ProtocolViolation);
            return out;
        }
        state_ = ClientState::Active;
        playerId_ = welcome->playerId;
        revision_ = welcome->revision;
        hostTick_ = welcome->hostTick;
        roster_ = welcome->roster;
        return out;
    }
    if (const auto* joined = std::get_if<PlayerJoinedMsg>(&message)) {
        if (joined->player.id == playerId_ || findRoster(joined->player.id) != nullptr || roster_.size() >= kMaxPlayers) {
            closeWith(CloseReason::ProtocolViolation);
            return out;
        }
        roster_.push_back(joined->player);
        out.events.push_back({SessionEventKind::PlayerJoined, joined->player.id, CloseReason::Left});
    } else if (const auto* left = std::get_if<PlayerLeftMsg>(&message)) {
        RosterEntry* entry = findRoster(left->id);
        if (left->id == playerId_ || left->id == 0 || entry == nullptr) {
            closeWith(CloseReason::ProtocolViolation);
            return out;
        }
        roster_.erase(roster_.begin() + (entry - roster_.data()));
        out.events.push_back({SessionEventKind::PlayerLeft, left->id, CloseReason::Left});
    } else if (const auto* delta = std::get_if<WorldDeltaMsg>(&message)) {
        if (delta->revision != revision_ + 1) {
            closeWith(CloseReason::Desync);
            return out;
        }
        revision_ = delta->revision;
        out.deltas.push_back(*delta);
    } else if (const auto* pong = std::get_if<PongMsg>(&message)) {
        hostTick_ = std::max(hostTick_, pong->hostTick);
    } else {
        closeWith(CloseReason::ProtocolViolation);
    }
    return out;
}

void ClientSession::advance(Tick now)
{
    if (state_ == ClientState::AwaitingWelcome && elapsed(now, startedAt_, limits_.helloTimeoutTicks)) closeWith(CloseReason::Timeout);
    if (state_ == ClientState::Active && elapsed(now, lastHeard_, limits_.idleTimeoutTicks)) closeWith(CloseReason::Timeout);
}

std::optional<std::vector<std::uint8_t>> ClientSession::makeInput(const InputMsg& input) const
{
    return state_ == ClientState::Active ? encodeClient(input) : std::nullopt;
}

std::optional<std::vector<std::uint8_t>> ClientSession::makeCommand(const CommandMsg& command) const
{
    return state_ == ClientState::Active ? encodeClient(command) : std::nullopt;
}

std::optional<std::vector<std::uint8_t>> ClientSession::makePing(std::uint32_t token) const
{
    return state_ == ClientState::Active ? encodeClient(PingMsg{token}) : std::nullopt;
}

std::optional<std::vector<std::uint8_t>> ClientSession::leave()
{
    if (state_ != ClientState::Active && state_ != ClientState::AwaitingWelcome) return std::nullopt;
    auto bytes = encodeClient(LeaveMsg{});
    closeWith(CloseReason::Left);
    return bytes;
}

}
