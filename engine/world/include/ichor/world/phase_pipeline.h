#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <typeindex>
#include <vector>

namespace ichor {

// Doc 08: every forge phase writes its output once and never touches an earlier phase's. A
// PhaseArtifact is that output: the value, the name of the phase that made it and a digest of the
// value. The value is held const, so nothing that holds an artifact (a later phase, a diff, an image
// dump) can change it, and copies of an artifact share the one value.
class PhaseArtifact {
public:
    template <class T>
    PhaseArtifact(std::string phase, T value, std::uint64_t digest)
        : phase_(std::move(phase))
        , digest_(digest)
        , type_(typeid(T))
        , value_(std::make_shared<const T>(std::move(value)))
    {
    }

    const std::string& phase() const { return phase_; }
    std::uint64_t digest() const { return digest_; }

    // Throws std::logic_error when the artifact is not a T.
    template <class T>
    const T& get() const
    {
        check(typeid(T));
        return *static_cast<const T*>(value_.get());
    }

private:
    void check(const std::type_info& wanted) const;

    std::string phase_;
    std::uint64_t digest_ = 0;
    std::type_index type_;
    std::shared_ptr<const void> value_;
};

// What a running phase can see: the artifacts of the phases before it, and nothing else.
class PhaseView {
public:
    explicit PhaseView(const std::vector<PhaseArtifact>& earlier)
        : earlier_(earlier)
    {
    }

    // Throws std::out_of_range when no earlier phase has that name.
    const PhaseArtifact& artifact(const std::string& phase) const;

    template <class T>
    const T& get(const std::string& phase) const
    {
        return artifact(phase).get<T>();
    }

private:
    const std::vector<PhaseArtifact>& earlier_;
};

struct Phase {
    std::string name;
    std::function<PhaseArtifact(const PhaseView&)> run;
};

// A phase that makes a T from the earlier artifacts; `digest` says what the T is, byte for byte.
template <class T, class Run, class Digest>
Phase makePhase(std::string name, Run run, Digest digest)
{
    return {name, [name, run = std::move(run), digest = std::move(digest)](const PhaseView& earlier) {
                T value = run(earlier);
                const std::uint64_t d = digest(static_cast<const T&>(value));
                return PhaseArtifact(name, std::move(value), d);
            }};
}

// The artifacts of one run, in phase order, with a chain digest that covers every phase up to and
// including each one, so two runs agree on chainDigest(i) exactly when they agree on phases 0 to i.
class PhaseRun {
public:
    const std::vector<PhaseArtifact>& artifacts() const { return artifacts_; }
    std::size_t size() const { return artifacts_.size(); }

    // Throws std::out_of_range when no phase has that name.
    const PhaseArtifact& artifact(const std::string& phase) const;

    template <class T>
    const T& get(const std::string& phase) const
    {
        return artifact(phase).get<T>();
    }

    // chainDigest(i) covers phases 0 to i; chainDigest() covers all of them (zero for an empty run).
    std::uint64_t chainDigest(std::size_t index) const;
    std::uint64_t chainDigest() const;

private:
    friend class PhasePipeline;
    std::vector<PhaseArtifact> artifacts_;
    std::vector<std::uint64_t> chain_;
};

// The first phase whose artifact differs between two runs, found by chain digest: a phase counts as
// different when its own digest differs or any phase before it did. Runs of different lengths or
// phase names differ at the first place they stop agreeing.
struct PhaseDivergence {
    std::size_t index = 0;
    std::string phase;           // the name in `a`, or in `b` when `a` ran out first
    std::uint64_t digestA = 0;   // zero when `a` has no such phase
    std::uint64_t digestB = 0;   // zero when `b` has no such phase
};

std::optional<PhaseDivergence> firstDivergence(const PhaseRun& a, const PhaseRun& b);

class PhasePipeline {
public:
    // Throws std::invalid_argument for an empty or repeated phase name.
    explicit PhasePipeline(std::vector<Phase> phases);

    const std::vector<Phase>& phases() const { return phases_; }
    std::size_t indexOf(const std::string& phase) const;

    PhaseRun run() const;

    // Runs phases `from` onward, taking phases 0 to from-1 as they are in `previous`. The caller
    // vouches that `previous` was made from the same inputs as the phases that follow; the result
    // is then what run() would have made. Throws std::invalid_argument when `previous` does not
    // hold the first `from` phases of this pipeline, by name, and std::out_of_range when `from`
    // is past the last phase.
    PhaseRun runFrom(const PhaseRun& previous, std::size_t from) const;
    PhaseRun runFrom(const PhaseRun& previous, const std::string& phase) const;

private:
    void finish(PhaseRun& result) const;

    std::vector<Phase> phases_;
};

}
