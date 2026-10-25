#pragma once

#include "ichor/combat_bridge/combat_bridge.h"
#include "ichor/gfx/render_view.h"
#include "ichor/sim/scene.h"
#include "ichor/world/dungeon_entry.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ichor {

struct DungeonFightConfig {
    double wakeDelaySeconds = 1.5; // from one kill to the next enemy of the room waking
};

// The enemies a baked dungeon holds (SEED-648), fought through the combat bridge. Stage0 fights one foe at a time, so a
// room's enemies take it in turns: the player stepping into a room wakes its first living enemy, the others stand at
// their posts until their turn, and a kill is recorded in the scene (recordKill) so it is saved with the campaign. An
// enemy whose player leaves the room goes back to its post unhurt. Nothing here ever shuts a door: the exit stays open
// whatever is alive.
//
// While the player is in a dungeon the bridge's one foe belongs to the dungeon, parked out of sight when nobody is
// engaged, and the player's death sends them back to the dungeon's entry. On leaving, the foe returns to its world post.
// The dungeon entry must be installed on the scene before this, so the content of the hall exists when it is read.
class DungeonFight {
public:
    // Both must outlive this, and the scene it is installed in.
    DungeonFight(CombatBridge& bridge, const DungeonEntry& entry, DungeonFightConfig config = {});
    DungeonFight(const DungeonFight&) = delete;
    DungeonFight& operator=(const DungeonFight&) = delete;

    void install(Scene& scene, StableId player);

    bool inDungeon() const { return here_; }
    // The enemy fighting the player, as an index into the entry's content().enemies.
    std::optional<std::size_t> engaged() const;
    // The enemies waiting at their posts, drawn as the Bull-Warden at rest. Nothing without the art or outside a dungeon.
    void appendView(const Scene& scene, RenderView& view) const;
    void overlay(const Scene& scene, std::vector<std::string>& lines) const;

private:
    struct Key {
        graph::NodeId room = 0;
        std::uint32_t slot = 0;
    };

    void step(Scene& scene);
    void arrive(Scene& scene);
    void leave();
    void engage(Scene& scene, const DungeonEnemy& enemy);
    void standDown(Scene& scene);
    const DungeonEnemy* find(const Key& key) const;
    glm::dvec3 entryCentre() const;
    glm::dvec3 parkingSpot() const;

    CombatBridge& bridge_;
    const DungeonEntry& entry_;
    DungeonFightConfig config_;
    StableId player_{};
    bool here_ = false;
    std::string poi_;
    std::optional<Key> engaged_;
    double wakeAt_ = 0.0;
};

}
