#include "../../../engine/tests/test_temp.h"
#include "ichor/stage0/ai_knobs.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <limits>

using namespace ichor;
using namespace ichor::stage0;
using Catch::Approx;

namespace {

DataRecord record(std::initializer_list<std::pair<const char*, DataValue>> fields)
{
    DataRecord r;
    r.id = "skirmisher";
    for (const auto& [k, v] : fields) r.fields.emplace(k, v);
    return r;
}

bool mentions(const KnobLoad& load, const char* text)
{
    for (const auto& p : load.problems) {
        if (p.find(text) != std::string::npos) return true;
    }
    return false;
}

}

TEST_CASE("knob defaults equal the Tuning defaults", "[knobs]")
{
    const Tuning base;
    const Tuning applied = applyKnobs(base, AiKnobs{});
    CHECK(applied.desireGainRate == base.desireGainRate);
    CHECK(applied.preferredSpacing == base.preferredSpacing);
    CHECK(applied.restDuration == base.restDuration);
    CHECK(applied.punishReactionTime == base.punishReactionTime);
    CHECK(applied.lightWindup == base.lightWindup);
    CHECK(applied.heavyWindup == base.heavyWindup);
    CHECK(applied.trackingCapWindupDegPerSec == base.trackingCapWindupDegPerSec);
}

TEST_CASE("a record sets every knob", "[knobs]")
{
    const auto load = knobsFromRecord(record({{"desire_gain_rate", 0.5}, {"preferred_spacing", 3.0}, {"rest_duration", 2.0},
        {"punish_reaction_time", 0.4}, {"windup_scale", 1.5}, {"tracking_cap_deg_per_sec", 60.0}}));
    CHECK(load.problems.empty());
    CHECK(load.knobs.desireGainRate == Approx(0.5f));
    CHECK(load.knobs.preferredSpacing == Approx(3.0f));
    CHECK(load.knobs.restDuration == Approx(2.0f));
    CHECK(load.knobs.punishReactionTime == Approx(0.4f));
    CHECK(load.knobs.windupScale == Approx(1.5f));
    CHECK(load.knobs.trackingCapDegPerSec == Approx(60.0f));

    const Tuning t = applyKnobs(Tuning{}, load.knobs);
    CHECK(t.lightWindup == Approx(0.35f * 1.5f));
    CHECK(t.heavyWindup == Approx(0.7f * 1.5f));
    CHECK(t.trackingCapWindupDegPerSec == Approx(60.0f));
}

TEST_CASE("out of range values are clamped and reported", "[knobs]")
{
    const auto load = knobsFromRecord(record({{"windup_scale", 100.0}, {"preferred_spacing", -4.0}, {"tracking_cap_deg_per_sec", 1e30}}));
    CHECK(load.knobs.windupScale == 4.0f);
    CHECK(load.knobs.preferredSpacing == 0.5f);
    CHECK(load.knobs.trackingCapDegPerSec == 720.0f);
    CHECK(load.problems.size() == 3);
}

TEST_CASE("hostile values keep the default", "[knobs]")
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    const auto load = knobsFromRecord(record({{"desire_gain_rate", nan}, {"rest_duration", -inf}, {"punish_reaction_time", std::string("fast")},
        {"windup_scale", true}}));
    const AiKnobs defaults;
    CHECK(load.knobs.desireGainRate == defaults.desireGainRate);
    CHECK(load.knobs.restDuration == defaults.restDuration);
    CHECK(load.knobs.punishReactionTime == defaults.punishReactionTime);
    CHECK(load.knobs.windupScale == defaults.windupScale);
    CHECK(load.problems.size() == 4);
}

TEST_CASE("an unknown field is reported", "[knobs]")
{
    const auto load = knobsFromRecord(record({{"desire_gain_rat", 0.5}}));
    CHECK(mentions(load, "desire_gain_rat"));
    CHECK(load.knobs.desireGainRate == AiKnobs{}.desireGainRate);
}

TEST_CASE("applyKnobs clamps knobs set directly in code", "[knobs]")
{
    AiKnobs knobs;
    knobs.windupScale = 1000.0f;
    knobs.restDuration = -5.0f;
    knobs.preferredSpacing = std::numeric_limits<float>::quiet_NaN();
    const Tuning t = applyKnobs(Tuning{}, knobs);
    CHECK(t.lightWindup == Approx(0.35f * 4.0f));
    CHECK(t.restDuration == 0.0f);
    CHECK(t.preferredSpacing == AiKnobs{}.preferredSpacing);
}

TEST_CASE("the shipped data file loads clean and matches the defaults", "[knobs]")
{
    DataSet data;
    const auto root = std::filesystem::path(__FILE__).parent_path() / ".." / ".." / ".." / "data";
    REQUIRE(data.loadDirectory(root));
    const auto load = loadKnobs(data);
    CHECK(load.problems.empty());
    const AiKnobs defaults;
    CHECK(load.knobs.desireGainRate == defaults.desireGainRate);
    CHECK(load.knobs.preferredSpacing == defaults.preferredSpacing);
    CHECK(load.knobs.restDuration == defaults.restDuration);
    CHECK(load.knobs.punishReactionTime == defaults.punishReactionTime);
    CHECK(load.knobs.windupScale == defaults.windupScale);
    CHECK(load.knobs.trackingCapDegPerSec == defaults.trackingCapDegPerSec);
}

TEST_CASE("a missing record gives defaults and a problem", "[knobs]")
{
    const DataSet empty;
    const auto load = loadKnobs(empty);
    CHECK(load.problems.size() == 1);
    CHECK(load.knobs.windupScale == 1.0f);
}

TEST_CASE("overlay shows the live values and follows the knobs", "[knobs][overlay]")
{
    Combatant foe;
    foe.side = 1;
    SkirmisherBrain brain;
    brain.state = static_cast<std::uint8_t>(BrainState::Rest);
    brain.stateTime = 0.5f;
    brain.desire = 0.42f;
    brain.nextThreshold = 1.1f;
    const OverlayInput in{&foe, &brain, 4.0f, 12.5f, 30.0f};

    Tuning t;
    t.restDuration = 3.0f;
    t.preferredSpacing = 2.5f;
    const auto lines = overlayLines(in, t);
    REQUIRE(lines.size() == 6);
    CHECK(lines[0].value.find("Rest") != std::string::npos);
    CHECK(lines[1].value == "0.42 / 1.10");
    CHECK(lines[2].value.find("4.00 m") != std::string::npos);
    CHECK(lines[2].value.find("+1.50") != std::string::npos);
    CHECK(lines[3].value == "rest 0.50 / 3.00 s");
    CHECK(lines[4].value == "12.5 deg off, cap 30.0 deg/s");

    t.restDuration = 1.0f;
    CHECK(overlayLines(in, t)[3].value == "rest 0.50 / 1.00 s");
}

TEST_CASE("overlay survives a missing foe and non-finite numbers", "[knobs][overlay]")
{
    const auto none = overlayLines(OverlayInput{}, Tuning{});
    REQUIRE(none.size() == 1);

    Combatant foe;
    SkirmisherBrain brain;
    brain.desire = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const auto lines = overlayLines(OverlayInput{&foe, &brain, inf, std::numeric_limits<float>::quiet_NaN(), inf}, Tuning{});
    CHECK(lines[2].value.find("0.00 m") != std::string::npos);
    for (const auto& l : lines) {
        CHECK(l.value.find("nan") == std::string::npos);
        CHECK(l.value.find("inf") == std::string::npos);
    }
}

namespace {

std::filesystem::path scratchDir(const char* name)
{
    const auto dir = ichor::test::outputPath(std::filesystem::temp_directory_path(), "ichor_stage0_knobs") / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "ai_knobs");
    return dir;
}

void writeKnobs(const std::filesystem::path& root, const std::string& body)
{
    std::ofstream(root / "ai_knobs" / "skirmisher.json") << body;
}

}

TEST_CASE("knob files in a data directory reach the tuning", "[knobs][startup]")
{
    const auto root = scratchDir("applied");
    writeKnobs(root, R"([{"id":"skirmisher","windup_scale":2.0,"preferred_spacing":4.0,"rest_duration":3.0}])");
    const auto load = loadKnobsFromDirectory(root);
    REQUIRE(load.problems.empty());
    const Tuning base;
    const Tuning t = applyKnobs(base, load.knobs);
    CHECK(t.lightWindup == Approx(base.lightWindup * 2.0f));
    CHECK(t.heavyWindup == Approx(base.heavyWindup * 2.0f));
    CHECK(t.preferredSpacing == Approx(4.0f));
    CHECK(t.restDuration == Approx(3.0f));
    CHECK(t.desireGainRate == base.desireGainRate);
}

TEST_CASE("a directory that fails to load gives defaults and a problem", "[knobs][startup]")
{
    const auto missing = ichor::test::outputPath(std::filesystem::temp_directory_path(), "ichor_stage0_knobs") / "does_not_exist";
    const auto load = loadKnobsFromDirectory(missing);
    CHECK(load.problems.size() == 1);
    CHECK(load.knobs.windupScale == 1.0f);

    const auto bad = scratchDir("bad");
    writeKnobs(bad, "{ not json");
    const auto badLoad = loadKnobsFromDirectory(bad);
    CHECK(badLoad.problems.size() == 1);
    CHECK(badLoad.knobs.windupScale == 1.0f);
}

TEST_CASE("the stamp line carries every applied knob and differs per knob set", "[knobs][startup]")
{
    AiKnobs a;
    AiKnobs b;
    b.windupScale = 1.5f;
    const std::string lineA = knobStampLine(a, "defaults", 7);
    const std::string lineB = knobStampLine(b, "defaults", 7);
    CHECK(lineA != lineB);
    CHECK(lineB.find("windup_scale=1.5") != std::string::npos);
    CHECK(lineA.find("first_fight=7") == 0);
    for (const auto& spec : kKnobSpecs) {
        CHECK(lineA.find(std::string(spec.key) + "=") != std::string::npos);
    }
    CHECK(knobStampLine(a, "x\ny", 1).find('\n') == std::string::npos);
}

TEST_CASE("stamps append one line per session beside the log and leave the log alone", "[knobs][startup]")
{
    const auto dir = scratchDir("stamp");
    const auto csv = dir / "fights.csv";
    std::ofstream(csv) << "fight,seconds\n";
    REQUIRE(appendKnobStamp(csv, knobStampLine(AiKnobs{}, "defaults", 1)));
    REQUIRE(appendKnobStamp(csv, knobStampLine(AiKnobs{}, "data", 3)));
    std::ifstream in(knobStampPath(csv));
    std::string first, second, third;
    std::getline(in, first);
    std::getline(in, second);
    CHECK(!std::getline(in, third));
    CHECK(first.find("first_fight=1") == 0);
    CHECK(second.find("first_fight=3") == 0);
    std::ifstream log(csv);
    std::string header;
    std::getline(log, header);
    CHECK(header == "fight,seconds");
    CHECK(!appendKnobStamp({}, "x"));
}
