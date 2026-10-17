#include "ichor/ui/landmark_compass.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <string>
#include <utility>
#include <vector>

using namespace ichor;

namespace {

std::vector<NavLandmark> world()
{
    return {
        {"lm.tower", "the broken tower", {0.0f, 400.0f}},
        {"lm.flats", "the salt flats", {0.0f, 1500.0f}},
        {"lm.well", "the dry well", {-30.0f, 0.0f}},
        {"lm.stone", "the leaning stone", {2000.0f, 0.0f}},
    };
}

constexpr float kInf = std::numeric_limits<float>::infinity();
constexpr float kNan = std::numeric_limits<float>::quiet_NaN();

} // namespace

TEST_CASE("bearing covers all eight points", "[landmark_compass]")
{
    CHECK(bearingOf(0, 5) == CompassPoint::North);
    CHECK(bearingOf(5, 5) == CompassPoint::NorthEast);
    CHECK(bearingOf(5, 0) == CompassPoint::East);
    CHECK(bearingOf(5, -5) == CompassPoint::SouthEast);
    CHECK(bearingOf(0, -5) == CompassPoint::South);
    CHECK(bearingOf(-5, -5) == CompassPoint::SouthWest);
    CHECK(bearingOf(-5, 0) == CompassPoint::West);
    CHECK(bearingOf(-5, 5) == CompassPoint::NorthWest);
    CHECK(bearingOf(1, 100) == CompassPoint::North);
    CHECK(bearingOf(100, 1) == CompassPoint::East);
}

TEST_CASE("relative sector turns with the facing", "[landmark_compass]")
{
    CHECK(relativeSectorOf(0, 10, 0, 1) == RelativeSector::Ahead);
    CHECK(relativeSectorOf(10, 0, 0, 1) == RelativeSector::Right);
    CHECK(relativeSectorOf(-10, 0, 0, 1) == RelativeSector::Left);
    CHECK(relativeSectorOf(0, -10, 0, 1) == RelativeSector::Behind);
    CHECK(relativeSectorOf(10, 10, 0, 1) == RelativeSector::AheadRight);
    CHECK(relativeSectorOf(-10, -10, 0, 1) == RelativeSector::BehindLeft);
    CHECK(relativeSectorOf(0, 10, 1, 0) == RelativeSector::Left);
    CHECK(relativeSectorOf(10, 0, 0, -1) == RelativeSector::Left);
    CHECK(relativeSectorOf(10, 0, 0, 500) == RelativeSector::Right);
}

TEST_CASE("distance bands", "[landmark_compass]")
{
    CHECK(distanceBandFor(0.0) == DistanceBand::StonesThrow);
    CHECK(distanceBandFor(49.0 * 49.0) == DistanceBand::StonesThrow);
    CHECK(distanceBandFor(50.0 * 50.0) == DistanceBand::ShortWalk);
    CHECK(distanceBandFor(500.0 * 500.0) == DistanceBand::GoodWalk);
    CHECK(distanceBandFor(5000.0 * 5000.0) == DistanceBand::LongWay);
    CHECK(distanceBandFor(std::numeric_limits<double>::quiet_NaN()) == DistanceBand::StonesThrow);
}

TEST_CASE("compass lists nearest landmarks first with sentences", "[landmark_compass]")
{
    const auto r = readCompass({0.0f, 0.0f}, {0.0f, 1.0f}, world(), {.maxResults = 3});
    REQUIRE(r.status == CompassStatus::Ok);
    CHECK(r.facing == CompassPoint::North);
    REQUIRE(r.nearest.size() == 3);
    CHECK(r.nearest[0].landmarkId == "lm.well");
    CHECK(r.nearest[0].distance == DistanceBand::StonesThrow);
    CHECK(r.nearest[1].landmarkId == "lm.tower");
    CHECK(r.nearest[1].bearing == CompassPoint::North);
    CHECK(r.nearest[1].relative == RelativeSector::Ahead);
    CHECK(r.nearest[1].distance == DistanceBand::GoodWalk);
    CHECK(r.nearest[1].text == "The broken tower lies north, straight ahead, a good walk off.");
    CHECK(r.nearest[2].landmarkId == "lm.flats");
    CHECK(r.nearest[2].distance == DistanceBand::LongWay);
}

TEST_CASE("compass reorders when the player moves", "[landmark_compass]")
{
    const auto r = readCompass({1900.0f, 0.0f}, {1.0f, 0.0f}, world(), {.maxResults = 1});
    REQUIRE(r.nearest.size() == 1);
    CHECK(r.nearest[0].landmarkId == "lm.stone");
    CHECK(r.nearest[0].bearing == CompassPoint::East);
    CHECK(r.nearest[0].distance == DistanceBand::ShortWalk);
}

TEST_CASE("compass ties break by id whatever the input order", "[landmark_compass]")
{
    std::vector<NavLandmark> lms = {
        {"b", "the b stone", {100.0f, 0.0f}},
        {"a", "the a stone", {-100.0f, 0.0f}},
    };
    const auto r1 = readCompass({0.0f, 0.0f}, {0.0f, 1.0f}, lms);
    std::swap(lms[0], lms[1]);
    const auto r2 = readCompass({0.0f, 0.0f}, {0.0f, 1.0f}, lms);
    REQUIRE(r1.nearest.size() == 2);
    CHECK(r1.nearest[0].landmarkId == "a");
    CHECK(r2.nearest[0].landmarkId == "a");
}

TEST_CASE("compass with no landmarks is ok and empty", "[landmark_compass]")
{
    const auto r = readCompass({0.0f, 0.0f}, {0.0f, 1.0f}, {});
    CHECK(r.status == CompassStatus::Ok);
    CHECK(r.nearest.empty());
}

TEST_CASE("compass rejects hostile input", "[landmark_compass]")
{
    const auto lms = world();
    CHECK(readCompass({kNan, 0.0f}, {0.0f, 1.0f}, lms).status == CompassStatus::InvalidPosition);
    CHECK(readCompass({0.0f, kInf}, {0.0f, 1.0f}, lms).status == CompassStatus::InvalidPosition);
    CHECK(readCompass({2.0e6f, 0.0f}, {0.0f, 1.0f}, lms).status == CompassStatus::InvalidPosition);
    CHECK(readCompass({0.0f, 0.0f}, {0.0f, 0.0f}, lms).status == CompassStatus::InvalidFacing);
    CHECK(readCompass({0.0f, 0.0f}, {kNan, 1.0f}, lms).status == CompassStatus::InvalidFacing);

    auto bad = lms;
    bad[1].position.x = kNan;
    CHECK(readCompass({0.0f, 0.0f}, {0.0f, 1.0f}, bad).status == CompassStatus::InvalidLandmark);
    bad = lms;
    bad[1].name = "";
    CHECK(readCompass({0.0f, 0.0f}, {0.0f, 1.0f}, bad).status == CompassStatus::InvalidLandmark);
    bad = lms;
    bad[1].name = std::string(65, 'x');
    CHECK(readCompass({0.0f, 0.0f}, {0.0f, 1.0f}, bad).status == CompassStatus::InvalidLandmark);
    bad = lms;
    bad[1].name = std::string("tower\x01x");
    CHECK(readCompass({0.0f, 0.0f}, {0.0f, 1.0f}, bad).status == CompassStatus::InvalidLandmark);
    bad = lms;
    bad[1].id = "has space";
    CHECK(readCompass({0.0f, 0.0f}, {0.0f, 1.0f}, bad).status == CompassStatus::InvalidLandmark);
    bad = lms;
    bad[1].id = bad[0].id;
    CHECK(readCompass({0.0f, 0.0f}, {0.0f, 1.0f}, bad).status == CompassStatus::DuplicateLandmark);
    CHECK(readCompass({0.0f, 0.0f}, {0.0f, 1.0f}, lms, {.maxLandmarks = 2}).status == CompassStatus::TooManyLandmarks);
}

TEST_CASE("compass sentence wraps the name in fixed vocabulary", "[landmark_compass]")
{
    const auto r = readCompass({0.0f, 0.0f}, {1.0f, 0.0f}, world(), {.maxResults = 8});
    REQUIRE(r.nearest.size() == 4);
    for (const auto& e : r.nearest) {
        const std::string tail = std::string(" lies ") + compassPointName(e.bearing) + ", "
            + relativeSectorPhrase(e.relative) + ", " + distanceBandPhrase(e.distance) + " off.";
        REQUIRE(e.text.size() > tail.size());
        CHECK(e.text.substr(e.text.size() - tail.size()) == tail);
    }
}

TEST_CASE("route goes through the landmark nearest the target", "[landmark_compass]")
{
    const NavLandmark mine{"site.mine", "the old mine", {600.0f, 1500.0f}};
    const auto r = describeRoute({0.0f, 0.0f}, mine, world());
    REQUIRE(r.status == CompassStatus::Ok);
    CHECK(r.viaId == "lm.flats");
    CHECK(r.text == "North past the salt flats, then east to the old mine, a long way off.");
}

TEST_CASE("route without a closer landmark gives a plain bearing", "[landmark_compass]")
{
    const NavLandmark mine{"site.mine", "the old mine", {0.0f, -300.0f}};
    const auto r = describeRoute({0.0f, 0.0f}, mine, {{"lm.far", "the far spire", {5000.0f, 5000.0f}}});
    REQUIRE(r.status == CompassStatus::Ok);
    CHECK(r.viaId.empty());
    CHECK(r.viaId.empty());
    CHECK(r.text == "Head south to the old mine, a good walk off.");
}

TEST_CASE("route skips the target itself and a landmark underfoot", "[landmark_compass]")
{
    const auto lms = world();
    const auto r = describeRoute({-30.0f, 0.0f}, lms[0], lms);
    REQUIRE(r.status == CompassStatus::Ok);
    CHECK(r.viaId.empty());
    CHECK(r.text == "Head north to the broken tower, a good walk off.");
}

TEST_CASE("route never doubles back through a landmark behind the player", "[landmark_compass]")
{
    const NavLandmark mine{"site.mine", "the old mine", {0.0f, 1000.0f}};
    const std::vector<NavLandmark> lms = {{"lm.behind", "the sunken road", {0.0f, -100.0f}}};
    CHECK(describeRoute({0.0f, 0.0f}, mine, lms).viaId.empty());
}

TEST_CASE("zero deltas read as north and ahead", "[landmark_compass]")
{
    CHECK(bearingOf(0, 0) == CompassPoint::North);
    CHECK(relativeSectorOf(0, 0, 0, 1) == RelativeSector::Ahead);
    const auto r = readCompass({5.0f, 5.0f}, {0.0f, 1.0f}, {{"lm.here", "the old well", {5.0f, 5.0f}}});
    REQUIRE(r.nearest.size() == 1);
    CHECK(r.nearest[0].bearing == CompassPoint::North);
    CHECK(r.nearest[0].distance == DistanceBand::StonesThrow);
}

TEST_CASE("compass accepts extreme facing vectors and zero results", "[landmark_compass]")
{
    const auto r = readCompass({0.0f, 0.0f}, {3.0e38f, 0.0f}, world());
    REQUIRE(r.status == CompassStatus::Ok);
    CHECK(r.facing == CompassPoint::East);
    CHECK(readCompass({0.0f, 0.0f}, {0.0f, 1.0f}, world(), {.maxResults = 0}).nearest.empty());
}

TEST_CASE("route to where you stand says so", "[landmark_compass]")
{
    const NavLandmark here{"site.here", "the old mine", {10.0f, 10.0f}};
    CHECK(describeRoute({10.0f, 10.0f}, here, {}).text == "You are standing at the old mine.");
}

TEST_CASE("route rejects hostile input", "[landmark_compass]")
{
    const NavLandmark mine{"site.mine", "the old mine", {600.0f, 1500.0f}};
    CHECK(describeRoute({kNan, 0.0f}, mine, world()).status == CompassStatus::InvalidPosition);
    NavLandmark bad = mine;
    bad.position.y = kInf;
    CHECK(describeRoute({0.0f, 0.0f}, bad, world()).status == CompassStatus::InvalidLandmark);
    bad = mine;
    bad.name = "";
    CHECK(describeRoute({0.0f, 0.0f}, bad, world()).status == CompassStatus::InvalidLandmark);
    auto dup = world();
    dup[1].id = dup[0].id;
    CHECK(describeRoute({0.0f, 0.0f}, mine, dup).status == CompassStatus::DuplicateLandmark);
}
