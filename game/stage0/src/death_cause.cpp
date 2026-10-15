#include "ichor/stage0/death_cause.h"

namespace ichor::stage0 {

DeathCause classifyDeath(const DeathFacts& facts)
{
    if (!facts.attackerOnScreen) return DeathCause::OffScreen;
    if (facts.victimRecovering) return DeathCause::WhiffPunished;
    if (facts.blocked) return DeathCause::MistimedBlock;
    if (facts.outOfStamina) return DeathCause::OutOfStamina;
    if (facts.againstWall) return DeathCause::Cornered;
    return DeathCause::Unreadable;
}

void DeathTally::add(DeathCause cause)
{
    const std::size_t index = static_cast<std::size_t>(cause);
    if (index < kDeathCauseCount) {
        counts[index] += 1;
    }
}

int DeathTally::count(DeathCause cause) const
{
    const std::size_t index = static_cast<std::size_t>(cause);
    return index < kDeathCauseCount ? counts[index] : 0;
}

int DeathTally::total() const
{
    int sum = 0;
    for (const int n : counts) {
        sum += n;
    }
    return sum;
}

double DeathTally::share(DeathCause cause) const
{
    const int all = total();
    return all > 0 ? static_cast<double>(count(cause)) / static_cast<double>(all) : 0.0;
}

Diagnosis diagnose(const DeathTally& tally)
{
    if (tally.total() < kMinDeathsForDiagnosis) {
        return Diagnosis::TooFewDeaths;
    }
    const double telegraph = tally.share(DeathCause::Unreadable) + tally.share(DeathCause::OffScreen);
    if (telegraph > 0.5) return Diagnosis::Telegraph;
    if (tally.share(DeathCause::OutOfStamina) > 0.5) return Diagnosis::Economy;
    const double readable = tally.share(DeathCause::WhiffPunished) + tally.share(DeathCause::MistimedBlock) + tally.share(DeathCause::Cornered);
    if (readable > 0.5) return Diagnosis::Readable;
    return Diagnosis::Mixed;
}

const char* name(Diagnosis diagnosis)
{
    switch (diagnosis) {
    case Diagnosis::TooFewDeaths: return "too few deaths";
    case Diagnosis::Telegraph: return "telegraph problem";
    case Diagnosis::Economy: return "economy problem";
    case Diagnosis::Readable: return "readable deaths";
    case Diagnosis::Mixed: return "mixed";
    }
    return "mixed";
}

}
