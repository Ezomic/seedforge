#pragma once

#include "ichor/stage0/death_cause.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace ichor::stage0 {

// A compact, versioned recording of one fight, for the first-20-fights review
// (doc 18). Times are whole milliseconds and damage is whole tenths, so the
// encoding holds no floats and is identical on every platform.
constexpr std::uint8_t kFightRecordVersion = 1;
constexpr std::size_t kMaxFightEvents = 4096;
constexpr std::size_t kMaxRecordedFights = 20;
constexpr std::uint32_t kMaxFightMs = 3'600'000;

enum class FightEventKind : std::uint8_t { Attack, Hit, Block, Whiff, Dodge, Stagger };
constexpr std::uint8_t kFightEventKindCount = 6;

enum class FightSide : std::uint8_t { Player, Foe };

struct FightEvent {
    std::uint32_t atMs = 0; // since the first swing
    FightEventKind kind = FightEventKind::Attack;
    FightSide actor = FightSide::Player; // who swung, dodged or was staggered
    std::uint16_t damageTenths = 0;      // Hit and Block only

    bool operator==(const FightEvent&) const = default;
};

struct FightRecord {
    std::uint32_t index = 0; // 1-based fight number
    FightSide loser = FightSide::Player;
    DeathCause cause = DeathCause::Unknown;
    std::uint32_t durationMs = 0;
    bool truncated = false; // events beyond kMaxFightEvents were dropped
    std::vector<FightEvent> events;

    bool operator==(const FightRecord&) const = default;
};

struct FightRecordSummary {
    std::uint32_t durationMs = 0;
    int playerHits = 0; // blows the player landed, blocked ones included
    int foeHits = 0;
    int playerBlocks = 0; // blows the player took on a raised guard
    int foeBlocks = 0;
    int playerWhiffs = 0;
    int foeWhiffs = 0;
    std::uint64_t playerDamageTenths = 0; // damage dealt by the player
    std::uint64_t foeDamageTenths = 0;
};

FightRecordSummary summarize(const FightRecord& record);

std::vector<std::uint8_t> encode(const FightRecord& record);

// Nullopt for anything that is not a well-formed version 1 record: bad magic or
// version, short or trailing bytes, out-of-range enums, an event count the
// bytes cannot hold, or events out of order or past the duration.
std::optional<FightRecord> decodeFightRecord(std::span<const std::uint8_t> bytes);

// Collects one fight at a time and keeps the first kMaxRecordedFights. Inputs
// are untrusted: NaN, infinite and negative values are clamped, never cast raw.
class FightRecordBuffer {
public:
    void noteEvent(FightEventKind kind, FightSide actor, double now, double damage = 0.0);
    // Closes the open fight. Nothing open, or a full buffer, drops it.
    bool end(FightSide loser, DeathCause cause, double now);
    // An abandoned fight is not recorded.
    void abandon();
    // The number the next closed fight gets; zero is ignored (indices are 1-based).
    void setNextIndex(std::uint32_t index) { if (index != 0) nextIndex_ = index; }

    bool running() const { return running_; }
    const std::vector<FightRecord>& fights() const { return fights_; }
    std::size_t dropped() const { return dropped_; }

private:
    std::uint32_t elapsedMs(double now) const;

    std::vector<FightRecord> fights_;
    FightRecord open_;
    bool running_ = false;
    double start_ = 0.0;
    std::uint32_t nextIndex_ = 1;
    std::size_t dropped_ = 0;
};

}
