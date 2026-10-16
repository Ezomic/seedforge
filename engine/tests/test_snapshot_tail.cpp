#include <catch2/catch_test_macros.hpp>

#include "ichor/save/snapshot_tail.h"

#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

using namespace ichor;

namespace {

using Bytes = std::vector<std::uint8_t>;

DeltaState step(const DeltaState& from, int n)
{
    DeltaState s = from;
    s["chest/" + std::to_string(n % 5)] = Bytes{static_cast<std::uint8_t>(n), static_cast<std::uint8_t>(n * 7)};
    if (n % 3 == 0) {
        s.erase("chest/" + std::to_string((n + 2) % 5));
    }
    s["clock"] = Bytes{static_cast<std::uint8_t>(n)};
    return s;
}

DeltaState baseState()
{
    return {{"world/seed", Bytes{1, 2, 3}}, {"npc/1", Bytes{9}}};
}

struct Built {
    SnapshotTailLog log;
    std::vector<DeltaState> states;
};

Built build(int records)
{
    DeltaState s = baseState();
    Built b{*SnapshotTailLog::fromSnapshot(s), {s}};
    for (int i = 1; i <= records; ++i) {
        s = step(s, i);
        REQUIRE(b.log.append(s));
        b.states.push_back(s);
    }
    return b;
}

std::size_t snapshotImageSize()
{
    return SnapshotTailLog::fromSnapshot(baseState())->encode()->size();
}

}

TEST_CASE("a log round-trips through its image", "[save][snapshot]")
{
    const auto b = build(6);
    const auto image = b.log.encode();
    REQUIRE(image.has_value());
    TailLoadResult r;
    const auto loaded = SnapshotTailLog::load(*image, &r);
    REQUIRE(loaded.has_value());
    CHECK(r.status == TailLoadStatus::Clean);
    CHECK(r.recordsApplied == 6);
    CHECK(r.bytesDiscarded == 0);
    CHECK(loaded->state() == b.log.state());
    CHECK(loaded->snapshot() == b.states.front());
    CHECK(*loaded->encode() == *image);
}

TEST_CASE("appending an unchanged state adds no record", "[save][snapshot]")
{
    auto b = build(2);
    CHECK(b.log.append(b.log.state()));
    CHECK(b.log.tailLength() == 2);
}

TEST_CASE("compaction keeps the state and empties the tail", "[save][snapshot]")
{
    auto b = build(5);
    const auto before = b.log.state();
    b.log.compact();
    CHECK(b.log.tailLength() == 0);
    CHECK(b.log.state() == before);
    CHECK(b.log.snapshot() == before);
    const auto loaded = SnapshotTailLog::load(*b.log.encode());
    REQUIRE(loaded.has_value());
    CHECK(loaded->state() == before);
}

TEST_CASE("appended frames extend an existing image byte for byte", "[save][snapshot]")
{
    const auto b = build(4);
    Bytes image = *SnapshotTailLog::fromSnapshot(b.states.front())->encode();
    for (std::size_t i = 0; i < b.log.tailLength(); ++i) {
        const auto frame = b.log.encodeTailFrame(i);
        REQUIRE(frame.has_value());
        image.insert(image.end(), frame->begin(), frame->end());
    }
    CHECK(image == *b.log.encode());
    CHECK_FALSE(b.log.encodeTailFrame(4).has_value());
}

TEST_CASE("truncating the image at every offset yields an exact earlier state or a refusal", "[save][snapshot][fault]")
{
    const auto b = build(7);
    const auto image = *b.log.encode();
    const std::size_t snapshotEnd = snapshotImageSize();
    std::size_t clean = 0;
    for (std::size_t cut = 0; cut <= image.size(); ++cut) {
        const Bytes part(image.begin(), image.begin() + static_cast<std::ptrdiff_t>(cut));
        TailLoadResult r;
        const auto loaded = SnapshotTailLog::load(part, &r);
        if (cut < snapshotEnd) {
            CHECK_FALSE(loaded.has_value());
            CHECK(r.status == TailLoadStatus::Unreadable);
            continue;
        }
        REQUIRE(loaded.has_value());
        REQUIRE(r.recordsApplied <= 7);
        CHECK(loaded->state() == b.states[r.recordsApplied]);
        CHECK(loaded->tailLength() == r.recordsApplied);
        if (r.status == TailLoadStatus::Clean) {
            ++clean;
            CHECK(r.bytesDiscarded == 0);
        } else {
            CHECK(r.status == TailLoadStatus::RecoveredTornTail);
            CHECK(r.bytesDiscarded > 0);
        }
    }
    CHECK(clean == 8);
}

TEST_CASE("flipping any byte never produces a state that was not in the history", "[save][snapshot][fault]")
{
    const auto b = build(5);
    const auto image = *b.log.encode();
    const std::size_t snapshotEnd = snapshotImageSize();
    for (std::size_t at = 0; at < image.size(); ++at) {
        for (const std::uint8_t mask : {0x01, 0x80, 0xFF}) {
            Bytes bad = image;
            bad[at] ^= mask;
            TailLoadResult r;
            const auto loaded = SnapshotTailLog::load(bad, &r);
            if (at < snapshotEnd) {
                CHECK_FALSE(loaded.has_value());
                continue;
            }
            REQUIRE(loaded.has_value());
            CHECK(r.status == TailLoadStatus::RecoveredTornTail);
            REQUIRE(r.recordsApplied < 5);
            CHECK(loaded->state() == b.states[r.recordsApplied]);
        }
    }
}

TEST_CASE("a damaged frame is not skipped to reach later ones", "[save][snapshot][fault]")
{
    const auto b = build(4);
    Bytes bad = *b.log.encode();
    const std::size_t secondFrame = snapshotImageSize() + b.log.encodeTailFrame(0)->size();
    bad[secondFrame + 9] ^= 0x10;
    TailLoadResult r;
    const auto loaded = SnapshotTailLog::load(bad, &r);
    REQUIRE(loaded.has_value());
    CHECK(r.recordsApplied == 1);
    CHECK(loaded->state() == b.states[1]);
}

TEST_CASE("a frame spliced to the wrong position is rejected", "[save][snapshot][fault]")
{
    const auto b = build(3);
    Bytes image = *SnapshotTailLog::fromSnapshot(b.states.front())->encode();
    const auto f1 = *b.log.encodeTailFrame(1);
    image.insert(image.end(), f1.begin(), f1.end());
    TailLoadResult r;
    const auto loaded = SnapshotTailLog::load(image, &r);
    REQUIRE(loaded.has_value());
    CHECK(r.recordsApplied == 0);
    CHECK(r.status == TailLoadStatus::RecoveredTornTail);
}

TEST_CASE("hostile images never crash or over-allocate", "[save][snapshot][fault]")
{
    CHECK_FALSE(SnapshotTailLog::load({}).has_value());
    CHECK_FALSE(SnapshotTailLog::load(Bytes(3, 0xFF)).has_value());
    CHECK_FALSE(SnapshotTailLog::load(Bytes(64, 0)).has_value());
    const auto good = *build(2).log.encode();
    Bytes huge = good;
    huge[5] = huge[6] = huge[7] = huge[8] = 0xFF;
    CHECK_FALSE(SnapshotTailLog::load(huge).has_value());
    std::mt19937 rng(7);
    for (int i = 0; i < 300; ++i) {
        Bytes junk(rng() % 80);
        for (auto& v : junk) {
            v = static_cast<std::uint8_t>(rng());
        }
        (void)SnapshotTailLog::load(junk);
        Bytes mutated = good;
        mutated[rng() % mutated.size()] = static_cast<std::uint8_t>(rng());
        (void)SnapshotTailLog::load(mutated);
    }
}

TEST_CASE("a full tail refuses to grow until compacted", "[save][snapshot]")
{
    auto log = *SnapshotTailLog::fromSnapshot({});
    DeltaState s;
    for (std::size_t i = 0; i < snapshot_limits::kMaxTailRecords; ++i) {
        s["n"] = Bytes{static_cast<std::uint8_t>(i), static_cast<std::uint8_t>(i >> 8), static_cast<std::uint8_t>(i >> 16)};
        REQUIRE(log.append(s));
    }
    s["n"] = Bytes{0xAA};
    CHECK_FALSE(log.append(s));
    CHECK(log.tailLength() == snapshot_limits::kMaxTailRecords);
    log.compact();
    CHECK(log.append(s));
}

TEST_CASE("an oversized state is refused without changing the log", "[save][snapshot]")
{
    auto b = build(1);
    DeltaState bad = b.log.state();
    bad["big"] = Bytes(delta_limits::kMaxValueBytes + 1, 0);
    CHECK_FALSE(b.log.append(bad));
    CHECK(b.log.tailLength() == 1);
    CHECK_FALSE(SnapshotTailLog::fromSnapshot(bad).has_value());
}

TEST_CASE("crc32 matches the standard check value", "[save][snapshot]")
{
    const std::string s = "123456789";
    CHECK(crc32(reinterpret_cast<const std::uint8_t*>(s.data()), s.size()) == 0xCBF43926u);
}
