#include "ichor/world/lighting_budget.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <limits>

using namespace ichor;

namespace {

LightCandidate light(std::uint32_t id, std::int32_t room, std::int32_t xMm, bool guide = false, std::uint32_t cost = 1)
{
    LightCandidate c;
    c.id = id;
    c.roomId = room;
    c.xMm = xMm;
    c.cost = cost;
    c.guide = guide;
    return c;
}

bool has(const LightingSelection& s, std::uint32_t id)
{
    return std::find(s.active.begin(), s.active.end(), id) != s.active.end();
}

LightingSelection pick(const std::vector<LightCandidate>& lights, const LightingViewer& viewer, const LightingBudget& budget, const std::vector<std::uint32_t>& previous = {})
{
    std::vector<std::string> problems;
    return selectLights(lights, viewer, budget, previous, problems);
}

}

TEST_CASE("everything lit when the budget allows", "[lighting]")
{
    const auto s = pick({light(1, 0, 0), light(2, 0, 100), light(3, 1, 200)}, {}, {10, 10, 0});
    CHECK(s.active == std::vector<std::uint32_t>{1, 2, 3});
    CHECK(s.spent == 3);
}

TEST_CASE("guide lights outrank a nearer player room light", "[lighting]")
{
    const std::vector<LightCandidate> lights = {light(1, 0, 0), light(2, 0, 10), light(3, 5, 900000, true)};
    const auto s = pick(lights, {0, 0, 0, 0}, {100, 1, 0});
    CHECK(s.active == std::vector<std::uint32_t>{3});
}

TEST_CASE("player room outranks nearer lights in other rooms", "[lighting]")
{
    const std::vector<LightCandidate> lights = {light(1, 7, 0), light(2, 0, 900000)};
    const auto s = pick(lights, {0, 0, 0, 0}, {100, 1, 0});
    CHECK(s.active == std::vector<std::uint32_t>{2});
}

TEST_CASE("nearer lights win within a tier and ties go to the smaller id", "[lighting]")
{
    const auto s = pick({light(1, 0, 5000), light(2, 0, 1000), light(3, 0, -1000)}, {}, {100, 2, 0});
    CHECK(s.active == std::vector<std::uint32_t>{2, 3});
}

TEST_CASE("a light too costly is skipped but a cheaper one behind it still fits", "[lighting]")
{
    const auto s = pick({light(1, 0, 0, false, 5), light(2, 0, 10, false, 2), light(3, 0, 20, false, 1)}, {}, {4, 10, 0});
    CHECK(s.active == std::vector<std::uint32_t>{2, 3});
    CHECK(s.spent == 3);
}

TEST_CASE("guide lights are still capped by the hard budget", "[lighting]")
{
    std::vector<LightCandidate> lights;
    for (std::uint32_t i = 0; i < 20; ++i) {
        lights.push_back(light(i, 0, static_cast<std::int32_t>(i) * 100, true));
    }
    const auto s = pick(lights, {}, {1000, 6, 0});
    CHECK(s.active.size() == 6);
    const auto byCost = pick(lights, {}, {4, 100, 0});
    CHECK(byCost.spent == 4);
}

TEST_CASE("hysteresis keeps a live light against a slightly nearer challenger", "[lighting]")
{
    const std::vector<LightCandidate> lights = {light(1, 0, 1000), light(2, 0, 950)};
    const LightingBudget budget{100, 1, 64};
    CHECK(pick(lights, {}, budget, {}).active == std::vector<std::uint32_t>{2});
    CHECK(pick(lights, {}, budget, {1}).active == std::vector<std::uint32_t>{1});
    const LightingBudget none{100, 1, 0};
    CHECK(pick(lights, {}, none, {1}).active == std::vector<std::uint32_t>{2});
}

TEST_CASE("hysteresis yields to a much nearer challenger", "[lighting]")
{
    const std::vector<LightCandidate> lights = {light(1, 0, 4000), light(2, 0, 500)};
    CHECK(pick(lights, {}, {100, 1, 64}, {1}).active == std::vector<std::uint32_t>{2});
}

TEST_CASE("a viewer jittering between two lights causes far fewer swaps with hysteresis", "[lighting]")
{
    const std::vector<LightCandidate> lights = {light(1, 0, -1000), light(2, 0, 1000)};
    auto swaps = [&](std::uint32_t hysteresis) {
        LightingBudgeter budgeter({100, 1, hysteresis});
        std::vector<std::string> problems;
        int changes = 0;
        std::vector<std::uint32_t> last;
        for (int step = 0; step < 100; ++step) {
            const LightingViewer viewer{0, (step % 2 == 0) ? -50 : 50, 0, 0};
            const auto& s = budgeter.update(lights, viewer, problems);
            if (s.active != last) {
                ++changes;
            }
            last = s.active;
        }
        return changes;
    };
    CHECK(swaps(0) > 90);
    CHECK(swaps(64) <= 2);
}

TEST_CASE("player room change moves the budget to the new room", "[lighting]")
{
    const std::vector<LightCandidate> lights = {light(1, 0, 0), light(2, 1, 100000)};
    LightingBudgeter budgeter({100, 1, 64});
    std::vector<std::string> problems;
    CHECK(budgeter.update(lights, {0, 0, 0, 0}, problems).active == std::vector<std::uint32_t>{1});
    CHECK(budgeter.update(lights, {1, 100000, 0, 0}, problems).active == std::vector<std::uint32_t>{2});
}

TEST_CASE("selection is deterministic and independent of candidate order", "[lighting]")
{
    std::vector<LightCandidate> lights;
    for (std::uint32_t i = 0; i < 30; ++i) {
        lights.push_back(light(i, static_cast<std::int32_t>(i % 3), static_cast<std::int32_t>((i * 7919) % 5000), i % 11 == 0));
    }
    const auto a = pick(lights, {1, 100, 0, 0}, {100, 8, 64});
    std::reverse(lights.begin(), lights.end());
    const auto b = pick(lights, {1, 100, 0, 0}, {100, 8, 64});
    CHECK(a.active == b.active);
    CHECK(a.active.size() == 8);
}

TEST_CASE("hostile inputs are rejected without crashing", "[lighting]")
{
    constexpr auto big = std::numeric_limits<std::int32_t>::max();
    constexpr auto small = std::numeric_limits<std::int32_t>::min();
    LightCandidate far = light(1, 0, big);
    far.yMm = small;
    far.zMm = big;
    LightCandidate zero = light(2, 0, 0, false, 0);
    const LightCandidate dup = light(1, 0, 5);
    LightCandidate huge = light(3, 0, 0, false, std::numeric_limits<std::uint32_t>::max());
    std::vector<std::string> problems;
    const LightingViewer viewer{0, small, big, small};
    const auto s = selectLights({far, zero, dup, huge}, viewer, {std::numeric_limits<std::uint32_t>::max(), std::numeric_limits<std::uint32_t>::max(), 9999}, {1, 1, 1}, problems);
    CHECK(problems.size() == 2);
    CHECK(has(s, 3));
    CHECK_FALSE(has(s, 1));
    CHECK_FALSE(has(s, 2));
    CHECK(s.active.size() == 1);
    CHECK(s.spent == std::numeric_limits<std::uint32_t>::max());
}

TEST_CASE("a zero cost candidate does not shadow a valid one with the same id", "[lighting]")
{
    std::vector<std::string> problems;
    const auto s = selectLights({light(1, 0, 0, false, 0), light(1, 0, 5)}, {}, {10, 10, 0}, {}, problems);
    CHECK(s.active == std::vector<std::uint32_t>{1});
    CHECK(problems.size() == 1);
}

TEST_CASE("empty input and zero budget yield nothing", "[lighting]")
{
    CHECK(pick({}, {}, {100, 100, 0}).active.empty());
    CHECK(pick({light(1, 0, 0, true)}, {}, {0, 100, 0}).active.empty());
    CHECK(pick({light(1, 0, 0, true)}, {}, {100, 0, 0}).active.empty());
}

TEST_CASE("an oversized candidate list is truncated, not unbounded", "[lighting]")
{
    std::vector<LightCandidate> lights;
    for (std::uint32_t i = 0; i < kMaxLightCandidates + 50; ++i) {
        lights.push_back(light(i, 0, 0));
    }
    std::vector<std::string> problems;
    const auto s = selectLights(lights, {}, {1000000, 1000000, 0}, {}, problems);
    CHECK(s.active.size() == kMaxLightCandidates);
    CHECK(problems.size() == 1);
}
