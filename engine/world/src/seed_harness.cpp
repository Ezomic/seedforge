#include "ichor/world/seed_harness.h"

#include <algorithm>
#include <chrono>
#include <format>
#include <stdexcept>

namespace ichor {

namespace {

constexpr std::uint32_t kHarnessRerollCeiling = 64; // forgeValidated clamps to the same number

GateStats& statsFor(std::vector<GateStats>& stats, const std::string& gate)
{
    for (GateStats& s : stats) {
        if (s.gate == gate) return s;
    }
    stats.push_back({gate});
    return stats.back();
}

void addSample(GateStats& stats, const std::string& reason)
{
    if (stats.sampleReasons.size() < kMaxSampleReasons && std::ranges::find(stats.sampleReasons, reason) == stats.sampleReasons.end()) {
        stats.sampleReasons.push_back(reason);
    }
}

std::string jsonString(const std::string& text)
{
    std::string out = "\"";
    for (const char c : text) {
        const auto u = static_cast<unsigned char>(c);
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (u < 0x20) out += std::format("\\u{:04x}", u);
        else out += c;
    }
    return out + "\"";
}

std::string printable(const std::string& text)
{
    std::string out;
    for (const char c : text) {
        const auto u = static_cast<unsigned char>(c);
        out += (u < 0x20 || u == 0x7f) ? '?' : c;
    }
    return out;
}

std::string failuresJson(const std::vector<GateFailure>& failures)
{
    std::string out = "[";
    for (std::size_t i = 0; i < failures.size(); ++i) {
        out += std::format("{}{{\"gate\":{},\"reason\":{}}}", i ? "," : "", jsonString(failures[i].gate), jsonString(failures[i].reason));
    }
    return out + "]";
}

std::uint64_t totalMicros(const HarnessReport& report)
{
    std::uint64_t total = 0;
    for (const SeedOutcome& s : report.seeds) total += s.micros;
    return total;
}

}

HarnessReport runHarness(const HarnessConfig& config)
{
    if (config.count == 0 || config.count > kMaxHarnessSeeds) throw std::invalid_argument("seed count out of range");
    if (!config.request.desc.valid()) throw std::invalid_argument("the world description is not valid");
    for (std::size_t i = 0; i < config.gates.size(); ++i) {
        for (std::size_t j = 0; j < i; ++j) {
            if (config.gates[i].name == config.gates[j].name) throw std::invalid_argument("gate names must be distinct");
        }
    }

    HarnessReport report;
    report.firstSeed = config.firstSeed;
    report.count = config.count;
    report.maxRerolls = std::min(config.maxRerolls, kHarnessRerollCeiling);
    for (const Gate& gate : config.gates) report.gates.push_back({gate.name});
    report.seeds.reserve(config.count);

    for (std::uint32_t i = 0; i < config.count; ++i) {
        ForgeRequest request = config.request;
        request.desc.seed = config.firstSeed + i;
        const auto start = std::chrono::steady_clock::now();
        const ValidatedWorld validated = forgeValidated(request, config.gates, report.maxRerolls);
        const auto elapsed = std::chrono::steady_clock::now() - start;

        SeedOutcome outcome;
        outcome.seed = request.desc.seed;
        outcome.passed = validated.passed;
        outcome.attempt = validated.passed || validated.rejected.empty() ? validated.attempt : validated.rejected.back().attempt;
        outcome.micros = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count());
        if (!validated.rejected.empty()) outcome.firstAttemptFailures = validated.rejected.front().failures;

        if (!validated.passed) ++report.failedAllAttempts;
        else if (validated.attempt == 0) ++report.passedFirstAttempt;
        else ++report.passedAfterReroll;

        for (const GateFailure& failure : outcome.firstAttemptFailures) {
            ++statsFor(report.gates, failure.gate).firstAttemptFailures;
        }
        for (const AttemptReport& attempt : validated.rejected) {
            for (const GateFailure& failure : attempt.failures) {
                GateStats& stats = statsFor(report.gates, failure.gate);
                ++stats.attemptFailures;
                addSample(stats, failure.reason);
            }
        }
        if (!validated.passed) {
            for (GateStats& stats : report.gates) {
                const bool everyAttempt = std::ranges::all_of(validated.rejected, [&](const AttemptReport& a) {
                    return std::ranges::any_of(a.failures, [&](const GateFailure& f) { return f.gate == stats.gate; });
                });
                if (everyAttempt) ++stats.blockedSeeds;
            }
        }
        report.seeds.push_back(std::move(outcome));
    }
    return report;
}

std::string harnessText(const HarnessReport& report, bool withTiming)
{
    std::string out = std::format("first seed {}, {} seeds, up to {} rerolls\n", report.firstSeed, report.count, report.maxRerolls);
    out += std::format("passed first attempt: {}\npassed after reroll: {}\nfailed every attempt: {}\n", report.passedFirstAttempt, report.passedAfterReroll, report.failedAllAttempts);
    out += "gates (seeds failing at attempt 0 / attempts failing / seeds blocked on every attempt):\n";
    for (const GateStats& g : report.gates) {
        out += std::format("  {}: {} / {} / {}\n", printable(g.gate), g.firstAttemptFailures, g.attemptFailures, g.blockedSeeds);
        for (const std::string& reason : g.sampleReasons) out += std::format("    e.g. {}\n", printable(reason));
    }
    std::string failed;
    for (const SeedOutcome& s : report.seeds) {
        if (!s.passed) failed += std::format(" {}", s.seed);
    }
    out += failed.empty() ? "failing seeds: none\n" : std::format("failing seeds:{}\n", failed);
    if (withTiming) {
        const std::uint64_t total = totalMicros(report);
        std::uint64_t slowest = 0;
        for (const SeedOutcome& s : report.seeds) slowest = std::max(slowest, s.micros);
        out += std::format("timing: total {} us, mean {} us, slowest {} us\n", total, total / report.count, slowest);
    }
    return out;
}

std::string harnessJson(const HarnessReport& report, bool withTiming)
{
    std::string out = std::format("{{\"firstSeed\":{},\"count\":{},\"maxRerolls\":{},\"passedFirstAttempt\":{},\"passedAfterReroll\":{},\"failedAllAttempts\":{},\"gates\":[", report.firstSeed,
        report.count, report.maxRerolls, report.passedFirstAttempt, report.passedAfterReroll, report.failedAllAttempts);
    for (std::size_t i = 0; i < report.gates.size(); ++i) {
        const GateStats& g = report.gates[i];
        out += std::format("{}{{\"gate\":{},\"firstAttemptFailures\":{},\"attemptFailures\":{},\"blockedSeeds\":{},\"sampleReasons\":[", i ? "," : "", jsonString(g.gate), g.firstAttemptFailures,
            g.attemptFailures, g.blockedSeeds);
        for (std::size_t r = 0; r < g.sampleReasons.size(); ++r) out += std::format("{}{}", r ? "," : "", jsonString(g.sampleReasons[r]));
        out += "]}";
    }
    out += "],\"seeds\":[";
    for (std::size_t i = 0; i < report.seeds.size(); ++i) {
        const SeedOutcome& s = report.seeds[i];
        out += std::format("{}{{\"seed\":{},\"passed\":{},\"attempt\":{},\"firstAttemptFailures\":{}", i ? "," : "", s.seed, s.passed ? "true" : "false", s.attempt, failuresJson(s.firstAttemptFailures));
        if (withTiming) out += std::format(",\"micros\":{}", s.micros);
        out += "}";
    }
    out += "]";
    if (withTiming) out += std::format(",\"totalMicros\":{}", totalMicros(report));
    return out + "}\n";
}

}
