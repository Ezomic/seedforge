#include "ichor/data/data_set.h"
#include "ichor/stage0/weapon_class.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>

using namespace ichor;
using namespace ichor::stage0;

namespace {

DataRecord record(std::initializer_list<std::pair<const char*, DataValue>> fields, const char* id = "c")
{
    DataRecord r;
    r.id = id;
    for (const auto& [key, value] : fields) {
        r.fields[key] = value;
    }
    return r;
}

DataRecord valid()
{
    return record({{"windup_ticks", 20.0}, {"strike_ticks", 6.0}, {"recovery_ticks", 18.0}, {"stamina_cost", 15.0},
                   {"poise_damage", 25.0}, {"reach_permille", 1000.0}});
}

const DataSet& shipped()
{
    static DataSet set;
    static const bool loaded = set.loadDirectory(ICHOR_REPO_DATA);
    REQUIRE(loaded);
    return set;
}

}

TEST_CASE("a well-formed record becomes a weapon class")
{
    const auto cls = weaponClassFromRecord(valid());
    REQUIRE(cls.has_value());
    REQUIRE(cls->id == "c");
    REQUIRE(cls->commitmentTicks() == 44);
    REQUIRE_FALSE(cls->ranged);
}

TEST_CASE("a malformed record is rejected, not clamped")
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    for (const char* key : {"windup_ticks", "strike_ticks", "recovery_ticks", "stamina_cost", "poise_damage", "reach_permille"}) {
        for (const DataValue& bad : {DataValue{nan}, DataValue{inf}, DataValue{-inf}, DataValue{-1.0}, DataValue{1e12}, DataValue{2.5}, DataValue{std::string("9")}, DataValue{true}}) {
            DataRecord r = valid();
            r.fields[key] = bad;
            REQUIRE_FALSE(weaponClassFromRecord(r).has_value());
        }
        DataRecord missing = valid();
        missing.fields.erase(key);
        REQUIRE_FALSE(weaponClassFromRecord(missing).has_value());
    }
    DataRecord zeroWindup = valid();
    zeroWindup.fields["windup_ticks"] = 0.0;
    REQUIRE_FALSE(weaponClassFromRecord(zeroWindup).has_value());
    DataRecord badFlag = valid();
    badFlag.fields["ranged"] = 1.0;
    REQUIRE_FALSE(weaponClassFromRecord(badFlag).has_value());
    DataRecord noId = valid();
    noId.id.clear();
    REQUIRE_FALSE(weaponClassFromRecord(noId).has_value());
}

TEST_CASE("the shipped data holds five distinct, valid classes")
{
    const auto classes = weaponClasses(shipped());
    REQUIRE(classes.size() == 5);
    const DataTable* table = shipped().table("weapon_classes");
    REQUIRE(table != nullptr);
    REQUIRE(table->records.size() == 5);
    for (std::size_t i = 1; i < classes.size(); ++i) {
        REQUIRE(classes[i - 1].id < classes[i].id);
    }
}

TEST_CASE("classes differ in commitment, reach and poise the way the design says")
{
    const auto classes = weaponClasses(shipped());
    auto find = [&](const char* id) {
        for (const auto& c : classes) {
            if (c.id == id) {
                return c;
            }
        }
        FAIL("missing class " << id);
        return WeaponClass{};
    };
    const WeaponClass blade = find("arming_blade");
    const WeaponClass knife = find("knife");
    const WeaponClass spear = find("spear");
    const WeaponClass maul = find("maul");
    const WeaponClass bow = find("bow");
    REQUIRE(knife.commitmentTicks() < blade.commitmentTicks());
    REQUIRE(blade.commitmentTicks() < maul.commitmentTicks());
    REQUIRE(spear.reachPermille > blade.reachPermille);
    REQUIRE(maul.poiseDamage > blade.poiseDamage);
    REQUIRE(bow.ranged);
    REQUIRE(bow.reachPermille > spear.reachPermille);
    for (const auto& c : classes) {
        if (c.id != "bow") {
            REQUIRE_FALSE(c.ranged);
        }
        REQUIRE(c.commitmentTicks() <= 120); // heavy but never a stall: fights are 3 to 8 seconds
    }
}

TEST_CASE("a weapon resolves its class by name and an unknown one by nothing")
{
    const DataSet& data = shipped();
    REQUIRE(classOfWeapon(data, record({{"weapon_class", std::string("spear")}}, "w"))->id == "spear");
    REQUIRE_FALSE(classOfWeapon(data, record({{"weapon_class", std::string("trident")}}, "w")).has_value());
    REQUIRE_FALSE(classOfWeapon(data, record({{"weapon_class", 3.0}}, "w")).has_value());
    REQUIRE_FALSE(classOfWeapon(data, record({}, "w")).has_value());
    REQUIRE_FALSE(classOfWeapon(DataSet{}, record({{"weapon_class", std::string("spear")}}, "w")).has_value());
}

TEST_CASE("invalid class records are skipped when listing")
{
    const auto root = std::filesystem::temp_directory_path() / "ichor_weapon_class_test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "weapon_classes");
    std::ofstream(root / "weapon_classes" / "x.json")
        << R"([{"id":"ok","windup_ticks":10,"strike_ticks":5,"recovery_ticks":10,"stamina_cost":5,"poise_damage":5,"reach_permille":900},
               {"id":"bad","windup_ticks":-4,"strike_ticks":5,"recovery_ticks":10,"stamina_cost":5,"poise_damage":5,"reach_permille":900}])";
    DataSet data;
    REQUIRE(data.loadDirectory(root));
    const auto classes = weaponClasses(data);
    std::filesystem::remove_all(root);
    REQUIRE(classes.size() == 1);
    REQUIRE(classes[0].id == "ok");
    REQUIRE(weaponClasses(DataSet{}).empty());
}
