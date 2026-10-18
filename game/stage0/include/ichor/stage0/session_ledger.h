#pragma once

#include <cstddef>
#include <set>
#include <string>
#include <string_view>

namespace ichor::stage0 {

constexpr int kBugFixFights = 20;
constexpr int kGateFights = 200;
constexpr std::size_t kMaxLabelLength = 64;

// Fights 1 to 20 only prove the build works; they are never tuning data.
constexpr bool isBugFixFight(int fight) { return fight >= 1 && fight <= kBugFixFights; }

struct SessionMarker {
    std::string label;
    std::string variable;

    bool empty() const { return label.empty() && variable.empty(); }
};

// Commas, quotes and control characters are dropped so a label can never break a row; the result is length-capped.
std::string cleanField(std::string_view text);

// The sidecar next to the fight CSV: one row per fight, joined to it on `fight`.
// It lives apart from the main CSV so the report parser and its columns stay untouched.
const char* ledgerHeader();
std::string ledgerRow(int fight, const SessionMarker& marker);

struct GateProgress {
    int recorded = 0;
    int bugFix = 0;
    int tuned = 0;
    int remaining = kGateFights;
    bool complete = false;
    std::set<std::string> sessions;
};

// Reads ledger text. Malformed rows and repeated fight numbers are skipped; the bug-fix flag is
// recomputed from the fight number, never trusted from the file.
GateProgress readProgress(std::string_view ledgerText);

std::string progressLine(const GateProgress& progress);

}
