#include "ichor/audio/ambience_suppress_rules.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace ichor;

namespace {

const std::vector<AmbienceSuppressLayer> kLayers = {
    {"birds", "fauna"}, {"crickets", "fauna"}, {"wind", "wind"}, {"stream", "water"}, {"hum", "mystery"}};

const std::vector<std::string> kArtemis = {"cataclysm.artemis_withdrew"};

}

TEST_CASE("Artemis Withdrew silences every fauna layer and nothing else")
{
    const auto out = suppressAmbience(kArtemis, kLayers);
    REQUIRE(out.size() == kLayers.size());
    CHECK(out[0].id == "birds");
    CHECK(out[0].suppression == 1.0f);
    CHECK(out[0].gainScale == 0.0f);
    CHECK(out[1].gainScale == 0.0f);
    for (std::size_t i = 2; i < out.size(); ++i) {
        CHECK(out[i].suppression == 0.0f);
        CHECK(out[i].gainScale == 1.0f);
    }
}

TEST_CASE("without the axiom no layer is suppressed")
{
    const std::vector<std::string> none;
    for (const auto& r : suppressAmbience(none, kLayers)) {
        CHECK(r.suppression == 0.0f);
        CHECK(r.gainScale == 1.0f);
    }
}

TEST_CASE("other axioms leave fauna alone and the result ignores order and duplicates")
{
    const std::vector<std::string> other = {"cataclysm.the_unbinding", "boon.something"};
    CHECK(sourceSuppression(other, "fauna") == 0.0f);

    const std::vector<std::string> a = {"cataclysm.the_unbinding", "cataclysm.artemis_withdrew"};
    const std::vector<std::string> b = {"cataclysm.artemis_withdrew", "cataclysm.artemis_withdrew",
        "cataclysm.the_unbinding"};
    CHECK(sourceSuppression(a, "fauna") == 1.0f);
    CHECK(sourceSuppression(b, "fauna") == 1.0f);
    CHECK(suppressAmbience(a, kLayers).size() == suppressAmbience(b, kLayers).size());
}

TEST_CASE("unknown source tags and empty tags are never suppressed")
{
    const std::vector<AmbienceSuppressLayer> odd = {{"x", "Fauna"}, {"y", ""}, {"z", "fauna "}, {"w", "zzz"}};
    for (const auto& r : suppressAmbience(kArtemis, odd))
        CHECK(r.gainScale == 1.0f);
}

TEST_CASE("empty layer list gives an empty result and duplicate ids keep their order")
{
    CHECK(suppressAmbience(kArtemis, {}).empty());

    const std::vector<AmbienceSuppressLayer> dup = {{"a", "fauna"}, {"a", "wind"}, {"a", "fauna"}};
    const auto out = suppressAmbience(kArtemis, dup);
    REQUIRE(out.size() == 3);
    CHECK(out[0].gainScale == 0.0f);
    CHECK(out[1].gainScale == 1.0f);
    CHECK(out[2].gainScale == 0.0f);
}

TEST_CASE("the same inputs always give the same answer")
{
    const auto a = suppressAmbience(kArtemis, kLayers);
    const auto b = suppressAmbience(kArtemis, kLayers);
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i].id == b[i].id);
        CHECK(a[i].suppression == b[i].suppression);
    }
}
