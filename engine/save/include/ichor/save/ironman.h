#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace ichor {

// Rules for the Ironman mode (doc 14): one non-rewindable slot, no manual
// saves, a single rotating autosave, and a death that ends the run. Pure logic:
// the caller asks before acting and persists encode() next to the slot, so
// quitting after a death cannot undo it.
class IronmanRun {
public:
    enum class Action {
        ManualSave,  // quicksave or any named slot
        Autosave,    // the rotating autosave, including save on quit
        LoadMidRun,  // reloading while a run is in progress
        Continue,    // resuming the rolling slot from the menu
        NewRun,
    };

    enum class Verdict {
        Allowed,
        NoManualSaves,
        NoReloadMidRun,
        RunEnded,
    };

    Verdict check(Action action) const;

    bool alive() const { return !deathTick_; }
    std::optional<std::uint64_t> deathTick() const { return deathTick_; }

    // The first death stands; later calls cannot move it.
    void recordDeath(std::uint64_t tick);

    // A fresh run starts alive, whatever the last one did.
    void startNewRun() { deathTick_.reset(); }

    std::string encode() const;
    // Strict: anything not produced by encode() is refused.
    static std::optional<IronmanRun> decode(std::string_view text);

private:
    std::optional<std::uint64_t> deathTick_;
};

}
