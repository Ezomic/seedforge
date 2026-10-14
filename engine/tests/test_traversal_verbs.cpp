#include "ichor/core/wading.h"
#include "ichor/data/data_set.h"
#include "ichor/traversal/verbs.h"
#include "ichor/world/water_field.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>
#define ICHOR_PID _getpid()
#else
#include <unistd.h>
#define ICHOR_PID getpid()
#endif

using namespace ichor;

namespace {

using Ids = std::vector<std::string>;
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

std::optional<VerbSet> setFromJson(const std::string& json, std::string& error)
{
    static int counter = 0;
    const auto root = std::filesystem::temp_directory_path()
        / ("ichor_verbs_test_" + std::to_string(ICHOR_PID) + "_" + std::to_string(counter++));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "traversal");
    std::ofstream(root / "traversal" / "verbs.json", std::ios::binary) << json;
    DataSet data;
    const bool loaded = data.loadDirectory(root);
    std::filesystem::remove_all(root);
    if (!loaded) {
        error = data.lastError();
        return std::nullopt;
    }
    const DataTable* table = data.table("traversal");
    if (table == nullptr) {
        error = "no traversal table";
        return std::nullopt;
    }
    return VerbSet::fromTable(*table, error);
}

VerbSet shippedSet()
{
    std::ifstream file(std::filesystem::path(ICHOR_GAME_DATA_DIR) / "traversal" / "verbs.json", std::ios::binary);
    REQUIRE(file.good());
    const std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::string error;
    auto set = setFromJson(json, error);
    INFO(error);
    REQUIRE(set.has_value());
    return *set;
}

Ids idsOf(const VerbSet& set, const Availability& a)
{
    Ids ids;
    for (const std::size_t i : a.verbs) {
        ids.push_back(set.verbs()[i].id);
    }
    return ids;
}

Verb verbNamed(const VerbSet& set, const std::string& id)
{
    const auto it = std::find_if(set.verbs().begin(), set.verbs().end(), [&](const Verb& v) { return v.id == id; });
    REQUIRE(it != set.verbs().end());
    return *it;
}

std::string verbJson(const std::string& fields)
{
    return "[{\"id\": \"v\", " + fields + "}]";
}

const std::string kGoodFields = "\"terrain\": \"ground\", \"speed_permille\": 1000, \"stamina_per_second\": 0";

}

TEST_CASE("the shipped verbs are walk, wade, swim, dive, climb, descend and boat")
{
    const VerbSet set = shippedSet();
    Ids ids;
    for (const Verb& v : set.verbs()) {
        ids.push_back(v.id);
    }
    CHECK(ids == Ids { "boat", "climb", "descend", "dive", "swim", "wade", "walk" });
    CHECK(verbNamed(set, "walk").needs == Capability::None);
    CHECK(verbNamed(set, "wade").needs == Capability::None);
    CHECK(verbNamed(set, "swim").needs == Capability::Swim);
}

TEST_CASE("the wade verb is the wading speed of core/wading.h, to the last bit")
{
    const Verb wade = verbNamed(shippedSet(), "wade");
    CHECK(wade.depthScaled);
    const float depths[] = { -5.0f, 0.0f, 0.05f, kWadeStart, std::nextafter(kWadeStart, 1.0f), 0.3f, 0.8f, 1.0f, 1.2f,
        std::nextafter(kWadeFull, 0.0f), kWadeFull, 1.6f, 40.0f, kNaN, kInf };
    for (const float depth : depths) {
        INFO("depth " << depth);
        CHECK(wade.speedFactor(depth) == wadingSpeedFactor(depth));
    }
    for (int i = 0; i <= 2000; ++i) {
        const float depth = static_cast<float>(i) * 0.001f;
        REQUIRE(wade.speedFactor(depth) == wadingSpeedFactor(depth));
    }
}

TEST_CASE("a verb that is not depth scaled has the same speed at any depth")
{
    const VerbSet set = shippedSet();
    CHECK(verbNamed(set, "walk").speedFactor(0.0f) == 1.0f);
    CHECK(verbNamed(set, "walk").speedFactor(kNaN) == 1.0f);
    CHECK(verbNamed(set, "swim").speedFactor(3.0f) == 0.45f);
    CHECK(verbNamed(set, "swim").staminaPerSecond == 8);
}

TEST_CASE("speed and the best verb follow the capability, and ties and zero speed are handled")
{
    const VerbSet set = shippedSet();
    CapabilitySet boater;
    boater.add(Capability::Boat);
    const Availability onWater = availableVerbs(set, boater, { true, 0.5f, 0.0f });
    REQUIRE(onWater.best.has_value());
    CHECK(set.verbs()[*onWater.best].id == "boat");
    CHECK(onWater.speedFactor == 1.4f);
    CHECK(availableVerbs(set, CapabilitySet {}, { true, 0.5f, 0.0f }).speedFactor == wadingSpeedFactor(0.5f));

    std::string error;
    Verb a;
    a.id = "a";
    Verb b;
    b.id = "b";
    const auto tied = VerbSet::fromVerbs({ b, a }, error);
    REQUIRE(tied.has_value());
    CHECK(tied->verbs()[*availableVerbs(*tied, CapabilitySet {}, { true, 0.0f, 0.0f }).best].id == "a");

    a.speedPermille = 0;
    const auto stopped = VerbSet::fromVerbs({ a }, error);
    REQUIRE(stopped.has_value());
    const Availability still = availableVerbs(*stopped, CapabilitySet {}, { true, 0.0f, 0.0f });
    REQUIRE(still.best.has_value());
    CHECK(still.speedFactor == 0.0f);
}

TEST_CASE("an unknown field, or a depth scaled verb on dry ground, is an error")
{
    std::string error;
    CHECK_FALSE(setFromJson(verbJson(kGoodFields + ", \"capabilty\": \"swim\""), error).has_value());
    CHECK(error.find("capabilty") != std::string::npos);
    CHECK_FALSE(setFromJson(verbJson(kGoodFields + ", \"depth_scaled\": true"), error).has_value());
    CHECK(error.find("depth_scaled") != std::string::npos);
    CHECK(CapabilitySet {}.has(Capability::None));
}

TEST_CASE("with no capabilities a player walks and wades but does not swim, dive, climb or boat")
{
    const VerbSet set = shippedSet();
    const CapabilitySet none;
    CHECK(idsOf(set, availableVerbs(set, none, { true, 0.0f, 10.0f })) == Ids { "walk" });
    CHECK(idsOf(set, availableVerbs(set, none, { true, 0.5f, 0.0f })) == Ids { "wade" });
    CHECK(availableVerbs(set, none, { false, 3.0f, 0.0f }).verbs.empty());
    CHECK(availableVerbs(set, none, { true, 0.0f, 80.0f }).verbs.empty());
}

TEST_CASE("with every capability the situation alone decides, in a fixed order")
{
    const VerbSet set = shippedSet();
    const CapabilitySet all = CapabilitySet::all();
    CHECK(idsOf(set, availableVerbs(set, all, { true, 0.0f, 10.0f })) == Ids { "walk" });
    CHECK(idsOf(set, availableVerbs(set, all, { true, 0.0f, 70.0f })) == Ids { "climb", "descend" });
    CHECK(idsOf(set, availableVerbs(set, all, { true, 0.5f, 0.0f })) == Ids { "boat", "wade" });
    CHECK(idsOf(set, availableVerbs(set, all, { false, 3.0f, 0.0f })) == Ids { "boat", "dive", "swim" });
}

TEST_CASE("each capability opens its own verb")
{
    const VerbSet set = shippedSet();
    const Situation deep { false, 3.0f, 0.0f };
    CapabilitySet swimmer;
    swimmer.add(Capability::Swim);
    CHECK(idsOf(set, availableVerbs(set, swimmer, deep)) == Ids { "swim" });
    CapabilitySet diver;
    diver.add(Capability::Dive);
    CHECK(idsOf(set, availableVerbs(set, diver, deep)) == Ids { "dive" });
    CapabilitySet cold;
    cold.add(Capability::ColdProtection);
    CHECK(availableVerbs(set, cold, deep).verbs.empty());
    CapabilitySet climber;
    climber.add(Capability::Climb);
    CHECK(idsOf(set, availableVerbs(set, climber, { true, 0.0f, 70.0f })) == Ids { "climb" });
}

TEST_CASE("depth exactly at the wading start is dry, and the smallest bit more is wading")
{
    const VerbSet set = shippedSet();
    const CapabilitySet none;
    CHECK(idsOf(set, availableVerbs(set, none, { true, kWadeStart, 0.0f })) == Ids { "walk" });
    CHECK(idsOf(set, availableVerbs(set, none, { true, std::nextafter(kWadeStart, 1.0f), 0.0f })) == Ids { "wade" });
    CHECK(availableVerbs(set, none, { true, kWadeFull, 0.0f }).verbs.empty());
    CHECK(idsOf(set, availableVerbs(set, CapabilitySet::all(), { false, kWadeFull, 0.0f })) == Ids { "boat", "dive", "swim" });
}

TEST_CASE("a slope exactly at the steep limit is still walkable")
{
    const VerbSet set = shippedSet();
    const CapabilitySet none;
    CHECK(idsOf(set, availableVerbs(set, none, { true, 0.0f, kSteepSlopeDegrees })) == Ids { "walk" });
    CHECK(availableVerbs(set, none, { true, 0.0f, std::nextafter(kSteepSlopeDegrees, 90.0f) }).verbs.empty());
}

TEST_CASE("a depth or slope that is not a number reads as none, and a negative one too")
{
    const VerbSet set = shippedSet();
    const CapabilitySet all = CapabilitySet::all();
    CHECK(idsOf(set, availableVerbs(set, all, { true, kNaN, kNaN })) == Ids { "walk" });
    CHECK(idsOf(set, availableVerbs(set, all, { true, -2.0f, -30.0f })) == Ids { "walk" });
    CHECK(idsOf(set, availableVerbs(set, all, { true, kInf, 0.0f })) == Ids { "boat", "dive", "swim" });
    CHECK(availableVerbs(set, all, { false, kNaN, kNaN }).speedFactor == 1.0f);
}

TEST_CASE("the movement modifier is the speed of the best available verb")
{
    const VerbSet set = shippedSet();
    const CapabilitySet none;
    const Availability dry = availableVerbs(set, none, { true, 0.0f, 0.0f });
    REQUIRE(dry.best.has_value());
    CHECK(set.verbs()[*dry.best].id == "walk");
    CHECK(dry.speedFactor == 1.0f);

    const Availability wet = availableVerbs(set, none, { true, 1.0f, 0.0f });
    REQUIRE(wet.best.has_value());
    CHECK(set.verbs()[*wet.best].id == "wade");
    CHECK(wet.speedFactor == wadingSpeedFactor(1.0f));

    const Availability boat = availableVerbs(set, CapabilitySet::all(), { true, 1.0f, 0.0f });
    REQUIRE(boat.best.has_value());
    CHECK(set.verbs()[*boat.best].id == "boat");
    CHECK(boat.speedFactor > 1.0f);

    const Availability nothing = availableVerbs(set, none, { false, 0.0f, 0.0f });
    CHECK_FALSE(nothing.best.has_value());
    CHECK(nothing.speedFactor == 1.0f);
}

TEST_CASE("availability is a pure function of its arguments")
{
    const VerbSet set = shippedSet();
    const CapabilitySet all = CapabilitySet::all();
    const Situation s { true, 0.7f, 20.0f };
    const Availability a = availableVerbs(set, all, s);
    const Availability b = availableVerbs(set, all, s);
    CHECK(a.verbs == b.verbs);
    CHECK(a.best == b.best);
    CHECK(a.speedFactor == b.speedFactor);
}

TEST_CASE("an empty verb file is an error")
{
    std::string error;
    CHECK_FALSE(setFromJson("[]", error).has_value());
    CHECK_FALSE(error.empty());
    error.clear();
    CHECK_FALSE(VerbSet::fromVerbs({}, error).has_value());
    CHECK_FALSE(error.empty());
}

TEST_CASE("verb data errors name the verb and the field")
{
    std::string error;
    CHECK(setFromJson(verbJson(kGoodFields), error).has_value());

    CHECK_FALSE(setFromJson(verbJson(kGoodFields + ", \"capability\": \"levitate\""), error).has_value());
    CHECK(error.find("'v'") != std::string::npos);
    CHECK(error.find("capability") != std::string::npos);

    CHECK_FALSE(setFromJson(verbJson("\"terrain\": \"moon\", \"speed_permille\": 1000, \"stamina_per_second\": 0"), error).has_value());
    CHECK(error.find("terrain") != std::string::npos);
    CHECK_FALSE(setFromJson(verbJson("\"speed_permille\": 1000, \"stamina_per_second\": 0"), error).has_value());

    CHECK_FALSE(setFromJson(verbJson("\"terrain\": \"ground\", \"speed_permille\": -1, \"stamina_per_second\": 0"), error).has_value());
    CHECK(error.find("speed_permille") != std::string::npos);
    CHECK_FALSE(setFromJson(verbJson("\"terrain\": \"ground\", \"speed_permille\": 2001, \"stamina_per_second\": 0"), error).has_value());
    CHECK_FALSE(setFromJson(verbJson("\"terrain\": \"ground\", \"speed_permille\": 1000.5, \"stamina_per_second\": 0"), error).has_value());
    CHECK_FALSE(setFromJson(verbJson("\"terrain\": \"ground\", \"speed_permille\": \"fast\", \"stamina_per_second\": 0"), error).has_value());
    CHECK_FALSE(setFromJson(verbJson("\"terrain\": \"ground\", \"stamina_per_second\": 0"), error).has_value());

    CHECK_FALSE(setFromJson(verbJson("\"terrain\": \"ground\", \"speed_permille\": 1000, \"stamina_per_second\": -4"), error).has_value());
    CHECK(error.find("stamina_per_second") != std::string::npos);
    CHECK_FALSE(setFromJson(verbJson("\"terrain\": \"ground\", \"speed_permille\": 1000, \"stamina_per_second\": 1001"), error).has_value());

    CHECK_FALSE(setFromJson(verbJson(kGoodFields + ", \"depth_scaled\": \"yes\""), error).has_value());
    CHECK(error.find("depth_scaled") != std::string::npos);
}

TEST_CASE("duplicate verb ids are an error")
{
    std::string error;
    CHECK_FALSE(setFromJson("[{\"id\": \"walk\", " + kGoodFields + "}, {\"id\": \"walk\", " + kGoodFields + "}]", error).has_value());
    CHECK_FALSE(error.empty());

    Verb v;
    v.id = "walk";
    error.clear();
    CHECK_FALSE(VerbSet::fromVerbs({ v, v }, error).has_value());
    CHECK(error.find("duplicate") != std::string::npos);
    v.id = "";
    CHECK_FALSE(VerbSet::fromVerbs({ v }, error).has_value());
}

TEST_CASE("fromVerbs rejects a negative or oversized multiplier and a negative stamina cost")
{
    std::string error;
    Verb v;
    v.id = "odd";
    v.speedPermille = -100;
    CHECK_FALSE(VerbSet::fromVerbs({ v }, error).has_value());
    v.speedPermille = kMaxSpeedPermille + 1;
    CHECK_FALSE(VerbSet::fromVerbs({ v }, error).has_value());
    v.speedPermille = 500;
    v.staminaPerSecond = -1;
    CHECK_FALSE(VerbSet::fromVerbs({ v }, error).has_value());
    v.staminaPerSecond = 3;
    CHECK(VerbSet::fromVerbs({ v }, error).has_value());
}
