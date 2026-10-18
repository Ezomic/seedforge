#pragma once

#include "ichor/ui/loading_screen.h"
#include "ichor/world/forge_job.h"
#include "ichor/world/world_desc.h"

#include <memory>
#include <string>

namespace ichor {

class NewGameScreen;

enum class NewGameStage { Choosing, Forging, Ready, Failed };

// The new-game flow (doc 08 phase 12): Begin starts a ForgeJob on the chosen seed, the loading screen
// follows its phases, and the finished world is handed to the app in place of the stand-in terrain.
// Pure logic over a ForgeJob, with no UI of its own, so it runs headless; applyNewGameFlow puts it on
// the two screens.
//
// `forged` is the flag that keeps the stand-in the default: while it is false, Begin goes straight to
// Ready with no job, and the app keeps drawing the stand-in.
class NewGameFlow {
public:
    explicit NewGameFlow(bool forged, int gridSize = kForgedDefaultGridSize);

    // Begin was clicked. Ignored unless the stage is Choosing or Failed. A desc that is not valid
    // fails the flow at once.
    void begin(const WorldDesc& desc);
    // Back out of the forge: the job is stopped and joined, the stage returns to Choosing, nothing is handed over.
    void cancel();
    // Reads the job and moves the stage on. Call once a frame.
    void update();

    NewGameStage stage() const { return stage_; }
    const std::string& error() const { return error_; }
    LoadingView loadingView() const;
    // The finished job (world and heightfield in its result()), once and only when the stage is
    // Ready and forging was on. Null otherwise.
    std::unique_ptr<ForgeJob> takeJob();
    const ForgeJobResult* result() const { return job_ ? job_->result() : nullptr; }

private:
    bool forged_;
    int gridSize_;
    NewGameStage stage_ = NewGameStage::Choosing;
    std::string error_;
    std::unique_ptr<ForgeJob> job_;
};

// Shows the new-game card while choosing, the loading screen while forging, and neither otherwise.
// Takes Begin from the card into the flow.
void applyNewGameFlow(NewGameFlow& flow, NewGameScreen& card, LoadingScreen& loading, const WorldDesc& desc);

}
