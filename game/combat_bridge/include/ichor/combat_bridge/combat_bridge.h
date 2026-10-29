#pragma once

#include "ichor/combat_bridge/combat_presentation.h"
#include "ichor/combat_bridge/combat_site.h"
#include "ichor/core/input.h"
#include "ichor/gfx/render_view.h"
#include "ichor/gfx/renderer.h"
#include "ichor/sim/scene.h"
#include "ichor/stage0/bull_warden.h"
#include "ichor/stage0/camera_rig.h"
#include "ichor/stage0/combat.h"
#include "ichor/stage0/exile_player.h"
#include "ichor/stage0/fight_bot.h"
#include "ichor/stage0/run_prompt.h"
#include "ichor/world/forged_walking.h"

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

struct CombatBridgeConfig {
    stage0::Tuning tuning;
    // Which role the foe plays (SEED-661). Anchor and Artillery are drawn with the Bull-Warden at another size.
    stage0::FoeRole role = stage0::FoeRole::Skirmisher;
    stage0::AnchorTuning anchor;
    stage0::ArtilleryTuning artillery;
    double fixedDt = 1.0 / 60.0;
    double respawnDelaySeconds = 3.0; // from the player's death to standing at the last settlement again
    // World-axis steering with the player always facing the foe: what a scripted player (FightBot) needs.
    // Off is the free look a person plays with.
    bool legacyControls = false;
    // Where every finished fight is written as a Stage 0 fights.csv row (plus world_seed and site columns), so
    // stage0_report reads it. Empty records nothing to disk.
    std::filesystem::path fightsCsv;
};

struct CombatHudState {
    float health = 100.0f;
    float maxHealth = 100.0f;
    float stamina = 100.0f;
    float maxStamina = 100.0f;
};

// The proof of concept that puts the stage0 fight into a forged world (SEED-643): one skirmisher at a settlement's
// outskirts, the app's player as the fighter, death and respawn at the last settlement. A thin layer over
// stage0::Stage0; it adds no rules of its own. The ForgedGame must outlive it, and so must it the scene it is installed in.
class CombatBridge {
public:
    CombatBridge(const ForgedGame& game, const CombatSite& site, CombatBridgeConfig config = {});
    CombatBridge(const CombatBridge&) = delete;
    CombatBridge& operator=(const CombatBridge&) = delete;

    // Registers the combat around the entity `player`, which the caller spawns (it may still be queued). `start` is
    // where the player stands when the fight is set up. The enemy appears once groundReady says its post has ground.
    void install(Scene& scene, StableId player, glm::dvec3 start);
    void setGroundCheck(std::function<bool(glm::dvec3)> groundReady) { groundReady_ = std::move(groundReady); }

    // A scripted player: botInput gives the frame for the next tick. It does nothing before the fight is set up.
    void enableBot(const stage0::BotSkill& skill, std::uint64_t seed);
    InputFrame botInput(Scene& scene);

    bool live() const { return phase_ == Phase::Live; }
    // The yaw the live fight turns the stick by (the player's facing, or the way to the enemy under lock-on): what
    // forward means for the keys right now. Zero while there is no fight.
    float moveYaw(const Scene& scene) const;
    // Still waiting for the enemy's post to have ground: the one state startAt applies to.
    bool waiting() const { return phase_ == Phase::WaitingForGround; }
    // For a player who began somewhere else than the world (inside a dungeon): the fight is set up at once, with the
    // enemy at `foeFeet` and the player where they stand, without waiting for the post's ground.
    void startAt(glm::dvec3 foeFeet) { startAt_ = foeFeet; }

    // The enemy stands at `feet` with full bars and a fresh brain (the fight is not reset: the player is left alone), or
    // is parked there, dead and not drawn, out of the fight until it is placed again. A host that moves the enemy to a
    // new post calls these (the dungeon fight); the world fight never does.
    // A live foe at `feet`, of the configured role unless one is given (a dungeon room picks its own).
    void placeFoe(Scene& scene, glm::dvec3 feet, std::optional<stage0::FoeRole> role = std::nullopt);
    void parkFoe(Scene& scene, glm::dvec3 feet);
    bool foeParked() const { return foeParked_; }
    // Where both fighters stand after a death or a retry, until the host says otherwise.
    void setFightSpawns(glm::dvec3 playerFeet, glm::dvec3 foeFeet);
    // Back from somewhere else: the world spawns again, and the enemy stands at its post once that ground is there.
    void returnToWorld();
    // The site column of the fights.csv rows from now on: a dungeon id while the dungeon fight has the player, else the settlement.
    void setFightSite(std::string_view site);
    const stage0::BullWardenKit* bullWarden() const { return bullWarden_.get(); }
    stage0::FightOutcome outcome(const Scene& scene) const;
    // The cause while the player lies dead in a live fight; nothing otherwise.
    std::optional<stage0::DeathCause> playerDeath(const Scene& scene) const { return live() ? stage_.playerDeath(scene) : std::nullopt; }
    CombatHudState hud(const Scene& scene) const;
    // How far a living foe stands from the player along the ground in a live fight; nothing otherwise.
    std::optional<double> foeDistance(const Scene& scene) const;
    std::size_t lastSettlement() const { return lastSettlement_; }
    int respawns() const { return respawns_; }
    const CombatSite& site() const { return site_; }
    stage0::Stage0& stage() { return stage_; }
    const stage0::Stage0& stage() const { return stage_; }

    // The bodies, drawn from the art folder. False when either did not load; the fight then runs without them.
    bool loadVisuals(Renderer& renderer, const std::filesystem::path& artDir);
    // The third person camera, the bodies and the enemy's ground telegraph. The Stage 0 debug lines are added only when
    // `aids.stageLines` is set. Frame time is taken from the scene's own clock.
    void buildView(const Scene& scene, double alpha, RenderView& view, CombatDebugAids aids = {});
    void overlay(const Scene& scene, std::vector<std::string>& lines) const;

private:
    static stage0::CameraTuning cameraTuning() { return {.framing = stage0::CameraFraming::LockOn, .distance = 4.5f, .height = 2.4f}; }

    enum class Phase : std::uint8_t { WaitingForGround, FoeQueued, Live };

    void step(Scene& scene, double dt);
    void setFoe(Scene& scene, glm::dvec3 feet, bool alive, stage0::FoeRole role);
    void logTransitions(const Scene& scene);
    void openFightLog();
    void stampFightLog(std::string_view site);
    void setSpawnsForLastSettlement();
    glm::dvec3 respawnFor(std::size_t settlement);

    const ForgedGame& game_;
    CombatSite site_;
    CombatBridgeConfig config_;
    stage0::Stage0 stage_;
    std::unique_ptr<ForgedWalking> walking_; // the foe's walking rules; null on a world with no ground
    StableId player_{};
    glm::dvec3 start_{0.0};
    Phase phase_ = Phase::WaitingForGround;
    std::optional<glm::dvec3> startAt_;
    bool foeParked_ = false;
    bool returningToWorld_ = false;
    bool returnParked_ = false;
    std::function<bool(glm::dvec3)> groundReady_;
    std::size_t lastSettlement_ = 0;
    std::map<std::size_t, glm::dvec3> respawnPoints_;
    double deadSeconds_ = 0.0;
    int respawns_ = 0;
    std::uint8_t lastEnemyState_ = 0;
    std::uint8_t lastRoleState_ = 0;
    bool lastEnemyAlive_ = true;
    bool lastPlayerAlive_ = true;

    std::optional<stage0::FightBot> bot_;
    int botFights_ = 0;
    int botRespawns_ = 0;

    stage0::ExilePlayer exile_;
    std::unique_ptr<stage0::BullWardenKit> bullWarden_;
    stage0::CameraRig rig_{cameraTuning()};
    double lastViewTime_ = -1.0;
    glm::dvec3 lastViewPlayer_{0.0};
};

}
