#include "ichor/save/death_markers.h"

#include <algorithm>
#include <cstring>
#include <tuple>

namespace ichor {

struct DeathMarkerSetAccess {
    static std::vector<DeathMarker>& markers(DeathMarkerSet& set) { return set.markers_; }
};

namespace {

constexpr std::uint8_t kMagic[4] = {'I', 'C', 'D', 'M'};
constexpr std::uint8_t kVersion = 1;
constexpr std::size_t kHeaderBytes = 4 + 1 + 8 + 4 + 4 + 4;
constexpr std::size_t kRecordBytes = 4 + 4 + 4 + 1 + 4;

std::int64_t floorDiv(std::int64_t value, std::int64_t divisor)
{
    std::int64_t q = value / divisor;
    if (value % divisor != 0 && value < 0) {
        --q;
    }
    return q;
}

using CellKey = std::pair<std::int64_t, std::int64_t>;

CellKey cellOf(const DeathMarker& m)
{
    return {floorDiv(m.xCm, kDeathMarkerCellCm), floorDiv(m.zCm, kDeathMarkerCellCm)};
}

auto canonicalKey(const DeathMarker& m)
{
    return std::make_tuple(cellOf(m), m.xCm, m.yCm, m.zCm, static_cast<std::uint8_t>(m.cause), m.day);
}

bool canonicalLess(const DeathMarker& a, const DeathMarker& b)
{
    return canonicalKey(a) < canonicalKey(b);
}

// Strict total order: newer first, then canonical order. Distinct markers never tie.
bool outranks(const DeathMarker& a, const DeathMarker& b)
{
    if (a.day != b.day) {
        return a.day > b.day;
    }
    return canonicalLess(a, b);
}

void putU32(std::vector<std::uint8_t>& out, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    }
}

void putU64(std::vector<std::uint8_t>& out, std::uint64_t v)
{
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    }
}

std::uint32_t getU32(const std::uint8_t* p)
{
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
        v |= static_cast<std::uint32_t>(p[i]) << (8 * i);
    }
    return v;
}

std::uint64_t getU64(const std::uint8_t* p)
{
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        v |= static_cast<std::uint64_t>(p[i]) << (8 * i);
    }
    return v;
}

DeathMarkerLimits clampLimits(DeathMarkerLimits limits)
{
    limits.maxPerCell = std::max<std::uint32_t>(limits.maxPerCell, 1);
    limits.maxTotal = std::clamp<std::uint32_t>(limits.maxTotal, 1, kDeathMarkerMaxTotalCeiling);
    return limits;
}

} // namespace

bool isValidDeathMarker(const DeathMarker& marker)
{
    const auto inRange = [](std::int32_t v) { return v >= -kDeathMarkerMaxCoord && v <= kDeathMarkerMaxCoord; };
    return inRange(marker.xCm) && inRange(marker.yCm) && inRange(marker.zCm)
        && static_cast<std::uint8_t>(marker.cause) < static_cast<std::uint8_t>(DeathCause::Count_);
}

DeathMarkerSet::DeathMarkerSet(std::uint64_t seed, DeathMarkerLimits limits)
    : seed_(seed)
    , limits_(clampLimits(limits))
{
}

void DeathMarkerSet::enforceCaps()
{
    std::sort(markers_.begin(), markers_.end(), canonicalLess);
    markers_.erase(std::unique(markers_.begin(), markers_.end()), markers_.end());

    std::vector<DeathMarker> kept;
    kept.reserve(markers_.size());
    for (std::size_t begin = 0; begin < markers_.size();) {
        const CellKey cell = cellOf(markers_[begin]);
        std::size_t end = begin;
        while (end < markers_.size() && cellOf(markers_[end]) == cell) {
            ++end;
        }
        const auto first = markers_.begin() + static_cast<std::ptrdiff_t>(begin);
        const auto last = markers_.begin() + static_cast<std::ptrdiff_t>(end);
        if (end - begin > limits_.maxPerCell) {
            const auto cut = first + static_cast<std::ptrdiff_t>(limits_.maxPerCell);
            std::nth_element(first, cut, last, outranks);
            kept.insert(kept.end(), first, cut);
        } else {
            kept.insert(kept.end(), first, last);
        }
        begin = end;
    }

    if (kept.size() > limits_.maxTotal) {
        std::nth_element(kept.begin(), kept.begin() + static_cast<std::ptrdiff_t>(limits_.maxTotal), kept.end(), outranks);
        kept.resize(limits_.maxTotal);
    }
    std::sort(kept.begin(), kept.end(), canonicalLess);
    markers_ = std::move(kept);
}

DeathAddResult DeathMarkerSet::add(const DeathMarker& marker)
{
    if (!isValidDeathMarker(marker)) {
        return DeathAddResult::Invalid;
    }
    if (std::binary_search(markers_.begin(), markers_.end(), marker, canonicalLess)) {
        return DeathAddResult::Duplicate;
    }
    markers_.push_back(marker);
    enforceCaps();
    return std::binary_search(markers_.begin(), markers_.end(), marker, canonicalLess) ? DeathAddResult::Added
                                                                                       : DeathAddResult::Dropped;
}

DeathMergeResult DeathMarkerSet::merge(const DeathMarkerSet& other)
{
    if (other.seed_ != seed_) {
        return DeathMergeResult::SeedMismatch;
    }
    if (!(other.limits_ == limits_)) {
        return DeathMergeResult::LimitsMismatch;
    }
    if (&other == this) {
        return DeathMergeResult::Ok;
    }
    markers_.insert(markers_.end(), other.markers_.begin(), other.markers_.end());
    enforceCaps();
    return DeathMergeResult::Ok;
}

std::vector<std::uint8_t> encodeDeathMarkers(const DeathMarkerSet& set)
{
    std::vector<std::uint8_t> out(std::begin(kMagic), std::end(kMagic));
    out.reserve(kHeaderBytes + set.size() * kRecordBytes);
    out.push_back(kVersion);
    putU64(out, set.seed());
    putU32(out, set.limits().maxPerCell);
    putU32(out, set.limits().maxTotal);
    putU32(out, static_cast<std::uint32_t>(set.size()));
    for (const auto& m : set.all()) {
        putU32(out, static_cast<std::uint32_t>(m.xCm));
        putU32(out, static_cast<std::uint32_t>(m.yCm));
        putU32(out, static_cast<std::uint32_t>(m.zCm));
        out.push_back(static_cast<std::uint8_t>(m.cause));
        putU32(out, m.day);
    }
    return out;
}

DeathDecodeResult decodeDeathMarkers(std::span<const std::uint8_t> bytes)
{
    const auto fail = [](DeathDecodeStatus status) { return DeathDecodeResult{status, DeathMarkerSet(0)}; };
    if (bytes.size() < kHeaderBytes) {
        return fail(DeathDecodeStatus::Truncated);
    }
    if (std::memcmp(bytes.data(), kMagic, sizeof kMagic) != 0) {
        return fail(DeathDecodeStatus::BadMagic);
    }
    if (bytes[4] != kVersion) {
        return fail(DeathDecodeStatus::BadVersion);
    }
    const std::uint64_t seed = getU64(bytes.data() + 5);
    const DeathMarkerLimits limits{getU32(bytes.data() + 13), getU32(bytes.data() + 17)};
    const std::uint32_t count = getU32(bytes.data() + 21);
    if (limits.maxPerCell == 0 || limits.maxTotal == 0 || limits.maxTotal > kDeathMarkerMaxTotalCeiling) {
        return fail(DeathDecodeStatus::BadLimits);
    }
    const std::size_t body = bytes.size() - kHeaderBytes;
    if (count > limits.maxTotal) {
        return fail(DeathDecodeStatus::TooMany);
    }
    const std::uint64_t expected = static_cast<std::uint64_t>(count) * kRecordBytes;
    if (expected > body) {
        return fail(DeathDecodeStatus::Truncated);
    }
    if (expected != body) {
        return fail(DeathDecodeStatus::TrailingBytes);
    }

    std::vector<DeathMarker> markers;
    markers.reserve(count);
    std::uint32_t inCell = 0;
    const std::uint8_t* p = bytes.data() + kHeaderBytes;
    for (std::uint32_t i = 0; i < count; ++i, p += kRecordBytes) {
        DeathMarker m;
        m.xCm = static_cast<std::int32_t>(getU32(p));
        m.yCm = static_cast<std::int32_t>(getU32(p + 4));
        m.zCm = static_cast<std::int32_t>(getU32(p + 8));
        m.cause = static_cast<DeathCause>(p[12]);
        m.day = getU32(p + 13);
        if (!isValidDeathMarker(m) || (!markers.empty() && !canonicalLess(markers.back(), m))) {
            return fail(DeathDecodeStatus::InvalidRecord);
        }
        inCell = (!markers.empty() && cellOf(markers.back()) == cellOf(m)) ? inCell + 1 : 1;
        if (inCell > limits.maxPerCell) {
            return fail(DeathDecodeStatus::InvalidRecord);
        }
        markers.push_back(m);
    }

    DeathMarkerSet set(seed, limits);
    DeathMarkerSetAccess::markers(set) = std::move(markers);
    return {DeathDecodeStatus::Ok, std::move(set)};
}

} // namespace ichor
