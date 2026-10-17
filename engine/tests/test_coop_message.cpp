#include "ichor/net/coop_message.h"

#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

using namespace ichor::net;

namespace {

HelloMsg hello()
{
    return {.worldSeed = 0xDEADBEEFCAFEull, .genVersion = 3, .name = "Ada Lovelace", .inventory = {{"torch", 3}, {"rope.hemp", 1}}};
}

std::vector<std::uint8_t> bytesOf(const ClientMessage& m)
{
    const auto bytes = encodeClient(m);
    REQUIRE(bytes.has_value());
    return *bytes;
}

std::vector<std::uint8_t> hostBytesOf(const HostMessage& m)
{
    const auto bytes = encodeHost(m);
    REQUIRE(bytes.has_value());
    return *bytes;
}

template <class M>
M roundTripClient(const M& m)
{
    const auto decoded = decodeClient(bytesOf(m));
    REQUIRE(decoded.message.has_value());
    REQUIRE(std::holds_alternative<M>(*decoded.message));
    return std::get<M>(*decoded.message);
}

template <class M>
M roundTripHost(const M& m)
{
    const auto decoded = decodeHost(hostBytesOf(m));
    REQUIRE(decoded.message.has_value());
    REQUIRE(std::holds_alternative<M>(*decoded.message));
    return std::get<M>(*decoded.message);
}

std::uint32_t lcg(std::uint32_t& s)
{
    s = s * 1664525u + 1013904223u;
    return s >> 8;
}

}

TEST_CASE("every client message survives a round trip", "[coop_message]")
{
    CHECK(roundTripClient(hello()) == hello());
    CHECK(roundTripClient(LeaveMsg{}) == LeaveMsg{});
    const InputMsg input{.tick = 77, .moveX = -0.5f, .moveY = 0.8f, .facing = 3.0f};
    CHECK(roundTripClient(input) == input);
    const CommandMsg command{.kind = CommandKind::UseItem, .tick = 9, .target = 0xFFFFFFFFFFFFFFFFull, .arg = 1};
    CHECK(roundTripClient(command) == command);
    CHECK(roundTripClient(PingMsg{123456}) == PingMsg{123456});
}

TEST_CASE("every host message survives a round trip", "[coop_message]")
{
    const WelcomeMsg welcome{.playerId = 2, .worldSeed = 5, .genVersion = 1, .revision = 40, .hostTick = 900, .roster = {{0, "Host"}, {2, "Ada"}}};
    CHECK(roundTripHost(welcome) == welcome);
    CHECK(roundTripHost(CloseMsg{CloseReason::NameTaken}) == CloseMsg{CloseReason::NameTaken});
    CHECK(roundTripHost(PlayerJoinedMsg{{3, "Bo"}}) == PlayerJoinedMsg{{3, "Bo"}});
    CHECK(roundTripHost(PlayerLeftMsg{1}) == PlayerLeftMsg{1});
    const WorldDeltaMsg delta{.revision = 1, .entity = 99, .kind = 7, .value = -4};
    CHECK(roundTripHost(delta) == delta);
    CHECK(roundTripHost(PongMsg{8, 1234}) == PongMsg{8, 1234});
}

TEST_CASE("encoding is byte stable", "[coop_message]")
{
    const std::vector<std::uint8_t> ping = {0x49, 0x43, kProtocolVersion, 5, 0x78, 0x56, 0x34, 0x12};
    CHECK(bytesOf(PingMsg{0x12345678}) == ping);
}

TEST_CASE("the header is checked before anything else", "[coop_message]")
{
    auto bytes = bytesOf(PingMsg{1});
    auto bad = bytes;
    bad[0] ^= 0xFF;
    CHECK(decodeClient(bad).error == DecodeError::BadMagic);
    bad = bytes;
    bad[2] = kProtocolVersion + 1;
    CHECK(decodeClient(bad).error == DecodeError::BadVersion);
    bad = bytes;
    bad[3] = 99;
    CHECK(decodeClient(bad).error == DecodeError::BadType);
    CHECK(decodeClient({}).error == DecodeError::Truncated);
    CHECK(decodeClient(std::vector<std::uint8_t>(bytes.begin(), bytes.begin() + 3)).error == DecodeError::Truncated);
}

TEST_CASE("a host message type is not accepted from a guest and the reverse", "[coop_message]")
{
    CHECK(decodeClient(hostBytesOf(WorldDeltaMsg{1, 2, 3, 4})).error == DecodeError::BadType);
    CHECK(decodeClient(hostBytesOf(WelcomeMsg{.playerId = 1, .roster = {{1, "A"}}})).error == DecodeError::BadType);
    CHECK(decodeHost(bytesOf(hello())).error == DecodeError::BadType);
    CHECK(decodeHost(bytesOf(CommandMsg{CommandKind::Interact, 1, 2, 0})).error == DecodeError::BadType);
}

TEST_CASE("an oversized message is refused before parsing", "[coop_message]")
{
    auto bytes = bytesOf(PingMsg{1});
    bytes.resize(kMaxMessageBytes + 1, 0);
    CHECK(decodeClient(bytes).error == DecodeError::TooLarge);
    CHECK(decodeHost(bytes).error == DecodeError::TooLarge);
}

TEST_CASE("trailing bytes and truncation are both refused", "[coop_message]")
{
    auto bytes = bytesOf(hello());
    for (std::size_t cut = 0; cut < bytes.size(); ++cut) {
        const std::vector<std::uint8_t> prefix(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(cut));
        CHECK_FALSE(decodeClient(prefix).message.has_value());
    }
    bytes.push_back(0);
    CHECK(decodeClient(bytes).error == DecodeError::Trailing);
}

TEST_CASE("names are restricted to a plain character set", "[coop_message]")
{
    CHECK(isValidName("Ada_Lovelace-2"));
    CHECK_FALSE(isValidName(""));
    CHECK_FALSE(isValidName(" lead"));
    CHECK_FALSE(isValidName("trail "));
    CHECK_FALSE(isValidName("two  spaces"));
    CHECK(isValidName("two spaces"));
    CHECK_FALSE(isValidName(std::string(kMaxNameLength + 1, 'a')));
    CHECK(isValidName(std::string(kMaxNameLength, 'a')));
    CHECK_FALSE(isValidName("tab\there"));
    CHECK_FALSE(isValidName(std::string("nul\0x", 5)));
    CHECK_FALSE(isValidName("caf\xC3\xA9"));
    CHECK_FALSE(isValidName("<script>"));
}

TEST_CASE("inventories reject duplicates, zero and huge counts, bad ids and too many entries", "[coop_message]")
{
    CHECK(isValidInventory({}));
    CHECK_FALSE(isValidInventory({{"a", 1}, {"a", 2}}));
    CHECK_FALSE(isValidInventory({{"a", 0}}));
    CHECK_FALSE(isValidInventory({{"a", static_cast<std::uint16_t>(kMaxItemCount + 1)}}));
    CHECK(isValidInventory({{"a", kMaxItemCount}}));
    CHECK_FALSE(isValidInventory({{"Upper", 1}}));
    CHECK_FALSE(isValidInventory({{"", 1}}));
    CHECK_FALSE(isValidInventory({{std::string(kMaxItemIdLength + 1, 'a'), 1}}));
    std::vector<InventoryEntry> many;
    for (std::size_t i = 0; i < kMaxInventoryEntries + 1; ++i) many.push_back({"item" + std::to_string(i), 1});
    CHECK_FALSE(isValidInventory(many));
    many.pop_back();
    CHECK(isValidInventory(many));
}

TEST_CASE("the largest legal hello fits in a message and round trips", "[coop_message]")
{
    HelloMsg big{.worldSeed = 1, .genVersion = 1, .name = std::string(kMaxNameLength, 'n'), .inventory = {}};
    for (std::size_t i = 0; i < kMaxInventoryEntries; ++i) {
        std::string id = std::to_string(i);
        id.resize(kMaxItemIdLength, 'x');
        big.inventory.push_back({id, kMaxItemCount});
    }
    REQUIRE(isValidInventory(big.inventory));
    const auto bytes = encodeClient(big);
    REQUIRE(bytes.has_value());
    CHECK(bytes->size() <= kMaxMessageBytes);
    const auto decoded = decodeClient(*bytes);
    REQUIRE(decoded.message.has_value());
    CHECK(std::get<HelloMsg>(*decoded.message) == big);
}

TEST_CASE("the largest legal welcome fits in a message", "[coop_message]")
{
    WelcomeMsg w{.playerId = 3, .roster = {}};
    for (PlayerId i = 0; i < kMaxPlayers; ++i) w.roster.push_back({i, std::string(kMaxNameLength, 'n')});
    const auto bytes = encodeHost(w);
    REQUIRE(bytes.has_value());
    CHECK(bytes->size() <= kMaxMessageBytes);
}

TEST_CASE("an invalid message cannot be encoded", "[coop_message]")
{
    HelloMsg bad = hello();
    bad.name = "";
    CHECK_FALSE(encodeClient(bad).has_value());
    CHECK_FALSE(encodeClient(CommandMsg{CommandKind::Attack, 0, 0, 5}).has_value());
    CHECK_FALSE(encodeHost(WelcomeMsg{.playerId = 9}).has_value());
    CHECK_FALSE(encodeHost(WorldDeltaMsg{0, 0, 0, 0}).has_value());
}

TEST_CASE("input floats must be finite and in range", "[coop_message]")
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    CHECK_FALSE(encodeClient(InputMsg{1, nan, 0, 0}).has_value());
    CHECK_FALSE(encodeClient(InputMsg{1, 0, inf, 0}).has_value());
    CHECK_FALSE(encodeClient(InputMsg{1, 0, 0, -inf}).has_value());
    CHECK_FALSE(encodeClient(InputMsg{1, 1.01f, 0, 0}).has_value());
    CHECK_FALSE(encodeClient(InputMsg{1, 1.0f, 1.0f, 0}).has_value());
    CHECK_FALSE(encodeClient(InputMsg{1, -1.0f, 0.5f, 0}).has_value());
    CHECK(encodeClient(InputMsg{1, 0.7f, 0.7f, 0}).has_value());
    CHECK(encodeClient(InputMsg{1, 0, -1.0f, 0}).has_value());
    CHECK_FALSE(encodeClient(InputMsg{1, 0, 0, 7.0f}).has_value());

    auto wire = bytesOf(InputMsg{1, 0, 0, 0});
    for (const std::uint32_t pattern : {0x7FC00000u, 0xFFC00001u, 0x7F800000u, 0xFF800000u, 0x7F7FFFFFu, 0x80000000u + 0x3F800001u}) {
        auto hostile = wire;
        for (int i = 0; i < 4; ++i) hostile[8 + i] = static_cast<std::uint8_t>(pattern >> (8 * i));
        const auto decoded = decodeClient(hostile);
        const float v = std::bit_cast<float>(pattern);
        CHECK(decoded.message.has_value() == (v >= -1.0f && v <= 1.0f));
    }
}

TEST_CASE("command arguments are validated per kind", "[coop_message]")
{
    CHECK(encodeClient(CommandMsg{CommandKind::UseItem, 0, 0, 31}).has_value());
    CHECK_FALSE(encodeClient(CommandMsg{CommandKind::UseItem, 0, 0, 32}).has_value());
    CHECK_FALSE(encodeClient(CommandMsg{CommandKind::UseItem, 0, 0, -1}).has_value());
    CHECK_FALSE(encodeClient(CommandMsg{CommandKind::UseItem, 0, 0, std::numeric_limits<std::int32_t>::min()}).has_value());
    CHECK(encodeClient(CommandMsg{CommandKind::Emote, 0, 0, 15}).has_value());
    CHECK_FALSE(encodeClient(CommandMsg{CommandKind::Emote, 0, 0, 16}).has_value());
    CHECK_FALSE(encodeClient(CommandMsg{CommandKind::Revive, 0, 0, 1}).has_value());
    CHECK_FALSE(encodeClient(CommandMsg{static_cast<CommandKind>(0), 0, 0, 0}).has_value());
    CHECK_FALSE(encodeClient(CommandMsg{static_cast<CommandKind>(200), 0, 0, 0}).has_value());
}

TEST_CASE("a hello that claims a huge inventory or string length is refused without allocating it", "[coop_message]")
{
    auto bytes = bytesOf(HelloMsg{.worldSeed = 1, .genVersion = 1, .name = "A", .inventory = {}});
    // header(4) seed(8) gen(4) nameLen(1) name(1) invCount(1)
    REQUIRE(bytes.size() == 19);
    bytes[18] = 255;
    CHECK_FALSE(decodeClient(bytes).message.has_value());
    bytes[18] = 0;
    bytes[16] = 255;
    CHECK(decodeClient(bytes).error == DecodeError::Truncated);
}

TEST_CASE("a welcome roster with a duplicate id or an out of range id is refused", "[coop_message]")
{
    CHECK_FALSE(encodeHost(WelcomeMsg{.playerId = 1, .roster = {{1, "A"}, {1, "B"}}}).has_value());
    CHECK_FALSE(encodeHost(WelcomeMsg{.playerId = 1, .roster = {{4, "A"}}}).has_value());
    CHECK_FALSE(encodeHost(WelcomeMsg{.playerId = 1, .roster = {{0, "A"}, {1, "B"}, {2, "C"}, {3, "D"}, {3, "E"}}}).has_value());
}

TEST_CASE("random bytes and bit flips never crash a decoder and anything accepted re-encodes identically", "[coop_message]")
{
    std::uint32_t seed = 12345;
    const std::vector<std::vector<std::uint8_t>> corpus = {
        bytesOf(hello()), bytesOf(InputMsg{4, 0.25f, -0.25f, 1.0f}), bytesOf(CommandMsg{CommandKind::UseItem, 5, 6, 1}), bytesOf(PingMsg{9}),
        hostBytesOf(WelcomeMsg{.playerId = 1, .roster = {{0, "Host"}, {1, "Ada"}}}), hostBytesOf(WorldDeltaMsg{3, 4, 5, 6}),
    };
    std::size_t accepted = 0;
    for (int iteration = 0; iteration < 20000; ++iteration) {
        std::vector<std::uint8_t> bytes;
        if (iteration % 4 == 0) {
            bytes.resize(lcg(seed) % 80);
            for (auto& b : bytes) b = static_cast<std::uint8_t>(lcg(seed));
        } else {
            bytes = corpus[lcg(seed) % corpus.size()];
            const int flips = 1 + static_cast<int>(lcg(seed) % 3);
            for (int f = 0; f < flips; ++f) bytes[lcg(seed) % bytes.size()] ^= static_cast<std::uint8_t>(1u << (lcg(seed) % 8));
        }
        if (const auto c = decodeClient(bytes); c.message) {
            ++accepted;
            CHECK(isValid(*c.message));
            CHECK(bytesOf(*c.message) == bytes);
        }
        if (const auto h = decodeHost(bytes); h.message) {
            ++accepted;
            CHECK(isValid(*h.message));
            CHECK(hostBytesOf(*h.message) == bytes);
        }
    }
    CHECK(accepted > 0);
}
