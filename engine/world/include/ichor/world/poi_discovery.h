#pragma once

#include "ichor/progression/discovery_xp.h"
#include "ichor/sim/scene.h"
#include "ichor/world/forged_game.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ichor {

// Discovery XP for walking into a place (docs/07-progression.md). The first time the player comes within reach of a
// settlement, dungeon or landmark the forged world placed, the place is scored by the generator (its sightline score for
// a dungeon or landmark, a fixed middling score for a settlement, which has none) and paid through DiscoveryLedger.
//
// What is saved is one marker entity per found place, "poi_discovered:<poi id>", with the stable id of its order of
// finding. They use only the scene's own components, so a scene that never finds a place hashes and saves as before. The
// ledger, and so the XP and the repeat decay, is replayed from the markers in that order on every query, never stored.
//
// Nothing here is thread safe: one owner, the game thread.

namespace poi_discovery {
inline constexpr const char* kMarkerPrefix = "poi_discovered:";
inline constexpr double kSettlementScore = 0.5;
}

struct PoiDiscoveryParams {
    double dungeonReachM = 10.0;
    double landmarkReachM = 30.0;
    double settlementReachM = 60.0;
    double noticeSeconds = 4.0;
};

struct PoiDiscoveryEntry {
    std::string poiId;
    std::int64_t xp = 0;
};

class PoiDiscovery {
public:
    // The game must outlive this.
    explicit PoiDiscovery(const ForgedGame& game, PoiDiscoveryParams params = {});

    // Adds the "poi_discovery" system. The scene must not outlive this.
    void install(Scene& scene);

    // The places found, in the order found, with what each paid.
    std::vector<PoiDiscoveryEntry> found(const Scene& scene) const;
    std::int64_t totalXp(const Scene& scene) const;
    // A short line about the latest grant while it is fresh, else empty. A look: not saved.
    std::string notice(const Scene& scene) const;

private:
    void step(Scene& scene);
    double reachOf(PlacedPoiKind kind) const;

    const ForgedGame& game_;
    PoiDiscoveryParams params_;
    std::string notice_;
    double noticeAt_ = -1.0e9;
};

}
