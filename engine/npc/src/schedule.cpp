#include "ichor/npc/schedule.h"

#include "ichor/core/hash.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace ichor {

namespace {

constexpr std::array<std::string_view, 4> kStateNames = {"home", "work", "social", "sleep"};

std::string inQuotes(std::string_view text)
{
    return "\"" + std::string(text) + "\"";
}

bool parseDigits(std::string_view text, int& out)
{
    if (text.empty() || text.size() > 2) {
        return false;
    }
    int value = 0;
    for (char c : text) {
        if (c < '0' || c > '9') {
            return false;
        }
        value = value * 10 + (c - '0');
    }
    out = value;
    return true;
}

std::string clock(int minute)
{
    std::string text = "00:00";
    text[0] = static_cast<char>('0' + minute / 60 / 10);
    text[1] = static_cast<char>('0' + minute / 60 % 10);
    text[3] = static_cast<char>('0' + minute % 60 / 10);
    text[4] = static_cast<char>('0' + minute % 60 % 10);
    return text;
}

}

std::string_view scheduleStateName(ScheduleState state)
{
    return kStateNames[static_cast<std::size_t>(state)];
}

std::optional<ScheduleState> scheduleStateFromName(std::string_view name)
{
    for (std::size_t i = 0; i < kStateNames.size(); ++i) {
        if (kStateNames[i] == name) {
            return static_cast<ScheduleState>(i);
        }
    }
    return std::nullopt;
}

ScheduleParse parseSchedule(const std::vector<std::string>& entries)
{
    ScheduleParse result;
    if (entries.empty()) {
        result.error = "schedule is empty, it needs at least one entry";
        return result;
    }

    Schedule schedule;
    for (const std::string& line : entries) {
        const std::size_t space = line.find(' ');
        const std::size_t colon = line.find(':');
        if (space == std::string::npos || colon == std::string::npos || colon > space) {
            result.error = "schedule entry " + inQuotes(line) + " is not \"HH:MM state\"";
            return result;
        }
        int hour = 0;
        int minute = 0;
        if (!parseDigits(std::string_view(line).substr(0, colon), hour) || !parseDigits(std::string_view(line).substr(colon + 1, space - colon - 1), minute)
            || hour >= 24 || minute >= 60) {
            result.error = "schedule entry " + inQuotes(line) + " has a start time outside 00:00 to 23:59";
            return result;
        }
        const std::string_view name = std::string_view(line).substr(space + 1);
        const std::optional<ScheduleState> state = scheduleStateFromName(name);
        if (!state) {
            result.error = "schedule entry " + inQuotes(line) + " names unknown state " + inQuotes(name) + ", expected home, work, social or sleep";
            return result;
        }
        const int start = hour * 60 + minute;
        if (!schedule.entries.empty() && start == schedule.entries.back().startMinute) {
            result.error = "schedule starts twice at " + clock(start);
            return result;
        }
        if (!schedule.entries.empty() && start < schedule.entries.back().startMinute) {
            result.error = "schedule entry " + inQuotes(line) + " is out of order, entries must ascend by start time";
            return result;
        }
        schedule.entries.push_back({start, *state});
    }
    result.schedule = std::move(schedule);
    return result;
}

ScheduleParse scheduleFromRecord(const DataRecord& record)
{
    const auto field = record.fields.find("entries");
    const std::vector<std::string>* list = field == record.fields.end() ? nullptr : std::get_if<std::vector<std::string>>(&field->second);
    if (list == nullptr) {
        ScheduleParse result;
        result.error = "schedule " + inQuotes(record.id) + " needs an \"entries\" list of strings";
        return result;
    }
    ScheduleParse result = parseSchedule(*list);
    if (!result.schedule) {
        result.error = "schedule " + inQuotes(record.id) + ": " + result.error;
    }
    return result;
}

ScheduleState stateAtMinute(const Schedule& schedule, int minuteOfDay)
{
    const int minute = std::clamp(minuteOfDay, 0, kMinutesPerDay - 1);
    const auto after = std::upper_bound(schedule.entries.begin(), schedule.entries.end(), minute,
        [](int value, const ScheduleEntry& entry) { return value < entry.startMinute; });
    return after == schedule.entries.begin() ? schedule.entries.back().state : std::prev(after)->state;
}

std::optional<ScheduleState> stateAt(const Schedule& schedule, double hour)
{
    if (!std::isfinite(hour)) {
        return std::nullopt;
    }
    // fmod is exact, so the wrap rounds the same on every platform.
    double wrapped = std::fmod(hour, 24.0);
    if (wrapped < 0.0) {
        wrapped += 24.0;
    }
    return stateAtMinute(schedule, static_cast<int>(wrapped * 60.0));
}

std::string canonicalForm(const Schedule& schedule)
{
    std::string text;
    for (const ScheduleEntry& entry : schedule.entries) {
        if (!text.empty()) {
            text += ';';
        }
        text += clock(entry.startMinute);
        text += ' ';
        text += scheduleStateName(entry.state);
    }
    return text;
}

std::uint64_t scheduleDigest(const Schedule& schedule)
{
    return hash64(canonicalForm(schedule));
}

}
