#pragma once

#include "ichor/gfx/render_view.h"
#include "ichor/sim/scene.h"
#include "ichor/world/dungeon_greybox.h"
#include "ichor/world/forged_game.h"

#include <glm/vec3.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace ichor {

// Walking into a dungeon from the forged world and back (doc 09). A dungeon entrance is a placed dungeon POI; standing
// within reach of it offers a prompt, and Confirm starts a short fade, swaps the player into the baked layout's grey-box
// and fades back in. The same on the way out, at the exit marker in the entry room, which returns the player to the exact
// spot they left from.
//
// The dungeon is built far outside the world (the "pocket"), so no world coordinate can mix with it and the one scene,
// its clock, its time of day and everything on the player carry through untouched. The dungeon's boxes are derived data,
// like terrain: they are made from the baked layout on entry and again after a load, and never saved. What is saved is one
// marker entity, "dungeon_visit:<poi id>", standing where the player left the world. It uses only the scene's own
// components, so a scene that never enters a dungeon hashes and saves exactly as it did before, and a save made inside
// restores inside. A marker naming a dungeon that cannot be baked sends the player back to the world.
//
// Nothing here is thread safe: one owner, the game thread, as ForgedGame::dungeons.

namespace dungeon_entry {
inline constexpr double kPocketX = -200000.0;      // the dungeon's origin, x and z
inline constexpr double kPocketLimitX = -100000.0; // x below this is the pocket
inline constexpr const char* kMarkerPrefix = "dungeon_visit:";
}

inline bool inDungeonPocket(const glm::dvec3& position) { return position.x < dungeon_entry::kPocketLimitX; }

struct DungeonEntryParams {
    double reachM = 6.0;                 // how close to a dungeon POI the entrance prompt appears
    double exitReachM = 2.5;             // how close to the exit marker
    std::uint32_t fadeOutTicks = 12;
    std::uint32_t fadeInTicks = 18;
    bool autoEnter = false;              // as if Confirm were pressed, once, the first time an entrance is in reach
};

// The dungeon POI nearest (x, z) within reach, else null. Ties go to the lower id.
const PlacedPoi* nearestDungeonEntrance(const ForgedGame& game, double x, double z, double reachM);

// Moves the spawn to dry, gentle ground just outside the entrance of dungeon `index` (in the POI set's order, counting
// only dungeons) and aims the view at it. Returns false, leaving the spawn alone, for an index past the end or when
// the nearest such ground is out of the entrance's reach.
bool spawnAtDungeon(ForgedGame& game, std::size_t index, double reachM = DungeonEntryParams{}.reachM);

class DungeonEntry {
public:
    // `kitFirstMesh` is the renderer's index of the first mesh of buildDungeonKit() once uploaded. The game must outlive this.
    DungeonEntry(ForgedGame& game, std::uint32_t kitFirstMesh, DungeonEntryParams params = {});

    // Adds the "dungeon_entry" system. The scene must not outlive this.
    void install(Scene& scene);

    bool inside(const Scene& scene) const;
    // The line to show the player: the entrance or exit prompt, else empty.
    std::string prompt(const Scene& scene) const;
    // 0 clear to 1 black. A look, not state: it is not saved. A load during a fade drops the fade and its swap, and the
    // player presses Enter again.
    float fade(const Scene& scene) const;
    // Set when the player was just placed in a dungeon: the yaw to face. Taken once.
    std::optional<double> takeViewYaw();
    // The dungeon's boxes as instances; nothing outside a dungeon.
    void appendInstances(RenderView& view) const;
    // The boxes of the dungeon the player is in now (for tests and the view); empty outside.
    const DungeonGreyBox& greyBox() const { return current_; }

private:
    enum class Phase : std::uint8_t { Idle, FadingOut, FadingIn };

    void step(Scene& scene, const InputFrame& input);
    bool advance(Scene& scene);
    void enter(Scene& scene, const PlacedPoi& poi);
    void leave(Scene& scene);
    bool buildGeometry(Scene& scene, const std::string& poiId);
    void removeGeometry(Scene& scene);

    ForgedGame& game_;
    std::uint32_t kitFirstMesh_;
    DungeonEntryParams params_;
    DungeonGreyBox current_;
    std::size_t boxes_ = 0;
    Phase phase_ = Phase::Idle;
    bool leaving_ = false;          // the swap the fade is waiting for: out of the dungeon rather than into one
    std::uint64_t phaseTick_ = 0;
    std::string pendingPoi_;
    bool confirmHeld_ = false;
    bool autoSpent_ = false;
    std::optional<double> viewYaw_;
};

// The dungeon's look in place of the world's: no sky, no weather, a dark hall lit from within.
void applyDungeonInterior(RenderView& view);
// Scales the picture towards black by `fade` (0 clear, 1 black).
void applyFade(RenderView& view, float fade);

}
