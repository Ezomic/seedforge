#include "ichor/ui/subtitle_track.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <string>

using namespace ichor;

namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

std::string stripSpaces(const std::string& s)
{
    std::string out;
    for (char c : s)
        if (c != ' ')
            out += c;
    return out;
}

std::string shown(const SubtitleTrack& track)
{
    std::string all;
    for (const SubtitleCue& cue : track.cues)
        all += cue.text;
    return stripSpaces(all);
}

std::string repeatWords(int n)
{
    std::string s;
    for (int i = 0; i < n; ++i)
        s += (i ? " word" : "word") + std::to_string(i);
    return s;
}

} // namespace

TEST_CASE("an empty or blank line has no cues and nothing is active", "[subtitle]")
{
    for (const char* text : {"", "   ", "\n\t "}) {
        const SubtitleTrack track = buildSubtitleTrack("Mara", text, 3.0f);
        CHECK(track.cues.empty());
        CHECK(track.speaker == "Mara");
        CHECK(activeSubtitle(track, 0.0f) == nullptr);
    }
}

TEST_CASE("a short line is one cue spanning the voice line", "[subtitle]")
{
    const SubtitleTrack track = buildSubtitleTrack("Mara", "Grain is short again.", 4.0f);
    REQUIRE(track.cues.size() == 1);
    CHECK(track.cues[0].text == "Grain is short again.");
    CHECK(track.cues[0].start == 0.0f);
    CHECK(track.cues[0].end == 4.0f);
}

TEST_CASE("a long line is split into chunks within the character limit", "[subtitle]")
{
    const SubtitleSettings settings;
    const std::string text = repeatWords(60);
    const SubtitleTrack track = buildSubtitleTrack("Mara", text, 20.0f, settings);
    REQUIRE(track.cues.size() > 3);
    for (const SubtitleCue& cue : track.cues)
        CHECK(cue.text.size() <= settings.maxCharsPerChunk);
    CHECK(shown(track) == stripSpaces(text));
}

TEST_CASE("cues run back to back with no gap or overlap", "[subtitle]")
{
    const SubtitleTrack track = buildSubtitleTrack("Mara", repeatWords(40), 15.0f);
    REQUIRE(track.cues.size() > 1);
    CHECK(track.cues.front().start == 0.0f);
    for (std::size_t i = 1; i < track.cues.size(); ++i)
        CHECK(track.cues[i].start == track.cues[i - 1].end);
}

TEST_CASE("a word longer than the limit is cut without losing characters", "[subtitle]")
{
    SubtitleSettings settings;
    settings.maxCharsPerChunk = 5;
    const SubtitleTrack track = buildSubtitleTrack("Mara", "abcdefghijklmnop qr", 2.0f, settings);
    for (const SubtitleCue& cue : track.cues)
        CHECK(cue.text.size() <= 5);
    CHECK(shown(track) == "abcdefghijklmnopqr");
}

TEST_CASE("multibyte text is never cut inside a code point", "[subtitle]")
{
    SubtitleSettings settings;
    settings.maxCharsPerChunk = 3;
    const std::string text = "\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9"; // five e-acute
    const SubtitleTrack track = buildSubtitleTrack("Mara", text, 2.0f, settings);
    REQUIRE(track.cues.size() == 2);
    CHECK(track.cues[0].text.size() == 6);
    CHECK(track.cues[1].text.size() == 4);
    CHECK(shown(track) == text);
}

TEST_CASE("the reading-speed rule stretches cues past a too-short voice line", "[subtitle]")
{
    const SubtitleSettings settings;
    const SubtitleTrack track = buildSubtitleTrack("Mara", repeatWords(20), 0.5f, settings);
    for (const SubtitleCue& cue : track.cues) {
        const float chars = static_cast<float>(cue.text.size());
        CHECK(cue.end - cue.start >= settings.minSecondsPerChar * chars - 1e-4f);
        CHECK(cue.end - cue.start >= settings.minChunkSeconds - 1e-4f);
    }
    CHECK(track.cues.back().end > 0.5f);
}

TEST_CASE("a slow voice line is not shortened by the reading-speed rule", "[subtitle]")
{
    const SubtitleTrack track = buildSubtitleTrack("Mara", repeatWords(30), 90.0f);
    CHECK(track.cues.back().end == 90.0f);
}

TEST_CASE("a NaN, negative or infinite voice length falls back to reading speed", "[subtitle]")
{
    for (float bad : {kNaN, -3.0f, std::numeric_limits<float>::infinity()}) {
        const SubtitleTrack track = buildSubtitleTrack("Mara", "Hello there friend.", bad);
        REQUIRE(track.cues.size() == 1);
        CHECK(std::isfinite(track.cues[0].end));
        CHECK(track.cues[0].end >= 1.0f);
    }
}

TEST_CASE("activeSubtitle returns the cue at a playback time", "[subtitle]")
{
    const SubtitleTrack track = buildSubtitleTrack("Mara", repeatWords(30), 12.0f);
    REQUIRE(track.cues.size() > 1);
    for (const SubtitleCue& cue : track.cues) {
        const SubtitleCue* active = activeSubtitle(track, (cue.start + cue.end) / 2);
        REQUIRE(active != nullptr);
        CHECK(active->text == cue.text);
    }
    CHECK(activeSubtitle(track, track.cues[1].start)->text == track.cues[1].text);
}

TEST_CASE("activeSubtitle is null before, after, NaN and negative times", "[subtitle]")
{
    SubtitleTrack track;
    track.cues = {{"late", 2.0f, 3.0f}};
    CHECK(activeSubtitle(track, 1.0f) == nullptr);
    CHECK(activeSubtitle(track, 3.0f) == nullptr);
    CHECK(activeSubtitle(track, 100.0f) == nullptr);
    CHECK(activeSubtitle(track, -1.0f) == nullptr);
    CHECK(activeSubtitle(track, kNaN) == nullptr);
    CHECK(activeSubtitle(track, std::numeric_limits<float>::infinity()) == nullptr);
    CHECK(activeSubtitle(track, 2.0f) != nullptr);
}

TEST_CASE("overlapping cues resolve to the one that started last", "[subtitle]")
{
    SubtitleTrack track;
    track.cues = {{"first", 0.0f, 4.0f}, {"second", 2.0f, 6.0f}};
    CHECK(activeSubtitle(track, 1.0f)->text == "first");
    CHECK(activeSubtitle(track, 3.0f)->text == "second");
    CHECK(activeSubtitle(track, 5.0f)->text == "second");
    track.cues = {{"second", 2.0f, 6.0f}, {"first", 0.0f, 4.0f}};
    CHECK(activeSubtitle(track, 3.0f)->text == "second");
}

TEST_CASE("every character of the source text is shown", "[subtitle]")
{
    const std::string text = "I kept the books for thirty winters, and not once did the granary come up short "
                             "until the Satrapy sent its own men to count it. Now, honestly? I cannot say whose "
                             "grain it is: supercalifragilisticexpialidocious-and-then-some, truly.";
    for (std::size_t limit : {1u, 4u, 10u, 42u, 500u}) {
        SubtitleSettings settings;
        settings.maxCharsPerChunk = limit;
        const SubtitleTrack track = buildSubtitleTrack("Mara", text, 30.0f, settings);
        CHECK(shown(track) == stripSpaces(text));
    }
}
