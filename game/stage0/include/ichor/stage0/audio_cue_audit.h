#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace ichor::stage0 {

// Objective audit of the three wind-up cue WAVs (SEED-432). Nobody can listen in CI, so this measures
// what a listener would hear: level, clipping, length, where the energy sits and how fast it starts,
// then scores how far apart two cues are. Integer maths and Goertzel sums only, with the cosine from
// dmath, so it agrees on every platform; nothing here is in the simulation or its hash.

constexpr int kAuditBands = 16;
// Band centres in Hz, log spaced (about x1.36 apart) from the bottom of the role pitch bands to 8 kHz.
constexpr std::array<int, kAuditBands> kAuditBandHz = {80, 110, 150, 205, 280, 380, 520, 710, 970, 1320, 1800, 2450, 3350, 4550, 6200, 8450};

struct WavMono16 {
    std::uint32_t sampleRate = 0;
    std::vector<std::int16_t> samples;
};

// Accepts only 16-bit PCM mono RIFF/WAVE with a sane rate and at most kMaxAuditFrames frames. Hostile
// bytes (truncated, bad chunk sizes, wrong format) yield false and an empty out; never throws or over-reads.
constexpr std::size_t kMaxAuditFrames = std::size_t(1) << 22;
bool parseWavMono16(std::span<const std::uint8_t> bytes, WavMono16& out);

struct CueStats {
    std::uint32_t sampleRate = 0;
    std::uint32_t frames = 0;
    std::uint32_t durationMs = 0;
    int peak = 0;              // 0..32768
    std::uint32_t clipped = 0; // samples at or beyond +-32767
    int rms = 0;               // 0..32768
    int peakPermille = 0;      // peak as a share of full scale
    int onsetUs = 0;           // time to the first sample above a tenth of the peak
    int attackUs = 0;          // time to the first sample at 90% of the peak
    std::array<int, kAuditBands> bandPermille{}; // share of Goertzel energy per band, sums to about 1000
    int dominantBand = 0;
    int centroidHz = 0; // energy-weighted mean of the band centres
};

// Empty or silent clips give zeroed level fields and dominantBand 0.
CueStats analyzeCue(const WavMono16& wav);

struct CueDistance {
    int spectralPermille = 0; // half the L1 distance of the band shares: 0 identical .. 1000 disjoint
    int centroidPermille = 0; // centroid difference over the higher centroid
    int durationPermille = 0; // length difference over the longer
    int onsetDiffUs = 0;
    int score = 0; // the largest of the three permille figures
};
CueDistance cueDistance(const CueStats& a, const CueStats& b);

// Floors for the shipped cues. A pair under kMinDistinctScore is one a listener would struggle to tell apart.
constexpr int kMinDistinctScore = 300;
constexpr int kMaxPeakPermille = 990; // headroom: the mixer sums voices
constexpr std::uint32_t kMaxClippedSamples = 0;
constexpr int kMinPeakPermille = 100; // quiet cues vanish under footsteps
constexpr int kMinCueDurationMs = 100;

enum class CueAuditIssue : std::uint8_t { Unreadable, Silent, TooQuiet, Clipped, TooShort, Indistinct };
const char* name(CueAuditIssue issue);

struct CueAuditProblem {
    CueAuditIssue issue;
    std::string cue;
    std::string other; // second cue for Indistinct
};

struct NamedCue {
    std::string name;
    CueStats stats;
    bool readable = true;
};

// Empty result means every cue is readable, loud enough, unclipped, long enough and every pair distinct.
std::vector<CueAuditProblem> auditCues(std::span<const NamedCue> cues);

} // namespace ichor::stage0
