#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <vector>

namespace ichor {

// The resident texture budget (SEED-690): how many bytes of texture, mips included, a renderer keeps on the device.
// The mip tail that stays resident is defined next to the texture sizes, in ichor/assets/bundle.h (mipTailStart).
constexpr std::uint64_t kDefaultTextureBudgetBytes = 512ull << 20;
// A texture whose full image was evicted comes back the frame it is next drawn, this many per frame at most, so a
// budget smaller than the frame's working set degrades a few textures at a time instead of stalling on all of them.
constexpr std::uint32_t kTextureReloadsPerFrame = 2;
struct TextureResidencyStats {
    std::uint64_t usedBytes = 0; // what is resident now: full images plus the tails of evicted ones
    std::uint64_t capBytes = 0;
    std::uint64_t evictions = 0; // full images dropped to their tail since the start
    std::uint64_t reloads = 0;   // tails brought back to the full image since the start
    std::uint32_t textures = 0;
    std::uint32_t evicted = 0; // textures resident as their tail only, right now
    double evictionsPerSecond = 0.0; // over the last whole second of frames
};

// Decides which textures are resident in full and which only as their mip tail. It knows sizes and use, not images:
// the renderers upload and drop what it says to. Least recently drawn goes first; a texture whose tail is its whole
// image is never evicted, since there is nothing to gain.
class TextureResidency {
public:
    static constexpr std::uint32_t kNone = std::numeric_limits<std::uint32_t>::max();
    static constexpr std::uint64_t kNeverReloaded = std::numeric_limits<std::uint64_t>::max();

    explicit TextureResidency(std::uint64_t capBytes = kDefaultTextureBudgetBytes)
        : cap_(capBytes)
    {
    }

    std::uint64_t cap() const { return cap_; }
    // Takes effect at the next add or frame; nothing is evicted here.
    void setCap(std::uint64_t capBytes) { cap_ = capBytes; }

    // Registers a texture that has just been uploaded in full, and names the textures to drop to their tail to
    // stay under the cap. Returns the new texture's id.
    std::uint32_t add(std::uint64_t fullBytes, std::uint64_t tailBytes, std::vector<std::uint32_t>& evicted)
    {
        const auto id = static_cast<std::uint32_t>(entries_.size());
        entries_.push_back({fullBytes, std::min(tailBytes, fullBytes), true, frame_, kNeverReloaded});
        used_ += fullBytes;
        evictToFit(id, evicted);
        return id;
    }

    // Starts a frame: the reload allowance is fresh, and `seconds` since the last frame feeds the eviction rate.
    void beginFrame(double seconds)
    {
        ++frame_;
        reloadsThisFrame_ = 0;
        windowSeconds_ += std::max(0.0, seconds);
        if (windowSeconds_ >= 1.0) {
            evictionsPerSecond_ = static_cast<double>(windowEvictions_) / windowSeconds_;
            windowSeconds_ = 0.0;
            windowEvictions_ = 0;
        }
    }

    // Measures the time itself, for the renderers.
    void beginFrame()
    {
        const auto now = std::chrono::steady_clock::now();
        const double seconds = lastFrameTime_ == std::chrono::steady_clock::time_point{} ? 0.0 : std::chrono::duration<double>(now - lastFrameTime_).count();
        lastFrameTime_ = now;
        beginFrame(seconds);
    }

    // The texture is drawn this frame. True when its full image is to be uploaded again now, which happens only
    // when it was evicted and this frame's reload allowance is not spent; `evicted` then names what makes room.
    bool touch(std::uint32_t id, std::vector<std::uint32_t>& evicted)
    {
        if (id >= entries_.size()) {
            return false;
        }
        Entry& entry = entries_[id];
        entry.lastUsed = frame_;
        if (entry.full || reloadsThisFrame_ >= kTextureReloadsPerFrame) {
            return false;
        }
        entry.full = true;
        entry.reloaded = frame_;
        used_ += entry.fullBytes - entry.tailBytes;
        ++reloadsThisFrame_;
        ++reloads_;
        evictToFit(id, evicted);
        return true;
    }

    // Forgets the textures from `count` on, as a failed upload drops them.
    void truncate(std::size_t count)
    {
        while (entries_.size() > count) {
            const Entry& entry = entries_.back();
            used_ -= entry.full ? entry.fullBytes : entry.tailBytes;
            entries_.pop_back();
        }
    }

    bool isFull(std::uint32_t id) const { return id < entries_.size() && entries_[id].full; }
    std::uint64_t usedBytes() const { return used_; }

    TextureResidencyStats stats() const
    {
        TextureResidencyStats stats;
        stats.usedBytes = used_;
        stats.capBytes = cap_;
        stats.evictions = evictions_;
        stats.reloads = reloads_;
        stats.textures = static_cast<std::uint32_t>(entries_.size());
        stats.evicted = static_cast<std::uint32_t>(std::count_if(entries_.begin(), entries_.end(), [](const Entry& entry) { return !entry.full; }));
        stats.evictionsPerSecond = evictionsPerSecond_;
        return stats;
    }

private:
    struct Entry {
        std::uint64_t fullBytes = 0;
        std::uint64_t tailBytes = 0;
        bool full = true;
        std::uint64_t lastUsed = 0;
        std::uint64_t reloaded = kNeverReloaded; // the frame it last came back in full; never evicted again in that frame
    };

    // Drops least recently used full images until under the cap. `keep` and anything reloaded this frame stay.
    void evictToFit(std::uint32_t keep, std::vector<std::uint32_t>& evicted)
    {
        while (used_ > cap_) {
            std::uint32_t victim = kNone;
            for (std::uint32_t id = 0; id < entries_.size(); ++id) {
                const Entry& entry = entries_[id];
                if (id == keep || !entry.full || entry.tailBytes >= entry.fullBytes || entry.reloaded == frame_) {
                    continue;
                }
                if (victim == kNone || entry.lastUsed < entries_[victim].lastUsed) {
                    victim = id;
                }
            }
            if (victim == kNone) {
                return;
            }
            Entry& entry = entries_[victim];
            entry.full = false;
            used_ -= entry.fullBytes - entry.tailBytes;
            ++evictions_;
            ++windowEvictions_;
            evicted.push_back(victim);
        }
    }

    std::uint64_t cap_;
    std::vector<Entry> entries_;
    std::uint64_t used_ = 0;
    std::uint64_t frame_ = 0;
    std::uint32_t reloadsThisFrame_ = 0;
    std::uint64_t evictions_ = 0;
    std::uint64_t reloads_ = 0;
    std::uint64_t windowEvictions_ = 0;
    double windowSeconds_ = 0.0;
    double evictionsPerSecond_ = 0.0;
    std::chrono::steady_clock::time_point lastFrameTime_{};
};

}
