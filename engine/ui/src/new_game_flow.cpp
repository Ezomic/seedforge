#include "ichor/ui/new_game_flow.h"

#include "ichor/ui/new_game_screen.h"

#include <utility>

namespace ichor {

NewGameFlow::NewGameFlow(bool forged, int gridSize, worldforge::ForgeConfig config)
    : forged_(forged)
    , gridSize_(gridSize)
    , config_(std::move(config))
{
}

void NewGameFlow::begin(const WorldDesc& desc)
{
    if (stage_ != NewGameStage::Choosing && stage_ != NewGameStage::Failed) {
        return;
    }
    job_.reset();
    error_.clear();
    if (!desc.valid()) {
        error_ = "the world description is not valid";
        stage_ = NewGameStage::Failed;
        return;
    }
    if (!forged_) {
        stage_ = NewGameStage::Ready;
        return;
    }
    job_ = std::make_unique<ForgeJob>(desc, gridSize_, PhaseObserver{}, config_);
    job_->start();
    stage_ = NewGameStage::Forging;
}

void NewGameFlow::cancel()
{
    job_.reset();
    error_.clear();
    stage_ = NewGameStage::Choosing;
}

void NewGameFlow::update()
{
    if (stage_ != NewGameStage::Forging || !job_) {
        return;
    }
    switch (job_->state()) {
    case ForgeJobState::Done:
        stage_ = NewGameStage::Ready;
        break;
    case ForgeJobState::Failed:
    case ForgeJobState::Cancelled:
        error_ = job_->error().empty() ? "the forge was stopped" : job_->error();
        job_.reset();
        stage_ = NewGameStage::Failed;
        break;
    case ForgeJobState::Idle:
    case ForgeJobState::Running:
        break;
    }
}

LoadingView NewGameFlow::loadingView() const
{
    return buildLoadingView(job_ ? job_->progress() : ForgeProgressSnapshot{});
}

std::unique_ptr<ForgeJob> NewGameFlow::takeJob()
{
    if (stage_ != NewGameStage::Ready) {
        return nullptr;
    }
    return std::move(job_);
}

void applyNewGameFlow(NewGameFlow& flow, NewGameScreen& card, LoadingScreen& loading, const WorldDesc& desc)
{
    if (card.takeBegin()) {
        flow.begin(desc);
    }
    flow.update();
    const NewGameStage stage = flow.stage();
    card.setVisible(stage == NewGameStage::Choosing || stage == NewGameStage::Failed);
    loading.setVisible(stage == NewGameStage::Forging);
    if (stage == NewGameStage::Forging) {
        loading.setView(flow.loadingView());
    }
}

}
