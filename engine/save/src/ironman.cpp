#include "ichor/save/ironman.h"

#include <charconv>

namespace ichor {

namespace {

constexpr std::string_view kAlive = "ichor-ironman 1 alive";
constexpr std::string_view kDeadPrefix = "ichor-ironman 1 dead ";

}

IronmanRun::Verdict IronmanRun::check(Action action) const
{
    switch (action) {
    case Action::ManualSave:
        return Verdict::NoManualSaves;
    case Action::NewRun:
        return Verdict::Allowed;
    case Action::Autosave:
    case Action::Continue:
        return alive() ? Verdict::Allowed : Verdict::RunEnded;
    case Action::LoadMidRun:
        return alive() ? Verdict::NoReloadMidRun : Verdict::RunEnded;
    }
    return Verdict::RunEnded;
}

void IronmanRun::recordDeath(std::uint64_t tick)
{
    if (!deathTick_) {
        deathTick_ = tick;
    }
}

std::string IronmanRun::encode() const
{
    if (!deathTick_) {
        return std::string(kAlive);
    }
    return std::string(kDeadPrefix) + std::to_string(*deathTick_);
}

std::optional<IronmanRun> IronmanRun::decode(std::string_view text)
{
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    IronmanRun run;
    if (text == kAlive) {
        return run;
    }
    if (!text.starts_with(kDeadPrefix)) {
        return std::nullopt;
    }
    const std::string_view digits = text.substr(kDeadPrefix.size());
    if (digits.empty() || digits.size() > 20 || (digits.size() > 1 && digits.front() == '0')) {
        return std::nullopt;
    }
    std::uint64_t tick = 0;
    const auto [end, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), tick);
    if (ec != std::errc{} || end != digits.data() + digits.size()) {
        return std::nullopt;
    }
    run.deathTick_ = tick;
    return run;
}

}
