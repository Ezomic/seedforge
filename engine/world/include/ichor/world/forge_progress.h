#pragma once

#include "ichor/world/phase_pipeline.h"

#include <cstddef>
#include <mutex>
#include <string>

namespace ichor {

// What a loading screen may read of a forge run.
struct ForgeProgressSnapshot {
    std::string phase;         // the phase running now, or the last one that ran
    std::size_t completed = 0; // phases finished
    std::size_t total = 0;     // phases in the pipeline; zero before the run starts
    bool started = false;
    bool done = false;         // every phase finished

    // Finished phases over all phases, in [0, 1]; zero before the run starts.
    float fraction() const;
};

// Collects the PhaseObserver events of a forge run so another thread can read them. The forge
// writes through observer() on its worker thread and the UI reads snapshot() on its own; nothing
// here reaches back into the forge, so the forged output is the same with or without it.
class ForgeProgress {
public:
    // The observer holds this object by address: keep it alive until the run has returned.
    PhaseObserver observer();
    ForgeProgressSnapshot snapshot() const;
    void reset();

private:
    mutable std::mutex mutex_;
    ForgeProgressSnapshot state_;
};

}
