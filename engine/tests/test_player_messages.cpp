#include "ichor/world/player_messages.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <vector>

using namespace ichor;

namespace {

MessageDraft draft(std::uint64_t author, std::int32_t x = 0, std::int32_t z = 0)
{
    MessageDraft d;
    d.author = author;
    d.xMm = x;
    d.zMm = z;
    d.templateId = 0;
    d.wordId = 1;
    return d;
}

MessageLimits roomy()
{
    MessageLimits l;
    l.minIntervalSeconds = 0;
    l.maxPerWindow = 1000;
    l.maxPerAuthor = 1000;
    l.maxInCrowd = 1000;
    return l;
}

} // namespace

TEST_CASE("every valid phrase renders and every invalid pair does not", "[player_messages]")
{
    std::size_t rendered = 0;
    for (std::size_t t = 0; t < 256; ++t) {
        for (std::size_t w = 0; w < 256; ++w) {
            const auto text = renderMessageText(static_cast<std::uint8_t>(t), static_cast<std::uint8_t>(w));
            const bool expected = t < messageTemplateCount() &&
                (messageTemplateTakesWord(static_cast<std::uint8_t>(t)) ? w < messageWordCount()
                                                                          : w == kNoMessageWord);
            REQUIRE(text.empty() != expected);
            REQUIRE(text.find("{}") == std::string::npos);
            rendered += expected ? 1 : 0;
        }
    }
    CHECK(rendered > 0);
    CHECK(renderMessageText(0, 0) == "Try a hidden path");
    CHECK(renderMessageText(8, kNoMessageWord) == "Danger ahead");
}

TEST_CASE("place assigns ascending ids and rejects bad input", "[player_messages]")
{
    MessageBoard board(roomy());
    const auto a = board.place(draft(1), 100);
    const auto b = board.place(draft(1, 50000), 100);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    CHECK(b.id == a.id + 1);

    CHECK(board.place(draft(0), 100).status == PlaceStatus::BadAuthor);

    auto badPhrase = draft(1);
    badPhrase.templateId = 200;
    CHECK(board.place(badPhrase, 100).status == PlaceStatus::BadPhrase);
    badPhrase = draft(1);
    badPhrase.wordId = 99;
    CHECK(board.place(badPhrase, 100).status == PlaceStatus::BadPhrase);
    badPhrase = draft(1);
    badPhrase.templateId = 8;
    badPhrase.wordId = 0;
    CHECK(board.place(badPhrase, 100).status == PlaceStatus::BadPhrase);

    auto far = draft(1, std::numeric_limits<std::int32_t>::max());
    CHECK(board.place(far, 100).status == PlaceStatus::OutOfRange);
    far = draft(1, std::numeric_limits<std::int32_t>::min());
    CHECK(board.place(far, 100).status == PlaceStatus::OutOfRange);
    CHECK(board.size() == 2);
}

TEST_CASE("rate limits: interval, window, and a clock that runs backwards", "[player_messages]")
{
    MessageLimits l = roomy();
    l.minIntervalSeconds = 10;
    l.windowSeconds = 100;
    l.maxPerWindow = 3;
    MessageBoard board(l);

    REQUIRE(board.place(draft(1, 0), 1000).ok());
    CHECK(board.place(draft(1, 10000), 1005).status == PlaceStatus::TooSoon);
    CHECK(board.place(draft(1, 10000), 999).status == PlaceStatus::TooSoon);
    REQUIRE(board.place(draft(1, 10000), 1010).ok());
    REQUIRE(board.place(draft(1, 20000), 1020).ok());
    CHECK(board.place(draft(1, 30000), 1030).status == PlaceStatus::RateLimited);
    CHECK(board.place(draft(1, 30000), 1099).status == PlaceStatus::RateLimited);
    REQUIRE(board.place(draft(2, 30000), 1030).ok()); // other authors are unaffected
    REQUIRE(board.place(draft(1, 30000), 1100).ok()); // first placement left the window
}

TEST_CASE("rate limit would pass many more placements without it", "[player_messages]")
{
    const auto placed = [](const MessageLimits& l) {
        MessageBoard board(l);
        int ok = 0;
        for (std::uint32_t t = 0; t < 100; ++t) {
            ok += board.place(draft(1, static_cast<std::int32_t>(t) * 10000), t).ok() ? 1 : 0;
        }
        return ok;
    };
    MessageLimits limited = roomy();
    limited.minIntervalSeconds = 10;
    limited.windowSeconds = 100;
    limited.maxPerWindow = 4;
    CHECK(placed(roomy()) == 100);
    CHECK(placed(limited) == 4);
}

TEST_CASE("caps: per author, total, and crowding", "[player_messages]")
{
    MessageLimits l = roomy();
    l.maxPerAuthor = 2;
    l.maxTotal = 3;
    MessageBoard board(l);
    REQUIRE(board.place(draft(1, 0), 0).ok());
    REQUIRE(board.place(draft(1, 100000), 1).ok());
    CHECK(board.place(draft(1, 200000), 2).status == PlaceStatus::AuthorFull);
    REQUIRE(board.place(draft(2, 200000), 2).ok());
    CHECK(board.place(draft(3, 300000), 3).status == PlaceStatus::BoardFull);

    MessageLimits c = roomy();
    c.crowdRadiusMm = 1000;
    c.maxInCrowd = 2;
    MessageBoard crowd(c);
    REQUIRE(crowd.place(draft(1, 0), 0).ok());
    REQUIRE(crowd.place(draft(2, 500), 0).ok());
    CHECK(crowd.place(draft(3, 900), 0).status == PlaceStatus::Crowded);
    REQUIRE(crowd.place(draft(3, 5000), 0).ok());
}

TEST_CASE("messages expire and free their caps", "[player_messages]")
{
    MessageLimits l = roomy();
    l.lifetimeSeconds = 100;
    l.maxPerAuthor = 1;
    MessageBoard board(l);
    const auto first = board.place(draft(1), 50);
    REQUIRE(first.ok());
    CHECK(board.near(0, 0, 0, 10, 149).size() == 1);
    CHECK(board.near(0, 0, 0, 10, 150).empty()); // hidden the moment it expires, before any purge
    CHECK(board.size() == 1);
    CHECK(board.place(draft(1, 90000), 120).status == PlaceStatus::AuthorFull);
    REQUIRE(board.place(draft(1, 90000), 150).ok());
    CHECK(board.size() == 1);
    CHECK(board.expire(1000) == 1);
    CHECK(board.size() == 0);
}

TEST_CASE("a lifetime that overflows the clock is refused, not wrapped", "[player_messages]")
{
    MessageBoard board(roomy());
    CHECK(board.place(draft(1), std::numeric_limits<std::uint32_t>::max() - 5).status == PlaceStatus::BoardFull);
    CHECK(board.size() == 0);
}

TEST_CASE("near is exact, 3D, and ordered by id", "[player_messages]")
{
    MessageBoard board(roomy());
    REQUIRE(board.place(draft(1, 3000), 0).ok());
    REQUIRE(board.place(draft(2, 0), 0).ok());
    REQUIRE(board.place(draft(3, 4000), 0).ok());
    auto d = draft(4, 0);
    d.yMm = 3000;
    REQUIRE(board.place(d, 0).ok());

    const auto hits = board.near(0, 0, 0, 3000, 1);
    REQUIRE(hits.size() == 3);
    CHECK(hits[0].xMm == 3000);
    CHECK(hits[1].xMm == 0);
    CHECK(hits[2].yMm == 3000);
    CHECK(board.near(0, 0, 0, std::numeric_limits<std::uint32_t>::max(), 1).size() == 4);
}

TEST_CASE("remove drops one message", "[player_messages]")
{
    MessageBoard board(roomy());
    const auto a = board.place(draft(1), 0);
    REQUIRE(board.place(draft(2, 90000), 0).ok());
    CHECK(board.remove(a.id));
    CHECK_FALSE(board.remove(a.id));
    CHECK(board.size() == 1);
}

namespace {

MessageBoard sampleBoard(const MessageLimits& l)
{
    MessageBoard board(l);
    REQUIRE(board.place(draft(7, -123456, 98765), 1000).ok());
    auto d = draft(9, 5000, -5000);
    d.yMm = -250;
    d.yaw = 40000;
    d.templateId = 9;
    d.wordId = kNoMessageWord;
    REQUIRE(board.place(d, 2000).ok());
    return board;
}

} // namespace

TEST_CASE("codec round-trips byte for byte", "[player_messages]")
{
    const auto board = sampleBoard(roomy());
    const auto bytes = encodeMessages(board);
    REQUIRE(bytes.size() == 13 + 2 * 36);
    const auto back = decodeMessages(bytes, roomy());
    REQUIRE(back.ok());
    CHECK(encodeMessages(back.board) == bytes);
    CHECK(back.board.nextId() == board.nextId());
    REQUIRE(back.board.size() == 2);
    CHECK(back.board.all()[1].yMm == -250);
    CHECK(back.board.all()[0].xMm == -123456);
}

TEST_CASE("decode rejects hostile bytes without crashing", "[player_messages]")
{
    const auto good = encodeMessages(sampleBoard(roomy()));
    const auto limits = roomy();

    SECTION("every truncation")
    {
        for (std::size_t n = 0; n < good.size(); ++n) {
            const auto r = decodeMessages(std::span<const std::uint8_t>(good.data(), n), limits);
            CHECK_FALSE(r.ok());
            CHECK(r.board.size() == 0);
        }
    }
    SECTION("trailing bytes")
    {
        auto bytes = good;
        bytes.push_back(0);
        CHECK(decodeMessages(bytes, limits).status == MessageDecodeStatus::TrailingBytes);
    }
    SECTION("bad magic and version")
    {
        auto bytes = good;
        bytes[0] = 'X';
        CHECK(decodeMessages(bytes, limits).status == MessageDecodeStatus::BadMagic);
        bytes = good;
        bytes[4] = 2;
        CHECK(decodeMessages(bytes, limits).status == MessageDecodeStatus::BadVersion);
    }
    SECTION("a huge count is refused before allocating")
    {
        auto bytes = good;
        bytes[9] = bytes[10] = bytes[11] = bytes[12] = 0xFF;
        CHECK(decodeMessages(bytes, limits).status == MessageDecodeStatus::TooMany);
        MessageLimits big = limits;
        big.maxTotal = std::numeric_limits<std::uint32_t>::max();
        CHECK(decodeMessages(bytes, big).status == MessageDecodeStatus::TooMany);
        bytes[9] = 0xA0;
        bytes[10] = 0x86;
        bytes[11] = 0x01;
        bytes[12] = 0x00;
        CHECK(decodeMessages(bytes, big).status == MessageDecodeStatus::Truncated);
    }
    SECTION("every single-byte corruption either decodes cleanly or is refused")
    {
        for (std::size_t i = 0; i < good.size(); ++i) {
            for (int bit = 0; bit < 8; ++bit) {
                auto bytes = good;
                bytes[i] = static_cast<std::uint8_t>(bytes[i] ^ (1u << bit));
                const auto r = decodeMessages(bytes, limits);
                if (r.ok()) {
                    CHECK(encodeMessages(r.board) == bytes);
                }
            }
        }
    }
    SECTION("invalid records")
    {
        const auto mutate = [&](std::size_t offset, std::uint8_t value) {
            auto bytes = good;
            bytes[offset] = value;
            return decodeMessages(bytes, limits).status;
        };
        constexpr std::size_t rec = 13;
        CHECK(mutate(rec + 5, 1) == MessageDecodeStatus::Ok);
        auto zeroAuthor = good;
        for (std::size_t k = 4; k < 12; ++k) {
            zeroAuthor[rec + k] = 0;
        }
        CHECK(decodeMessages(zeroAuthor, limits).status == MessageDecodeStatus::InvalidRecord);
        CHECK(mutate(rec + 26, 200) == MessageDecodeStatus::InvalidRecord); // template out of range
        CHECK(mutate(rec + 0, 0) == MessageDecodeStatus::InvalidRecord);        // id 0
        CHECK(mutate(rec + 3, 0x80) == MessageDecodeStatus::InvalidRecord);     // id >= nextId
        CHECK(mutate(rec + 15, 0x7F) == MessageDecodeStatus::InvalidRecord);    // x far out of range
    }
    SECTION("duplicate or descending ids")
    {
        auto bytes = good;
        for (std::size_t k = 0; k < 4; ++k) {
            bytes[13 + 36 + k] = bytes[13 + k];
        }
        CHECK(decodeMessages(bytes, limits).status == MessageDecodeStatus::InvalidRecord);
    }
    SECTION("lifetime longer than the limit")
    {
        MessageLimits tight = limits;
        tight.lifetimeSeconds = 10;
        CHECK(decodeMessages(good, tight).status == MessageDecodeStatus::InvalidRecord);
    }
    SECTION("empty and tiny inputs")
    {
        CHECK_FALSE(decodeMessages({}, limits).ok());
        const std::vector<std::uint8_t> tiny(12, 0);
        CHECK_FALSE(decodeMessages(tiny, limits).ok());
    }
}

TEST_CASE("decode enforces the per-author cap", "[player_messages]")
{
    MessageBoard board(roomy());
    for (int i = 0; i < 3; ++i) {
        REQUIRE(board.place(draft(5, i * 90000), static_cast<std::uint32_t>(i)).ok());
    }
    MessageLimits strict = roomy();
    strict.maxPerAuthor = 2;
    CHECK(decodeMessages(encodeMessages(board), strict).status == MessageDecodeStatus::InvalidRecord);
}

TEST_CASE("a restored board keeps issuing fresh ids", "[player_messages]")
{
    auto restored = decodeMessages(encodeMessages(sampleBoard(roomy())), roomy());
    REQUIRE(restored.ok());
    const auto next = restored.board.place(draft(1, 777000), 5000);
    REQUIRE(next.ok());
    CHECK(next.id == 3);
}

TEST_CASE("restore rejects nextId 0 and leaves the board untouched on failure", "[player_messages]")
{
    const std::vector<std::uint8_t> zeroNext = {'I', 'C', 'P', 'M', 1, 0, 0, 0, 0, 0, 0, 0, 0};
    CHECK(decodeMessages(zeroNext, roomy()).status == MessageDecodeStatus::InvalidRecord);

    MessageBoard board(roomy());
    REQUIRE(board.place(draft(1), 0).ok());
    CHECK_FALSE(board.restore({}, 0));
    CHECK(board.size() == 1);
}

TEST_CASE("distance maths survives extreme limits and queries", "[player_messages]")
{
    MessageLimits l = roomy();
    l.maxHorizontalMm = std::numeric_limits<std::int32_t>::max();
    l.maxVerticalMm = std::numeric_limits<std::int32_t>::max();
    l.crowdRadiusMm = std::numeric_limits<std::uint32_t>::max();
    l.maxInCrowd = 1;
    MessageBoard board(l);
    const auto hi = std::numeric_limits<std::int32_t>::max();
    const auto lo = std::numeric_limits<std::int32_t>::min() + 1;
    auto d = draft(1, hi, hi);
    d.yMm = hi;
    REQUIRE(board.place(d, 0).ok());
    CHECK(board.near(lo, lo, lo, 1000, 1).empty());
    CHECK(board.near(hi, hi, hi, 0, 1).size() == 1);
    d = draft(2, lo, lo);
    d.yMm = lo;
    CHECK(board.place(d, 0).ok()); // opposite corners are not near, even with a wrapped-looking sum
}

TEST_CASE("idle authors do not keep a rate history forever", "[player_messages]")
{
    MessageLimits l = roomy();
    l.minIntervalSeconds = 10;
    l.windowSeconds = 100;
    l.maxPerWindow = 1;
    MessageBoard board(l);
    REQUIRE(board.place(draft(1, 0), 0).ok());
    CHECK(board.place(draft(1, 90000), 50).status == PlaceStatus::RateLimited);
    REQUIRE(board.place(draft(1, 90000), 100).ok());
}
