#include "ichor/save/death_markers.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <random>
#include <vector>

using namespace ichor;

namespace {

constexpr std::uint64_t kSeed = 0xC0FFEE;

DeathMarker marker(std::int32_t x, std::int32_t z, std::uint32_t day = 1, DeathCause cause = DeathCause::Combat)
{
    return DeathMarker{x, 0, z, cause, day};
}

DeathMarkerSet setOf(const std::vector<DeathMarker>& markers, DeathMarkerLimits limits = {})
{
    DeathMarkerSet set(kSeed, limits);
    for (const auto& m : markers) {
        set.add(m);
    }
    return set;
}

// Many markers crowded into few cells so both caps bite.
std::vector<DeathMarker> crowd(std::uint32_t n, std::uint32_t salt)
{
    std::mt19937 rng(salt);
    std::vector<DeathMarker> out;
    for (std::uint32_t i = 0; i < n; ++i) {
        out.push_back(marker(static_cast<std::int32_t>(rng() % 6400), static_cast<std::int32_t>(rng() % 6400), rng() % 50));
    }
    return out;
}

} // namespace

TEST_CASE("markers add, dedupe and reject invalid input", "[death_markers]")
{
    DeathMarkerSet set(kSeed);
    REQUIRE(set.add(marker(10, 10)) == DeathAddResult::Added);
    REQUIRE(set.add(marker(10, 10)) == DeathAddResult::Duplicate);
    REQUIRE(set.add(marker(kDeathMarkerMaxCoord + 1, 0)) == DeathAddResult::Invalid);
    REQUIRE(set.add(marker(-kDeathMarkerMaxCoord - 1, 0)) == DeathAddResult::Invalid);
    REQUIRE(set.add(marker(INT32_MIN, 0)) == DeathAddResult::Invalid);
    REQUIRE(set.add(marker(0, 0, 1, DeathCause::Count_)) == DeathAddResult::Invalid);
    REQUIRE(set.add(marker(kDeathMarkerMaxCoord, -kDeathMarkerMaxCoord)) == DeathAddResult::Added);
    REQUIRE(set.size() == 2);
}

TEST_CASE("a full cell keeps the newest markers and refuses older ones", "[death_markers]")
{
    DeathMarkerSet set(kSeed, {.maxPerCell = 2, .maxTotal = 100});
    REQUIRE(set.add(marker(1, 1, 5)) == DeathAddResult::Added);
    REQUIRE(set.add(marker(2, 2, 6)) == DeathAddResult::Added);
    REQUIRE(set.add(marker(3, 3, 1)) == DeathAddResult::Dropped);
    REQUIRE(set.add(marker(4, 4, 9)) == DeathAddResult::Added);
    REQUIRE(set.size() == 2);
    std::vector<std::uint32_t> days;
    for (const auto& m : set.all()) days.push_back(m.day);
    std::sort(days.begin(), days.end());
    REQUIRE(days == std::vector<std::uint32_t>{6, 9});
}

TEST_CASE("cells split on negative coordinates and different cells have separate caps", "[death_markers]")
{
    DeathMarkerSet set(kSeed, {.maxPerCell = 1, .maxTotal = 100});
    REQUIRE(set.add(marker(-1, 0)) == DeathAddResult::Added);
    REQUIRE(set.add(marker(0, 0)) == DeathAddResult::Added);
    REQUIRE(set.add(marker(-kDeathMarkerCellCm, 0, 0)) == DeathAddResult::Dropped); // same cell as x=-1, older
    REQUIRE(set.add(marker(kDeathMarkerCellCm - 1, 0, 0)) == DeathAddResult::Dropped); // same cell as x=0
    REQUIRE(set.size() == 2);
}

TEST_CASE("the total cap bounds the set and drops the oldest", "[death_markers]")
{
    DeathMarkerSet set(kSeed, {.maxPerCell = 4, .maxTotal = 3});
    for (std::uint32_t i = 0; i < 10; ++i) {
        set.add(marker(static_cast<std::int32_t>(i) * kDeathMarkerCellCm, 0, i));
    }
    REQUIRE(set.size() == 3);
    for (const auto& m : set.all()) REQUIRE(m.day >= 7);
}

TEST_CASE("absurd limits are clamped", "[death_markers]")
{
    DeathMarkerSet set(kSeed, {.maxPerCell = 0, .maxTotal = 0xFFFFFFFFu});
    REQUIRE(set.limits().maxPerCell == 1);
    REQUIRE(set.limits().maxTotal == kDeathMarkerMaxTotalCeiling);
}

TEST_CASE("add order never changes the result, even when caps bite", "[death_markers]")
{
    const DeathMarkerLimits limits{.maxPerCell = 2, .maxTotal = 20};
    auto markers = crowd(300, 1);
    const auto reference = setOf(markers, limits);
    REQUIRE(reference.size() == 20);
    for (std::uint32_t salt = 0; salt < 5; ++salt) {
        std::mt19937 rng(salt);
        std::shuffle(markers.begin(), markers.end(), rng);
        REQUIRE(setOf(markers, limits) == reference);
    }
}

TEST_CASE("merge is commutative, associative and idempotent", "[death_markers]")
{
    const DeathMarkerLimits limits{.maxPerCell = 2, .maxTotal = 15};
    const auto a = setOf(crowd(80, 11), limits);
    const auto b = setOf(crowd(80, 12), limits);
    const auto c = setOf(crowd(80, 13), limits);

    auto ab = a;
    REQUIRE(ab.merge(b) == DeathMergeResult::Ok);
    auto ba = b;
    REQUIRE(ba.merge(a) == DeathMergeResult::Ok);
    REQUIRE(ab == ba);

    auto abThenC = ab;
    abThenC.merge(c);
    auto bc = b;
    bc.merge(c);
    auto aThenBc = a;
    aThenBc.merge(bc);
    REQUIRE(abThenC == aThenBc);

    auto twice = ab;
    twice.merge(ab);
    REQUIRE(twice == ab);
    auto self = ab;
    REQUIRE(self.merge(self) == DeathMergeResult::Ok);
    REQUIRE(self == ab);

    // The merge is the same as adding everything to one set.
    auto all = crowd(80, 11);
    for (auto s : {crowd(80, 12), crowd(80, 13)}) all.insert(all.end(), s.begin(), s.end());
    auto abc = abThenC;
    REQUIRE(abc == setOf(all, limits));
}

TEST_CASE("merge refuses another seed or other limits and leaves the set unchanged", "[death_markers]")
{
    auto a = setOf({marker(1, 1)});
    const DeathMarkerSet otherSeed(kSeed + 1);
    REQUIRE(a.merge(otherSeed) == DeathMergeResult::SeedMismatch);
    const DeathMarkerSet otherLimits(kSeed, {.maxPerCell = 9, .maxTotal = 9});
    REQUIRE(a.merge(otherLimits) == DeathMergeResult::LimitsMismatch);
    REQUIRE(a.size() == 1);
}

TEST_CASE("codec round trips and is canonical", "[death_markers]")
{
    auto markers = crowd(200, 5);
    markers.push_back(marker(-kDeathMarkerMaxCoord, kDeathMarkerMaxCoord, 0xFFFFFFFFu, DeathCause::Drowning));
    const auto set = setOf(markers);
    const auto bytes = encodeDeathMarkers(set);
    const auto decoded = decodeDeathMarkers(bytes);
    REQUIRE(decoded.ok());
    REQUIRE(decoded.set == set);
    REQUIRE(encodeDeathMarkers(decoded.set) == bytes);

    std::reverse(markers.begin(), markers.end());
    REQUIRE(encodeDeathMarkers(setOf(markers)) == bytes);
}

TEST_CASE("an empty set round trips", "[death_markers]")
{
    const DeathMarkerSet set(42);
    const auto decoded = decodeDeathMarkers(encodeDeathMarkers(set));
    REQUIRE(decoded.ok());
    REQUIRE(decoded.set == set);
    REQUIRE(decoded.set.seed() == 42);
}

TEST_CASE("every truncation and every single byte flip decodes without crashing", "[death_markers]")
{
    const auto set = setOf(crowd(12, 3));
    const auto bytes = encodeDeathMarkers(set);
    REQUIRE(decodeDeathMarkers(bytes).ok());
    for (std::size_t n = 0; n < bytes.size(); ++n) {
        const auto r = decodeDeathMarkers(std::span(bytes.data(), n));
        REQUIRE_FALSE(r.ok());
        REQUIRE(r.set.size() == 0);
    }
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        auto copy = bytes;
        copy[i] ^= 0xFF;
        const auto r = decodeDeathMarkers(copy);
        if (r.ok()) {
            REQUIRE(encodeDeathMarkers(r.set) == copy); // an accepted blob is always canonical
        } else {
            REQUIRE(r.set.size() == 0);
        }
    }
}

TEST_CASE("decoder rejects hostile headers", "[death_markers]")
{
    const auto good = encodeDeathMarkers(setOf({marker(1, 1), marker(5000, 5000)}));
    const auto with = [&](std::size_t offset, std::uint32_t value) {
        auto copy = good;
        for (int i = 0; i < 4; ++i) copy[offset + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(value >> (8 * i));
        return decodeDeathMarkers(copy).status;
    };
    constexpr std::size_t kCapOffset = 13, kTotalOffset = 17, kCountOffset = 21;
    REQUIRE(with(kCountOffset, 0xFFFFFFFFu) == DeathDecodeStatus::TooMany);
    REQUIRE(with(kCountOffset, 3) == DeathDecodeStatus::Truncated);
    REQUIRE(with(kCountOffset, 1) == DeathDecodeStatus::TrailingBytes);
    REQUIRE(with(kTotalOffset, 0) == DeathDecodeStatus::BadLimits);
    REQUIRE(with(kTotalOffset, 0xFFFFFFFFu) == DeathDecodeStatus::BadLimits);
    REQUIRE(with(kCapOffset, 0) == DeathDecodeStatus::BadLimits);

    auto bad = good;
    bad[0] = 'X';
    REQUIRE(decodeDeathMarkers(bad).status == DeathDecodeStatus::BadMagic);
    bad = good;
    bad[4] = 2;
    REQUIRE(decodeDeathMarkers(bad).status == DeathDecodeStatus::BadVersion);
    bad = good;
    bad.push_back(0);
    REQUIRE(decodeDeathMarkers(bad).status == DeathDecodeStatus::TrailingBytes);
}

TEST_CASE("decoder rejects duplicates, disorder, bad causes and over-cap cells", "[death_markers]")
{
    const auto good = encodeDeathMarkers(setOf({marker(1, 1, 1), marker(5000, 5000, 2)}));
    constexpr std::size_t kHeader = 25, kRecord = 17;

    auto dup = good;
    std::copy(good.begin() + kHeader, good.begin() + kHeader + kRecord, dup.begin() + kHeader + kRecord);
    REQUIRE(decodeDeathMarkers(dup).status == DeathDecodeStatus::InvalidRecord);

    auto swapped = good;
    std::copy(good.begin() + kHeader + kRecord, good.end(), swapped.begin() + kHeader);
    std::copy(good.begin() + kHeader, good.begin() + kHeader + kRecord, swapped.begin() + kHeader + kRecord);
    REQUIRE(decodeDeathMarkers(swapped).status == DeathDecodeStatus::InvalidRecord);

    auto cause = good;
    cause[kHeader + 12] = static_cast<std::uint8_t>(DeathCause::Count_);
    REQUIRE(decodeDeathMarkers(cause).status == DeathDecodeStatus::InvalidRecord);

    auto outOfRange = good;
    const std::uint32_t big = 0x7FFFFFFFu;
    for (int i = 0; i < 4; ++i) outOfRange[kHeader + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(big >> (8 * i));
    REQUIRE(decodeDeathMarkers(outOfRange).status == DeathDecodeStatus::InvalidRecord);

    // Two markers in one cell with maxPerCell 1 in the header.
    auto crowded = encodeDeathMarkers(setOf({marker(1, 1, 1), marker(2, 2, 2)}));
    crowded[13] = 1;
    REQUIRE(decodeDeathMarkers(crowded).status == DeathDecodeStatus::InvalidRecord);
}
