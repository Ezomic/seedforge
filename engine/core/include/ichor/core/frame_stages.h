#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// Wall-clock cost of the named stages of a frame, for the overlay, the --frame-stats log and the frame budget
// tests. Presentation only: it never reaches the simulation or a hash.
class FrameStages {
public:
    struct Stage {
        std::string name;
        double lastMs = 0.0;   // the most recent sample
        double meanMs = 0.0;   // smoothed over the last few dozen samples
        double peakMs = 0.0;   // the worst sample since reset
        double totalMs = 0.0;  // all samples since reset
        std::uint64_t samples = 0;
    };

    // Adds one sample to a stage; bad values (negative, NaN) count as zero.
    void record(std::string_view name, double ms)
    {
        if (!std::isfinite(ms) || ms < 0.0) {
            ms = 0.0;
        }
        Stage& stage = stages_[find(name)];
        stage.lastMs = ms;
        stage.meanMs = stage.samples == 0 ? ms : stage.meanMs * 0.95 + ms * 0.05;
        stage.peakMs = std::max(stage.peakMs, ms);
        stage.totalMs += ms;
        ++stage.samples;
    }

    // Adds to the current frame's sample for a stage that runs more than once a frame (cell uploads, ticks).
    void accumulate(std::string_view name, double ms)
    {
        const std::size_t index = find(name);
        accumulated_[index] = true;
        pending_.push_back({index, ms});
    }

    // Closes a frame: every accumulated stage becomes one sample, and stages that did not run get a zero.
    void endFrame()
    {
        std::vector<double> sums(stages_.size(), 0.0);
        for (const auto& [index, ms] : pending_) {
            sums[index] += ms;
        }
        for (std::size_t i = 0; i < accumulated_.size(); ++i) {
            if (accumulated_[i]) {
                record(stages_[i].name, sums[i]);
            }
        }
        pending_.clear();
    }

    void reset()
    {
        stages_.clear();
        pending_.clear();
        accumulated_.clear();
    }

    const std::vector<Stage>& stages() const { return stages_; }

    const Stage* get(std::string_view name) const
    {
        for (const Stage& stage : stages_) {
            if (stage.name == name) return &stage;
        }
        return nullptr;
    }

    std::string summary() const
    {
        std::string text;
        for (const Stage& stage : stages_) {
            text += std::format("{} {:.1f}/{:.1f}  ", stage.name, stage.meanMs, stage.peakMs);
        }
        return text;
    }

    class Scope {
    public:
        Scope(FrameStages& stages, std::string_view name, bool accumulate = false)
            : stages_(stages), name_(name), accumulate_(accumulate), start_(std::chrono::steady_clock::now())
        {
        }
        ~Scope()
        {
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_).count();
            if (accumulate_) {
                stages_.accumulate(name_, ms);
            } else {
                stages_.record(name_, ms);
            }
        }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

    private:
        FrameStages& stages_;
        std::string_view name_;
        bool accumulate_;
        std::chrono::steady_clock::time_point start_;
    };

private:
    std::size_t find(std::string_view name)
    {
        for (std::size_t i = 0; i < stages_.size(); ++i) {
            if (stages_[i].name == name) return i;
        }
        stages_.push_back({std::string(name)});
        accumulated_.push_back(false);
        return stages_.size() - 1;
    }

    std::vector<Stage> stages_;
    std::vector<std::pair<std::size_t, double>> pending_;
    std::vector<bool> accumulated_;
};

}
