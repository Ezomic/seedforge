#include "ichor/world/forge_progress.h"

#include <algorithm>

namespace ichor {

float ForgeProgressSnapshot::fraction() const
{
    if (total == 0) {
        return 0.0f;
    }
    return static_cast<float>(std::min(completed, total)) / static_cast<float>(total);
}

PhaseObserver ForgeProgress::observer()
{
    return [this](const PhaseProgress& progress) {
        const std::lock_guard lock(mutex_);
        state_.started = true;
        state_.phase = progress.phase;
        state_.total = progress.count;
        state_.completed = progress.finished ? progress.index + 1 : progress.index;
        state_.done = state_.completed >= state_.total;
    };
}

ForgeProgressSnapshot ForgeProgress::snapshot() const
{
    const std::lock_guard lock(mutex_);
    return state_;
}

void ForgeProgress::reset()
{
    const std::lock_guard lock(mutex_);
    state_ = {};
}

}
