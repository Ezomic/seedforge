#include "ichor/audio/oneshot_scheduler.h"

#include "ichor/core/hash.h"

#include <algorithm>
#include <cmath>

namespace ichor {

namespace {

constexpr double kMaxCoordinate = 1.0e9;
constexpr double kMaxSeconds = 1.0e6;
// A voice-cap miss tries again soon, not a whole interval later.
constexpr double kRetrySeconds = 1.0;

bool sane(double v)
{
    return std::isfinite(v) && std::abs(v) <= kMaxCoordinate;
}

double seconds(double v, double fallback)
{
    return std::isfinite(v) ? std::clamp(v, 0.0, kMaxSeconds) : fallback;
}

double unitFrom(std::uint64_t seed, std::uint32_t id, std::uint32_t fires)
{
    std::uint64_t h = hashCombine(hashCombine(seed, id), fires);
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdull;
    h ^= h >> 33;
    return static_cast<double>(h >> 11) / 9007199254740992.0; // [0, 1)
}

}

OneShotScheduler::OneShotScheduler(std::uint64_t seed, std::size_t maxVoices)
    : seed_(seed)
    , maxVoices_(maxVoices)
{
}

float OneShotScheduler::gainAt(double distance, double radius)
{
    if (!std::isfinite(distance) || !std::isfinite(radius) || !(radius > 0.0) || distance >= radius) {
        return 0.0f;
    }
    const double t = 1.0 - std::max(distance, 0.0) / radius;
    return static_cast<float>(t * t);
}

float OneShotScheduler::panFor(const OneShotListener& l, double ex, double ez)
{
    if (!sane(l.x) || !sane(l.z) || !sane(l.forwardX) || !sane(l.forwardZ) || !sane(ex) || !sane(ez)) {
        return 0.0f;
    }
    const double fl = std::sqrt(l.forwardX * l.forwardX + l.forwardZ * l.forwardZ);
    const double dx = ex - l.x;
    const double dz = ez - l.z;
    const double dl = std::sqrt(dx * dx + dz * dz);
    if (!(fl > 1e-9) || !(dl > 1e-9)) {
        return 0.0f;
    }
    const double rightX = -l.forwardZ / fl;
    const double rightZ = l.forwardX / fl;
    const double pan = (rightX * dx + rightZ * dz) / dl;
    return static_cast<float>(std::clamp(pan, -1.0, 1.0));
}

OneShotScheduler::Timer& OneShotScheduler::timerFor(std::uint32_t id)
{
    const auto it = std::lower_bound(timers_.begin(), timers_.end(), id,
        [](const Timer& t, std::uint32_t key) { return t.id < key; });
    if (it != timers_.end() && it->id == id) {
        return *it;
    }
    Timer t;
    t.id = id;
    return *timers_.insert(it, t);
}

double OneShotScheduler::nextInterval(const OneShotEmitter& e, const Timer& t) const
{
    const double mean = seconds(e.meanIntervalSeconds, 30.0);
    const double floor = seconds(e.minIntervalSeconds, 0.0);
    // Uniform over 0.5..1.5 of the mean, so the average is the mean and it never clusters.
    const double jittered = mean * (0.5 + unitFrom(seed_, e.id, t.fires));
    return std::max(jittered, floor);
}

std::vector<OneShotCue> OneShotScheduler::step(double dtSeconds, const OneShotListener& listener,
    std::span<const OneShotEmitter> emitters)
{
    std::vector<OneShotCue> cues;
    const double dt = (std::isfinite(dtSeconds) && dtSeconds > 0.0) ? std::min(dtSeconds, kMaxStepSeconds) : 0.0;

    for (Voice& v : voices_) {
        v.remaining -= dt;
    }
    std::erase_if(voices_, [](const Voice& v) { return !(v.remaining > 0.0); });

    for (Timer& t : timers_) {
        t.seen = false;
        t.cooldown = std::max(t.cooldown - dt, 0.0);
    }

    if (!sane(listener.x) || !sane(listener.y) || !sane(listener.z)) {
        for (Timer& t : timers_) {
            t.armed = false;
        }
        return cues;
    }

    std::vector<const OneShotEmitter*> order;
    for (const OneShotEmitter& e : emitters) {
        if (sane(e.x) && sane(e.y) && sane(e.z) && std::isfinite(e.radius) && e.radius > 0.0) {
            order.push_back(&e);
        }
    }
    std::stable_sort(order.begin(), order.end(),
        [](const OneShotEmitter* a, const OneShotEmitter* b) { return a->id < b->id; });
    order.erase(std::unique(order.begin(), order.end(),
                    [](const OneShotEmitter* a, const OneShotEmitter* b) { return a->id == b->id; }),
        order.end());
    if (order.size() > kMaxEmitters) {
        order.resize(kMaxEmitters);
    }

    for (const OneShotEmitter* ep : order) {
        const OneShotEmitter& e = *ep;
        Timer& t = timerFor(e.id);
        if (t.seen) {
            continue;
        }
        t.seen = true;

        const double distance = std::sqrt((e.x - listener.x) * (e.x - listener.x)
            + (e.y - listener.y) * (e.y - listener.y) + (e.z - listener.z) * (e.z - listener.z));
        if (!(distance < e.radius)) {
            t.armed = false;
            continue;
        }
        if (!t.armed) {
            // Entering the radius starts a fresh wait, so a sound never fires on the doorstep.
            t.armed = true;
            t.untilFire = nextInterval(e, t);
            continue;
        }
        t.untilFire -= dt;
        if (t.untilFire > 0.0 || t.cooldown > 0.0) {
            continue;
        }
        if (voices_.size() >= maxVoices_) {
            t.untilFire = kRetrySeconds;
            continue;
        }
        const float base = std::isfinite(e.baseGain) ? e.baseGain : 0.0f;
        const float gain = std::clamp(gainAt(distance, e.radius) * base, 0.0f, 1.0f);
        ++t.fires;
        t.untilFire = nextInterval(e, t);
        if (!(gain > 0.0f)) {
            continue;
        }
        const double duration = seconds(e.durationSeconds, 0.0);
        t.cooldown = seconds(e.minIntervalSeconds, 0.0);
        voices_.push_back(Voice{duration});
        cues.push_back(OneShotCue{e.id, panFor(listener, e.x, e.z), gain, duration});
    }

    std::erase_if(timers_, [](const Timer& t) { return !t.seen; });
    return cues;
}

}
