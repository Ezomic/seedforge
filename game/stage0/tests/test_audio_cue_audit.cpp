#include "ichor/stage0/audio_cue_audit.h"

#include "ichor/audio/mixer.h"
#include "ichor/audio/oneshot_scheduler.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace ichor;
using namespace ichor::stage0;

namespace {

const char* const kCueNames[] = {"A_WindupLight", "A_WindupHeavy", "A_Tell"};

std::vector<std::uint8_t> readFile(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

struct Shipped {
    std::vector<NamedCue> cues;
    std::vector<WavMono16> wavs;
};

const Shipped& shipped()
{
    static const Shipped s = [] {
        Shipped out;
        for (const char* n : kCueNames) {
            const auto bytes = readFile(std::string(ICHOR_REPO_DATA) + "/../art/audio/" + n + ".wav");
            WavMono16 w;
            NamedCue c;
            c.name = n;
            c.readable = parseWavMono16(bytes, w);
            if (c.readable) c.stats = analyzeCue(w);
            out.cues.push_back(c);
            out.wavs.push_back(std::move(w));
        }
        return out;
    }();
    return s;
}

WavMono16 tone(int hz, int ms, int amp, std::uint32_t rate = 44100)
{
    WavMono16 w;
    w.sampleRate = rate;
    const std::size_t n = std::size_t(rate) * ms / 1000;
    for (std::size_t i = 0; i < n; ++i)
        w.samples.push_back(static_cast<std::int16_t>(std::lround(amp * std::sin(6.283185307 * hz * double(i) / rate))));
    return w;
}

bool has(const std::vector<CueAuditProblem>& p, CueAuditIssue issue)
{
    return std::any_of(p.begin(), p.end(), [&](const CueAuditProblem& x) { return x.issue == issue; });
}

NamedCue named(const char* n, const WavMono16& w)
{
    return NamedCue{n, analyzeCue(w), true};
}

void le(std::vector<std::uint8_t>& v, std::uint32_t x, int bytes)
{
    for (int i = 0; i < bytes; ++i) v.push_back(static_cast<std::uint8_t>(x >> (8 * i)));
}

std::vector<std::uint8_t> wavBytes(std::uint16_t channels, std::uint16_t bits, std::uint32_t dataBytes, std::uint32_t declared)
{
    std::vector<std::uint8_t> v = {'R', 'I', 'F', 'F'};
    le(v, 36 + dataBytes, 4);
    for (char c : std::string("WAVEfmt ")) v.push_back(static_cast<std::uint8_t>(c));
    le(v, 16, 4);
    le(v, 1, 2);
    le(v, channels, 2);
    le(v, 44100, 4);
    le(v, 44100 * 2, 4);
    le(v, 2, 2);
    le(v, bits, 2);
    for (char c : std::string("data")) v.push_back(static_cast<std::uint8_t>(c));
    le(v, declared, 4);
    for (std::uint32_t i = 0; i < dataBytes; ++i) v.push_back(static_cast<std::uint8_t>(i));
    return v;
}

} // namespace

TEST_CASE("audit report: numbers for the PR", "[.audio_cue_report]")
{
    for (const NamedCue& c : shipped().cues) {
        const CueStats& s = c.stats;
        std::printf("%s: %u ms, peak %d (%d permille), clipped %u, rms %d, onset %d us, attack %d us, dominant %d Hz, centroid %d Hz\n",
            c.name.c_str(), s.durationMs, s.peak, s.peakPermille, s.clipped, s.rms, s.onsetUs, s.attackUs, kAuditBandHz[s.dominantBand], s.centroidHz);
    }
    for (std::size_t i = 0; i < shipped().cues.size(); ++i)
        for (std::size_t j = i + 1; j < shipped().cues.size(); ++j) {
            const CueDistance d = cueDistance(shipped().cues[i].stats, shipped().cues[j].stats);
            std::printf("%s vs %s: spectral %d, centroid %d, duration %d, onset diff %d us, score %d\n", shipped().cues[i].name.c_str(),
                shipped().cues[j].name.c_str(), d.spectralPermille, d.centroidPermille, d.durationPermille, d.onsetDiffUs, d.score);
        }
}

TEST_CASE("the shipped cues are readable, loud, unclipped and mutually distinct", "[audio_cue_audit]")
{
    REQUIRE(shipped().cues.size() == 3);
    for (const NamedCue& c : shipped().cues) REQUIRE(c.readable);
    CHECK(auditCues(shipped().cues).empty());
}

TEST_CASE("cooked cue stats are deterministic", "[audio_cue_audit]")
{
    const CueStats a = analyzeCue(shipped().wavs[0]);
    const CueStats b = analyzeCue(shipped().wavs[0]);
    CHECK(a.bandPermille == b.bandPermille);
    CHECK(a.rms == b.rms);
    CHECK(a.centroidHz == b.centroidHz);
}

TEST_CASE("analysis recovers the level, length and band of a known tone", "[audio_cue_audit]")
{
    const CueStats s = analyzeCue(tone(520, 500, 16384));
    CHECK(s.durationMs == 500);
    CHECK(s.peak >= 16380);
    CHECK(s.peak <= 16384);
    CHECK(s.rms >= 11500);
    CHECK(s.rms <= 11700); // 16384 / sqrt(2)
    CHECK(s.clipped == 0);
    CHECK(kAuditBandHz[s.dominantBand] == 520);
}

TEST_CASE("two identical cues go red as indistinct", "[audio_cue_audit]")
{
    std::vector<NamedCue> cues = shipped().cues;
    cues[1].stats = cues[0].stats;
    const auto p = auditCues(cues);
    REQUIRE(has(p, CueAuditIssue::Indistinct));
    CHECK(p.front().cue == cues[0].name);
    CHECK(p.front().other == cues[1].name);
}

TEST_CASE("a cue that is the same tone at another level is still indistinct", "[audio_cue_audit]")
{
    CHECK(cueDistance(analyzeCue(tone(520, 400, 12000)), analyzeCue(tone(520, 400, 20000))).score < kMinDistinctScore);
    CHECK(cueDistance(analyzeCue(tone(520, 400, 12000)), analyzeCue(tone(2450, 400, 12000))).score >= kMinDistinctScore);
}

TEST_CASE("replacing a shipped cue with a copy of another goes red for each pair", "[audio_cue_audit]")
{
    for (std::size_t a = 0; a < 3; ++a)
        for (std::size_t b = 0; b < 3; ++b) {
            if (a == b) continue;
            std::vector<NamedCue> cues = shipped().cues;
            cues[b].stats = cues[a].stats;
            CHECK(has(auditCues(cues), CueAuditIssue::Indistinct));
        }
}

TEST_CASE("a clipped or hot cue goes red", "[audio_cue_audit]")
{
    std::vector<NamedCue> cues = shipped().cues;
    WavMono16 w = shipped().wavs[0];
    w.samples[w.samples.size() / 2] = 32767;
    cues[0].stats = analyzeCue(w);
    CHECK(has(auditCues(cues), CueAuditIssue::Clipped));

    cues = shipped().cues;
    cues[2].stats = analyzeCue(tone(380, 300, 32767));
    CHECK(has(auditCues(cues), CueAuditIssue::Clipped));

    w = shipped().wavs[1];
    w.samples[10] = -32768;
    cues = shipped().cues;
    cues[1].stats = analyzeCue(w);
    CHECK(cues[1].stats.clipped >= 1);
    CHECK(has(auditCues(cues), CueAuditIssue::Clipped));
}

TEST_CASE("silent, quiet, short and unreadable cues go red", "[audio_cue_audit]")
{
    std::vector<NamedCue> cues = shipped().cues;
    cues[0].stats = analyzeCue(tone(520, 300, 0));
    CHECK(has(auditCues(cues), CueAuditIssue::Silent));

    cues = shipped().cues;
    cues[0].stats = analyzeCue(tone(520, 300, 1000));
    CHECK(has(auditCues(cues), CueAuditIssue::TooQuiet));

    cues = shipped().cues;
    cues[0].stats = analyzeCue(tone(520, 40, 12000));
    CHECK(has(auditCues(cues), CueAuditIssue::TooShort));

    cues = shipped().cues;
    cues[2].readable = false;
    CHECK(has(auditCues(cues), CueAuditIssue::Unreadable));
}

TEST_CASE("empty and degenerate clips analyse without crashing", "[audio_cue_audit]")
{
    CHECK(analyzeCue(WavMono16{}).peak == 0);
    WavMono16 one;
    one.sampleRate = 44100;
    one.samples = {32767};
    const CueStats s = analyzeCue(one);
    CHECK(s.clipped == 1);
    WavMono16 low;
    low.sampleRate = 8000;
    low.samples.assign(800, 1000);
    CHECK(analyzeCue(low).durationMs == 100);
    CHECK(auditCues({}).empty());
}

TEST_CASE("hostile WAV bytes are rejected", "[audio_cue_audit]")
{
    WavMono16 w;
    CHECK_FALSE(parseWavMono16({}, w));
    CHECK_FALSE(parseWavMono16(wavBytes(2, 16, 64, 64), w));
    CHECK_FALSE(parseWavMono16(wavBytes(1, 8, 64, 64), w));
    CHECK_FALSE(parseWavMono16(wavBytes(1, 16, 64, 0xffffffffu), w));
    CHECK(w.samples.empty());
    auto trunc = wavBytes(1, 16, 64, 64);
    trunc.resize(30);
    CHECK_FALSE(parseWavMono16(trunc, w));
    auto big = wavBytes(1, 16, 64, 64);
    big[0] = 'X';
    CHECK_FALSE(parseWavMono16(big, w));
    CHECK(parseWavMono16(wavBytes(1, 16, 64, 64), w));
    CHECK(w.samples.size() == 32);
}

TEST_CASE("a foe behind the listener still gets an audible level and a stereo image", "[audio_cue_audit]")
{
    OneShotListener l;
    l.forwardX = 0.0;
    l.forwardZ = -1.0;

    SECTION("scheduler math: no front/back muting, side offset pans")
    {
        const double radius = 30.0;
        const float front = OneShotScheduler::gainAt(8.0, radius);
        CHECK(front > 0.0f);
        CHECK(OneShotScheduler::gainAt(std::hypot(0.0, 8.0), radius) == front);
        CHECK(OneShotScheduler::panFor(l, 4.0, 8.0) > 0.3f);  // behind right
        CHECK(OneShotScheduler::panFor(l, -4.0, 8.0) < -0.3f); // behind left
        CHECK(OneShotScheduler::panFor(l, 4.0, 8.0) == OneShotScheduler::panFor(l, 4.0, -8.0)); // mirrors the front
    }

    SECTION("mixer render with the real windup cues")
    {
        for (std::size_t c = 0; c < 3; ++c) {
            struct Case {
                double x, z;
            };
            for (const Case pos : {Case{0.0, 6.0}, Case{3.0, 6.0}, Case{-3.0, 6.0}, Case{0.0, -6.0}}) {
                Mixer mixer;
                SceneBundle bundle;
                bundle.sounds.push_back(SoundClip{shipped().cues[c].name, shipped().wavs[c].sampleRate, shipped().wavs[c].samples});
                mixer.addBundle(bundle);
                mixer.setListener({0, 0, 0}, {0, 0, -1}, {1, 0, 0});
                mixer.play(mixer.findClip(shipped().cues[c].name), {pos.x, 0.0, pos.z}, 1.0f, true);
                std::vector<float> out(2 * 4096);
                mixer.mix(out.data(), 4096);
                float pl = 0.0f;
                float pr = 0.0f;
                for (std::size_t i = 0; i < 4096; ++i) {
                    pl = std::max(pl, std::abs(out[i * 2]));
                    pr = std::max(pr, std::abs(out[i * 2 + 1]));
                }
                INFO(shipped().cues[c].name << " at " << pos.x << "," << pos.z);
                CHECK(pl > 0.01f);
                CHECK(pr > 0.01f);
                if (pos.x > 0.0) CHECK(pr > pl);
                if (pos.x < 0.0) CHECK(pl > pr);
                if (pos.x == 0.0) CHECK(std::abs(pl - pr) < 0.001f);
            }
        }
    }

    SECTION("directly behind is centred, which is the known front/back ambiguity")
    {
        CHECK(OneShotScheduler::panFor(l, 0.0, 8.0) == 0.0f);
    }
}
