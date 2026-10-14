#pragma once

#include "ichor/data/data_set.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// The four states of an NPC's day (doc 10). Nothing is simulated while the
// player is away: where an NPC is follows from the state at the current hour.
enum class ScheduleState : std::uint8_t { Home, Work, Social, Sleep };

std::string_view scheduleStateName(ScheduleState state);
std::optional<ScheduleState> scheduleStateFromName(std::string_view name);

struct ScheduleEntry {
    int startMinute = 0; // minute of the day, [0, 1440)
    ScheduleState state = ScheduleState::Home;
};

// Entries are strictly ascending by start and never empty. The state of the
// last entry carries over midnight until the first entry begins.
struct Schedule {
    std::vector<ScheduleEntry> entries;
};

struct ScheduleParse {
    std::optional<Schedule> schedule;
    std::string error;
};

constexpr int kMinutesPerDay = 24 * 60;

// Entries are written "HH:MM state", for example "06:30 work".
ScheduleParse parseSchedule(const std::vector<std::string>& entries);

// Reads the "entries" list field of a record in the "schedules" table.
ScheduleParse scheduleFromRecord(const DataRecord& record);

// The minute wraps modulo a day. An empty schedule has no answer.
std::optional<ScheduleState> stateAtMinute(const Schedule& schedule, int minuteOfDay);

// The hour wraps modulo 24, negative and huge values included. An hour that
// is not finite, or an empty schedule, has no answer.
std::optional<ScheduleState> stateAt(const Schedule& schedule, double hour);

std::string canonicalForm(const Schedule& schedule);
std::uint64_t scheduleDigest(const Schedule& schedule);

}
