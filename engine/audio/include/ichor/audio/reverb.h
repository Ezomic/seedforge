#pragma once

#include <glm/vec3.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace ichor {

class DataSet;

struct ReverbSettings {
    float wet = 0.0f;      // 0 is dry; 1 is a very wet room
    float roomSize = 0.5f; // 0 small and quick to die, 1 a long cavern
    float damping = 0.5f;  // 0 bright, 1 muffled tail

    bool operator==(const ReverbSettings&) const = default;
};

// A stereo Freeverb: eight damped feedback combs and four allpass filters per
// channel. Pure arithmetic on floats, so rendering it to a buffer is exactly
// repeatable. Changes to the settings ramp in over a few milliseconds.
class Reverb {
public:
    static constexpr std::uint32_t kRate = 48000;

    Reverb();

    void setSettings(const ReverbSettings& settings);
    const ReverbSettings& settings() const { return target_; }

    // Adds the reverb of the stereo signal in place. At zero wet, settled, the
    // buffer is left untouched and the tail is dropped.
    void process(float* interleaved, std::uint32_t frames);


private:
    static constexpr int kCombs = 8;
    static constexpr int kAllpasses = 4;

    struct Comb {
        std::vector<float> buffer;
        std::size_t position = 0;
        float store = 0.0f;
    };
    struct Allpass {
        std::vector<float> buffer;
        std::size_t position = 0;
    };
    struct Channel {
        std::array<Comb, kCombs> combs;
        std::array<Allpass, kAllpasses> allpasses;
    };

    float tick(Channel& channel, float input) const;

    std::array<Channel, 2> channels_;
    ReverbSettings target_;
    ReverbSettings current_;
    bool dirty_ = false; // the buffers hold something since they were last cleared
};

// A sphere of space with its own reverb: a cave mouth, a temple, a canyon.
struct ReverbStamp {
    glm::dvec3 centre{0.0};
    float radius = 20.0f; // full effect inside the inner 60 percent, fading to nothing at the edge
    ReverbSettings settings;
};

// The stamps of a world, blended around a point.
class ReverbField {
public:
    void add(const ReverbStamp& stamp) { stamps_.push_back(stamp); }
    void clear() { stamps_.clear(); }
    std::size_t size() const { return stamps_.size(); }

    // Dry outside every stamp. Where stamps overlap the settings are averaged
    // by how deep inside each the point is.
    ReverbSettings at(glm::dvec3 position) const;

private:
    std::vector<ReverbStamp> stamps_;
};

// Stamps from the data layer's "reverb" table: x, y, z, radius, wet, room and
// damping, all numbers. Returns false, leaving the field alone, when a record
// lacks one or has a nonsense value.
bool loadReverbStamps(const DataSet& data, ReverbField& field);

}
