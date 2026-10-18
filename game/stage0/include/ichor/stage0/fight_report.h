#pragma once

#include "ichor/stage0/death_cause.h"

#include <string>
#include <string_view>
#include <vector>

namespace ichor::stage0 {

// One parsed row of the fight log.
struct FightRow {
    int fight = 0;
    double seconds = 0.0;
    double gapToRetry = 0.0;
    bool playerLost = false;
    DeathCause cause = DeathCause::Unknown;
    double playerHealthLeft = 0.0;
    int playerAttacks = 0;
    int playerHits = 0;
    int playerWhiffs = 0;
    int playerBlocked = 0;
    int playerDodges = 0;
    double playerDamageTaken = 0.0;
};

// Rows that do not parse (fewer than 23 columns, non-numeric or non-finite fields, unknown loser) are skipped,
// so a half-written or hostile file yields fewer rows rather than a crash. Extra trailing columns are ignored.
std::vector<FightRow> parseFightLog(std::string_view csv);

struct FightSummary {
    int fights = 0;
    int playerLosses = 0;
    int playerWins = 0;
    DeathTally playerDeaths; // only fights the player lost
    double medianSeconds = 0.0;
    double medianGapToRetry = 0.0; // over fights that were preceded by a retry gap
    Diagnosis diagnosis = Diagnosis::TooFewDeaths;
};

FightSummary summarise(const std::vector<FightRow>& rows);

// The death-cause distribution and the diagnosis, as plain text for a session log.
std::string formatReport(const FightSummary& summary);

}
