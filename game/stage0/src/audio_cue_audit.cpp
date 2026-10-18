#include "ichor/stage0/audio_cue_audit.h"

#include "ichor/core/dmath.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace ichor::stage0 {

namespace {

std::uint32_t le32(const std::uint8_t* p)
{
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}

std::uint16_t le16(const std::uint8_t* p)
{
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

std::uint64_t isqrt(std::uint64_t v)
{
    std::uint64_t r = 0;
    std::uint64_t bit = std::uint64_t(1) << 62;
    while (bit > v) bit >>= 2;
    while (bit != 0) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return r;
}

// Goertzel power at `hz`: doubles with only + - * /, coefficient from dmath.
double goertzel(const std::vector<std::int16_t>& x, std::uint32_t rate, int hz)
{
    const float w = dmath::kTwoPi * static_cast<float>(hz) / static_cast<float>(rate);
    const double c = 2.0 * static_cast<double>(dmath::cos(w));
    double s1 = 0.0;
    double s2 = 0.0;
    for (const std::int16_t v : x) {
        const double s = static_cast<double>(v) + c * s1 - s2;
        s2 = s1;
        s1 = s;
    }
    return s1 * s1 + s2 * s2 - c * s1 * s2;
}

int permilleDiff(std::uint32_t a, std::uint32_t b)
{
    const std::uint32_t hi = std::max(a, b);
    if (hi == 0) return 0;
    return static_cast<int>((static_cast<std::uint64_t>(hi - std::min(a, b)) * 1000u) / hi);
}

} // namespace

bool parseWavMono16(std::span<const std::uint8_t> b, WavMono16& out)
{
    out = {};
    if (b.size() < 12 || std::memcmp(b.data(), "RIFF", 4) != 0 || std::memcmp(b.data() + 8, "WAVE", 4) != 0) return false;
    bool haveFmt = false;
    std::size_t pos = 12;
    while (pos + 8 <= b.size()) {
        const std::uint32_t size = le32(b.data() + pos + 4);
        const std::size_t body = pos + 8;
        if (size > b.size() - body) return false;
        if (std::memcmp(b.data() + pos, "fmt ", 4) == 0) {
            if (size < 16) return false;
            const std::uint16_t fmt = le16(b.data() + body);
            const std::uint16_t ch = le16(b.data() + body + 2);
            const std::uint32_t rate = le32(b.data() + body + 4);
            const std::uint16_t bits = le16(b.data() + body + 14);
            if (fmt != 1 || ch != 1 || bits != 16 || rate < 8000 || rate > 192000) return false;
            out.sampleRate = rate;
            haveFmt = true;
        } else if (std::memcmp(b.data() + pos, "data", 4) == 0) {
            if (!haveFmt) return false;
            const std::size_t n = size / 2;
            if (n > kMaxAuditFrames) {
                out = {};
                return false;
            }
            out.samples.resize(n);
            for (std::size_t i = 0; i < n; ++i) out.samples[i] = static_cast<std::int16_t>(le16(b.data() + body + i * 2));
            return true;
        }
        pos = body + size + (size & 1u);
    }
    out = {};
    return false;
}

CueStats analyzeCue(const WavMono16& wav)
{
    CueStats s;
    s.sampleRate = wav.sampleRate;
    s.frames = static_cast<std::uint32_t>(wav.samples.size());
    if (wav.sampleRate == 0 || wav.samples.empty()) return s;
    s.durationMs = static_cast<std::uint32_t>((static_cast<std::uint64_t>(s.frames) * 1000u) / wav.sampleRate);

    std::uint64_t sumSq = 0;
    for (const std::int16_t v : wav.samples) {
        const int a = std::abs(static_cast<int>(v));
        s.peak = std::max(s.peak, a);
        if (a >= 32767) ++s.clipped;
        sumSq += static_cast<std::uint64_t>(a) * static_cast<std::uint64_t>(a);
    }
    s.rms = static_cast<int>(isqrt(sumSq / wav.samples.size()));
    s.peakPermille = s.peak * 1000 / 32768;
    if (s.peak == 0) return s;

    const int onsetLevel = std::max(1, s.peak / 10);
    const int attackLevel = std::max(1, s.peak * 9 / 10);
    bool gotOnset = false;
    for (std::size_t i = 0; i < wav.samples.size(); ++i) {
        const int a = std::abs(static_cast<int>(wav.samples[i]));
        const int us = static_cast<int>((static_cast<std::uint64_t>(i) * 1000000u) / wav.sampleRate);
        if (!gotOnset && a >= onsetLevel) {
            s.onsetUs = us;
            gotOnset = true;
        }
        if (a >= attackLevel) {
            s.attackUs = us;
            break;
        }
    }

    std::array<double, kAuditBands> e{};
    double total = 0.0;
    for (int k = 0; k < kAuditBands; ++k) {
        if (kAuditBandHz[k] * 2 >= static_cast<int>(wav.sampleRate)) continue;
        e[k] = std::max(goertzel(wav.samples, wav.sampleRate, kAuditBandHz[k]), 0.0);
        total += e[k];
    }
    if (!(total > 0.0)) return s;
    std::int64_t weighted = 0;
    int shareSum = 0;
    for (int k = 0; k < kAuditBands; ++k) {
        s.bandPermille[k] = static_cast<int>(e[k] / total * 1000.0);
        shareSum += s.bandPermille[k];
        weighted += static_cast<std::int64_t>(s.bandPermille[k]) * kAuditBandHz[k];
        if (e[k] > e[s.dominantBand]) s.dominantBand = k;
    }
    s.centroidHz = shareSum > 0 ? static_cast<int>(weighted / shareSum) : 0;
    return s;
}

CueDistance cueDistance(const CueStats& a, const CueStats& b)
{
    CueDistance d;
    int l1 = 0;
    for (int k = 0; k < kAuditBands; ++k) l1 += std::abs(a.bandPermille[k] - b.bandPermille[k]);
    d.spectralPermille = l1 / 2;
    d.centroidPermille = permilleDiff(static_cast<std::uint32_t>(a.centroidHz), static_cast<std::uint32_t>(b.centroidHz));
    d.durationPermille = permilleDiff(a.durationMs, b.durationMs);
    d.onsetDiffUs = std::abs(a.onsetUs - b.onsetUs);
    d.score = std::max({d.spectralPermille, d.centroidPermille, d.durationPermille});
    return d;
}

const char* name(CueAuditIssue issue)
{
    switch (issue) {
    case CueAuditIssue::Unreadable: return "Unreadable";
    case CueAuditIssue::Silent: return "Silent";
    case CueAuditIssue::TooQuiet: return "TooQuiet";
    case CueAuditIssue::Clipped: return "Clipped";
    case CueAuditIssue::TooShort: return "TooShort";
    case CueAuditIssue::Indistinct: return "Indistinct";
    }
    return "?";
}

std::vector<CueAuditProblem> auditCues(std::span<const NamedCue> cues)
{
    std::vector<CueAuditProblem> out;
    for (const NamedCue& c : cues) {
        if (!c.readable) {
            out.push_back({CueAuditIssue::Unreadable, c.name, {}});
            continue;
        }
        if (c.stats.peak == 0) {
            out.push_back({CueAuditIssue::Silent, c.name, {}});
            continue;
        }
        if (c.stats.peakPermille < kMinPeakPermille) out.push_back({CueAuditIssue::TooQuiet, c.name, {}});
        if (c.stats.clipped > kMaxClippedSamples || c.stats.peakPermille > kMaxPeakPermille) out.push_back({CueAuditIssue::Clipped, c.name, {}});
        if (c.stats.durationMs < static_cast<std::uint32_t>(kMinCueDurationMs)) out.push_back({CueAuditIssue::TooShort, c.name, {}});
    }
    for (std::size_t i = 0; i < cues.size(); ++i)
        for (std::size_t j = i + 1; j < cues.size(); ++j) {
            if (!cues[i].readable || !cues[j].readable) continue;
            if (cueDistance(cues[i].stats, cues[j].stats).score < kMinDistinctScore)
                out.push_back({CueAuditIssue::Indistinct, cues[i].name, cues[j].name});
        }
    return out;
}

} // namespace ichor::stage0
