#include "ichor/world/phase_pipeline.h"

#include "ichor/core/hash.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace ichor {

namespace {

std::uint64_t link(std::uint64_t previous, const PhaseArtifact& artifact)
{
    return hashCombine(previous, hashCombine(hash64(artifact.phase()), artifact.digest()));
}

}

void PhaseArtifact::check(const std::type_info& wanted) const
{
    if (type_ != std::type_index(wanted)) {
        throw std::logic_error("phase artifact '" + phase_ + "' is not a " + wanted.name());
    }
}

const PhaseArtifact& PhaseView::artifact(const std::string& phase) const
{
    for (const PhaseArtifact& a : earlier_) {
        if (a.phase() == phase) {
            return a;
        }
    }
    throw std::out_of_range("no earlier phase named '" + phase + "'");
}

const PhaseArtifact& PhaseRun::artifact(const std::string& phase) const
{
    for (const PhaseArtifact& a : artifacts_) {
        if (a.phase() == phase) {
            return a;
        }
    }
    throw std::out_of_range("no phase named '" + phase + "'");
}

std::uint64_t PhaseRun::chainDigest(std::size_t index) const
{
    return chain_.at(index);
}

std::uint64_t PhaseRun::chainDigest() const
{
    return chain_.empty() ? 0 : chain_.back();
}

std::optional<PhaseDivergence> firstDivergence(const PhaseRun& a, const PhaseRun& b)
{
    const std::size_t shared = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < shared; ++i) {
        if (a.chainDigest(i) != b.chainDigest(i)) {
            return PhaseDivergence{i, a.artifacts()[i].phase(), a.artifacts()[i].digest(), b.artifacts()[i].digest()};
        }
    }
    if (a.size() == b.size()) {
        return std::nullopt;
    }
    const PhaseArtifact& extra = a.size() > b.size() ? a.artifacts()[shared] : b.artifacts()[shared];
    return PhaseDivergence{shared, extra.phase(), a.size() > b.size() ? extra.digest() : 0, a.size() > b.size() ? 0 : extra.digest()};
}

PhasePipeline::PhasePipeline(std::vector<Phase> phases)
    : phases_(std::move(phases))
{
    for (std::size_t i = 0; i < phases_.size(); ++i) {
        if (phases_[i].name.empty() || !phases_[i].run) {
            throw std::invalid_argument("a phase needs a name and something to run");
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (phases_[j].name == phases_[i].name) {
                throw std::invalid_argument("two phases are named '" + phases_[i].name + "'");
            }
        }
    }
}

std::size_t PhasePipeline::indexOf(const std::string& phase) const
{
    for (std::size_t i = 0; i < phases_.size(); ++i) {
        if (phases_[i].name == phase) {
            return i;
        }
    }
    throw std::out_of_range("no phase named '" + phase + "'");
}

void PhasePipeline::finish(PhaseRun& result, const PhaseObserver& observer) const
{
    for (std::size_t i = result.size(); i < phases_.size(); ++i) {
        if (observer) {
            observer({i, phases_.size(), phases_[i].name, false});
        }
        PhaseArtifact artifact = phases_[i].run(PhaseView(result.artifacts_));
        if (artifact.phase() != phases_[i].name) {
            throw std::logic_error("phase '" + phases_[i].name + "' made an artifact named '" + artifact.phase() + "'");
        }
        result.chain_.push_back(link(result.chain_.empty() ? 0 : result.chain_.back(), artifact));
        result.artifacts_.push_back(std::move(artifact));
        if (observer) {
            observer({i, phases_.size(), phases_[i].name, true});
        }
    }
}

PhaseRun PhasePipeline::run(const PhaseObserver& observer) const
{
    PhaseRun result;
    finish(result, observer);
    return result;
}

PhaseRun PhasePipeline::runFrom(const PhaseRun& previous, std::size_t from, const PhaseObserver& observer) const
{
    if (from >= phases_.size()) {
        throw std::out_of_range("there is no phase " + std::to_string(from));
    }
    if (previous.size() < from) {
        throw std::invalid_argument("the earlier run has only " + std::to_string(previous.size()) + " phases, " + std::to_string(from) + " are needed");
    }
    for (std::size_t i = 0; i < from; ++i) {
        if (previous.artifacts()[i].phase() != phases_[i].name) {
            throw std::invalid_argument("the earlier run has '" + previous.artifacts()[i].phase() + "' where this pipeline has '" + phases_[i].name + "'");
        }
    }
    PhaseRun result;
    result.artifacts_.assign(previous.artifacts_.begin(), previous.artifacts_.begin() + static_cast<std::ptrdiff_t>(from));
    result.chain_.assign(previous.chain_.begin(), previous.chain_.begin() + static_cast<std::ptrdiff_t>(from));
    finish(result, observer);
    return result;
}

PhaseRun PhasePipeline::runFrom(const PhaseRun& previous, const std::string& phase, const PhaseObserver& observer) const
{
    return runFrom(previous, indexOf(phase), observer);
}

}
