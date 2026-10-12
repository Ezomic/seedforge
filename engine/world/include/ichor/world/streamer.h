#pragma once

#include "ichor/assets/bundle.h"
#include "ichor/world/heightfield.h"

#include <glm/vec3.hpp>

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

namespace ichor {

// What a loaded cell carries: its height samples for collision and one mesh
// per level of detail for drawing, all built off the game thread.
struct CellPayload {
    Heightfield::Cell cell;
    std::vector<Mesh> lods; // 0 is full resolution
};

struct StreamerConfig {
    std::int32_t radiusCells = 2;     // resident ring around the player's cell
    std::int32_t requiredRadius = 1;  // the simulation waits for these
    std::int32_t hysteresisCells = 1; // unload only beyond radius plus this
    std::uint32_t activationsPerUpdate = 2;
    bool synchronous = false; // build on the calling thread; tests use this
    std::uint32_t lodCount = 3;
};

// Keeps the cells around the player resident, building them on a worker
// thread and applying the results on the game thread under a budget.
// Residency is a pure function of the player's path and the budget, so a
// replay sees the same cells on the same ticks.
class WorldStreamer {
public:
    WorldStreamer(const Heightfield& heightfield, StreamerConfig config);
    ~WorldStreamer();
    WorldStreamer(const WorldStreamer&) = delete;
    WorldStreamer& operator=(const WorldStreamer&) = delete;

    using Loaded = std::function<void(const CellPayload&)>;
    using Unloaded = std::function<void(CellId)>;
    void onLoaded(Loaded callback) { loaded_ = std::move(callback); }
    void onUnloaded(Unloaded callback) { unloaded_ = std::move(callback); }

    // Call once per tick with the anchor position: queues, applies finished
    // loads under the budget, drops cells that fell out of the ring.
    void update(glm::dvec3 anchor);

    // True when every cell within requiredRadius of the anchor is resident.
    bool ready(glm::dvec3 anchor) const;

    const std::set<CellId>& resident() const { return resident_; }
    std::uint64_t loads() const { return loads_; }
    std::uint64_t unloads() const { return unloads_; }
    std::size_t pending() const;

    // Drops everything, synchronously.
    void clear();

    static void buildPayload(const Heightfield& heightfield, CellId id, std::uint32_t lodCount, CellPayload& out);

private:
    void worker();
    void request(CellId id);

    const Heightfield& heightfield_;
    StreamerConfig config_;
    Loaded loaded_;
    Unloaded unloaded_;
    std::set<CellId> resident_;
    std::set<CellId> requested_;
    std::uint64_t loads_ = 0;
    std::uint64_t unloads_ = 0;

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<CellId> queue_;
    std::map<CellId, std::unique_ptr<CellPayload>> finished_;
    std::uint64_t generation_ = 0; // bumped by clear(); builds from before it are dropped
    bool stop_ = false;
    std::thread thread_;
};

}
