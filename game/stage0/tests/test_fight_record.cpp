#include "ichor/stage0/fight_record.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <vector>

using namespace ichor::stage0;

namespace {

FightRecord sample()
{
    FightRecord r;
    r.index = 3;
    r.loser = FightSide::Player;
    r.cause = DeathCause::WhiffPunished;
    r.durationMs = 9000;
    r.events = {
        {0, FightEventKind::Attack, FightSide::Player, 0},
        {400, FightEventKind::Hit, FightSide::Player, 250},
        {1200, FightEventKind::Whiff, FightSide::Foe, 0},
        {2000, FightEventKind::Block, FightSide::Foe, 120},
        {9000, FightEventKind::Hit, FightSide::Foe, 1000},
    };
    return r;
}

}

TEST_CASE("a record round-trips through the codec")
{
    const FightRecord r = sample();
    const auto back = decodeFightRecord(encode(r));
    REQUIRE(back.has_value());
    CHECK(*back == r);
}

TEST_CASE("the summary counts hits, blocks, whiffs and damage per side")
{
    const FightSummary s = summarize(sample());
    CHECK(s.durationMs == 9000);
    CHECK(s.playerHits == 1);
    CHECK(s.foeHits == 2);
    CHECK(s.foeBlocks == 0);
    CHECK(s.playerBlocks == 1);
    CHECK(s.foeWhiffs == 1);
    CHECK(s.playerWhiffs == 0);
    CHECK(s.playerDamageTenths == 250);
    CHECK(s.foeDamageTenths == 1120);
}

TEST_CASE("decoding rejects every truncation and trailing byte")
{
    const auto bytes = encode(sample());
    for (std::size_t n = 0; n < bytes.size(); ++n) {
        CHECK_FALSE(decodeFightRecord(std::span(bytes.data(), n)).has_value());
    }
    auto extra = bytes;
    extra.push_back(0);
    CHECK_FALSE(decodeFightRecord(extra).has_value());
}

TEST_CASE("decoding rejects bad header fields")
{
    const auto good = encode(sample());
    const auto accepts = [&](std::size_t i, std::uint8_t v) {
        auto b = good;
        b[i] = v;
        return decodeFightRecord(b).has_value();
    };
    CHECK_FALSE(accepts(0, 'X'));
    CHECK_FALSE(accepts(4, 2));
    CHECK_FALSE(accepts(9, 2));
    CHECK_FALSE(accepts(10, 200));
    CHECK_FALSE(accepts(15, 2));
    CHECK_FALSE(accepts(20 + 4, 99)); // event kind
    CHECK_FALSE(accepts(20 + 5, 7));  // event actor
    CHECK(accepts(10, 3));
}

TEST_CASE("a huge claimed event count is rejected before any allocation")
{
    auto b = encode(sample());
    b[16] = b[17] = b[18] = b[19] = 0xFF;
    CHECK_FALSE(decodeFightRecord(b).has_value());
    b[16] = 0xFF;
    b[17] = 0x0F;
    b[18] = b[19] = 0;
    CHECK_FALSE(decodeFightRecord(b).has_value());
}

TEST_CASE("decoding rejects events out of order or past the duration")
{
    FightRecord r = sample();
    std::swap(r.events[1], r.events[2]);
    CHECK_FALSE(decodeFightRecord(encode(r)).has_value());
    r = sample();
    r.events.back().atMs = r.durationMs + 1;
    CHECK_FALSE(decodeFightRecord(encode(r)).has_value());
}

TEST_CASE("hostile bytes never crash the decoder")
{
    std::uint32_t seed = 12345;
    for (int i = 0; i < 2000; ++i) {
        std::vector<std::uint8_t> b(static_cast<std::size_t>(i % 80));
        for (auto& v : b) {
            seed = seed * 1664525u + 1013904223u;
            v = static_cast<std::uint8_t>(seed >> 24);
        }
        (void)decodeFightRecord(b);
    }
    auto b = encode(sample());
    for (std::size_t i = 0; i < b.size(); ++i) {
        auto m = b;
        m[i] ^= 0xFF;
        (void)decodeFightRecord(m);
    }
}

TEST_CASE("the buffer records a fight from the first swing to the death")
{
    FightRecordBuffer buf;
    buf.noteEvent(FightEventKind::Hit, FightSide::Player, 1.0, 5.0);
    CHECK_FALSE(buf.running());
    buf.noteEvent(FightEventKind::Attack, FightSide::Player, 10.0);
    buf.noteEvent(FightEventKind::Hit, FightSide::Player, 10.5, 12.34);
    REQUIRE(buf.end(FightSide::Foe, DeathCause::Unknown, 12.0));
    REQUIRE(buf.fights().size() == 1);
    const FightRecord& r = buf.fights()[0];
    CHECK(r.index == 1);
    CHECK(r.durationMs == 2000);
    CHECK(r.events.size() == 2);
    CHECK(r.events[1].atMs == 500);
    CHECK(r.events[1].damageTenths == 123);
    CHECK(summarize(r).playerDamageTenths == 123);
    CHECK_FALSE(buf.end(FightSide::Foe, DeathCause::Unknown, 13.0));
}

TEST_CASE("an abandoned fight is not recorded and the next one is numbered 1")
{
    FightRecordBuffer buf;
    buf.noteEvent(FightEventKind::Attack, FightSide::Player, 0.0);
    buf.abandon();
    CHECK(buf.fights().empty());
    buf.noteEvent(FightEventKind::Attack, FightSide::Foe, 5.0);
    buf.end(FightSide::Player, DeathCause::Cornered, 6.0);
    CHECK(buf.fights()[0].index == 1);
}

TEST_CASE("the buffer keeps only the first 20 fights")
{
    FightRecordBuffer buf;
    for (int i = 0; i < 25; ++i) {
        buf.noteEvent(FightEventKind::Attack, FightSide::Player, i * 10.0);
        buf.end(FightSide::Foe, DeathCause::Unknown, i * 10.0 + 1);
    }
    CHECK(buf.fights().size() == kMaxRecordedFights);
    CHECK(buf.dropped() == 5);
    CHECK(buf.fights().back().index == 20);
}

TEST_CASE("events per fight are bounded and flagged")
{
    FightRecordBuffer buf;
    for (std::size_t i = 0; i < kMaxFightEvents + 100; ++i) {
        buf.noteEvent(FightEventKind::Attack, FightSide::Player, static_cast<double>(i) * 0.001);
    }
    buf.end(FightSide::Foe, DeathCause::Unknown, 10.0);
    const FightRecord& r = buf.fights()[0];
    CHECK(r.events.size() == kMaxFightEvents);
    CHECK(r.truncated);
    const auto back = decodeFightRecord(encode(r));
    REQUIRE(back.has_value());
    CHECK(back->truncated);
}

TEST_CASE("hostile numbers are clamped, not cast")
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    FightRecordBuffer buf;
    buf.noteEvent(FightEventKind::Attack, FightSide::Player, nan);
    CHECK_FALSE(buf.running());
    buf.noteEvent(FightEventKind::Attack, FightSide::Player, 0.0);
    buf.noteEvent(FightEventKind::Hit, FightSide::Player, 1.0, nan);
    buf.noteEvent(FightEventKind::Hit, FightSide::Player, 2.0, -inf);
    buf.noteEvent(FightEventKind::Hit, FightSide::Player, 3.0, inf);
    buf.noteEvent(FightEventKind::Hit, FightSide::Player, 4.0, 1e300);
    buf.noteEvent(FightEventKind::Hit, FightSide::Player, inf, 1.0);
    buf.noteEvent(FightEventKind::Hit, FightSide::Player, -50.0, 1.0);
    REQUIRE(buf.end(FightSide::Foe, DeathCause::Unknown, 1e300));
    const FightRecord& r = buf.fights()[0];
    CHECK(r.durationMs == kMaxFightMs);
    CHECK(r.events[1].damageTenths == 0);
    CHECK(r.events[2].damageTenths == 0);
    CHECK(r.events[3].damageTenths == 0);
    CHECK(r.events[4].damageTenths == 65535);
    for (std::size_t i = 1; i < r.events.size(); ++i) CHECK(r.events[i].atMs >= r.events[i - 1].atMs);
    CHECK(decodeFightRecord(encode(r)).has_value());
}

TEST_CASE("an out-of-range cause is stored as unknown")
{
    FightRecordBuffer buf;
    buf.noteEvent(FightEventKind::Attack, FightSide::Player, 0.0);
    buf.end(FightSide::Player, static_cast<DeathCause>(250), 1.0);
    CHECK(buf.fights()[0].cause == DeathCause::Unknown);
}
