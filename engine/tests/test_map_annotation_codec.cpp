#include "ichor/ui/map_annotation_codec.h"

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <limits>
#include <string>

using namespace ichor;

namespace {

MapAnnotationStore sampleStore()
{
    MapAnnotationStore store;
    MapAnnotation m;
    m.id = "camp-1";
    m.kind = AnnotationKind::Mark;
    m.symbol = AnnotationSymbol::Camp;
    m.text = "safe camp \xE2\x82\xAC";
    m.points = {{12.5f, -40.25f}};
    REQUIRE(store.add(m) == AnnotationResult::Ok);

    MapAnnotation n;
    n.id = "note.2";
    n.kind = AnnotationKind::Note;
    n.text = "the smith said the ford is east of here";
    n.points = {{-3.0f, 7.0f}};
    REQUIRE(store.add(n) == AnnotationResult::Ok);

    MapAnnotation p;
    p.id = "road_3";
    p.kind = AnnotationKind::Path;
    p.symbol = AnnotationSymbol::Direction;
    p.points = {{0, 0}, {1, 1}, {2, 4}, {-5, 9}};
    REQUIRE(store.add(p) == AnnotationResult::Ok);
    return store;
}

void putU32(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i) {
        bytes[at + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(v >> (8 * i));
    }
}

} // namespace

TEST_CASE("annotations round trip through bytes", "[map_annotation_codec]")
{
    const auto store = sampleStore();
    const auto bytes = encodeAnnotations(store);
    const auto decoded = decodeAnnotations(bytes);
    REQUIRE(decoded.ok());
    REQUIRE(decoded.store.size() == 3);
    for (std::size_t i = 0; i < store.size(); ++i) {
        const auto& a = store.all()[i];
        const auto& b = decoded.store.all()[i];
        REQUIRE(a.id == b.id);
        REQUIRE(a.kind == b.kind);
        REQUIRE(a.symbol == b.symbol);
        REQUIRE(a.text == b.text);
        REQUIRE(a.points.size() == b.points.size());
        for (std::size_t k = 0; k < a.points.size(); ++k) {
            REQUIRE(a.points[k].x == b.points[k].x);
            REQUIRE(a.points[k].y == b.points[k].y);
        }
    }
    REQUIRE(encodeAnnotations(decoded.store) == bytes);
}

TEST_CASE("an empty store round trips", "[map_annotation_codec]")
{
    const auto bytes = encodeAnnotations(MapAnnotationStore{});
    REQUIRE(bytes.size() == 9);
    const auto decoded = decodeAnnotations(bytes);
    REQUIRE(decoded.ok());
    REQUIRE(decoded.store.size() == 0);
}

TEST_CASE("encoding is a fixed little-endian layout", "[map_annotation_codec]")
{
    MapAnnotationStore store;
    MapAnnotation m;
    m.id = "a";
    m.kind = AnnotationKind::Mark;
    m.symbol = AnnotationSymbol::Water;
    m.text = "hi";
    m.points = {{1.0f, -2.0f}};
    REQUIRE(store.add(m) == AnnotationResult::Ok);
    const std::vector<std::uint8_t> expected = {
        'I', 'C', 'M', 'A', 1,
        1, 0, 0, 0,             // count
        0, 1,                   // kind Mark, symbol Water
        1, 0, 'a',              // id
        2, 0, 'h', 'i',         // text
        1, 0,                   // point count
        0x00, 0x00, 0x80, 0x3F, // 1.0f
        0x00, 0x00, 0x00, 0xC0, // -2.0f
    };
    REQUIRE(encodeAnnotations(store) == expected);
}

TEST_CASE("every truncation of valid bytes is refused", "[map_annotation_codec]")
{
    const auto bytes = encodeAnnotations(sampleStore());
    for (std::size_t len = 0; len < bytes.size(); ++len) {
        const auto result = decodeAnnotations(std::span<const std::uint8_t>(bytes.data(), len));
        REQUIRE_FALSE(result.ok());
        REQUIRE(result.store.size() == 0);
    }
}

TEST_CASE("trailing bytes are refused", "[map_annotation_codec]")
{
    auto bytes = encodeAnnotations(sampleStore());
    bytes.push_back(0);
    REQUIRE(decodeAnnotations(bytes).status == AnnotationDecodeStatus::TrailingBytes);
}

TEST_CASE("bad magic and version are refused", "[map_annotation_codec]")
{
    auto bytes = encodeAnnotations(sampleStore());
    auto wrongMagic = bytes;
    wrongMagic[0] = 'X';
    REQUIRE(decodeAnnotations(wrongMagic).status == AnnotationDecodeStatus::BadMagic);
    auto wrongVersion = bytes;
    wrongVersion[4] = 2;
    REQUIRE(decodeAnnotations(wrongVersion).status == AnnotationDecodeStatus::BadVersion);
}

TEST_CASE("a huge count cannot force a huge allocation", "[map_annotation_codec]")
{
    auto bytes = encodeAnnotations(MapAnnotationStore{});
    putU32(bytes, 5, 0xFFFFFFFFu);
    REQUIRE(decodeAnnotations(bytes).status == AnnotationDecodeStatus::TooMany);
    putU32(bytes, 5, 257);
    REQUIRE(decodeAnnotations(bytes).status == AnnotationDecodeStatus::TooMany);
    putU32(bytes, 5, 3); // within limits but the bytes cannot hold three records
    REQUIRE_FALSE(decodeAnnotations(bytes).ok());
}

TEST_CASE("a stricter limit rejects a larger store", "[map_annotation_codec]")
{
    const auto bytes = encodeAnnotations(sampleStore());
    AnnotationLimits tight;
    tight.maxAnnotations = 2;
    REQUIRE(decodeAnnotations(bytes, tight).status == AnnotationDecodeStatus::TooMany);
}

TEST_CASE("corrupt records are refused, not trusted", "[map_annotation_codec]")
{
    const auto good = encodeAnnotations(sampleStore());
    // kind byte of the first record sits right after the 9 byte header
    auto badKind = good;
    badKind[9] = 9;
    REQUIRE(decodeAnnotations(badKind).status == AnnotationDecodeStatus::InvalidRecord);
    auto badSymbol = good;
    badSymbol[10] = 99;
    REQUIRE(decodeAnnotations(badSymbol).status == AnnotationDecodeStatus::InvalidRecord);
    auto badId = good;
    badId[13] = ' '; // first id byte
    REQUIRE(decodeAnnotations(badId).status == AnnotationDecodeStatus::InvalidRecord);
    auto controlText = good;
    controlText[11 + 2 + 6 + 2] = 0x07; // first text byte
    REQUIRE(decodeAnnotations(controlText).status == AnnotationDecodeStatus::InvalidRecord);
}

TEST_CASE("NaN, infinite and out of range points in bytes are refused", "[map_annotation_codec]")
{
    MapAnnotationStore store;
    MapAnnotation m;
    m.id = "a";
    m.kind = AnnotationKind::Mark;
    m.points = {{1.0f, 1.0f}};
    REQUIRE(store.add(m) == AnnotationResult::Ok);
    const auto good = encodeAnnotations(store);
    const std::size_t xAt = good.size() - 8;

    const float values[] = {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                            -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::max(), 2.0e6f};
    for (float v : values) {
        auto bytes = good;
        std::uint32_t bits = 0;
        std::memcpy(&bits, &v, 4);
        putU32(bytes, xAt, bits);
        REQUIRE(decodeAnnotations(bytes).status == AnnotationDecodeStatus::InvalidRecord);
        auto yBytes = good;
        putU32(yBytes, xAt + 4, bits);
        REQUIRE(decodeAnnotations(yBytes).status == AnnotationDecodeStatus::InvalidRecord);
    }
}

TEST_CASE("a duplicate id in bytes is refused", "[map_annotation_codec]")
{
    MapAnnotationStore store;
    MapAnnotation m;
    m.id = "a";
    m.kind = AnnotationKind::Mark;
    m.points = {{1.0f, 1.0f}};
    REQUIRE(store.add(m) == AnnotationResult::Ok);
    auto one = encodeAnnotations(store);
    auto two = one;
    two.insert(two.end(), one.begin() + 9, one.end());
    putU32(two, 5, 2);
    REQUIRE(decodeAnnotations(two).status == AnnotationDecodeStatus::InvalidRecord);
}

TEST_CASE("oversized lengths in bytes are refused without reading past the end", "[map_annotation_codec]")
{
    auto bytes = encodeAnnotations(sampleStore());
    bytes[11] = 0xFF; // id length low byte of the first record
    bytes[12] = 0xFF;
    REQUIRE_FALSE(decodeAnnotations(bytes).ok());
}

TEST_CASE("random garbage never decodes into a crash", "[map_annotation_codec]")
{
    const auto good = encodeAnnotations(sampleStore());
    std::uint32_t state = 12345;
    auto next = [&state]() {
        state = state * 1664525u + 1013904223u;
        return state >> 24;
    };
    for (int round = 0; round < 2000; ++round) {
        auto bytes = good;
        const int flips = 1 + static_cast<int>(next() % 4);
        for (int i = 0; i < flips; ++i) {
            bytes[next() % bytes.size()] = static_cast<std::uint8_t>(next());
        }
        const auto result = decodeAnnotations(bytes);
        if (result.ok()) {
            REQUIRE(decodeAnnotations(encodeAnnotations(result.store)).ok());
        } else {
            REQUIRE(result.store.size() == 0);
        }
    }
}
