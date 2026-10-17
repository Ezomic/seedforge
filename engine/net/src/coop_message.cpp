#include "ichor/net/coop_message.h"

#include <algorithm>
#include <bit>

namespace ichor::net {

namespace {

bool inRange(float v, float limit)
{
    return v >= -limit && v <= limit;
}

bool isNameChar(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '_' || c == '-';
}

bool isItemChar(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == ':' || c == '-';
}

std::optional<std::vector<std::uint8_t>> capped(std::vector<std::uint8_t> bytes)
{
    if (bytes.size() > kMaxMessageBytes) return std::nullopt;
    return bytes;
}

bool validRoster(const std::vector<RosterEntry>& roster)
{
    if (roster.size() > kMaxPlayers) return false;
    std::uint32_t seen = 0;
    for (const RosterEntry& entry : roster) {
        if (entry.id >= kMaxPlayers || !isValidName(entry.name)) return false;
        const std::uint32_t bit = 1u << entry.id;
        if ((seen & bit) != 0) return false;
        seen |= bit;
    }
    return true;
}

bool validCommand(const CommandMsg& c)
{
    switch (c.kind) {
    case CommandKind::Interact:
    case CommandKind::Attack:
    case CommandKind::Revive:
        return c.arg == 0;
    case CommandKind::UseItem:
        return c.arg >= 0 && c.arg < static_cast<std::int32_t>(kMaxInventoryEntries);
    case CommandKind::Emote:
        return c.arg >= 0 && c.arg <= kMaxEmote;
    }
    return false;
}

bool validClose(CloseReason r)
{
    return r >= CloseReason::ServerFull && r <= CloseReason::Desync;
}

class Writer {
public:
    explicit Writer(MessageType type)
    {
        u16(kMagic);
        u8(kProtocolVersion);
        u8(static_cast<std::uint8_t>(type));
    }
    void u8(std::uint8_t v) { bytes_.push_back(v); }
    void u16(std::uint16_t v)
    {
        u8(static_cast<std::uint8_t>(v & 0xFF));
        u8(static_cast<std::uint8_t>(v >> 8));
    }
    void u32(std::uint32_t v)
    {
        u16(static_cast<std::uint16_t>(v & 0xFFFF));
        u16(static_cast<std::uint16_t>(v >> 16));
    }
    void u64(std::uint64_t v)
    {
        u32(static_cast<std::uint32_t>(v & 0xFFFFFFFFu));
        u32(static_cast<std::uint32_t>(v >> 32));
    }
    void i32(std::int32_t v) { u32(std::bit_cast<std::uint32_t>(v)); }
    void f32(float v) { u32(std::bit_cast<std::uint32_t>(v)); }
    void str(const std::string& s)
    {
        u8(static_cast<std::uint8_t>(s.size()));
        bytes_.insert(bytes_.end(), s.begin(), s.end());
    }
    std::vector<std::uint8_t> take() { return std::move(bytes_); }

private:
    std::vector<std::uint8_t> bytes_;
};

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> bytes)
        : bytes_(bytes)
    {
    }
    bool u8(std::uint8_t& v)
    {
        if (offset_ >= bytes_.size()) return fail();
        v = bytes_[offset_++];
        return true;
    }
    bool u16(std::uint16_t& v)
    {
        std::uint8_t lo = 0, hi = 0;
        if (!u8(lo) || !u8(hi)) return false;
        v = static_cast<std::uint16_t>(lo | (hi << 8));
        return true;
    }
    bool u32(std::uint32_t& v)
    {
        std::uint16_t lo = 0, hi = 0;
        if (!u16(lo) || !u16(hi)) return false;
        v = static_cast<std::uint32_t>(lo) | (static_cast<std::uint32_t>(hi) << 16);
        return true;
    }
    bool u64(std::uint64_t& v)
    {
        std::uint32_t lo = 0, hi = 0;
        if (!u32(lo) || !u32(hi)) return false;
        v = static_cast<std::uint64_t>(lo) | (static_cast<std::uint64_t>(hi) << 32);
        return true;
    }
    bool i32(std::int32_t& v)
    {
        std::uint32_t u = 0;
        if (!u32(u)) return false;
        v = std::bit_cast<std::int32_t>(u);
        return true;
    }
    bool f32(float& v)
    {
        std::uint32_t u = 0;
        if (!u32(u)) return false;
        v = std::bit_cast<float>(u);
        return true;
    }
    bool str(std::string& s)
    {
        std::uint8_t size = 0;
        if (!u8(size)) return false;
        if (size > bytes_.size() - offset_) return fail();
        s.assign(reinterpret_cast<const char*>(bytes_.data() + offset_), size);
        offset_ += size;
        return true;
    }
    bool atEnd() const { return offset_ == bytes_.size(); }
    bool truncated() const { return truncated_; }

private:
    bool fail()
    {
        truncated_ = true;
        return false;
    }
    std::span<const std::uint8_t> bytes_;
    std::size_t offset_ = 0;
    bool truncated_ = false;
};

struct Header {
    DecodeError error = DecodeError::None;
    std::uint8_t type = 0;
};

Header readHeader(Reader& r, std::span<const std::uint8_t> bytes)
{
    if (bytes.size() > kMaxMessageBytes) return {DecodeError::TooLarge, 0};
    std::uint16_t magic = 0;
    std::uint8_t version = 0, type = 0;
    if (!r.u16(magic) || !r.u8(version) || !r.u8(type)) return {DecodeError::Truncated, 0};
    if (magic != kMagic) return {DecodeError::BadMagic, 0};
    if (version != kProtocolVersion) return {DecodeError::BadVersion, 0};
    return {DecodeError::None, type};
}

template <class M>
Decoded<M> fail(DecodeError error)
{
    return {std::nullopt, error};
}

template <class M, class T>
Decoded<M> finish(Reader& r, bool ok, T&& value)
{
    if (!ok) return fail<M>(r.truncated() ? DecodeError::Truncated : DecodeError::BadField);
    if (!r.atEnd()) return fail<M>(DecodeError::Trailing);
    M message = std::forward<T>(value);
    if (!isValid(message)) return fail<M>(DecodeError::BadField);
    return {std::move(message), DecodeError::None};
}

bool readRoster(Reader& r, std::vector<RosterEntry>& roster)
{
    std::uint8_t count = 0;
    if (!r.u8(count) || count > kMaxPlayers) return false;
    roster.resize(count);
    for (RosterEntry& e : roster) {
        if (!r.u8(e.id) || !r.str(e.name)) return false;
    }
    return true;
}

void writeRoster(Writer& w, const std::vector<RosterEntry>& roster)
{
    w.u8(static_cast<std::uint8_t>(roster.size()));
    for (const RosterEntry& e : roster) {
        w.u8(e.id);
        w.str(e.name);
    }
}

}

bool isValidName(const std::string& name)
{
    if (name.empty() || name.size() > kMaxNameLength) return false;
    if (name.front() == ' ' || name.back() == ' ' || name.find("  ") != std::string::npos) return false;
    return std::all_of(name.begin(), name.end(), isNameChar);
}

bool isValidItemId(const std::string& id)
{
    return !id.empty() && id.size() <= kMaxItemIdLength && std::all_of(id.begin(), id.end(), isItemChar);
}

bool isValidInventory(const std::vector<InventoryEntry>& inventory)
{
    if (inventory.size() > kMaxInventoryEntries) return false;
    std::vector<const std::string*> ids;
    ids.reserve(inventory.size());
    for (const InventoryEntry& e : inventory) {
        if (!isValidItemId(e.itemId) || e.count == 0 || e.count > kMaxItemCount) return false;
        ids.push_back(&e.itemId);
    }
    std::sort(ids.begin(), ids.end(), [](const std::string* a, const std::string* b) { return *a < *b; });
    return std::adjacent_find(ids.begin(), ids.end(), [](const std::string* a, const std::string* b) { return *a == *b; }) == ids.end();
}

bool isValid(const ClientMessage& message)
{
    struct Visitor {
        bool operator()(const HelloMsg& m) const { return isValidName(m.name) && isValidInventory(m.inventory); }
        bool operator()(const LeaveMsg&) const { return true; }
        bool operator()(const InputMsg& m) const
        {
            return inRange(m.moveX, 1.0f) && inRange(m.moveY, 1.0f) && m.moveX * m.moveX + m.moveY * m.moveY <= 1.001f && inRange(m.facing, 6.5f);
        }
        bool operator()(const CommandMsg& m) const { return validCommand(m); }
        bool operator()(const PingMsg&) const { return true; }
    };
    return std::visit(Visitor{}, message);
}

bool isValid(const HostMessage& message)
{
    struct Visitor {
        bool operator()(const WelcomeMsg& m) const { return m.playerId < kMaxPlayers && validRoster(m.roster); }
        bool operator()(const CloseMsg& m) const { return validClose(m.reason); }
        bool operator()(const PlayerJoinedMsg& m) const { return m.player.id < kMaxPlayers && isValidName(m.player.name); }
        bool operator()(const PlayerLeftMsg& m) const { return m.id < kMaxPlayers; }
        bool operator()(const WorldDeltaMsg& m) const { return m.revision != 0; }
        bool operator()(const PongMsg&) const { return true; }
    };
    return std::visit(Visitor{}, message);
}

std::optional<std::vector<std::uint8_t>> encodeClient(const ClientMessage& message)
{
    if (!isValid(message)) return std::nullopt;
    struct Visitor {
        std::vector<std::uint8_t> operator()(const HelloMsg& m) const
        {
            Writer w(MessageType::Hello);
            w.u64(m.worldSeed);
            w.u32(m.genVersion);
            w.str(m.name);
            w.u8(static_cast<std::uint8_t>(m.inventory.size()));
            for (const InventoryEntry& e : m.inventory) {
                w.str(e.itemId);
                w.u16(e.count);
            }
            return w.take();
        }
        std::vector<std::uint8_t> operator()(const LeaveMsg&) const { return Writer(MessageType::Leave).take(); }
        std::vector<std::uint8_t> operator()(const InputMsg& m) const
        {
            Writer w(MessageType::Input);
            w.u32(m.tick);
            w.f32(m.moveX);
            w.f32(m.moveY);
            w.f32(m.facing);
            return w.take();
        }
        std::vector<std::uint8_t> operator()(const CommandMsg& m) const
        {
            Writer w(MessageType::Command);
            w.u8(static_cast<std::uint8_t>(m.kind));
            w.u32(m.tick);
            w.u64(m.target);
            w.i32(m.arg);
            return w.take();
        }
        std::vector<std::uint8_t> operator()(const PingMsg& m) const
        {
            Writer w(MessageType::Ping);
            w.u32(m.token);
            return w.take();
        }
    };
    return capped(std::visit(Visitor{}, message));
}

std::optional<std::vector<std::uint8_t>> encodeHost(const HostMessage& message)
{
    if (!isValid(message)) return std::nullopt;
    struct Visitor {
        std::vector<std::uint8_t> operator()(const WelcomeMsg& m) const
        {
            Writer w(MessageType::Welcome);
            w.u8(m.playerId);
            w.u64(m.worldSeed);
            w.u32(m.genVersion);
            w.u32(m.revision);
            w.u32(m.hostTick);
            writeRoster(w, m.roster);
            return w.take();
        }
        std::vector<std::uint8_t> operator()(const CloseMsg& m) const
        {
            Writer w(MessageType::Close);
            w.u8(static_cast<std::uint8_t>(m.reason));
            return w.take();
        }
        std::vector<std::uint8_t> operator()(const PlayerJoinedMsg& m) const
        {
            Writer w(MessageType::PlayerJoined);
            w.u8(m.player.id);
            w.str(m.player.name);
            return w.take();
        }
        std::vector<std::uint8_t> operator()(const PlayerLeftMsg& m) const
        {
            Writer w(MessageType::PlayerLeft);
            w.u8(m.id);
            return w.take();
        }
        std::vector<std::uint8_t> operator()(const WorldDeltaMsg& m) const
        {
            Writer w(MessageType::WorldDelta);
            w.u32(m.revision);
            w.u64(m.entity);
            w.u16(m.kind);
            w.i32(m.value);
            return w.take();
        }
        std::vector<std::uint8_t> operator()(const PongMsg& m) const
        {
            Writer w(MessageType::Pong);
            w.u32(m.token);
            w.u32(m.hostTick);
            return w.take();
        }
    };
    return capped(std::visit(Visitor{}, message));
}

Decoded<ClientMessage> decodeClient(std::span<const std::uint8_t> bytes)
{
    Reader r(bytes);
    const Header h = readHeader(r, bytes);
    if (h.error != DecodeError::None) return fail<ClientMessage>(h.error);
    switch (static_cast<MessageType>(h.type)) {
    case MessageType::Hello: {
        HelloMsg m;
        std::uint8_t count = 0;
        bool ok = r.u64(m.worldSeed) && r.u32(m.genVersion) && r.str(m.name) && r.u8(count) && count <= kMaxInventoryEntries;
        if (ok) {
            m.inventory.resize(count);
            for (InventoryEntry& e : m.inventory) {
                ok = ok && r.str(e.itemId) && r.u16(e.count);
            }
        }
        return finish<ClientMessage>(r, ok, std::move(m));
    }
    case MessageType::Leave:
        return finish<ClientMessage>(r, true, LeaveMsg{});
    case MessageType::Input: {
        InputMsg m;
        const bool ok = r.u32(m.tick) && r.f32(m.moveX) && r.f32(m.moveY) && r.f32(m.facing);
        return finish<ClientMessage>(r, ok, m);
    }
    case MessageType::Command: {
        CommandMsg m;
        std::uint8_t kind = 0;
        const bool ok = r.u8(kind) && r.u32(m.tick) && r.u64(m.target) && r.i32(m.arg);
        m.kind = static_cast<CommandKind>(kind);
        return finish<ClientMessage>(r, ok, m);
    }
    case MessageType::Ping: {
        PingMsg m;
        const bool ok = r.u32(m.token);
        return finish<ClientMessage>(r, ok, m);
    }
    default:
        return fail<ClientMessage>(DecodeError::BadType);
    }
}

Decoded<HostMessage> decodeHost(std::span<const std::uint8_t> bytes)
{
    Reader r(bytes);
    const Header h = readHeader(r, bytes);
    if (h.error != DecodeError::None) return fail<HostMessage>(h.error);
    switch (static_cast<MessageType>(h.type)) {
    case MessageType::Welcome: {
        WelcomeMsg m;
        const bool ok = r.u8(m.playerId) && r.u64(m.worldSeed) && r.u32(m.genVersion) && r.u32(m.revision) && r.u32(m.hostTick)
            && readRoster(r, m.roster);
        return finish<HostMessage>(r, ok, std::move(m));
    }
    case MessageType::Close: {
        CloseMsg m;
        std::uint8_t reason = 0;
        const bool ok = r.u8(reason);
        m.reason = static_cast<CloseReason>(reason);
        return finish<HostMessage>(r, ok, m);
    }
    case MessageType::PlayerJoined: {
        PlayerJoinedMsg m;
        const bool ok = r.u8(m.player.id) && r.str(m.player.name);
        return finish<HostMessage>(r, ok, std::move(m));
    }
    case MessageType::PlayerLeft: {
        PlayerLeftMsg m;
        const bool ok = r.u8(m.id);
        return finish<HostMessage>(r, ok, m);
    }
    case MessageType::WorldDelta: {
        WorldDeltaMsg m;
        const bool ok = r.u32(m.revision) && r.u64(m.entity) && r.u16(m.kind) && r.i32(m.value);
        return finish<HostMessage>(r, ok, m);
    }
    case MessageType::Pong: {
        PongMsg m;
        const bool ok = r.u32(m.token) && r.u32(m.hostTick);
        return finish<HostMessage>(r, ok, m);
    }
    default:
        return fail<HostMessage>(DecodeError::BadType);
    }
}

}
