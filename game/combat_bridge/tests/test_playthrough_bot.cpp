#include "ichor/combat_bridge/playthrough_bot.h"
#include "ichor/data/data_set.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <string>
#include <vector>

using namespace ichor;

namespace {

std::vector<std::uint64_t> seedsFromEnv(const char* text)
{
    std::vector<std::uint64_t> seeds;
    std::string token;
    for (const char* c = text;; ++c) {
        if (*c >= '0' && *c <= '9' && token.size() < 18) {
            token.push_back(*c);
            continue;
        }
        if (!token.empty()) {
            seeds.push_back(std::strtoull(token.c_str(), nullptr, 10));
            token.clear();
        }
        if (*c == '\0') break;
    }
    return seeds;
}

double budgetFromEnv()
{
    const char* text = std::getenv("ICHOR_NIGHTLY_PLAYTHROUGH_BUDGET_S");
    const double value = text != nullptr ? std::atof(text) : 0.0;
    return value > 0.0 ? value : 1200.0;
}

void say(const std::string& line)
{
    std::fputs(line.c_str(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

}

TEST_CASE("the playthrough bot reports the eight steps in order and a run is complete only when none failed", "[playthrough]")
{
    REQUIRE(kPlaythroughSteps == 8);
    const char* expected[] = {"walk", "talk", "accept", "fight", "loot", "save", "quit", "load"};
    PlaythroughResult result;
    for (std::size_t i = 0; i < kPlaythroughSteps; ++i) {
        result.steps[i].step = static_cast<PlaythroughStep>(i);
        CHECK(std::string(playthroughStepName(result.steps[i].step)) == expected[i]);
        result.steps[i].status = PlaythroughStatus::Done;
    }
    CHECK(result.completed());
    result.steps[6].status = PlaythroughStatus::Unsupported;
    CHECK(result.completed());
    CHECK(result.summary() == "walk ok, talk ok, accept ok, fight ok, loot ok, save ok, quit UNSUPPORTED, load ok");
    result.steps[3].status = PlaythroughStatus::Stalled;
    CHECK_FALSE(result.completed());
    result.steps[3].status = PlaythroughStatus::NotRun;
    CHECK_FALSE(result.completed());
    result.worldError = "the forge failed";
    CHECK(result.summary() == "no world: the forge failed");
    CHECK_FALSE(result.completed());
}

// Nightly only (SEED-748): the SEED-492 walk bot carried on into a whole errand, headless. Skipped unless
// ICHOR_NIGHTLY_PLAYTHROUGH_SEEDS names the seeds ("1,2,3,5"). Every step prints one line; a step that fails or stalls
// fails the night and the steps after it print as NOT RUN. A step with no headless driver prints UNSUPPORTED with what
// is missing and does not fail the night: it is the list of what this bot cannot drive yet, not a regression.
TEST_CASE("nightly: the playthrough bot talks, accepts, fights, loots, saves, quits and loads, or names the step it cannot", "[playthrough][nightly]")
{
    const char* seedsText = std::getenv("ICHOR_NIGHTLY_PLAYTHROUGH_SEEDS");
    if (seedsText == nullptr || seedsText[0] == '\0') {
        SKIP("set ICHOR_NIGHTLY_PLAYTHROUGH_SEEDS (for example 2) to run the nightly playthrough");
    }
    const std::vector<std::uint64_t> seeds = seedsFromEnv(seedsText);
    REQUIRE_FALSE(seeds.empty());
    const double budget = budgetFromEnv();
    const auto start = std::chrono::steady_clock::now();
    const auto elapsed = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(); };

    DataSet data;
    REQUIRE(data.loadDirectory(ICHOR_GAME_DATA_DIR));
    PlaythroughParams params;
    if (const char* skill = std::getenv("ICHOR_NIGHTLY_PLAYTHROUGH_SKILL"); skill != nullptr && skill[0] != '\0') params.skill = skill;

    int problems = 0;
    int unsupported = 0;
    int complete = 0;
    for (const std::uint64_t seed : seeds) {
        if (elapsed() > budget) {
            say(std::format("PLAY seed {}: NOT RUN, the {:.0f} s budget is spent", seed, budget));
            ++problems;
            continue;
        }
        const PlaythroughResult result = playThrough(seed, data, ICHOR_GAME_DATA_DIR, params);
        if (!result.worldError.empty()) {
            say(std::format("PLAY seed {}: no world ({}), wall {:.1f} s", seed, result.worldError, result.wallSeconds));
            ++problems;
            FAIL_CHECK(std::format("seed {}: {}", seed, result.worldError));
            continue;
        }
        for (const PlaythroughStepResult& step : result.steps) {
            say(std::format("PLAY seed {} step {}: {}{}{}, {:.0f} s scene, wall {:.1f} s", seed, playthroughStepName(step.step), playthroughStatusName(step.status),
                step.detail.empty() ? "" : " | ", step.detail, step.simSeconds, step.wallSeconds));
            if (step.status == PlaythroughStatus::Unsupported) ++unsupported;
            if (step.status == PlaythroughStatus::Failed || step.status == PlaythroughStatus::Stalled) {
                FAIL_CHECK(std::format("seed {} step {} {}: {}", seed, playthroughStepName(step.step), playthroughStatusName(step.status), step.detail));
            }
        }
        say(std::format("PLAY seed {}: {} | {}, wall {:.1f} s", seed, result.completed() ? "complete" : "INCOMPLETE", result.summary(), result.wallSeconds));
        if (result.completed()) {
            ++complete;
        } else {
            ++problems;
        }
    }
    say(std::format("PLAY summary: {} of {} seed(s) complete, {} problem(s), {} unsupported step(s), total wall {:.0f} s", complete, seeds.size(), problems, unsupported, elapsed()));
    CHECK(problems == 0);
}
