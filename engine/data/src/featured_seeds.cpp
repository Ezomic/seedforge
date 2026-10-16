#include "ichor/data/featured_seeds.h"

#include "ichor/core/hash.h"

#include <algorithm>
#include <functional>
#include <utility>
#include <set>

namespace ichor {

namespace {

constexpr std::string_view kPrefix = "ichorf1";
constexpr std::size_t kMaxPayloadBytes = 2 + kMaxFeaturedSeeds * (8 + 1 + kMaxFeaturedTitleBytes + 1 + kMaxFeaturedAxioms * (1 + kMaxFeaturedAxiomIdBytes));
constexpr std::size_t kMaxTextBytes = kPrefix.size() + 1 + 2 * kMaxPayloadBytes + 1 + 4;

bool printable(std::string_view text)
{
    return std::all_of(text.begin(), text.end(), [](unsigned char c) { return c >= 0x20 && c <= 0x7e; });
}

bool titleOk(std::string_view title)
{
    return !title.empty() && title.size() <= kMaxFeaturedTitleBytes && printable(title);
}

bool axiomIdOk(std::string_view id)
{
    return !id.empty() && id.size() <= kMaxFeaturedAxiomIdBytes && printable(id);
}

bool entryShapeOk(const FeaturedSeed& entry)
{
    return titleOk(entry.title) && entry.expectedAxioms.size() <= kMaxFeaturedAxioms
        && std::all_of(entry.expectedAxioms.begin(), entry.expectedAxioms.end(), [](const std::string& id) { return axiomIdOk(id); });
}

std::uint16_t checkOf(std::string_view payload)
{
    return static_cast<std::uint16_t>(mix64(hash64(payload)) >> 48);
}

void appendHex(std::string& out, std::uint64_t value, int digits)
{
    constexpr char kDigits[] = "0123456789abcdef";
    for (int i = digits - 1; i >= 0; --i) {
        out.push_back(kDigits[(value >> (4 * i)) & 0xf]);
    }
}

int hexValue(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return -1;
}

class Reader {
public:
    explicit Reader(std::string_view bytes)
        : bytes_(bytes)
    {
    }
    bool u8(std::uint8_t& out)
    {
        if (pos_ >= bytes_.size()) {
            return false;
        }
        out = static_cast<std::uint8_t>(bytes_[pos_++]);
        return true;
    }
    bool u16(std::uint16_t& out)
    {
        std::uint8_t lo = 0;
        std::uint8_t hi = 0;
        if (!u8(lo) || !u8(hi)) {
            return false;
        }
        out = static_cast<std::uint16_t>(lo | (hi << 8));
        return true;
    }
    bool u64(std::uint64_t& out)
    {
        out = 0;
        for (int i = 0; i < 8; ++i) {
            std::uint8_t b = 0;
            if (!u8(b)) {
                return false;
            }
            out |= static_cast<std::uint64_t>(b) << (8 * i);
        }
        return true;
    }
    bool str(std::size_t length, std::string& out)
    {
        if (bytes_.size() - pos_ < length) {
            return false;
        }
        out.assign(bytes_.substr(pos_, length));
        pos_ += length;
        return true;
    }
    bool done() const { return pos_ == bytes_.size(); }

private:
    std::string_view bytes_;
    std::size_t pos_ = 0;
};

FeaturedCheck fail(FeaturedProblem problem, std::string message, std::size_t index = 0)
{
    return FeaturedCheck{problem, index, std::move(message)};
}

std::int64_t floorDiv(std::int64_t a, std::int64_t b)
{
    std::int64_t q = a / b;
    if (a % b < 0) {
        --q;
    }
    return q;
}

}

std::int64_t featuredWeekOf(std::int64_t dayIndex)
{
    return floorDiv(dayIndex, kFeaturedPeriodDays);
}

std::optional<std::size_t> featuredIndexForDay(std::size_t listSize, std::int64_t dayIndex)
{
    if (listSize == 0) {
        return std::nullopt;
    }
    const std::int64_t week = featuredWeekOf(dayIndex);
    const std::uint64_t n = listSize;
    // -(week + 1) is the non-negative mirror of a negative week, so no signed overflow at the extremes.
    const std::uint64_t r = static_cast<std::uint64_t>(week < 0 ? -(week + 1) : week) % n;
    return static_cast<std::size_t>(week < 0 ? n - 1 - r : r);
}

const FeaturedSeed* featuredForDay(std::span<const FeaturedSeed> list, std::int64_t dayIndex)
{
    const std::optional<std::size_t> index = featuredIndexForDay(list.size(), dayIndex);
    return index ? &list[*index] : nullptr;
}

FeaturedCheck validateFeaturedSeed(const FeaturedSeed& entry, std::span<const Axiom> pool, const AxiomCoexist& mayCoexist)
{
    if (!titleOk(entry.title)) {
        return fail(FeaturedProblem::BadTitle, "title must be 1 to 64 printable ASCII bytes");
    }
    if (entry.expectedAxioms.size() > kMaxFeaturedAxioms) {
        return fail(FeaturedProblem::TooManyAxioms, "too many pinned axioms");
    }
    for (const std::string& id : entry.expectedAxioms) {
        if (!axiomIdOk(id)) {
            return fail(FeaturedProblem::BadAxiomId, "pinned axiom id must be 1 to 64 printable ASCII bytes");
        }
    }

    const SlotRollResult roll = rollSlots(entry.seed, pool, mayCoexist);
    if (!roll.ok()) {
        return fail(FeaturedProblem::NoSlotRoll, roll.message);
    }
    if (entry.expectedAxioms.empty()) {
        return {};
    }

    std::set<std::string, std::less<>> known;
    for (const Axiom& axiom : pool) {
        known.insert(axiom.id);
    }
    for (const std::string& id : entry.expectedAxioms) {
        if (!known.contains(id)) {
            return fail(FeaturedProblem::UnknownExpected, "pinned axiom is not in the pool: " + id);
        }
    }

    std::vector<std::string> rolled;
    for (const SlotAssignment& slot : roll.slots) {
        rolled.push_back(slot.axiomId);
    }
    std::vector<std::string> expected = entry.expectedAxioms;
    std::sort(rolled.begin(), rolled.end());
    std::sort(expected.begin(), expected.end());
    if (rolled != expected) {
        return fail(FeaturedProblem::RolledMismatch, "the seed rolls a different axiom set than the entry pins");
    }
    return {};
}

FeaturedCheck validateFeaturedList(std::span<const FeaturedSeed> list, std::span<const Axiom> pool, const AxiomCoexist& mayCoexist)
{
    if (list.empty()) {
        return fail(FeaturedProblem::Empty, "the featured list is empty");
    }
    if (list.size() > kMaxFeaturedSeeds) {
        return fail(FeaturedProblem::TooManyEntries, "too many featured seeds");
    }
    std::set<std::uint64_t> seen;
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (!seen.insert(list[i].seed).second) {
            return fail(FeaturedProblem::DuplicateSeed, "two entries share a seed", i);
        }
        FeaturedCheck check = validateFeaturedSeed(list[i], pool, mayCoexist);
        if (!check.ok()) {
            check.index = i;
            return check;
        }
    }
    return {};
}

std::optional<std::string> encodeFeaturedList(std::span<const FeaturedSeed> list)
{
    if (list.size() > kMaxFeaturedSeeds || !std::all_of(list.begin(), list.end(), entryShapeOk)) {
        return std::nullopt;
    }

    std::string payload;
    const auto putByte = [&](std::size_t v) { payload.push_back(static_cast<char>(v & 0xff)); };
    putByte(list.size());
    putByte(list.size() >> 8);
    for (const FeaturedSeed& entry : list) {
        for (int i = 0; i < 8; ++i) {
            putByte(static_cast<std::size_t>(entry.seed >> (8 * i)));
        }
        putByte(entry.title.size());
        payload += entry.title;
        putByte(entry.expectedAxioms.size());
        for (const std::string& id : entry.expectedAxioms) {
            putByte(id.size());
            payload += id;
        }
    }

    std::string out(kPrefix);
    out.push_back('.');
    for (const char c : payload) {
        appendHex(out, static_cast<unsigned char>(c), 2);
    }
    out.push_back('.');
    appendHex(out, checkOf(payload), 4);
    return out;
}

std::optional<std::vector<FeaturedSeed>> decodeFeaturedList(std::string_view text)
{
    constexpr std::size_t kHead = kPrefix.size() + 1;
    if (text.size() > kMaxTextBytes || text.size() < kHead + 4 + 1 + 4 || text.substr(0, kPrefix.size()) != kPrefix || text[kPrefix.size()] != '.'
        || text[text.size() - 5] != '.') {
        return std::nullopt;
    }
    const std::string_view hex = text.substr(kHead, text.size() - 5 - kHead);
    if (hex.size() % 2 != 0) {
        return std::nullopt;
    }
    std::string payload;
    payload.reserve(hex.size() / 2);
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        const int hi = hexValue(hex[i]);
        const int lo = hexValue(hex[i + 1]);
        if (hi < 0 || lo < 0) {
            return std::nullopt;
        }
        payload.push_back(static_cast<char>((hi << 4) | lo));
    }
    std::string expectedCheck;
    appendHex(expectedCheck, checkOf(payload), 4);
    if (text.substr(text.size() - 4) != expectedCheck) {
        return std::nullopt;
    }

    Reader in(payload);
    std::uint16_t count = 0;
    if (!in.u16(count) || count > kMaxFeaturedSeeds) {
        return std::nullopt;
    }
    std::vector<FeaturedSeed> list;
    list.reserve(count);
    for (std::uint16_t i = 0; i < count; ++i) {
        FeaturedSeed entry;
        std::uint8_t titleLength = 0;
        std::uint8_t axiomCount = 0;
        if (!in.u64(entry.seed) || !in.u8(titleLength) || !in.str(titleLength, entry.title) || !in.u8(axiomCount) || axiomCount > kMaxFeaturedAxioms) {
            return std::nullopt;
        }
        for (std::uint8_t a = 0; a < axiomCount; ++a) {
            std::uint8_t idLength = 0;
            std::string id;
            if (!in.u8(idLength) || !in.str(idLength, id)) {
                return std::nullopt;
            }
            entry.expectedAxioms.push_back(std::move(id));
        }
        if (!entryShapeOk(entry)) {
            return std::nullopt;
        }
        list.push_back(std::move(entry));
    }
    if (!in.done()) {
        return std::nullopt;
    }
    return list;
}

}
