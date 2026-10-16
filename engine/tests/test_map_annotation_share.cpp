#include "ichor/ui/map_annotation_share.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace ichor;

namespace {

MapAnnotation mark(const std::string& id, float x, const std::string& text = "")
{
    MapAnnotation m;
    m.id = id;
    m.kind = AnnotationKind::Mark;
    m.symbol = AnnotationSymbol::Camp;
    m.text = text;
    m.points = {{x, 1.0f}};
    return m;
}

MapAnnotationStore storeOf(std::initializer_list<MapAnnotation> items)
{
    MapAnnotationStore s;
    for (const auto& i : items) {
        REQUIRE(s.add(i) == AnnotationResult::Ok);
    }
    return s;
}

std::vector<std::uint8_t> share(const MapAnnotationStore& s, std::string_view seed = "KRAKEN-77")
{
    auto e = encodeAnnotationShare(s, seed, "Mira");
    REQUIRE(e.status == AnnotationShareStatus::Ok);
    return e.bytes;
}

void refreshCrc(std::vector<std::uint8_t>& b)
{
    const auto crc = annotationCrc32(std::span<const std::uint8_t>(b).first(b.size() - 4));
    for (int i = 0; i < 4; ++i) {
        b[b.size() - 4 + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(crc >> (8 * i));
    }
}

} // namespace

TEST_CASE("crc32 matches the standard check value", "[ui][share]")
{
    const std::string s = "123456789";
    const std::vector<std::uint8_t> b(s.begin(), s.end());
    CHECK(annotationCrc32(b) == 0xCBF43926u);
}

TEST_CASE("share round trip keeps seed, author and annotations", "[ui][share]")
{
    const auto store = storeOf({mark("a", 1, "water \xE2\x82\xAC"), mark("b", 2)});
    const auto bytes = share(store);
    CHECK(bytes == share(store));
    const auto d = decodeAnnotationShare(bytes);
    REQUIRE(d.status == AnnotationShareStatus::Ok);
    CHECK(d.seed == "KRAKEN-77");
    CHECK(d.author == "Mira");
    REQUIRE(d.store.size() == 2);
    CHECK(d.store.all()[0].text == "water \xE2\x82\xAC");
    CHECK(encodeAnnotations(d.store) == encodeAnnotations(store));
}

TEST_CASE("encode rejects bad seed and author", "[ui][share]")
{
    MapAnnotationStore s;
    CHECK(encodeAnnotationShare(s, "", "x").status == AnnotationShareStatus::InvalidSeed);
    CHECK(encodeAnnotationShare(s, std::string(65, 'a'), "x").status == AnnotationShareStatus::InvalidSeed);
    CHECK(encodeAnnotationShare(s, "a\nb", "x").status == AnnotationShareStatus::InvalidSeed);
    CHECK(encodeAnnotationShare(s, "ok", std::string(33, 'a')).status == AnnotationShareStatus::InvalidAuthor);
    CHECK(encodeAnnotationShare(s, "ok", "\xFF").status == AnnotationShareStatus::InvalidAuthor);
    CHECK(encodeAnnotationShare(s, "ok", "").status == AnnotationShareStatus::Ok);
    AnnotationShareLimits tiny;
    tiny.maxEnvelopeBytes = 10;
    CHECK(encodeAnnotationShare(s, "ok", "", tiny).status == AnnotationShareStatus::TooLarge);
}

TEST_CASE("a configured limit above the u16 length field cannot wrap it", "[ui][share]")
{
    AnnotationShareLimits wide;
    wide.maxSeedBytes = 100000;
    wide.maxAuthorBytes = 100000;
    const MapAnnotationStore s;
    CHECK(encodeAnnotationShare(s, std::string(65536, 'a'), "", wide).status == AnnotationShareStatus::InvalidSeed);
    CHECK(encodeAnnotationShare(s, "ok", std::string(65536, 'a'), wide).status == AnnotationShareStatus::InvalidAuthor);
}

TEST_CASE("every single flipped bit is caught", "[ui][share]")
{
    const auto bytes = share(storeOf({mark("a", 1, "hi")}));
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        for (int bit = 0; bit < 8; ++bit) {
            auto copy = bytes;
            copy[i] ^= static_cast<std::uint8_t>(1u << bit);
            CHECK(decodeAnnotationShare(copy).status != AnnotationShareStatus::Ok);
        }
    }
}

TEST_CASE("truncated, empty, trailing and wrong header are rejected", "[ui][share]")
{
    const auto bytes = share(storeOf({mark("a", 1)}));
    CHECK(decodeAnnotationShare({}).status == AnnotationShareStatus::Truncated);
    for (std::size_t n = 0; n < bytes.size(); ++n) {
        CHECK(decodeAnnotationShare(std::span<const std::uint8_t>(bytes).first(n)).status !=
              AnnotationShareStatus::Ok);
    }
    auto magic = bytes;
    magic[0] = 'X';
    CHECK(decodeAnnotationShare(magic).status == AnnotationShareStatus::BadMagic);
    auto version = bytes;
    version[4] = 2;
    CHECK(decodeAnnotationShare(version).status == AnnotationShareStatus::BadVersion);

    auto trailing = bytes;
    trailing.insert(trailing.end() - 4, 0);
    refreshCrc(trailing);
    CHECK(decodeAnnotationShare(trailing).status == AnnotationShareStatus::TrailingBytes);
}

TEST_CASE("hostile lengths with a valid checksum are rejected", "[ui][share]")
{
    const auto bytes = share(storeOf({mark("a", 1)}));
    auto hugeSeed = bytes;
    hugeSeed[5] = 0xFF;
    hugeSeed[6] = 0xFF;
    refreshCrc(hugeSeed);
    CHECK(decodeAnnotationShare(hugeSeed).status == AnnotationShareStatus::Truncated);

    auto hugePayload = bytes;
    const std::size_t at = 5 + 2 + 9 + 2 + 4 + 0; // seed "KRAKEN-77" is 9 bytes, author "Mira" is 4
    hugePayload[at] = 0xFF;
    hugePayload[at + 1] = 0xFF;
    hugePayload[at + 2] = 0xFF;
    hugePayload[at + 3] = 0xFF;
    refreshCrc(hugePayload);
    CHECK(decodeAnnotationShare(hugePayload).status == AnnotationShareStatus::Truncated);

    auto badPayload = bytes;
    badPayload[at + 4 + 4] = 0; // payload version byte
    refreshCrc(badPayload);
    CHECK(decodeAnnotationShare(badPayload).status == AnnotationShareStatus::BadPayload);

    auto badSeed = bytes;
    badSeed[7] = 0x01; // control character in the seed
    refreshCrc(badSeed);
    CHECK(decodeAnnotationShare(badSeed).status == AnnotationShareStatus::BadPayload);
}

TEST_CASE("oversized input is refused before any work", "[ui][share]")
{
    AnnotationShareLimits tiny;
    tiny.maxEnvelopeBytes = 16;
    const std::vector<std::uint8_t> big(17, 0);
    CHECK(decodeAnnotationShare(big, {}, tiny).status == AnnotationShareStatus::TooLarge);
}

TEST_CASE("merge never overwrites own annotations", "[ui][share]")
{
    auto own = storeOf({mark("a", 1, "mine"), mark("a-2", 5, "mine too")});
    const auto theirs = storeOf({mark("a", 9, "theirs"), mark("c", 3)});
    const auto r = mergeSharedAnnotations(own, theirs);
    CHECK(r.added == 2);
    CHECK(r.renamed == 1);
    CHECK(r.dropped == 0);
    REQUIRE(own.size() == 4);
    CHECK(own.find("a")->text == "mine");
    CHECK(own.find("a-2")->text == "mine too");
    REQUIRE(own.find("a-3") != nullptr);
    CHECK(own.find("a-3")->text == "theirs");
    CHECK(own.find("c") != nullptr);
}

TEST_CASE("merge truncates long stems and respects the limits", "[ui][share]")
{
    const std::string longId(48, 'x');
    auto own = storeOf({mark(longId, 1)});
    const auto r = mergeSharedAnnotations(own, storeOf({mark(longId, 2)}));
    CHECK(r.renamed == 1);
    REQUIRE(own.size() == 2);
    CHECK(own.all()[1].id == std::string(46, 'x') + "-2");

    AnnotationLimits small;
    small.maxAnnotations = 2;
    MapAnnotationStore full(small);
    REQUIRE(full.add(mark("a", 1)) == AnnotationResult::Ok);
    const auto r2 = mergeSharedAnnotations(full, storeOf({mark("b", 1), mark("c", 2), mark("d", 3)}));
    CHECK(r2.added == 1);
    CHECK(r2.dropped == 2);
    CHECK(full.size() == 2);
}

TEST_CASE("import checks the seed and leaves own untouched on failure", "[ui][share]")
{
    auto own = storeOf({mark("a", 1)});
    const auto bytes = share(storeOf({mark("z", 2)}), "KRAKEN-77");

    CHECK(importAnnotationShare(own, "OTHER-1", bytes).status == AnnotationShareStatus::WrongSeed);
    CHECK(own.size() == 1);

    auto broken = bytes;
    broken[broken.size() / 2] ^= 0x10;
    CHECK(importAnnotationShare(own, "KRAKEN-77", broken).status == AnnotationShareStatus::BadChecksum);
    CHECK(own.size() == 1);

    const auto ok = importAnnotationShare(own, "KRAKEN-77", bytes);
    CHECK(ok.status == AnnotationShareStatus::Ok);
    CHECK(ok.added == 1);
    CHECK(own.size() == 2);
}

TEST_CASE("importing the same share twice keeps both copies apart", "[ui][share]")
{
    MapAnnotationStore own;
    const auto bytes = share(storeOf({mark("z", 2)}));
    CHECK(importAnnotationShare(own, "KRAKEN-77", bytes).added == 1);
    const auto second = importAnnotationShare(own, "KRAKEN-77", bytes);
    CHECK(second.renamed == 1);
    CHECK(own.size() == 2);
}
