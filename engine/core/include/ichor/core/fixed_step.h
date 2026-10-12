#pragma once

#include <algorithm>

namespace ichor {

// Simulation ticks at a fixed rate; rendering interpolates with alpha().
// maxSteps caps the catch-up after a stall so a hitch cannot spiral.
class FixedStep {
public:
    explicit FixedStep(double dt, int maxSteps = 8)
        : dt_(dt), maxSteps_(maxSteps)
    {
    }

    // Returns how many fixed steps the caller must run for this frame.
    int advance(double frameSeconds)
    {
        accumulator_ += std::max(0.0, frameSeconds);
        int steps = 0;
        while (accumulator_ >= dt_ && steps < maxSteps_) {
            accumulator_ -= dt_;
            ++steps;
        }
        if (steps == maxSteps_ && accumulator_ >= dt_) {
            accumulator_ = 0.0;
        }
        return steps;
    }

    double dt() const { return dt_; }
    double alpha() const { return accumulator_ / dt_; }

private:
    double dt_;
    int maxSteps_;
    double accumulator_ = 0.0;
};

}
