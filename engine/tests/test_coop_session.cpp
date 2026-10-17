#include "ichor/net/coop_session.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

using namespace ichor::net;

namespace {

constexpr std::uint64_t kSeed = 777;
constexpr std::uint32_t kGen = 2;

HostSession makeHost(SessionLimits limits = {})
{
    return HostSession(HostConfig{.worldSeed = kSeed, .genVersion = kGen, .hostName = "Host"}, limits);
}

HelloMsg hello(const std::string& name, std::vector<InventoryEntry> inventory = {{"torch", 2}, {"rope", 1}})
{
    return HelloMsg{.worldSeed = kSeed, .genVersion = kGen, .name = name, .inventory = std::move(inventory)};
}

std::vector<std::uint8_t> client(const ClientMessage& m)
{
    const auto bytes = encodeClient(m);
    REQUIRE(bytes.has_value());
    return *bytes;
}

std::vector<HostMessage> decodeSends(const HostOutput& out, ConnId to)
{
    std::vector<HostMessage> messages;
    for (const Outbound& s : out.sends) {
        if (s.to != to) continue;
        const auto decoded = decodeHost(s.bytes);
        REQUIRE(decoded.message.has_value());
        messages.push_back(*decoded.message);
    }
    return messages;
}

template <class M>
const M* firstOf(const std::vector<HostMessage>& messages)
{
    for (const HostMessage& m : messages) {
        if (const auto* p = std::get_if<M>(&m)) return p;
    }
    return nullptr;
}

bool dropped(const HostOutput& out, ConnId conn)
{
    return std::find(out.drops.begin(), out.drops.end(), conn) != out.drops.end();
}

PlayerId join(HostSession& host, ConnId conn, const std::string& name, Tick now = 0, std::vector<InventoryEntry> inv = {{"torch", 2}, {"rope", 1}})
{
    host.onConnect(conn, now);
    const HostOutput out = host.onMessage(conn, client(hello(name, std::move(inv))), now);
    const auto messages = decodeSends(out, conn);
    const auto* welcome = firstOf<WelcomeMsg>(messages);
    REQUIRE(welcome != nullptr);
    return welcome->playerId;
}

std::uint32_t lcg(std::uint32_t& s)
{
    s = s * 1664525u + 1013904223u;
    return s >> 8;
}

}

TEST_CASE("a fresh host is a one player listen server", "[coop_session]")
{
    HostSession host = makeHost();
    CHECK(host.playerCount() == 1);
    REQUIRE(host.player(0) != nullptr);
    CHECK(host.player(0)->isHost);
    CHECK(host.player(0)->name == "Host");
    CHECK(host.player(1) == nullptr);
    CHECK(host.player(200) == nullptr);
}

TEST_CASE("a guest joins, gets a welcome with the roster, and the others hear about it", "[coop_session]")
{
    HostSession host = makeHost();
    const PlayerId ada = join(host, 10, "Ada");
    CHECK(ada == 1);

    host.onConnect(11, 5);
    const HostOutput out = host.onMessage(11, client(hello("Bo")), 5);
    const auto welcome = decodeSends(out, 11);
    REQUIRE(firstOf<WelcomeMsg>(welcome) != nullptr);
    const WelcomeMsg& w = *firstOf<WelcomeMsg>(welcome);
    CHECK(w.playerId == 2);
    CHECK(w.worldSeed == kSeed);
    CHECK(w.genVersion == kGen);
    REQUIRE(w.roster.size() == 3);
    CHECK(w.roster[0].name == "Host");
    CHECK(w.roster[1].name == "Ada");
    CHECK(w.roster[2].name == "Bo");

    const auto toAda = decodeSends(out, 10);
    REQUIRE(firstOf<PlayerJoinedMsg>(toAda) != nullptr);
    CHECK(firstOf<PlayerJoinedMsg>(toAda)->player.name == "Bo");
    CHECK(decodeSends(out, 11).size() == 1);

    REQUIRE(out.events.size() == 1);
    CHECK(out.events[0].kind == SessionEventKind::PlayerJoined);
    CHECK(out.events[0].player == 2);
    CHECK(host.playerCount() == 3);
    CHECK(host.player(2)->inventory == hello("Bo").inventory);
}

TEST_CASE("a join is refused for the wrong world, a taken name, a full host, or a bad version", "[coop_session]")
{
    HostSession host = makeHost();

    host.onConnect(1, 0);
    HelloMsg wrongSeed = hello("Ada");
    wrongSeed.worldSeed = kSeed + 1;
    HostOutput out = host.onMessage(1, client(wrongSeed), 0);
    CHECK(firstOf<CloseMsg>(decodeSends(out, 1))->reason == CloseReason::WorldMismatch);
    CHECK(dropped(out, 1));

    host.onConnect(2, 0);
    HelloMsg wrongGen = hello("Ada");
    wrongGen.genVersion = kGen + 1;
    out = host.onMessage(2, client(wrongGen), 0);
    CHECK(firstOf<CloseMsg>(decodeSends(out, 2))->reason == CloseReason::WorldMismatch);

    join(host, 3, "Ada");
    host.onConnect(10, 0);
    out = host.onMessage(10, client(hello("aDA")), 0);
    CHECK(firstOf<CloseMsg>(decodeSends(out, 10))->reason == CloseReason::NameTaken);
    host.onConnect(11, 0);
    out = host.onMessage(11, client(hello("HOST")), 0);
    CHECK(firstOf<CloseMsg>(decodeSends(out, 11))->reason == CloseReason::NameTaken);
    host.onConnect(4, 0);
    out = host.onMessage(4, client(hello("Ada")), 0);
    CHECK(firstOf<CloseMsg>(decodeSends(out, 4))->reason == CloseReason::NameTaken);
    host.onConnect(5, 0);
    out = host.onMessage(5, client(hello("Host")), 0);
    CHECK(firstOf<CloseMsg>(decodeSends(out, 5))->reason == CloseReason::NameTaken);

    join(host, 6, "Bo");
    join(host, 7, "Cy");
    CHECK(host.playerCount() == kMaxPlayers);
    host.onConnect(8, 0);
    out = host.onMessage(8, client(hello("Di")), 0);
    CHECK(firstOf<CloseMsg>(decodeSends(out, 8))->reason == CloseReason::ServerFull);
    CHECK(host.playerCount() == kMaxPlayers);
    CHECK(host.pendingCount() == 0);

    host.onConnect(9, 0);
    auto bytes = client(hello("Ed"));
    bytes[2] = kProtocolVersion + 1;
    out = host.onMessage(9, bytes, 0);
    CHECK(firstOf<CloseMsg>(decodeSends(out, 9))->reason == CloseReason::VersionMismatch);
}

TEST_CASE("a freed slot is reused by the next guest", "[coop_session]")
{
    HostSession host = makeHost();
    join(host, 1, "Ada");
    join(host, 2, "Bo");
    host.onDisconnect(1, 10);
    CHECK(host.player(1) == nullptr);
    CHECK(join(host, 3, "Cy", 11) == 1);
}

TEST_CASE("leaving tells the others and frees the slot", "[coop_session]")
{
    HostSession host = makeHost();
    join(host, 1, "Ada");
    join(host, 2, "Bo");

    const HostOutput out = host.onMessage(1, client(LeaveMsg{}), 3);
    CHECK(dropped(out, 1));
    CHECK(host.playerCount() == 2);
    REQUIRE(out.events.size() == 1);
    CHECK(out.events[0].kind == SessionEventKind::PlayerLeft);
    CHECK(out.events[0].player == 1);
    const auto toBo = decodeSends(out, 2);
    REQUIRE(firstOf<PlayerLeftMsg>(toBo) != nullptr);
    CHECK(firstOf<PlayerLeftMsg>(toBo)->id == 1);
    CHECK(decodeSends(out, 1).empty());

    const HostOutput gone = host.onMessage(1, client(PingMsg{1}), 4);
    CHECK(dropped(gone, 1));
    CHECK(gone.sends.empty());
}

TEST_CASE("a transport disconnect behaves like a leave but asks for no drop", "[coop_session]")
{
    HostSession host = makeHost();
    join(host, 1, "Ada");
    join(host, 2, "Bo");
    const HostOutput out = host.onDisconnect(1, 5);
    CHECK(out.drops.empty());
    CHECK(host.playerCount() == 2);
    CHECK(firstOf<PlayerLeftMsg>(decodeSends(out, 2)) != nullptr);
    CHECK(host.onDisconnect(1, 6).events.empty());
    CHECK(host.onDisconnect(99, 6).events.empty());
}

TEST_CASE("only a hello is accepted from a connection that has not joined", "[coop_session]")
{
    HostSession host = makeHost();
    for (const ClientMessage& m : std::vector<ClientMessage>{InputMsg{}, CommandMsg{}, PingMsg{1}}) {
        host.onConnect(1, 0);
        const HostOutput out = host.onMessage(1, client(m), 0);
        CHECK(dropped(out, 1));
        CHECK(firstOf<CloseMsg>(decodeSends(out, 1))->reason == CloseReason::ProtocolViolation);
        CHECK(out.commands.empty());
        CHECK(host.playerCount() == 1);
    }
    host.onConnect(2, 0);
    CHECK(host.onMessage(2, client(LeaveMsg{}), 0).drops.size() == 1);
}

TEST_CASE("garbage from a connection that has not joined drops it at once", "[coop_session]")
{
    HostSession host = makeHost();
    host.onConnect(1, 0);
    const HostOutput out = host.onMessage(1, std::vector<std::uint8_t>{1, 2, 3, 4, 5}, 0);
    CHECK(dropped(out, 1));
    CHECK(firstOf<CloseMsg>(decodeSends(out, 1))->reason == CloseReason::Malformed);
    CHECK(host.pendingCount() == 0);
}

TEST_CASE("a second hello from an active guest is a strike, not a rejoin", "[coop_session]")
{
    HostSession host = makeHost();
    join(host, 1, "Ada");
    const HostOutput out = host.onMessage(1, client(hello("Evil")), 1);
    CHECK_FALSE(dropped(out, 1));
    CHECK(host.player(1)->name == "Ada");
    CHECK(host.playerCount() == 2);
}

TEST_CASE("repeated garbage kicks an active guest and frees the slot", "[coop_session]")
{
    SessionLimits limits;
    limits.maxStrikes = 3;
    HostSession host = makeHost(limits);
    join(host, 1, "Ada");
    join(host, 2, "Bo");
    const std::vector<std::uint8_t> junk = {0, 0, 0};
    CHECK_FALSE(dropped(host.onMessage(1, junk, 1), 1));
    CHECK_FALSE(dropped(host.onMessage(1, junk, 1), 1));
    const HostOutput out = host.onMessage(1, junk, 1);
    CHECK(dropped(out, 1));
    CHECK(firstOf<CloseMsg>(decodeSends(out, 1))->reason == CloseReason::Malformed);
    CHECK(host.player(1) == nullptr);
    CHECK(firstOf<PlayerLeftMsg>(decodeSends(out, 2)) != nullptr);
}

TEST_CASE("a flood is rate limited and the guest is eventually kicked", "[coop_session]")
{
    SessionLimits limits;
    limits.maxMessagesPerWindow = 10;
    limits.rateWindowTicks = 60;
    limits.maxStrikes = 4;
    HostSession host = makeHost(limits);
    join(host, 1, "Ada");

    const auto ping = client(PingMsg{1});
    std::size_t pongs = 0;
    bool kicked = false;
    for (int i = 0; i < 40 && !kicked; ++i) {
        const HostOutput out = host.onMessage(1, ping, 1);
        for (const HostMessage& m : decodeSends(out, 1)) {
            if (std::holds_alternative<PongMsg>(m)) ++pongs;
        }
        kicked = dropped(out, 1);
    }
    CHECK(kicked);
    CHECK(pongs < 12);
    CHECK(host.player(1) == nullptr);
}

TEST_CASE("the rate window resets so a normal guest is never limited", "[coop_session]")
{
    SessionLimits limits;
    limits.maxMessagesPerWindow = 10;
    limits.rateWindowTicks = 60;
    HostSession host = makeHost(limits);
    join(host, 1, "Ada", 0);
    const auto ping = client(PingMsg{1});
    for (Tick now = 1; now < 2000; now += 60) {
        for (int i = 0; i < 5; ++i) CHECK_FALSE(dropped(host.onMessage(1, ping, now), 1));
    }
    CHECK(host.player(1) != nullptr);
}

TEST_CASE("a pending connection that never says hello times out", "[coop_session]")
{
    SessionLimits limits;
    limits.helloTimeoutTicks = 100;
    HostSession host = makeHost(limits);
    host.onConnect(1, 0);
    CHECK(host.advance(100).drops.empty());
    const HostOutput out = host.advance(101);
    CHECK(dropped(out, 1));
    CHECK(host.pendingCount() == 0);
}

TEST_CASE("a silent guest times out but a pinging guest stays", "[coop_session]")
{
    SessionLimits limits;
    limits.idleTimeoutTicks = 100;
    HostSession host = makeHost(limits);
    join(host, 1, "Ada", 0);
    join(host, 2, "Bo", 0);
    for (Tick now = 50; now <= 400; now += 50) {
        host.onMessage(2, client(PingMsg{1}), now);
        host.advance(now);
    }
    CHECK(host.player(1) == nullptr);
    CHECK(host.player(2) != nullptr);
}

TEST_CASE("pending connections are capped", "[coop_session]")
{
    SessionLimits limits;
    limits.maxPending = 3;
    HostSession host = makeHost(limits);
    for (ConnId c = 1; c <= 3; ++c) CHECK(host.onConnect(c, 0).drops.empty());
    const HostOutput out = host.onConnect(4, 0);
    CHECK(dropped(out, 4));
    CHECK(host.pendingCount() == 3);
    CHECK(host.onConnect(2, 0).sends.empty());
    CHECK(host.pendingCount() == 3);
}

TEST_CASE("time never runs backwards inside the session", "[coop_session]")
{
    HostSession host = makeHost();
    host.advance(500);
    host.advance(10);
    CHECK(host.hostTick() == 500);
    join(host, 1, "Ada", 20);
    CHECK(host.hostTick() == 500);
}

TEST_CASE("a message from an unknown connection is dropped without effect", "[coop_session]")
{
    HostSession host = makeHost();
    const HostOutput out = host.onMessage(42, client(hello("Ada")), 0);
    CHECK(dropped(out, 42));
    CHECK(host.playerCount() == 1);
}

TEST_CASE("a command from a guest is passed up, never applied", "[coop_session]")
{
    HostSession host = makeHost();
    join(host, 1, "Ada");
    host.advance(100);
    const CommandMsg attack{CommandKind::Attack, 100, 555, 0};
    const HostOutput out = host.onMessage(1, client(attack), 100);
    REQUIRE(out.commands.size() == 1);
    CHECK(out.commands[0].player == 1);
    CHECK(out.commands[0].command == attack);
    CHECK(host.revision() == 0);
    CHECK(out.sends.empty());
}

TEST_CASE("commands are checked against the host's tick and the guest's declared inventory", "[coop_session]")
{
    SessionLimits limits;
    limits.maxFutureTicks = 10;
    limits.maxPastTicks = 50;
    limits.maxStrikes = 100;
    HostSession host = makeHost(limits);
    join(host, 1, "Ada", 0, {{"torch", 2}, {"rope", 1}});
    host.advance(1000);

    CHECK(host.onMessage(1, client(CommandMsg{CommandKind::Interact, 1005, 1, 0}), 1000).commands.size() == 1);
    CHECK(host.onMessage(1, client(CommandMsg{CommandKind::Interact, 1011, 1, 0}), 1000).commands.empty());
    CHECK(host.onMessage(1, client(CommandMsg{CommandKind::Interact, 949, 1, 0}), 1000).commands.empty());
    CHECK(host.onMessage(1, client(CommandMsg{CommandKind::Interact, 950, 1, 0}), 1000).commands.size() == 1);
    CHECK(host.onMessage(1, client(CommandMsg{CommandKind::Interact, std::numeric_limits<std::uint32_t>::max(), 1, 0}), 1000).commands.empty());

    CHECK(host.onMessage(1, client(CommandMsg{CommandKind::UseItem, 1000, 0, 1}), 1000).commands.size() == 1);
    CHECK(host.onMessage(1, client(CommandMsg{CommandKind::UseItem, 1000, 0, 2}), 1000).commands.empty());
    CHECK(host.onMessage(1, client(CommandMsg{CommandKind::UseItem, 1000, 0, 31}), 1000).commands.empty());
}

TEST_CASE("a guest that spams commands from the future is kicked", "[coop_session]")
{
    SessionLimits limits;
    limits.maxStrikes = 3;
    HostSession host = makeHost(limits);
    join(host, 1, "Ada");
    bool kicked = false;
    for (int i = 0; i < 3; ++i) kicked = dropped(host.onMessage(1, client(CommandMsg{CommandKind::Attack, 100000, 1, 0}), 0), 1);
    CHECK(kicked);
}

TEST_CASE("input keeps only the newest tick", "[coop_session]")
{
    HostSession host = makeHost();
    join(host, 1, "Ada");
    host.advance(100);
    host.onMessage(1, client(InputMsg{100, 1.0f, 0, 0}), 100);
    host.onMessage(1, client(InputMsg{99, -1.0f, 0, 0}), 100);
    host.onMessage(1, client(InputMsg{100, 0.5f, 0, 0}), 100);
    REQUIRE(host.player(1)->lastInput.has_value());
    CHECK(host.player(1)->lastInput->moveX == 1.0f);
    host.onMessage(1, client(InputMsg{101, 0.25f, 0, 0}), 100);
    CHECK(host.player(1)->lastInput->moveX == 0.25f);
    CHECK_FALSE(host.player(0)->lastInput.has_value());
}

TEST_CASE("a guest can never change what the host holds about another player", "[coop_session]")
{
    HostSession host = makeHost();
    join(host, 1, "Ada", 0, {{"torch", 1}});
    join(host, 2, "Bo", 0, {{"rope", 1}});
    host.onMessage(1, client(InputMsg{0, 0.7f, 0.7f, 1}), 0);
    host.onMessage(1, client(CommandMsg{CommandKind::UseItem, 0, 2, 0}), 0);
    CHECK(host.player(2)->inventory == std::vector<InventoryEntry>{{"rope", 1}});
    CHECK_FALSE(host.player(2)->lastInput.has_value());
    CHECK(host.player(0)->inventory.empty());
}

TEST_CASE("a published delta reaches only active guests, in revision order", "[coop_session]")
{
    HostSession host = makeHost();
    join(host, 1, "Ada");
    host.onConnect(2, 0);
    const HostOutput first = host.publishDelta(7, 1, 5);
    const HostOutput second = host.publishDelta(8, 2, -5);
    CHECK(host.revision() == 2);
    CHECK(first.sends.size() == 1);
    CHECK(decodeSends(first, 2).empty());
    CHECK(firstOf<WorldDeltaMsg>(decodeSends(first, 1))->revision == 1);
    CHECK(firstOf<WorldDeltaMsg>(decodeSends(second, 1))->revision == 2);
    CHECK(firstOf<WorldDeltaMsg>(decodeSends(second, 1))->value == -5);
}

TEST_CASE("a late joiner is told the current revision", "[coop_session]")
{
    HostSession host = makeHost();
    host.publishDelta(1, 1, 1);
    host.publishDelta(2, 1, 1);
    host.onConnect(1, 0);
    const auto welcome = decodeSends(host.onMessage(1, client(hello("Ada")), 0), 1);
    CHECK(firstOf<WelcomeMsg>(welcome)->revision == 2);
}

TEST_CASE("closing the host closes every connection", "[coop_session]")
{
    HostSession host = makeHost();
    join(host, 1, "Ada");
    host.onConnect(2, 0);
    const HostOutput out = host.closeAll(CloseReason::HostClosed);
    CHECK(dropped(out, 1));
    CHECK(dropped(out, 2));
    CHECK(firstOf<CloseMsg>(decodeSends(out, 1))->reason == CloseReason::HostClosed);
    CHECK(host.playerCount() == 1);
}

TEST_CASE("fuzzed traffic from many connections keeps the host consistent", "[coop_session]")
{
    SessionLimits limits;
    limits.maxStrikes = 3;
    limits.maxMessagesPerWindow = 30;
    HostSession host = makeHost(limits);
    std::uint32_t seed = 99;
    const std::vector<std::vector<std::uint8_t>> corpus = {
        client(hello("Ada")), client(hello("Bo")), client(hello("Cy")), client(hello("Di")), client(hello("Ed")),
        client(InputMsg{3, 0.5f, 0.5f, 1.0f}), client(CommandMsg{CommandKind::UseItem, 3, 1, 1}), client(PingMsg{1}), client(LeaveMsg{}),
    };
    Tick now = 0;
    for (int i = 0; i < 30000; ++i) {
        now += lcg(seed) % 4;
        const ConnId conn = 1 + lcg(seed) % 10;
        switch (lcg(seed) % 6) {
        case 0:
            host.onConnect(conn, now);
            break;
        case 1:
            host.onDisconnect(conn, now);
            break;
        case 2:
            host.advance(now);
            break;
        default: {
            std::vector<std::uint8_t> bytes = corpus[lcg(seed) % corpus.size()];
            if (lcg(seed) % 3 == 0) bytes[lcg(seed) % bytes.size()] ^= static_cast<std::uint8_t>(1u << (lcg(seed) % 8));
            if (lcg(seed) % 11 == 0) bytes.resize(lcg(seed) % 40);
            const HostOutput out = host.onMessage(conn, bytes, now);
            for (const AcceptedCommand& c : out.commands) {
                REQUIRE(host.player(c.player) != nullptr);
                CHECK(c.player != 0);
            }
            for (const Outbound& s : out.sends) CHECK(decodeHost(s.bytes).message.has_value());
        }
        }
        REQUIRE(host.playerCount() >= 1);
        REQUIRE(host.playerCount() <= kMaxPlayers);
        REQUIRE(host.pendingCount() <= limits.maxPending);
        REQUIRE(host.player(0) != nullptr);
    }
}

namespace {

struct Pair {
    HostSession host = makeHost();
    ClientSession guest;
    ConnId conn = 5;
    Tick now = 0;

    Pair() { host.onConnect(conn, 0); }

    void startGuest(const HelloMsg& h)
    {
        const auto bytes = guest.begin(h, now);
        REQUIRE(bytes.has_value());
        deliverToGuest(host.onMessage(conn, *bytes, now));
    }

    void deliverToGuest(const HostOutput& out)
    {
        for (const Outbound& s : out.sends) {
            if (s.to == conn) lastClient = guest.onMessage(s.bytes, now);
        }
    }

    ClientOutput lastClient;
};

}

TEST_CASE("a guest and a host complete the handshake and stay in step", "[coop_session]")
{
    Pair p;
    CHECK(p.guest.state() == ClientState::Idle);
    p.startGuest(hello("Ada"));
    CHECK(p.guest.state() == ClientState::Active);
    CHECK(p.guest.playerId() == 1);
    CHECK(p.guest.roster().size() == 2);

    p.deliverToGuest(p.host.publishDelta(9, 4, 12));
    CHECK(p.lastClient.deltas.size() == 1);
    CHECK(p.guest.revision() == 1);

    const auto input = p.guest.makeInput(InputMsg{1, 0.5f, 0, 0});
    REQUIRE(input.has_value());
    p.host.onMessage(p.conn, *input, 1);
    CHECK(p.host.player(1)->lastInput.has_value());

    const auto command = p.guest.makeCommand(CommandMsg{CommandKind::Interact, 0, 4, 0});
    REQUIRE(command.has_value());
    CHECK(p.host.onMessage(p.conn, *command, 1).commands.size() == 1);

    const auto ping = p.guest.makePing(77);
    REQUIRE(ping.has_value());
    p.host.advance(500);
    p.deliverToGuest(p.host.onMessage(p.conn, *ping, 500));
    CHECK(p.guest.state() == ClientState::Active);
    CHECK(p.guest.hostTick() == 500);
}

TEST_CASE("a guest sees other players come and go and leaves cleanly", "[coop_session]")
{
    Pair p;
    p.startGuest(hello("Ada"));
    const HostOutput joined = [&] {
        p.host.onConnect(9, 0);
        return p.host.onMessage(9, client(hello("Bo")), 0);
    }();
    p.deliverToGuest(joined);
    CHECK(p.guest.roster().size() == 3);
    REQUIRE(p.lastClient.events.size() == 1);
    CHECK(p.lastClient.events[0].kind == SessionEventKind::PlayerJoined);

    p.deliverToGuest(p.host.onDisconnect(9, 1));
    CHECK(p.guest.roster().size() == 2);

    const auto bytes = p.guest.leave();
    REQUIRE(bytes.has_value());
    CHECK(p.guest.state() == ClientState::Closed);
    CHECK(p.guest.closeReason() == CloseReason::Left);
    CHECK(p.host.onMessage(p.conn, *bytes, 2).drops.size() == 1);
    CHECK(p.host.playerCount() == 1);
    CHECK_FALSE(p.guest.makeInput(InputMsg{}).has_value());
}

TEST_CASE("a guest cannot send before the host welcomed it", "[coop_session]")
{
    ClientSession guest;
    CHECK_FALSE(guest.makeInput(InputMsg{}).has_value());
    CHECK_FALSE(guest.makeCommand(CommandMsg{}).has_value());
    CHECK_FALSE(guest.makePing(1).has_value());
    CHECK_FALSE(guest.leave().has_value());
    HelloMsg bad = hello("Ada");
    bad.name = "";
    CHECK_FALSE(guest.begin(bad, 0).has_value());
    CHECK(guest.state() == ClientState::Idle);
    CHECK(guest.begin(hello("Ada"), 0).has_value());
    CHECK_FALSE(guest.begin(hello("Ada"), 0).has_value());
}

TEST_CASE("a guest refuses a delta log with a gap, a repeat, or a revision from the past", "[coop_session]")
{
    for (const std::uint32_t revision : {2u, 0u, 5u, 1u}) {
        Pair p;
        p.startGuest(hello("Ada"));
        p.deliverToGuest(p.host.publishDelta(1, 1, 1));
        REQUIRE(p.guest.revision() == 1);
        const auto bytes = encodeHost(WorldDeltaMsg{revision == 0 ? 1u : revision, 1, 1, 1});
        REQUIRE(bytes.has_value());
        const ClientOutput out = p.guest.onMessage(*bytes, 1);
        if (revision == 2) {
            CHECK(out.deltas.size() == 1);
            CHECK(p.guest.state() == ClientState::Active);
        } else {
            CHECK(out.deltas.empty());
            CHECK(p.guest.state() == ClientState::Closed);
            CHECK(p.guest.closeReason() == CloseReason::Desync);
        }
    }
}

TEST_CASE("a guest rejects a hostile host", "[coop_session]")
{
    const auto host = [](const HostMessage& m) { return *encodeHost(m); };
    const auto expectClosed = [&](ClientSession& g, const std::vector<std::uint8_t>& bytes, CloseReason reason) {
        g.onMessage(bytes, 1);
        CHECK(g.state() == ClientState::Closed);
        CHECK(g.closeReason() == reason);
    };

    const auto active = [] {
        Pair p;
        p.startGuest(hello("Ada"));
        return p;
    };

    {
        Pair p = active();
        expectClosed(p.guest, host(PlayerLeftMsg{p.guest.playerId()}), CloseReason::ProtocolViolation);
    }
    {
        Pair p = active();
        expectClosed(p.guest, host(PlayerLeftMsg{3}), CloseReason::ProtocolViolation);
    }
    {
        Pair p = active();
        expectClosed(p.guest, host(PlayerLeftMsg{0}), CloseReason::ProtocolViolation);
    }
    {
        Pair p = active();
        expectClosed(p.guest, host(PlayerJoinedMsg{{p.guest.playerId(), "Evil"}}), CloseReason::ProtocolViolation);
    }
    {
        Pair p = active();
        expectClosed(p.guest, host(PlayerJoinedMsg{{0, "Evil"}}), CloseReason::ProtocolViolation);
    }
    {
        Pair p = active();
        expectClosed(p.guest, host(WelcomeMsg{.playerId = 2, .worldSeed = kSeed, .genVersion = kGen, .roster = {{2, "Ada"}}}), CloseReason::ProtocolViolation);
    }
    {
        Pair p = active();
        expectClosed(p.guest, std::vector<std::uint8_t>{1, 2, 3}, CloseReason::Malformed);
    }
    {
        Pair p = active();
        expectClosed(p.guest, client(PingMsg{1}), CloseReason::Malformed);
    }
    {
        Pair p = active();
        expectClosed(p.guest, host(CloseMsg{CloseReason::HostClosed}), CloseReason::HostClosed);
    }
}

TEST_CASE("a guest rejects a welcome that is not for it", "[coop_session]")
{
    const auto attempt = [](const HostMessage& reply) {
        ClientSession g;
        REQUIRE(g.begin(hello("Ada"), 0).has_value());
        g.onMessage(*encodeHost(reply), 1);
        return g.state();
    };
    CHECK(attempt(WelcomeMsg{.playerId = 1, .worldSeed = kSeed, .genVersion = kGen, .roster = {{0, "Host"}, {1, "Ada"}}}) == ClientState::Active);
    CHECK(attempt(WelcomeMsg{.playerId = 1, .worldSeed = kSeed + 1, .genVersion = kGen, .roster = {{1, "Ada"}}}) == ClientState::Closed);
    CHECK(attempt(WelcomeMsg{.playerId = 1, .worldSeed = kSeed, .genVersion = kGen + 1, .roster = {{1, "Ada"}}}) == ClientState::Closed);
    CHECK(attempt(WelcomeMsg{.playerId = 0, .worldSeed = kSeed, .genVersion = kGen, .roster = {{0, "Ada"}}}) == ClientState::Closed);
    CHECK(attempt(WelcomeMsg{.playerId = 1, .worldSeed = kSeed, .genVersion = kGen, .roster = {{1, "Mallory"}}}) == ClientState::Closed);
    CHECK(attempt(WelcomeMsg{.playerId = 1, .worldSeed = kSeed, .genVersion = kGen, .roster = {{0, "Host"}}}) == ClientState::Closed);
    CHECK(attempt(WelcomeMsg{.playerId = 1, .worldSeed = kSeed, .genVersion = kGen, .roster = {{1, "Ada"}}}) == ClientState::Closed);
    CHECK(attempt(WorldDeltaMsg{1, 1, 1, 1}) == ClientState::Closed);
    CHECK(attempt(CloseMsg{CloseReason::ServerFull}) == ClientState::Closed);
}

TEST_CASE("a guest times out waiting for a welcome or a silent host", "[coop_session]")
{
    SessionLimits limits;
    limits.helloTimeoutTicks = 50;
    limits.idleTimeoutTicks = 200;
    ClientSession waiting(limits);
    REQUIRE(waiting.begin(hello("Ada"), 0).has_value());
    waiting.advance(50);
    CHECK(waiting.state() == ClientState::AwaitingWelcome);
    waiting.advance(51);
    CHECK(waiting.state() == ClientState::Closed);
    CHECK(waiting.closeReason() == CloseReason::Timeout);

    ClientSession active(limits);
    REQUIRE(active.begin(hello("Ada"), 0).has_value());
    active.onMessage(*encodeHost(WelcomeMsg{.playerId = 1, .worldSeed = kSeed, .genVersion = kGen, .roster = {{0, "Host"}, {1, "Ada"}}}), 10);
    REQUIRE(active.state() == ClientState::Active);
    active.advance(210);
    CHECK(active.state() == ClientState::Active);
    active.advance(211);
    CHECK(active.state() == ClientState::Closed);
}

TEST_CASE("a client survives fuzzed host traffic", "[coop_session]")
{
    std::uint32_t seed = 4242;
    const std::vector<std::vector<std::uint8_t>> corpus = {
        *encodeHost(WelcomeMsg{.playerId = 1, .worldSeed = kSeed, .genVersion = kGen, .roster = {{0, "Host"}, {1, "Ada"}}}),
        *encodeHost(PlayerJoinedMsg{{2, "Bo"}}), *encodeHost(PlayerLeftMsg{2}), *encodeHost(WorldDeltaMsg{1, 2, 3, 4}), *encodeHost(PongMsg{1}),
    };
    for (int round = 0; round < 500; ++round) {
        ClientSession g;
        REQUIRE(g.begin(hello("Ada"), 0).has_value());
        for (int i = 0; i < 40; ++i) {
            auto bytes = corpus[lcg(seed) % corpus.size()];
            if (lcg(seed) % 2 == 0) bytes[lcg(seed) % bytes.size()] ^= static_cast<std::uint8_t>(1u << (lcg(seed) % 8));
            g.onMessage(bytes, static_cast<Tick>(i));
            CHECK(g.roster().size() <= kMaxPlayers);
        }
    }
}
