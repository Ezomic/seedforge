#include "ichor/traversal/boat.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

using namespace ichor;

namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

CapabilitySet withBoat()
{
    CapabilitySet set;
    set.add(Capability::Boat);
    return set;
}

WaterReading water(float depth, float current = 0.0f, float alignment = 0.0f)
{
    return WaterReading{depth, current, alignment};
}

CrossResult cross(const WaterReading& w, const WeatherReading& weather = {}, const Load& load = Load{0, 0})
{
    return crossWater(BoatSpec{}, withBoat(), w, weather, load);
}

}

TEST_CASE("a boat crosses calm deep water at its base speed", "[boat]")
{
    const CrossResult result = cross(water(3.0f));
    REQUIRE(result.verdict == CrossVerdict::Ok);
    REQUIRE(result.speedPermille == BoatSpec{}.baseSpeedPermille);
}

TEST_CASE("crossing needs the boat capability", "[boat]")
{
    const CrossResult result = crossWater(BoatSpec{}, CapabilitySet{}, water(3.0f), {}, Load{0, 0});
    REQUIRE(result.verdict == CrossVerdict::NoCapability);
    REQUIRE(result.speedPermille == 0);

    CapabilitySet swimOnly;
    swimOnly.add(Capability::Swim);
    REQUIRE(crossWater(BoatSpec{}, swimOnly, water(3.0f), {}, Load{0, 0}).verdict == CrossVerdict::NoCapability);
}

TEST_CASE("draft is a hard boundary", "[boat]")
{
    const float draft = BoatSpec{}.draftMetres;
    REQUIRE(cross(water(draft)).verdict == CrossVerdict::Ok);
    REQUIRE(cross(water(std::nextafter(draft, 0.0f))).verdict == CrossVerdict::TooShallow);
    REQUIRE(cross(water(0.0f)).verdict == CrossVerdict::TooShallow);
    REQUIRE(cross(water(-5.0f)).verdict == CrossVerdict::TooShallow);
}

TEST_CASE("the current limit is a hard boundary", "[boat]")
{
    const float limit = BoatSpec{}.maxCurrentMetresPerSecond;
    REQUIRE(cross(water(3.0f, limit, 1.0f)).verdict == CrossVerdict::Ok);
    REQUIRE(cross(water(3.0f, std::nextafter(limit, 100.0f), 1.0f)).verdict == CrossVerdict::CurrentTooStrong);
}

TEST_CASE("weather limits are hard boundaries", "[boat]")
{
    const BoatSpec spec;
    REQUIRE(cross(water(3.0f), {spec.maxWaveMetres, 0.0f}).verdict == CrossVerdict::Ok);
    REQUIRE(cross(water(3.0f), {std::nextafter(spec.maxWaveMetres, 100.0f), 0.0f}).verdict == CrossVerdict::TooRough);
    REQUIRE(cross(water(3.0f), {0.0f, spec.maxWindMetresPerSecond}).verdict == CrossVerdict::Ok);
    REQUIRE(cross(water(3.0f), {0.0f, std::nextafter(spec.maxWindMetresPerSecond, 100.0f)}).verdict
            == CrossVerdict::TooRough);
}

TEST_CASE("weather slows the hull in proportion, and the worse of waves and wind counts", "[boat]")
{
    const BoatSpec spec;
    REQUIRE(weatherSpeedPermille(spec, {}) == 1000);
    REQUIRE(weatherSpeedPermille(spec, {spec.maxWaveMetres, 0.0f}) == 1000 - kWeatherMaxLossPermille);
    REQUIRE(weatherSpeedPermille(spec, {0.0f, spec.maxWindMetresPerSecond}) == 1000 - kWeatherMaxLossPermille);
    REQUIRE(weatherSpeedPermille(spec, {spec.maxWaveMetres * 0.5f, 0.0f}) == 1000 - kWeatherMaxLossPermille / 2);
    REQUIRE(weatherSpeedPermille(spec, {spec.maxWaveMetres * 0.5f, spec.maxWindMetresPerSecond * 0.25f})
            == weatherSpeedPermille(spec, {spec.maxWaveMetres * 0.5f, 0.0f}));

    const int calm = cross(water(3.0f)).speedPermille;
    const int rough = cross(water(3.0f), {spec.maxWaveMetres, 0.0f}).speedPermille;
    REQUIRE(rough < calm);
    REQUIRE(rough == calm / 2);
}

TEST_CASE("a current speeds a boat downstream and slows it upstream", "[boat]")
{
    const int still = cross(water(3.0f, 0.0f, 1.0f)).speedPermille;
    const int down = cross(water(3.0f, 1.0f, 1.0f)).speedPermille;
    const int up = cross(water(3.0f, 1.0f, -1.0f)).speedPermille;
    REQUIRE(down == still + kCurrentPermillePerMetrePerSecond);
    REQUIRE(up == still - kCurrentPermillePerMetrePerSecond);
    REQUIRE(cross(water(3.0f, 1.0f, 0.0f)).speedPermille == still);
}

TEST_CASE("a current stronger than the hull leaves no headway upstream", "[boat]")
{
    BoatSpec weak;
    weak.baseSpeedPermille = 100;
    weak.maxCurrentMetresPerSecond = 2.0f;
    const CrossResult result = crossWater(weak, withBoat(), water(3.0f, 2.0f, -1.0f), {}, Load{0, 0});
    REQUIRE(result.verdict == CrossVerdict::NoHeadway);
    REQUIRE(result.speedPermille == 0);
    REQUIRE(crossWater(weak, withBoat(), water(3.0f, 2.0f, 1.0f), {}, Load{0, 0}).verdict == CrossVerdict::Ok);
}

TEST_CASE("alignment outside minus one to one is clamped", "[boat]")
{
    REQUIRE(cross(water(3.0f, 1.0f, 50.0f)).speedPermille == cross(water(3.0f, 1.0f, 1.0f)).speedPermille);
    REQUIRE(cross(water(3.0f, 1.0f, -50.0f)).speedPermille == cross(water(3.0f, 1.0f, -1.0f)).speedPermille);
    REQUIRE(cross(water(3.0f, 1.0f, kInf)).verdict == CrossVerdict::Invalid);
}

TEST_CASE("speed never exceeds the cap", "[boat]")
{
    BoatSpec fast;
    fast.baseSpeedPermille = kMaxBoatSpeedPermille;
    fast.maxCurrentMetresPerSecond = kMaxBoatLimitMetres;
    const CrossResult result = crossWater(fast, withBoat(), water(3.0f, kMaxBoatLimitMetres, 1.0f), {}, Load{0, 0});
    REQUIRE(result.verdict == CrossVerdict::Ok);
    REQUIRE(result.speedPermille == kMaxBoatSpeedPermille);
}

TEST_CASE("load slows the hull and an overload is refused", "[boat]")
{
    const BoatSpec spec;
    REQUIRE(loadSpeedPermille(spec, Load{0, 0}) == 1000);
    REQUIRE(loadSpeedPermille(spec, Load{spec.seats, 0}) == 1000 - kLoadMaxLossPermille);
    REQUIRE(loadSpeedPermille(spec, Load{0, spec.cargoKilograms}) == 1000 - kLoadMaxLossPermille);
    REQUIRE(loadSpeedPermille(spec, Load{spec.seats + 1, 0}) == 0);
    REQUIRE(loadSpeedPermille(spec, Load{0, spec.cargoKilograms + 1}) == 0);
    REQUIRE(loadSpeedPermille(spec, Load{-1, 0}) == 0);
    REQUIRE(loadSpeedPermille(spec, Load{0, -1}) == 0);

    const int light = cross(water(3.0f)).speedPermille;
    const int full = cross(water(3.0f), {}, Load{spec.seats, spec.cargoKilograms}).speedPermille;
    REQUIRE(full < light);
    REQUIRE(cross(water(3.0f), {}, Load{spec.seats + 1, 0}).verdict == CrossVerdict::Invalid);
}

TEST_CASE("non-finite or negative readings are invalid, never a crossing", "[boat]")
{
    for (const float bad : {kNaN, kInf, -kInf}) {
        REQUIRE(cross(water(bad)).verdict == CrossVerdict::Invalid);
    }
    for (const float bad : {kNaN, kInf, -1.0f}) {
        REQUIRE(cross(water(3.0f, bad)).verdict == CrossVerdict::Invalid);
        REQUIRE(cross(water(3.0f), {bad, 0.0f}).verdict == CrossVerdict::Invalid);
        REQUIRE(cross(water(3.0f), {0.0f, bad}).verdict == CrossVerdict::Invalid);
    }
    REQUIRE(cross(water(3.0f, 0.0f, kNaN)).verdict == CrossVerdict::Invalid);
    REQUIRE(cross(water(std::numeric_limits<float>::max())).verdict == CrossVerdict::Ok);
}

TEST_CASE("an unusable hull spec is invalid", "[boat]")
{
    auto bad = [](auto edit) {
        BoatSpec spec;
        edit(spec);
        return crossWater(spec, withBoat(), water(3.0f), {}, Load{0, 0}).verdict;
    };
    REQUIRE(bad([](BoatSpec& s) { s.draftMetres = kNaN; }) == CrossVerdict::Invalid);
    REQUIRE(bad([](BoatSpec& s) { s.draftMetres = 0.0f; }) == CrossVerdict::Invalid);
    REQUIRE(bad([](BoatSpec& s) { s.maxCurrentMetresPerSecond = kInf; }) == CrossVerdict::Invalid);
    REQUIRE(bad([](BoatSpec& s) { s.maxCurrentMetresPerSecond = 1e30f; }) == CrossVerdict::Invalid);
    REQUIRE(bad([](BoatSpec& s) { s.maxWaveMetres = -1.0f; }) == CrossVerdict::Invalid);
    REQUIRE(bad([](BoatSpec& s) { s.maxWindMetresPerSecond = kNaN; }) == CrossVerdict::Invalid);
    REQUIRE(bad([](BoatSpec& s) { s.baseSpeedPermille = 0; }) == CrossVerdict::Invalid);
    REQUIRE(bad([](BoatSpec& s) { s.baseSpeedPermille = kMaxBoatSpeedPermille + 1; }) == CrossVerdict::Invalid);
    REQUIRE(bad([](BoatSpec& s) { s.seats = 0; }) == CrossVerdict::Invalid);
    REQUIRE(bad([](BoatSpec& s) { s.seats = std::numeric_limits<int>::max(); }) == CrossVerdict::Invalid);
    REQUIRE(bad([](BoatSpec& s) { s.cargoKilograms = -1; }) == CrossVerdict::Invalid);
    REQUIRE(bad([](BoatSpec& s) { s.cargoKilograms = std::numeric_limits<int>::max(); }) == CrossVerdict::Invalid);
    REQUIRE(weatherSpeedPermille(BoatSpec{0.0f}, {}) == 0);
}

TEST_CASE("crossing is deterministic", "[boat]")
{
    const CrossResult a = cross(water(2.5f, 1.3f, -0.4f), {0.4f, 6.0f}, Load{2, 80});
    const CrossResult b = cross(water(2.5f, 1.3f, -0.4f), {0.4f, 6.0f}, Load{2, 80});
    REQUIRE(a.verdict == b.verdict);
    REQUIRE(a.speedPermille == b.speedPermille);
    REQUIRE(a.speedPermille > 0);
}

TEST_CASE("boarding needs the capability, a seat, wadeable water and a slow boat", "[boat]")
{
    const BoatSpec spec;
    const Load one{1, 0};
    REQUIRE(canBoard(spec, withBoat(), one, one, 0.5f, 0.0f) == BoardVerdict::Ok);
    REQUIRE(canBoard(spec, CapabilitySet{}, one, one, 0.5f, 0.0f) == BoardVerdict::NoCapability);

    REQUIRE(canBoard(spec, withBoat(), Load{spec.seats - 1, 0}, one, 0.0f, 0.0f) == BoardVerdict::Ok);
    REQUIRE(canBoard(spec, withBoat(), Load{spec.seats, 0}, one, 0.0f, 0.0f) == BoardVerdict::NoSeat);
    REQUIRE(canBoard(spec, withBoat(), one, Load{0, spec.cargoKilograms}, 0.0f, 0.0f) == BoardVerdict::Ok);
    REQUIRE(canBoard(spec, withBoat(), one, Load{0, spec.cargoKilograms + 1}, 0.0f, 0.0f) == BoardVerdict::NoSeat);

    REQUIRE(canBoard(spec, withBoat(), one, one, kBoardingDepthMetres, 0.0f) == BoardVerdict::Ok);
    REQUIRE(canBoard(spec, withBoat(), one, one, std::nextafter(kBoardingDepthMetres, 10.0f), 0.0f)
            == BoardVerdict::TooDeep);
    REQUIRE(canBoard(spec, withBoat(), one, one, -3.0f, 0.0f) == BoardVerdict::Ok);

    REQUIRE(canBoard(spec, withBoat(), one, one, 0.5f, kMooringSpeedMetresPerSecond) == BoardVerdict::Ok);
    REQUIRE(canBoard(spec, withBoat(), one, one, 0.5f, std::nextafter(kMooringSpeedMetresPerSecond, 10.0f))
            == BoardVerdict::TooFast);
}

TEST_CASE("boarding rejects hostile input", "[boat]")
{
    const BoatSpec spec;
    const Load one{1, 0};
    REQUIRE(canBoard(spec, withBoat(), one, one, kNaN, 0.0f) == BoardVerdict::Invalid);
    REQUIRE(canBoard(spec, withBoat(), one, one, kInf, 0.0f) == BoardVerdict::Invalid);
    REQUIRE(canBoard(spec, withBoat(), one, one, 0.0f, kNaN) == BoardVerdict::Invalid);
    REQUIRE(canBoard(spec, withBoat(), one, one, 0.0f, -1.0f) == BoardVerdict::Invalid);
    REQUIRE(canBoard(spec, withBoat(), one, Load{std::numeric_limits<int>::max(), 0}, 0.0f, 0.0f)
            == BoardVerdict::Invalid);
    REQUIRE(canBoard(spec, withBoat(), Load{-1, 0}, one, 0.0f, 0.0f) == BoardVerdict::Invalid);
    REQUIRE(canBoard(BoatSpec{kNaN}, withBoat(), one, one, 0.0f, 0.0f) == BoardVerdict::Invalid);
}

TEST_CASE("landing needs footing and a bank gentle enough, from a slow boat", "[boat]")
{
    REQUIRE(canLand(0.3f, 10.0f, 0.0f) == LandVerdict::Ok);
    REQUIRE(canLand(kBoardingDepthMetres, 10.0f, 0.0f) == LandVerdict::Ok);
    REQUIRE(canLand(std::nextafter(kBoardingDepthMetres, 10.0f), 10.0f, 0.0f) == LandVerdict::TooDeep);
    REQUIRE(canLand(-1.0f, 10.0f, 0.0f) == LandVerdict::Ok);

    REQUIRE(canLand(0.0f, std::nextafter(kSteepSlopeDegrees, 0.0f), 0.0f) == LandVerdict::Ok);
    REQUIRE(canLand(0.0f, kSteepSlopeDegrees, 0.0f) == LandVerdict::TooSteep);
    REQUIRE(canLand(0.0f, 0.0f, kMooringSpeedMetresPerSecond) == LandVerdict::Ok);
    REQUIRE(canLand(0.0f, 0.0f, std::nextafter(kMooringSpeedMetresPerSecond, 10.0f)) == LandVerdict::TooFast);
}

TEST_CASE("landing rejects hostile input", "[boat]")
{
    REQUIRE(canLand(kNaN, 0.0f, 0.0f) == LandVerdict::Invalid);
    REQUIRE(canLand(kInf, 0.0f, 0.0f) == LandVerdict::Invalid);
    REQUIRE(canLand(0.0f, kNaN, 0.0f) == LandVerdict::Invalid);
    REQUIRE(canLand(0.0f, -1.0f, 0.0f) == LandVerdict::Invalid);
    REQUIRE(canLand(0.0f, 0.0f, kNaN) == LandVerdict::Invalid);
    REQUIRE(canLand(0.0f, 0.0f, kInf) == LandVerdict::Invalid);
}
