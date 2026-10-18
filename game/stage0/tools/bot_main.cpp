#include "ichor/stage0/fight_bot.h"

#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <system_error>

namespace {

bool parseCount(const char* text, long long maximum, long long& out)
{
    const std::string_view view(text);
    const auto [end, error] = std::from_chars(view.data(), view.data() + view.size(), out);
    return error == std::errc() && end == view.data() + view.size() && out >= 0 && out <= maximum;
}

}

int main(int argc, char** argv)
{
    using namespace ichor::stage0;
    BotConfig config;
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
        } else if (arg == "--reaction" && hasValue && parseCount(argv[++i], 63, number)) {
            config.skill.reactionTicks = static_cast<int>(number);
        } else if ((arg == "--block" || arg == "--dodge" || arg == "--aggression") && hasValue) {
            const float value = std::strtof(argv[++i], nullptr);
            (arg == "--block" ? config.skill.blockChance : arg == "--dodge" ? config.skill.dodgeChance : config.skill.aggression) = value;
        } else if (arg == "--csv" && hasValue) {
            config.csv = argv[++i];
        } else {
            std::fputs("usage: stage0_bot [--skill novice|regular|expert] [--fights N] [--seed N] [--reaction TICKS] [--block P] [--dodge P] [--aggression P] [--csv fights.csv]\n", stderr);
            return 2;
        }
    }
    const BotResult result = runBotFights(config);
    std::fputs(formatBotResult(config, result).c_str(), stdout);
    return 0;
}
