#include "ichor/core/file_io.h"
#include "ichor/core/log.h"
#include "ichor/world/seed_harness.h"

#include <charconv>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace {

template <class T>
bool parseNumber(std::string_view text, T& out)
{
    const auto r = std::from_chars(text.data(), text.data() + text.size(), out);
    return r.ec == std::errc{} && r.ptr == text.data() + text.size();
}

}

// ichor_seed_harness <count> [--first <seed>] [--size <m>] [--sample <m>] [--cell <m>] [--water <m>] [--river-threshold <n>] [--rerolls <n>] [--json] [--timing] [--out <file>]
// Forges `count` consecutive seeds through the validation gates and prints pass/fail stats and failure reasons. The report is deterministic unless --timing is given.
// Exit code: 0 when every seed passed (possibly after rerolls), 1 when some seed failed every attempt, 2 for bad arguments.
int main(int argc, char** argv)
{
    ichor::HarnessConfig config;
    config.gates = ichor::defaultGates();
    config.request.desc.sizeMetres = 1024;
    config.request.desc.metresPerSample = 16;
    config.request.desc.cellMetres = 256;
    config.request.desc.waterHeight = 20.0f;
    config.request.carveRivers = true;
    config.request.rivers.riverThreshold = 40;
    bool json = false;
    bool timing = false;
    std::filesystem::path outPath;
    bool haveCount = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        auto value = [&](auto& target) { return i + 1 < argc && parseNumber(argv[++i], target); };
        bool ok = true;
        if (arg == "--json") {
            json = true;
        } else if (arg == "--timing") {
            timing = true;
        } else if (arg == "--first") {
            ok = value(config.firstSeed);
        } else if (arg == "--size") {
            ok = value(config.request.desc.sizeMetres);
        } else if (arg == "--sample") {
            ok = value(config.request.desc.metresPerSample);
        } else if (arg == "--cell") {
            ok = value(config.request.desc.cellMetres);
        } else if (arg == "--rerolls") {
            ok = value(config.maxRerolls);
        } else if (arg == "--water") {
            std::uint32_t water = 0;
            ok = value(water);
            config.request.desc.waterHeight = static_cast<float>(water);
        } else if (arg == "--river-threshold") {
            std::uint32_t threshold = 0;
            ok = value(threshold);
            config.request.carveRivers = threshold > 0;
            if (threshold > 0) config.request.rivers.riverThreshold = threshold;
        } else if (arg == "--out") {
            ok = i + 1 < argc && outPath.empty();
            if (ok) outPath = argv[++i];
        } else if (!haveCount && !arg.starts_with("--")) {
            ok = parseNumber(arg, config.count);
            haveCount = true;
        } else {
            ok = false;
        }
        if (!ok) {
            ichor::logf(ichor::LogLevel::Error, "bad argument near '{}'", arg);
            return 2;
        }
    }
    if (!haveCount) {
        ichor::log(ichor::LogLevel::Info, "usage: ichor_seed_harness <count> [--first <seed>] [--size <m>] [--sample <m>] [--cell <m>] [--water <m>] [--river-threshold <n>, 0 for no rivers] [--rerolls <n>] [--json] [--timing] [--out <file>]");
        return 2;
    }
    try {
        const ichor::HarnessReport report = ichor::runHarness(config);
        const std::string text = json ? ichor::harnessJson(report, timing) : ichor::harnessText(report, timing);
        if (outPath.empty()) {
            std::fputs(text.c_str(), stdout);
        } else if (!ichor::writeFileAtomically(outPath, std::vector<std::uint8_t>(text.begin(), text.end()))) {
            ichor::logf(ichor::LogLevel::Error, "cannot write {}", outPath.string());
            return 2;
        }
        return report.failedAllAttempts == 0 ? 0 : 1;
    } catch (const std::exception& e) {
        ichor::logf(ichor::LogLevel::Error, "{}", e.what());
        return 2;
    }
}
