#include "ichor/npc/schedule.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

using namespace ichor;

namespace {

Schedule make(const std::vector<std::string>& lines)
{
    ScheduleParse parsed = parseSchedule(lines);
    REQUIRE(parsed.schedule.has_value());
    return *parsed.schedule;
}

std::string errorOf(const std::vector<std::string>& lines)
{
    ScheduleParse parsed = parseSchedule(lines);
    REQUIRE_FALSE(parsed.schedule.has_value());
    REQUIRE_FALSE(parsed.error.empty());
    return parsed.error;
}

bool mentions(const std::string& text, const std::string& needle)
{
    return text.find(needle) != std::string::npos;
}

const std::vector<std::string> kDay = {"00:00 sleep", "06:00 home", "08:00 work", "17:30 social", "21:00 home", "22:00 sleep"};

}

TEST_CASE("a schedule gives the state in force at each hour", "[npc][schedule]")
{
    const Schedule day = make(kDay);
    REQUIRE(stateAt(day, 3.0) == ScheduleState::Sleep);
    REQUIRE(stateAt(day, 7.0) == ScheduleState::Home);
    REQUIRE(stateAt(day, 12.0) == ScheduleState::Work);
    REQUIRE(stateAt(day, 18.0) == ScheduleState::Social);
    REQUIRE(stateAt(day, 21.5) == ScheduleState::Home);
    REQUIRE(stateAt(day, 23.99) == ScheduleState::Sleep);
}

TEST_CASE("a boundary belongs to the entry that starts there", "[npc][schedule]")
{
    const Schedule day = make(kDay);
    REQUIRE(stateAt(day, 8.0) == ScheduleState::Work);
    REQUIRE(stateAt(day, 7.9999) == ScheduleState::Home);
    REQUIRE(stateAt(day, 17.5) == ScheduleState::Social);
    REQUIRE(stateAt(day, 0.0) == ScheduleState::Sleep);
    REQUIRE(stateAtMinute(day, 479) == ScheduleState::Home);
    REQUIRE(stateAtMinute(day, 480) == ScheduleState::Work);
}

TEST_CASE("hours wrap around the day", "[npc][schedule]")
{
    const Schedule day = make(kDay);
    REQUIRE(stateAt(day, 24.0) == stateAt(day, 0.0));
    REQUIRE(stateAt(day, 32.0) == stateAt(day, 8.0));
    REQUIRE(stateAt(day, -1.0) == stateAt(day, 23.0));
    REQUIRE(stateAt(day, -24.0) == stateAt(day, 0.0));
    REQUIRE(stateAt(day, 24.0 * 1000000.0 + 12.0) == ScheduleState::Work);
    REQUIRE(stateAt(day, -24.0 * 1000000.0 + 12.0) == ScheduleState::Work);
    REQUIRE(stateAt(day, 1e300).has_value());
    REQUIRE(stateAt(day, -1e-30) == stateAt(day, 23.9999));
}

TEST_CASE("an hour that is not finite has no state", "[npc][schedule]")
{
    const Schedule day = make(kDay);
    REQUIRE_FALSE(stateAt(day, std::numeric_limits<double>::quiet_NaN()).has_value());
    REQUIRE_FALSE(stateAt(day, std::numeric_limits<double>::infinity()).has_value());
    REQUIRE_FALSE(stateAt(day, -std::numeric_limits<double>::infinity()).has_value());
}

TEST_CASE("a single entry holds all day", "[npc][schedule]")
{
    const Schedule only = make({"09:00 work"});
    for (double hour : {0.0, 8.99, 9.0, 23.5, -3.0}) {
        REQUIRE(stateAt(only, hour) == ScheduleState::Work);
    }
}

TEST_CASE("without an entry at midnight the last state carries over", "[npc][schedule]")
{
    const Schedule day = make({"06:00 work", "20:00 sleep"});
    REQUIRE(stateAt(day, 0.0) == ScheduleState::Sleep);
    REQUIRE(stateAt(day, 5.99) == ScheduleState::Sleep);
    REQUIRE(stateAt(day, 6.0) == ScheduleState::Work);
    REQUIRE(stateAt(day, 23.0) == ScheduleState::Sleep);
}

TEST_CASE("bad schedules are errors with a message", "[npc][schedule]")
{
    REQUIRE(mentions(errorOf({}), "empty"));
    REQUIRE(mentions(errorOf({"06:00 work", "06:00 home"}), "06:00"));
    REQUIRE(mentions(errorOf({"08:00 work", "06:00 home"}), "order"));
    REQUIRE(mentions(errorOf({"06:00 napping"}), "napping"));
    REQUIRE(mentions(errorOf({"24:00 work"}), "24:00"));
    REQUIRE(mentions(errorOf({"-1:00 work"}), "-1:00"));
    REQUIRE(mentions(errorOf({"12:60 work"}), "12:60"));
    REQUIRE(mentions(errorOf({"6 work"}), "6 work"));
    REQUIRE(mentions(errorOf({"06:00"}), "06:00"));
    REQUIRE(mentions(errorOf({"06:00  work"}), "work"));
    REQUIRE(mentions(errorOf({"06:00 work extra"}), "extra"));
    REQUIRE(mentions(errorOf({"06:00 Work"}), "Work"));
    REQUIRE(mentions(errorOf({""}), "entry"));
    REQUIRE(mentions(errorOf({"99999999999:00 work"}), "99999999999"));
}

TEST_CASE("schedules load from the data layer", "[npc][schedule]")
{
    DataRecord good;
    good.id = "farmer";
    good.fields["entries"] = std::vector<std::string>(kDay);
    ScheduleParse parsed = scheduleFromRecord(good);
    REQUIRE(parsed.schedule.has_value());
    REQUIRE(stateAt(*parsed.schedule, 12.0) == ScheduleState::Work);

    DataRecord missing;
    missing.id = "ghost";
    parsed = scheduleFromRecord(missing);
    REQUIRE_FALSE(parsed.schedule.has_value());
    REQUIRE(mentions(parsed.error, "ghost"));

    DataRecord wrongType;
    wrongType.id = "scalar";
    wrongType.fields["entries"] = std::string("06:00 work");
    parsed = scheduleFromRecord(wrongType);
    REQUIRE_FALSE(parsed.schedule.has_value());
    REQUIRE(mentions(parsed.error, "scalar"));

    DataRecord broken;
    broken.id = "baker";
    broken.fields["entries"] = std::vector<std::string>{"06:00 napping"};
    parsed = scheduleFromRecord(broken);
    REQUIRE_FALSE(parsed.schedule.has_value());
    REQUIRE(mentions(parsed.error, "baker"));
}

TEST_CASE("canonical form and digest are stable", "[npc][schedule]")
{
    const Schedule day = make(kDay);
    REQUIRE(canonicalForm(day) == "00:00 sleep;06:00 home;08:00 work;17:30 social;21:00 home;22:00 sleep");
    REQUIRE(scheduleDigest(day) == scheduleDigest(make(kDay)));
    REQUIRE(scheduleDigest(day) != scheduleDigest(make({"00:00 sleep", "06:00 home", "08:00 work", "17:31 social", "21:00 home", "22:00 sleep"})));
    REQUIRE(scheduleDigest(make({"06:00 work"})) != scheduleDigest(make({"06:00 home"})));
}

TEST_CASE("the state depends only on the hour, never on history", "[npc][schedule]")
{
    const Schedule day = make(kDay);
    constexpr double kDt = 1.0 / 60.0;
    constexpr double kDaySeconds = 1200.0;
    const auto hourAt = [&](std::uint64_t tick) {
        return std::fmod(8.0 + static_cast<double>(tick) * kDt / kDaySeconds * 24.0, 24.0);
    };

    // Stepping every tick and jumping straight to the same tick must agree.
    ScheduleState stepped = ScheduleState::Home;
    constexpr std::uint64_t kTicks = 300000;
    std::uint64_t transitions = 0;
    for (std::uint64_t tick = 0; tick <= kTicks; ++tick) {
        const ScheduleState next = *stateAt(day, hourAt(tick));
        transitions += next != stepped ? 1 : 0;
        stepped = next;
        if (tick % 997 == 0 || tick == kTicks) {
            REQUIRE(*stateAt(make(kDay), hourAt(tick)) == stepped);
        }
    }
    REQUIRE(transitions > 15);
    REQUIRE(*stateAt(day, hourAt(kTicks)) == stepped);
}
