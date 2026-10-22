#include "forged_walk_support.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <string>
#include <vector>

using namespace ichor;
using namespace ichor::walksupport;

namespace {

constexpr std::uint64_t kFirstSeed = 1;
constexpr std::uint64_t kLastSeed = 8;

std::vector<int> gridsFromEnv(const char* text)
{
    std::vector<int> grids;
    std::string token;
    for (const char* c = text;; ++c) {
        if (*c >= '0' && *c <= '9' && token.size() < 6) {
            token.push_back(*c);
            continue;
        }
        if (!token.empty()) {
            grids.push_back(std::atoi(token.c_str()));
            token.clear();
        }
        if (*c == '\0') {
            break;
        }
    }
    return grids;
}

double budgetFromEnv()
{
    const char* text = std::getenv("ICHOR_NIGHTLY_WALK_BUDGET_S");
    const double value = text != nullptr ? std::atof(text) : 0.0;
    return value > 0.0 ? value : 1200.0;
}

void say(const std::string& line)
{
    std::fputs(line.c_str(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

const char* outcomeName(WalkOutcome o)
{
    return o == WalkOutcome::Arrived ? "arrived" : o == WalkOutcome::Blocked ? "blocked" : "TIMED OUT";
}

}

// Nightly only (SEED-501): the SEED-492 walk bot over forged worlds far bigger than the PR suite's 129 grid. Skipped
// unless ICHOR_NIGHTLY_WALK_GRIDS names the grids ("256,1024"), because a 1024 forge takes minutes under the sanitizers.
// It does not demand arrival: a blocked walk with a named blocker is an honest answer. It fails on a walk that is
// silently stuck: timed out while still moving, stopped without a blocker and report, or never left the spawn.
TEST_CASE("nightly: the walk bot on big forged worlds arrives or names what stopped it", "[walking][nightly]")
{
    const char* gridsText = std::getenv("ICHOR_NIGHTLY_WALK_GRIDS");
    if (gridsText == nullptr || gridsText[0] == '\0') {
        SKIP("set ICHOR_NIGHTLY_WALK_GRIDS (for example 256,1024) to run the nightly walk");
    }
    const std::vector<int> grids = gridsFromEnv(gridsText);
    REQUIRE_FALSE(grids.empty());
    const double budget = budgetFromEnv();
    const auto start = std::chrono::steady_clock::now();
    const auto elapsed = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(); };

    int problems = 0;
    for (const int grid : grids) {
        int walked = 0;
        int arrived = 0;
        int blocked = 0;
        for (std::uint64_t seed = kFirstSeed; seed <= kLastSeed; ++seed) {
            if (elapsed() > budget) {
                say(std::format("WALK grid {} seed {}: SILENT not run, the {:.0f} s budget is spent", grid, seed, budget));
                ++problems;
                continue;
            }
            const auto t0 = std::chrono::steady_clock::now();
            const SeedWalk walk = walkSeed(seed, grid);
            const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            if (!walk.hadSettlement) {
                say(std::format("WALK grid {} seed {}: no trip (no second settlement {:.0f} m or more away), wall {:.1f} s", grid, seed, kMinTrip, wall));
                continue;
            }
            ++walked;
            const WalkResult& r = walk.result;
            const bool named = r.outcome == WalkOutcome::Arrived || (r.outcome == WalkOutcome::Blocked && r.blocker != WalkBlock::None && !r.report.empty());
            const bool moved = r.metresWalked > 1.0 || r.outcome == WalkOutcome::Blocked;
            const bool ok = named && moved;
            say(std::format("WALK grid {} seed {}: {} {}, trip {:.0f} m, path {}, walked {:.0f} m in {:.0f} s sim, {:.0f} m short, wall {:.1f} s{}{}", grid, seed,
                ok ? "ok" : "SILENT", outcomeName(r.outcome), walk.trip, walk.hadPath ? "yes" : "no", r.metresWalked, r.seconds, r.metresLeft, wall,
                r.report.empty() ? "" : " | blocker: ", r.report));
            if (r.outcome == WalkOutcome::Arrived) {
                ++arrived;
            } else if (r.outcome == WalkOutcome::Blocked) {
                ++blocked;
            }
            if (!ok) {
                ++problems;
                FAIL_CHECK(std::format("grid {} seed {} is silently stuck: {}", grid, seed, outcomeName(r.outcome)));
            }
        }
        say(std::format("WALK grid {}: {} walked, {} arrived, {} blocked, total wall {:.0f} s", grid, walked, arrived, blocked, elapsed()));
        if (walked == 0) {
            ++problems;
            FAIL_CHECK(std::format("grid {}: no seed had a trip to walk", grid));
        }
    }
    say(std::format("WALK summary: {} problem(s), total wall {:.0f} s", problems, elapsed()));
    CHECK(problems == 0);
}
