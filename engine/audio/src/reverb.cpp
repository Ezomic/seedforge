#include "ichor/audio/reverb.h"

#include "ichor/data/data_set.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace ichor {

namespace {

// The classic Freeverb tunings, written for 44.1 kHz and scaled to the mixer's rate.
constexpr std::array<int, 8> kCombTuning = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
constexpr std::array<int, 4> kAllpassTuning = {556, 441, 341, 225};
constexpr int kStereoSpread = 23;
constexpr float kInputGain = 0.015f;
constexpr float kWetScale = 3.0f;
constexpr float kRampPerFrame = 1.0f / 960.0f; // about 20 ms from dry to full

std::size_t scaled(int samples)
{
    return static_cast<std::size_t>(std::lround(static_cast<double>(samples) * static_cast<double>(Reverb::kRate) / 44100.0));
}

float smooth(float edge0, float edge1, float x)
{
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float approach(float value, float target)
{
    return value < target ? std::min(value + kRampPerFrame, target) : std::max(value - kRampPerFrame, target);
}

}

Reverb::Reverb()
{
    for (std::size_t c = 0; c < channels_.size(); ++c) {
        const int spread = c == 0 ? 0 : kStereoSpread;
        for (int i = 0; i < kCombs; ++i) {
            channels_[c].combs[static_cast<std::size_t>(i)].buffer.assign(scaled(kCombTuning[static_cast<std::size_t>(i)] + spread), 0.0f);
        }
        for (int i = 0; i < kAllpasses; ++i) {
            channels_[c].allpasses[static_cast<std::size_t>(i)].buffer.assign(scaled(kAllpassTuning[static_cast<std::size_t>(i)] + spread), 0.0f);
        }
    }
}

void Reverb::setSettings(const ReverbSettings& settings)
{
    target_.wet = std::clamp(settings.wet, 0.0f, 1.0f);
    target_.roomSize = std::clamp(settings.roomSize, 0.0f, 1.0f);
    target_.damping = std::clamp(settings.damping, 0.0f, 1.0f);
}

float Reverb::tick(Channel& channel, float input) const
{
    const float feedback = current_.roomSize * 0.28f + 0.7f;
    const float damp = current_.damping * 0.4f;
    float out = 0.0f;
    for (Comb& comb : channel.combs) {
        const float held = comb.buffer[comb.position];
        comb.store = held * (1.0f - damp) + comb.store * damp;
        comb.buffer[comb.position] = input + comb.store * feedback;
        comb.position = comb.position + 1 == comb.buffer.size() ? 0 : comb.position + 1;
        out += held;
    }
    for (Allpass& allpass : channel.allpasses) {
        const float held = allpass.buffer[allpass.position];
        allpass.buffer[allpass.position] = out + held * 0.5f;
        allpass.position = allpass.position + 1 == allpass.buffer.size() ? 0 : allpass.position + 1;
        out = -out + held;
    }
    return out;
}

void Reverb::process(float* interleaved, std::uint32_t frames)
{
    if (current_.wet == 0.0f && target_.wet == 0.0f) {
        // Dry and settled: leave the buffer alone, and let the tail go so the next room starts clean.
        for (Channel& channel : channels_) {
            for (Comb& comb : channel.combs) {
                std::fill(comb.buffer.begin(), comb.buffer.end(), 0.0f);
                comb.store = 0.0f;
            }
            for (Allpass& allpass : channel.allpasses) {
                std::fill(allpass.buffer.begin(), allpass.buffer.end(), 0.0f);
            }
        }
        current_ = target_;
        return;
    }
    for (std::uint32_t f = 0; f < frames; ++f) {
        current_.wet = approach(current_.wet, target_.wet);
        current_.roomSize = approach(current_.roomSize, target_.roomSize);
        current_.damping = approach(current_.damping, target_.damping);
        float& left = interleaved[f * 2];
        float& right = interleaved[f * 2 + 1];
        const float input = (left + right) * kInputGain;
        const float wetLeft = tick(channels_[0], input);
        const float wetRight = tick(channels_[1], input);
        left += wetLeft * current_.wet * kWetScale;
        right += wetRight * current_.wet * kWetScale;
    }
}

ReverbSettings ReverbField::at(glm::dvec3 position) const
{
    float total = 0.0f;
    ReverbSettings sum{0.0f, 0.0f, 0.0f};
    for (const ReverbStamp& stamp : stamps_) {
        const auto distance = static_cast<float>(glm::length(position - stamp.centre));
        const float weight = 1.0f - smooth(stamp.radius * 0.6f, stamp.radius, distance);
        if (weight <= 0.0f) {
            continue;
        }
        total += weight;
        sum.wet += weight * stamp.settings.wet;
        sum.roomSize += weight * stamp.settings.roomSize;
        sum.damping += weight * stamp.settings.damping;
    }
    if (total <= 1e-6f) {
        return {};
    }
    // Overlap averages the rooms; wet only ever fades in, so a weak edge is quiet, not loud.
    return {sum.wet / std::max(1.0f, total), sum.roomSize / total, sum.damping / total};
}

bool loadReverbStamps(const DataSet& data, ReverbField& field)
{
    const DataTable* table = data.table("reverb");
    if (table == nullptr) {
        return true;
    }
    std::vector<ReverbStamp> loaded;
    for (const auto& [id, record] : table->records) {
        const auto number = [&](const char* name, double& out) {
            const auto found = record.fields.find(name);
            if (found == record.fields.end() || !std::holds_alternative<double>(found->second)) {
                return false;
            }
            out = std::get<double>(found->second);
            return std::isfinite(out);
        };
        double x = 0, y = 0, z = 0, radius = 0, wet = 0, room = 0, damping = 0;
        if (!number("x", x) || !number("y", y) || !number("z", z) || !number("radius", radius) || !number("wet", wet) || !number("room", room) || !number("damping", damping)) {
            return false;
        }
        if (radius <= 0.0 || wet < 0.0 || wet > 1.0 || room < 0.0 || room > 1.0 || damping < 0.0 || damping > 1.0) {
            return false;
        }
        ReverbStamp stamp;
        stamp.centre = {x, y, z};
        stamp.radius = static_cast<float>(radius);
        stamp.settings = {static_cast<float>(wet), static_cast<float>(room), static_cast<float>(damping)};
        loaded.push_back(stamp);
    }
    for (const ReverbStamp& stamp : loaded) {
        field.add(stamp);
    }
    return true;
}

}
