#include "ichor/stage0/recorder.h"

#include "ichor/core/log.h"

#include <format>
#include <fstream>
#include <iterator>
#include <sstream>

namespace ichor::stage0 {

namespace {

const char* causeName(DeathCause cause)
{
    switch (cause) {
    case DeathCause::OutOfStamina: return "stamina";
    case DeathCause::WhiffPunished: return "whiff_punished";
    case DeathCause::Cornered: return "cornered";
    case DeathCause::MistimedBlock: return "mistimed_block";
    case DeathCause::OffScreen: return "offscreen";
    case DeathCause::Unreadable: return "unreadable";
    case DeathCause::Bleed: return "bleed";
    default: return "unknown";
    }
}

std::filesystem::path ledgerPath(const std::filesystem::path& csv)
{
    std::filesystem::path path = csv;
    path.replace_extension(".sessions.csv");
    return path;
}

}

GateProgress FightRecorder::gateProgress() const
{
    if (csv_.empty()) {
        return {};
    }
    std::ifstream in(ledgerPath(csv_), std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return readProgress(text);
}

void FightRecorder::open(const std::filesystem::path& csv)
{
    csv_ = csv;
    if (csv_.empty()) {
        return;
    }
    if (!std::filesystem::exists(csv_)) {
        std::ofstream out(csv_);
        out << "fight,seconds,gap_to_retry,loser,cause,"
               "p_hp,p_attacks,p_hits,p_whiffs,p_blocked,p_dodges,p_staggers,p_dealt,p_taken,"
               "f_hp,f_attacks,f_hits,f_whiffs,f_blocked,f_dodges,f_staggers,f_dealt,f_taken\n";
        return;
    }
    // Carry the count across sessions so rows stay numbered in order.
    std::ifstream in(csv_);
    std::string line;
    int lines = 0;
    while (std::getline(in, line)) {
        ++lines;
    }
    if (lines == 0) {
        std::ofstream out(csv_, std::ios::app);
        out << "fight,seconds,gap_to_retry,loser,cause,"
               "p_hp,p_attacks,p_hits,p_whiffs,p_blocked,p_dodges,p_staggers,p_dealt,p_taken,"
               "f_hp,f_attacks,f_hits,f_whiffs,f_blocked,f_dodges,f_staggers,f_dealt,f_taken\n";
    }
    fights_ = std::max(0, lines - 1);
    logf(LogLevel::Info, "fight log has {} fights already; next is #{}", fights_, fights_ + 1);
}

void FightRecorder::begin(double now)
{
    if (running_) {
        return;
    }
    player_ = {};
    foe_ = {};
    running_ = true;
    start_ = now;
}

void FightRecorder::noteAttack(bool playerSide, double now)
{
    // A fight starts at the first swing, not at aggro.
    begin(now);
    side(playerSide).attacks += 1;
}

void FightRecorder::noteHit(bool attackerIsPlayer, float damage, bool blocked)
{
    if (!running_) {
        return;
    }
    side(attackerIsPlayer).hits += 1;
    side(attackerIsPlayer).damageDealt += damage;
    side(!attackerIsPlayer).damageTaken += damage;
    if (blocked) {
        side(!attackerIsPlayer).blocked += 1;
    }
}

void FightRecorder::noteWhiff(bool playerSide)
{
    if (running_) side(playerSide).whiffs += 1;
}

void FightRecorder::noteDodge(bool playerSide)
{
    if (running_) side(playerSide).dodges += 1;
}

void FightRecorder::noteStagger(bool playerSide)
{
    if (running_) side(playerSide).staggers += 1;
}

void FightRecorder::noteSwarmGrant()
{
    swarmGrants_ += 1;
}

void FightRecorder::noteSwarmStrike(double now)
{
    swarmStrikes_ += 1;
    noteAttack(false, now);
}

void FightRecorder::noteDeath(bool playerDied, DeathCause cause, float playerHealth, float foeHealth, double now)
{
    if (!running_) {
        return;
    }
    player_.healthLeft = playerHealth;
    foe_.healthLeft = foeHealth;
    lastCause_ = cause;
    if (playerDied) {
        playerDeaths_.add(cause);
    }
    const double gap = retryGap_;
    lastRow_ = std::format("{},{:.2f},{:.2f},{},{},{:.0f},{},{},{},{},{},{},{:.0f},{:.0f},{:.0f},{},{},{},{},{},{},{:.0f},{:.0f}\n",
        fights_ + 1, now - start_, gap, playerDied ? "player" : "foe", causeName(cause),
        player_.healthLeft, player_.attacks, player_.hits, player_.whiffs, player_.blocked, player_.dodges, player_.staggers, player_.damageDealt, player_.damageTaken,
        foe_.healthLeft, foe_.attacks, foe_.hits, foe_.whiffs, foe_.blocked, foe_.dodges, foe_.staggers, foe_.damageDealt, foe_.damageTaken);
    if (!csv_.empty()) {
        std::ofstream out(csv_, std::ios::app);
        out << lastRow_;
        const std::filesystem::path ledger = ledgerPath(csv_);
        const bool fresh = !std::filesystem::exists(ledger);
        std::ofstream side(ledger, std::ios::app);
        if (fresh) {
            side << ledgerHeader();
        }
        side << ledgerRow(fights_ + 1, session_);
    }
    logf(LogLevel::Info, "fight #{} {} in {:.1f}s: {} died ({}) | player {}/{} hits, {} whiffs, {} dodges, {:.0f} hp left",
        fights_ + 1, playerDied ? "LOST" : "won", now - start_, playerDied ? "player" : "foe", causeName(cause),
        player_.hits, player_.attacks, player_.whiffs, player_.dodges, player_.healthLeft);
    running_ = false;
    fights_ += 1;
    lastEnd_ = now;
    retryGap_ = 0.0;
}

void FightRecorder::noteRetry(double now)
{
    if (running_) {
        logf(LogLevel::Info, "fight #{} abandoned on retry, not recorded", fights_ + 1);
        running_ = false;
    }
    // Hesitation before restarting is what frustration looks like in telemetry.
    retryGap_ = lastEnd_ >= 0.0 ? now - lastEnd_ : 0.0;
}

}
