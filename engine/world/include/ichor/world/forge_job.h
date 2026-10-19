#pragma once

#include "ichor/world/forge_progress.h"
#include "ichor/world/forged_heightfield.h"
#include "ichor/world/world_desc.h"

#include <worldforge/forge_world.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace ichor {

enum class ForgeJobState { Idle, Running, Done, Failed, Cancelled };

// What a finished job holds: the whole forged world (phases 1 to 11) and its terrain as the engine's
// Heightfield, taken from forgedWorldDesc(desc) so the height range fits what worldforge can make.
struct ForgeJobResult {
    std::shared_ptr<const worldforge::ForgedWorld> world;
    Heightfield heightfield;
};

// Runs forgeWorld on a worker thread for the new-game flow (doc 08 phase 12). The forged output
// depends only on desc.seed and the grid size: progress and cancel only watch and stop the run, they
// never feed into it, so a seed forges the same world however it is watched.
//
// Cancel is cooperative: the forge is stopped at its next phase boundary by unwinding out of the
// progress callback, and the destructor cancels and joins, so a job never outlives its owner and
// never leaves a thread behind. A cancelled job has no result. A phase already running is not
// interrupted, so cancel() and the destructor wait for it to end.
//
// The accessors (state, progress, error, result) may be called from any thread. start() and cancel()
// belong to the one thread that owns the job.
class ForgeJob {
public:
    // `config` is the forge's; the default is what the new-game flow always used. A config that names items, cast archetypes
    // or dungeon archetypes fills the content manifest (phase 11) with them and changes nothing before that phase.
    explicit ForgeJob(WorldDesc desc, int gridSize = kForgedDefaultGridSize, PhaseObserver extraObserver = {}, worldforge::ForgeConfig config = {});
    ~ForgeJob();
    ForgeJob(const ForgeJob&) = delete;
    ForgeJob& operator=(const ForgeJob&) = delete;

    // Starts the worker. False when the job was started or cancelled already.
    bool start();
    // Stops the run at the next phase boundary and waits for the worker. A job that already finished
    // keeps its state.
    void cancel();

    ForgeJobState state() const;
    ForgeProgressSnapshot progress() const { return progress_.snapshot(); }
    // The reason a Failed job has no result; empty otherwise.
    std::string error() const;
    // Null unless the state is Done. The result stays valid until the job is destroyed.
    const ForgeJobResult* result() const;

private:
    struct Cancelled {};

    void run();
    void finish(ForgeJobState state, std::string error);

    WorldDesc desc_;
    int gridSize_;
    PhaseObserver extraObserver_;
    worldforge::ForgeConfig config_;
    ForgeProgress progress_;
    std::atomic<bool> cancelRequested_{false};
    std::thread worker_;
    mutable std::mutex mutex_;
    ForgeJobState state_ = ForgeJobState::Idle;
    std::string error_;
    ForgeJobResult result_;
};

}
