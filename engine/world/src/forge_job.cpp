#include "ichor/world/forge_job.h"

#include <exception>
#include <utility>

namespace ichor {

namespace {

PhaseProgress toPhaseProgress(const worldforge::ForgeProgress& p)
{
    PhaseProgress out;
    out.index = static_cast<std::size_t>(p.phase) - 1;
    out.count = static_cast<std::size_t>(worldforge::kForgePhaseCount);
    out.phase = std::string(worldforge::forgePhaseName(p.phase));
    out.finished = p.finished;
    return out;
}

}

ForgeJob::ForgeJob(WorldDesc desc, int gridSize, PhaseObserver extraObserver, worldforge::ForgeConfig config)
    : desc_(desc)
    , gridSize_(gridSize)
    , extraObserver_(std::move(extraObserver))
    , config_(std::move(config))
{
}

ForgeJob::~ForgeJob()
{
    cancel();
}

bool ForgeJob::start()
{
    const std::lock_guard lock(mutex_);
    if (state_ != ForgeJobState::Idle) {
        return false;
    }
    worker_ = std::thread([this] { run(); });
    state_ = ForgeJobState::Running;
    return true;
}

void ForgeJob::cancel()
{
    cancelRequested_.store(true);
    if (worker_.joinable()) {
        worker_.join();
    }
    const std::lock_guard lock(mutex_);
    if (state_ == ForgeJobState::Idle || state_ == ForgeJobState::Running) {
        state_ = ForgeJobState::Cancelled;
    }
}

ForgeJobState ForgeJob::state() const
{
    const std::lock_guard lock(mutex_);
    return state_;
}

std::string ForgeJob::error() const
{
    const std::lock_guard lock(mutex_);
    return error_;
}

const ForgeJobResult* ForgeJob::result() const
{
    const std::lock_guard lock(mutex_);
    return state_ == ForgeJobState::Done ? &result_ : nullptr;
}

void ForgeJob::finish(ForgeJobState state, std::string error)
{
    const std::lock_guard lock(mutex_);
    state_ = state;
    error_ = std::move(error);
}

void ForgeJob::run()
{
    try {
        const PhaseObserver observer = progress_.observer();
        const auto onProgress = [&](const worldforge::ForgeProgress& p) {
            if (cancelRequested_.load()) {
                throw Cancelled{};
            }
            if (p.finished && !p.ok) {
                return;
            }
            const PhaseProgress event = toPhaseProgress(p);
            observer(event);
            if (extraObserver_) {
                extraObserver_(event);
            }
        };
        auto world = std::make_shared<const worldforge::ForgedWorld>(
            worldforge::forgeWorld(desc_.seed, gridSize_, config_, worldforge::ForgePhase::Content, onProgress));
        if (cancelRequested_.load()) {
            throw Cancelled{};
        }
        if (!world->complete()) {
            finish(ForgeJobState::Failed, world->ok() ? "the forge stopped early" : world->error());
            return;
        }
        ForgedHeightfield adapted = heightfieldFromForgedWorld(*world, forgedWorldDesc(desc_));
        if (!adapted.ok()) {
            finish(ForgeJobState::Failed, adapted.error);
            return;
        }
        const std::lock_guard lock(mutex_);
        result_.world = std::move(world);
        result_.heightfield = std::move(adapted.field);
        state_ = ForgeJobState::Done;
    } catch (const Cancelled&) {
        finish(ForgeJobState::Cancelled, {});
    } catch (const std::exception& e) {
        finish(ForgeJobState::Failed, e.what());
    }
}

}
