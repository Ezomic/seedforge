#pragma once

#include "ichor/stage0/death_cause.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace ichor::stage0 {

struct SideRecord {
    float healthLeft = 0.0f;
    int attacks = 0;
    int hits = 0;
    int whiffs = 0;
    int blocked = 0;
    int dodges = 0;
    int staggers = 0;
    float damageDealt = 0.0f;
    float damageTaken = 0.0f;
};

// Every fight becomes one CSV row. A fight starts at the first swing and ends
// at a death; an abandoned fight is dropped rather than written.
class FightRecorder {
public:
    // No path means record in memory only.
    void open(const std::filesystem::path& csv);

    void noteAttack(bool playerSide, double now);
    void noteHit(bool attackerIsPlayer, float damage, bool blocked);
    void noteWhiff(bool playerSide);
    void noteDodge(bool playerSide);
    void noteStagger(bool playerSide);
    // A swarm unit's strike is a foe attack; the grant count shows how the tokens were paced.
    void noteSwarmGrant();
    void noteSwarmStrike(double now);
    void noteDeath(bool playerDied, DeathCause cause, float playerHealth, float foeHealth, double now, const DeathFacts& facts = {});
    // A retry press: an unfinished fight is dropped, and the time since the
    // last death becomes the next row's gap_to_retry.
    void noteRetry(double now);

    bool running() const { return running_; }
    int fightsWritten() const { return fights_; }
    int swarmGrants() const { return swarmGrants_; }
    int swarmStrikes() const { return swarmStrikes_; }
    const SideRecord& player() const { return player_; }
    const SideRecord& foe() const { return foe_; }
    DeathCause lastCause() const { return lastCause_; }
    const DeathFacts& lastFacts() const { return lastFacts_; }
    // Player deaths only, this session; the live read of the distribution.
    const DeathTally& playerDeaths() const { return playerDeaths_; }
    const std::string& lastRow() const { return lastRow_; }

private:
    SideRecord& side(bool playerSide) { return playerSide ? player_ : foe_; }
    void begin(double now);

    std::filesystem::path csv_;
    SideRecord player_;
    SideRecord foe_;
    bool running_ = false;
    int fights_ = 0;
    int swarmGrants_ = 0;
    int swarmStrikes_ = 0;
    double start_ = 0.0;
    double lastEnd_ = -1.0;
    double retryGap_ = 0.0;
    DeathCause lastCause_ = DeathCause::Unknown;
    DeathFacts lastFacts_;
    DeathTally playerDeaths_;
    std::string lastRow_;
};

}
