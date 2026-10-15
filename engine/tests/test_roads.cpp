#include "ichor/core/wading.h"
#include "ichor/data/data_set.h"
#include "ichor/traversal/roads.h"
#include "ichor/world/water_field.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>

#ifdef _WIN32
#include <process.h>
#define ICHOR_PID _getpid()
#else
#include <unistd.h>
#define ICHOR_PID getpid()
#endif

using namespace ichor;

namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

bool loadFrom(const std::string& json, int& bonus, std::string& note, std::string& error)
{
    static int counter = 0;
    const auto root = std::filesystem::temp_directory_path()
        / ("ichor_roads_test_" + std::to_string(ICHOR_PID) + "_" + std::to_string(counter++));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "roads");
    std::ofstream(root / "roads" / "road_bonus.json", std::ios::binary) << json;
    DataSet data;
    const bool loaded = data.loadDirectory(root);
    std::filesystem::remove_all(root);
    REQUIRE(loaded);
    return loadRoadBonus(data, bonus, note, error);
}

}

TEST_CASE("the road bonus scales with coverage", "[roads]")
{
    CHECK(roadSpeedPermille(0.0f, 250) == 1000);
    CHECK(roadSpeedPermille(0.5f, 250) == 1125);
    CHECK(roadSpeedPermille(1.0f, 250) == 1250);
    CHECK(roadSpeedPermille(1.0f, 200) == 1200);
    CHECK(roadSpeedPermille(1.0f, 300) == 1300);
}

TEST_CASE("coverage that is not a sensible number is held to none or all", "[roads]")
{
    CHECK(roadSpeedPermille(kNaN, 250) == 1000);
    CHECK(roadSpeedPermille(-0.5f, 250) == 1000);
    CHECK(roadSpeedPermille(-kInf, 250) == 1000);
    CHECK(roadSpeedPermille(1.5f, 250) == 1250);
    CHECK(roadSpeedPermille(kInf, 250) == 1250);
}

TEST_CASE("a bonus outside the doc's range is clamped when used", "[roads]")
{
    CHECK(roadSpeedPermille(1.0f, 0) == 1200);
    CHECK(roadSpeedPermille(1.0f, 999) == 1300);
    CHECK(roadSpeedPermille(1.0f, -500) == 1200);
}

TEST_CASE("the null field has no road anywhere, even far outside the world", "[roads]")
{
    const NullRoadField field;
    CHECK(field.coverageAt(0.0, 0.0) == 0.0f);
    CHECK(field.coverageAt(1e300, -1e300) == 0.0f);
    CHECK(roadSpeedFactor(field.coverageAt(1e300, 0.0), 0.0f, 250) == 1.0);
}

TEST_CASE("a uniform field gives its coverage everywhere", "[roads]")
{
    const UniformRoadField field(0.5f);
    CHECK(field.coverageAt(-1e9, 1e9) == 0.5f);
    CHECK(roadSpeedFactor(field.coverageAt(3.0, 4.0), 0.0f, 250) == 1.125);
}

TEST_CASE("water and road do not stack", "[roads]")
{
    CHECK(roadSpeedFactor(1.0f, 0.0f, 250) == 1.25);
    CHECK(roadSpeedFactor(1.0f, kWadeStart, 250) == 1.25);
    CHECK(roadSpeedFactor(1.0f, kWadeStart + 0.01f, 250) == 1.0);
    CHECK(roadSpeedFactor(1.0f, 1.5f, 250) == 1.0);
    CHECK(roadSpeedFactor(1.0f, kNaN, 250) == 1.25);
    // Wading and a road at once: the walker is slowed by the water alone.
    CHECK(wadingSpeedFactor(1.0f) * roadSpeedFactor(1.0f, 1.0f, 250) == static_cast<double>(wadingSpeedFactor(1.0f)));
}

TEST_CASE("the shipped road bonus is inside the doc's range", "[roads]")
{
    DataSet data;
    REQUIRE(data.loadDirectory(ICHOR_GAME_DATA_DIR));
    const DataTable* table = data.table("roads");
    REQUIRE(table != nullptr);
    REQUIRE(table->records.count("road_bonus") == 1);
    int bonus = 0;
    std::string note;
    std::string error;
    REQUIRE(loadRoadBonus(data, bonus, note, error));
    CHECK(note.empty());
    CHECK(bonus >= kMinRoadBonusPermille);
    CHECK(bonus <= kMaxRoadBonusPermille);
}

TEST_CASE("the bonus is read from data and clamped to the doc's range", "[roads]")
{
    int bonus = 0;
    std::string note;
    std::string error;

    REQUIRE(loadFrom(R"([{"id": "road_bonus", "bonus_permille": 220}])", bonus, note, error));
    CHECK(bonus == 220);
    CHECK(note.empty());

    REQUIRE(loadFrom(R"([{"id": "road_bonus", "bonus_permille": 900}])", bonus, note, error));
    CHECK(bonus == kMaxRoadBonusPermille);
    CHECK(!note.empty());

    note.clear();
    REQUIRE(loadFrom(R"([{"id": "road_bonus", "bonus_permille": -40}])", bonus, note, error));
    CHECK(bonus == kMinRoadBonusPermille);
    CHECK(!note.empty());

    REQUIRE(!loadFrom(R"([{"id": "road_bonus", "bonus_permille": "fast"}])", bonus, note, error));
    CHECK(!error.empty());
    REQUIRE(!loadFrom(R"([{"id": "road_bonus", "bonus_permille": 250.5}])", bonus, note, error));
}

TEST_CASE("without a roads table the default bonus applies", "[roads]")
{
    DataSet data;
    int bonus = 0;
    std::string note;
    std::string error;
    REQUIRE(loadRoadBonus(data, bonus, note, error));
    CHECK(bonus == kDefaultRoadBonusPermille);
}
