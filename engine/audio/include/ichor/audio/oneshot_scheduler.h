#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace ichor {

// Where a one-shot can be heard from: a cliff that cracks, a ruin where wings beat. It sounds
// only while the listener is inside `radius`, at most once per `minIntervalSeconds`, and on
// average once per `meanIntervalSeconds`.
struct OneShotEmitter {
    std::uint32_t id = 0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double radius = 0.0;
    double meanIntervalSeconds = 30.0;
    double minIntervalSeconds = 10.0;
    double durationSeconds = 2.0; // how long the voice it takes stays taken
    float baseGain = 1.0f;
};

// The listener's place and the way they face on the ground plane (a vector in x and z, any
// length). Looking down -z, "right" is +x.
struct OneShotListener {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double forwardX = 0.0;
    double forwardZ = -1.0;
};

struct OneShotCue {
    std::uint32_t emitterId = 0;
    float pan = 0.0f;  // -1 hard left .. +1 hard right
    float gain = 0.0f; // 0..1, falls to nothing at the radius
    double durationSeconds = 0.0;
};

// Decides which one-shots fire. Pure logic with no audio device: it is fed the listener, the
// emitters near them and the elapsed time once per tick, and returns the cues to play, in
// emitter id order. The same seed and the same inputs give the same cues on every platform,
// and nothing here is part of the simulation's state or its hash.
class OneShotScheduler {
public:
    static constexpr std::size_t kMaxEmitters = 256;
    // A tick longer than this is a pause or a load, not time that passed in the world.
    static constexpr double kMaxStepSeconds = 1.0;

    explicit OneShotScheduler(std::uint64_t seed, std::size_t maxVoices = 4);

    // Emitters beyond kMaxEmitters, with a bad radius or a non-finite position, are ignored,
    // and so is a repeated id. An emitter missing from a call forgets its timer, so pass the
    // ones near the listener every tick.
    std::vector<OneShotCue> step(double dtSeconds, const OneShotListener& listener,
        std::span<const OneShotEmitter> emitters);

    std::size_t activeVoices() const { return voices_.size(); }

    static float gainAt(double distance, double radius);
    static float panFor(const OneShotListener& listener, double emitterX, double emitterZ);

private:
    struct Timer {
        std::uint32_t id = 0;
        std::uint32_t fires = 0;
        double untilFire = 0.0;
        double cooldown = 0.0;
        bool armed = false;
        bool seen = false;
    };
    struct Voice {
        double remaining = 0.0;
    };

    Timer& timerFor(std::uint32_t id);
    double nextInterval(const OneShotEmitter& e, const Timer& t) const;

    std::uint64_t seed_;
    std::size_t maxVoices_;
    std::vector<Timer> timers_; // sorted by id
    std::vector<Voice> voices_;
};

}
