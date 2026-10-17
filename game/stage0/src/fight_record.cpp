#include "ichor/stage0/fight_record.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace ichor::stage0 {

namespace {

constexpr std::array<std::uint8_t, 4> kMagic{'I', 'F', 'R', 'D'};
constexpr std::size_t kHeaderBytes = 4 + 1 + 4 + 1 + 1 + 4 + 1 + 4;
constexpr std::size_t kEventBytes = 4 + 1 + 1 + 2;

void put32(std::vector<std::uint8_t>& out, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

void put16(std::vector<std::uint8_t>& out, std::uint16_t v)
{
    out.push_back(static_cast<std::uint8_t>(v));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
}

std::uint32_t get32(const std::uint8_t* p)
{
    return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 | static_cast<std::uint32_t>(p[2]) << 16
        | static_cast<std::uint32_t>(p[3]) << 24;
}

std::uint16_t get16(const std::uint8_t* p)
{
    return static_cast<std::uint16_t>(p[0] | p[1] << 8);
}

std::uint32_t clampedMs(double seconds)
{
    if (!std::isfinite(seconds) || seconds <= 0.0) return 0;
    const double ms = seconds * 1000.0;
    return ms >= kMaxFightMs ? kMaxFightMs : static_cast<std::uint32_t>(ms);
}

}

FightSummary summarize(const FightRecord& record)
{
    FightSummary s;
    s.durationMs = record.durationMs;
    for (const FightEvent& e : record.events) {
        const bool player = e.actor == FightSide::Player;
        switch (e.kind) {
        case FightEventKind::Hit:
        case FightEventKind::Block:
            (player ? s.playerHits : s.foeHits) += 1;
            (player ? s.playerDamageTenths : s.foeDamageTenths) += e.damageTenths;
            if (e.kind == FightEventKind::Block) (player ? s.foeBlocks : s.playerBlocks) += 1;
            break;
        case FightEventKind::Whiff: (player ? s.playerWhiffs : s.foeWhiffs) += 1; break;
        default: break;
        }
    }
    return s;
}

std::vector<std::uint8_t> encode(const FightRecord& record)
{
    const std::size_t count = std::min(record.events.size(), kMaxFightEvents);
    std::vector<std::uint8_t> out;
    out.reserve(kHeaderBytes + count * kEventBytes);
    out.insert(out.end(), kMagic.begin(), kMagic.end());
    out.push_back(kFightRecordVersion);
    put32(out, record.index);
    out.push_back(static_cast<std::uint8_t>(record.loser));
    out.push_back(static_cast<std::uint8_t>(record.cause));
    put32(out, std::min(record.durationMs, kMaxFightMs));
    out.push_back(record.truncated || record.events.size() > count ? 1 : 0);
    put32(out, static_cast<std::uint32_t>(count));
    for (std::size_t i = 0; i < count; ++i) {
        const FightEvent& e = record.events[i];
        put32(out, e.atMs);
        out.push_back(static_cast<std::uint8_t>(e.kind));
        out.push_back(static_cast<std::uint8_t>(e.actor));
        put16(out, e.damageTenths);
    }
    return out;
}

std::optional<FightRecord> decodeFightRecord(std::span<const std::uint8_t> bytes)
{
    if (bytes.size() < kHeaderBytes) return std::nullopt;
    const std::uint8_t* p = bytes.data();
    if (!std::equal(kMagic.begin(), kMagic.end(), p) || p[4] != kFightRecordVersion) return std::nullopt;

    FightRecord r;
    r.index = get32(p + 5);
    const std::uint8_t loser = p[9];
    const std::uint8_t cause = p[10];
    r.durationMs = get32(p + 11);
    const std::uint8_t truncated = p[15];
    const std::uint32_t count = get32(p + 16);
    if (loser > static_cast<std::uint8_t>(FightSide::Foe) || cause >= kDeathCauseCount || truncated > 1) return std::nullopt;
    if (r.durationMs > kMaxFightMs || count > kMaxFightEvents) return std::nullopt;
    if (bytes.size() - kHeaderBytes != static_cast<std::size_t>(count) * kEventBytes) return std::nullopt;
    r.loser = static_cast<FightSide>(loser);
    r.cause = static_cast<DeathCause>(cause);
    r.truncated = truncated == 1;

    r.events.reserve(count);
    const std::uint8_t* e = p + kHeaderBytes;
    std::uint32_t previous = 0;
    for (std::uint32_t i = 0; i < count; ++i, e += kEventBytes) {
        FightEvent event;
        event.atMs = get32(e);
        if (e[4] >= kFightEventKindCount || e[5] > static_cast<std::uint8_t>(FightSide::Foe)) return std::nullopt;
        event.kind = static_cast<FightEventKind>(e[4]);
        event.actor = static_cast<FightSide>(e[5]);
        event.damageTenths = get16(e + 6);
        if (event.atMs < previous || event.atMs > r.durationMs) return std::nullopt;
        previous = event.atMs;
        r.events.push_back(event);
    }
    return r;
}

std::uint32_t FightRecordBuffer::elapsedMs(double now) const
{
    return clampedMs(now - start_);
}

void FightRecordBuffer::noteEvent(FightEventKind kind, FightSide actor, double now, double damage)
{
    if (!std::isfinite(now)) return;
    if (!running_) {
        // A fight starts at the first swing.
        if (kind != FightEventKind::Attack) return;
        running_ = true;
        start_ = now;
        open_ = {};
    }
    if (open_.events.size() >= kMaxFightEvents) {
        open_.truncated = true;
        return;
    }
    FightEvent e;
    e.atMs = std::max(elapsedMs(now), open_.events.empty() ? 0u : open_.events.back().atMs);
    e.kind = kind;
    e.actor = actor;
    if (kind == FightEventKind::Hit || kind == FightEventKind::Block) {
        const double tenths = std::isfinite(damage) && damage > 0.0 ? damage * 10.0 : 0.0;
        e.damageTenths = tenths >= 65535.0 ? std::uint16_t{65535} : static_cast<std::uint16_t>(tenths);
    }
    open_.events.push_back(e);
}

bool FightRecordBuffer::end(FightSide loser, DeathCause cause, double now)
{
    if (!running_) return false;
    running_ = false;
    if (fights_.size() >= kMaxRecordedFights) {
        ++dropped_;
        open_ = {};
        return false;
    }
    open_.index = nextIndex_++;
    open_.loser = loser;
    open_.cause = static_cast<std::uint8_t>(cause) < kDeathCauseCount ? cause : DeathCause::Unknown;
    open_.durationMs = std::max(elapsedMs(now), open_.events.empty() ? 0u : open_.events.back().atMs);
    fights_.push_back(std::move(open_));
    open_ = {};
    return true;
}

void FightRecordBuffer::abandon()
{
    running_ = false;
    open_ = {};
}

}
