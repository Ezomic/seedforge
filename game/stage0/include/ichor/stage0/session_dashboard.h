#pragma once

#include "ichor/stage0/death_cause.h"
#include "ichor/stage0/fight_report.h"

#include <string>
#include <string_view>
#include <vector>

namespace ichor::stage0 {

constexpr const char* kUnlabelledSession = "(no sidecar)";

struct SessionRow {
    std::string label;
    std::string variable;
    int firstFight = 0;
    int fights = 0;
    int playerLosses = 0;
    DeathTally deaths;
    double meanSecondsToKill = 0.0; // mean length of the fights the player lost; zero with no losses
    Diagnosis diagnosis = Diagnosis::TooFewDeaths;
};

struct Dashboard {
    std::vector<SessionRow> sessions; // ordered by first fight
    int bugFixFightsSkipped = 0;      // fights 1 to 20 are never tuning data
};

// Joins fights.csv rows to the sidecar on `fight`. A missing or empty sidecar puts every fight in one
// unlabelled session; malformed sidecar rows and repeated fight numbers are skipped. Never throws.
Dashboard buildDashboard(const std::vector<FightRow>& fights, std::string_view ledgerText);

// The stage0-log.md section 6 table, ending in a newline.
std::string formatDashboard(const Dashboard& dashboard);

}
