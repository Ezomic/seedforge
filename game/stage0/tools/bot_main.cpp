#include "ichor/stage0/fight_bot.h"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string_view>
#include <system_error>

namespace {

bool parseCount(const char* text, long long maximum, long long& out)
{
    const std::string_view view(text);
    const auto [end, error] = std::from_chars(view.data(), view.data() + view.size(), out);
    return error == std::errc() && end == view.data() + view.size() && out >= 0 && out <= maximum;
}

bool parseUnit(const char* text, std::optional<float>& out)
{
    const std::string_view view(text);
    float value = 0.0f;
    const auto [end, error] = std::from_chars(view.data(), view.data() + view.size(), value);
    if (error != std::errc() || end != view.data() + view.size() || !std::isfinite(value)) return false;
    out = value;
    return true;
}

}

int main(int argc, char** argv)
{
    using namespace ichor::stage0;
    BotConfig config;
    // Overrides are applied after --skill whatever the flag order.
    std::optional<int> reaction;
    std::optional<float> block;
    std::optional<float> dodge;
    std::optional<float> aggression;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const bool hasValue = i + 1 < argc;
        long long number = 0;
        if (arg == "--skill" && hasValue) {
            const auto skill = findBotSkill(argv[++i]);
            if (!skill) {
                std::fputs("stage0_bot: skill must be novice, regular or expert\n", stderr);
                return 2;
            }
            config.skill = *skill;
        } else if (arg == "--fights" && hasValue && parseCount(argv[++i], 100000, number)) {
            config.fights = static_cast<int>(number);
        } else if (arg == "--seed" && hasValue && parseCount(argv[++i], 0x7fffffffffffffffll, number)) {
            config.seed = static_cast<std::uint64_t>(number);
        } else if (arg == "--reaction" && hasValue && parseCount(argv[++i], kBotHistoryTicks - 1, number)) {
            reaction = static_cast<int>(number);
        } else if (arg == "--block" && hasValue && parseUnit(argv[++i], block)) {
        } else if (arg == "--dodge" && hasValue && parseUnit(argv[++i], dodge)) {
        } else if (arg == "--aggression" && hasValue && parseUnit(argv[++i], aggression)) {
        } else if (arg == "--csv" && hasValue) {
            config.csv = argv[++i];
        } else {
            std::fputs("usage: stage0_bot [--skill novice|regular|expert] [--fights N] [--seed N] [--reaction TICKS] "
                       "[--block P] [--dodge P] [--aggression P] [--csv fights.csv]\n",
                stderr);
            return 2;
        }
    }
    if (reaction) config.skill.reactionTicks = *reaction;
    if (block) config.skill.blockChance = *block;
    if (dodge) config.skill.dodgeChance = *dodge;
    if (aggression) config.skill.aggression = *aggression;
    const BotResult result = runBotFights(config);
    std::fputs(formatBotResult(config, result).c_str(), stdout);
    return 0;
}
