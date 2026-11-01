#include "ichor/audio/ambience_suppress_rules.h"
#include "ichor/data/data_set.h"
#include "ichor/data/world_config.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
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

namespace {

Soundscape realSoundscape()
{
    DataSet data;
    REQUIRE(data.loadDirectory(std::filesystem::path(ICHOR_GAME_DATA_DIR)));
    Soundscape soundscape;
    std::string error;
    REQUIRE(Soundscape::load(data, soundscape, error));
    return soundscape;
}

std::vector<std::string> worldAxioms(std::string_view ids)
{
    const WorldConfigResult world = loadPinnedWorldConfig(ids, ICHOR_GAME_DATA_DIR);
    REQUIRE(world.ok());
    std::vector<std::string> out;
    for (const SlotAssignment& slot : world.config.slots)
        out.push_back(slot.axiomId);
    return out;
}

float faunaGain(const std::vector<BedGain>& beds)
{
    float total = 0.0f;
    for (const BedGain& bed : beds)
        if (bedSource(bed.clip) == kSourceFauna)
            total += bed.gain;
    return total;
}

}

TEST_CASE("The beds of a forest at night follow the axioms of the world")
{
    const Soundscape soundscape = realSoundscape();
    const auto plain = worldAxioms("cataclysm.flood");
    const auto withdrawn = worldAxioms("cataclysm.artemis_withdrew");
    REQUIRE(plain != withdrawn);

    const auto lively = axiomBeds(soundscape, plain, "forest", "forest", 0.0f, 23.0f);
    const auto silent = axiomBeds(soundscape, withdrawn, "forest", "forest", 0.0f, 23.0f);

    CHECK(faunaGain(lively) > 0.0f);
    CHECK(faunaGain(silent) == 0.0f);
    CHECK(lively != silent);
    CHECK_FALSE(silent.empty());
    CHECK(lively == blendedBeds(soundscape, "forest", "forest", 0.0f, 23.0f));
}

TEST_CASE("Artemis Withdrew leaves the bed and the wind of every biome untouched")
{
    const Soundscape soundscape = realSoundscape();
    const auto withdrawn = worldAxioms("cataclysm.artemis_withdrew");
    for (const char* biome : {"forest", "tundra", "taiga", "lake"}) {
        for (float hour : {5.0f, 12.0f, 18.0f, 23.0f}) {
            auto expected = blendedBeds(soundscape, biome, biome, 0.0f, hour);
            std::erase_if(expected, [](const BedGain& b) { return bedSource(b.clip) == kSourceFauna; });
            CHECK(axiomBeds(soundscape, withdrawn, biome, biome, 0.0f, hour) == expected);
        }
    }
}
