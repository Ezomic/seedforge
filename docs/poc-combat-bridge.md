# PoC: combat bridge (SEED-643)

Written before the bridge exists. It answers one question: how much of `game/stage0` can the engine app
(`ichor --forged`) reuse as a library, what has to change, and what gets built. The Stage 0 verdict is
still pending ([stage0-log.md](stage0-log.md)), so the bridge is a thin, throwaway-tolerant layer behind
`--forged --combat`. Nothing changes when the flag is absent.

## How much of game/stage0 links into the app as a library

Almost all of it, with no restructuring and no second build of anything.

- `game/stage0/CMakeLists.txt` already splits the code: every `src/*.cpp` except `main.cpp` goes into the
  static library `ichor_stage0` (about 5,600 of 5,800 source lines; `main.cpp` is the 250 line executable).
  It is added by `engine/CMakeLists.txt` and built by `all` today, so the app only has to link it.
- The pieces the bridge uses are already free of the arena and of the stage0 executable:
  - `Stage0` in `combat.cpp` (the combat, skirmisher and stagger systems, tuning, telegraph timers),
    `skirmisher.cpp`, `weapon.cpp`, `status_effect.cpp`, `attack_rules.cpp`, `death_cause.cpp`.
  - Readability: `telegraph_geometry.cpp` and `Stage0::debugLines` (the wind-up lines), the tell and wind-up
    sound cues through `Tuning`.
  - Look: `exile_player.cpp` (the Exile, SEED-505 side), `bull_warden.cpp` (the Bull-Warden, SEED-488),
    `camera_rig.cpp`, `free_look.cpp`. All of them are render-only and read the combat components.
  - Test driver: `fight_bot.cpp` (`FightBot`) plays through the ordinary `InputFrame`, so it works unchanged.
- Not used and not touched: the other roles (anchor, artillery, ambusher, swarm, controller), the bow, the
  session ledger, dashboard and run prompt, the replay corpus tooling, the fight review tools.
- The app already has the other halves: `CombatHudScreen` is bound with placeholder bars ("the app has no
  combatants yet"), `Scene`, `CharacterBody` physics over the streamed terrain, and `--screenshot` offscreen.

## What must change in stage0 (behaviour preserving)

The replay corpus and every stage0 test must stay green and unchanged, so each change defaults to today's
behaviour.

1. `Stage0::install` always builds the 20 m arena, its walls and its own player. A new `Stage0::attach`
   adopts an existing player entity (the app's `player`), spawns only the foe, and registers the same
   components and systems in the same order. `install` is left as it is.
2. `resetFight` teleports to two fixed spawns set at install. A `setSpawns` setter lets the bridge move them
   (the respawn at the last settlement, the enemy's post).
3. Death classification calls a wall "cornered" using `kArenaHalf`. In the open world every spot would count
   as against a wall, so an `arena` flag (default true, false after `attach`) keeps that telemetry honest.
4. The camera framing (`frameCamera`) clamps the eye to the arena's walls, and the "was the attacker on
   screen" telemetry uses it. At world coordinates both would be wrong, so outside the arena `viewPose` works
   the pose out around the player and moves it back (the arena case adds and subtracts zero).
5. `spawnFoe` is split out of `install`, so the bridge can hold the enemy back until the ground under its
   post is resident.

Nothing else in stage0 is edited.

## What gets built

A new small library `game/combat_bridge` (`ichor_combat_bridge`, one `add_subdirectory` line), which depends on
`ichor_stage0` and `ichor_world`; stage0 itself does not learn about the world.

- `combat_site`: a pure function of the `ForgedGame`. The enemy's post is the nearest dry, gentle ground
  clear of buildings just outside a settlement's edge; the respawn point is that settlement's own spawn
  point (`spawnAtSettlement` is split so the point can be asked for without moving `game.spawn`).
- `CombatBridge`: owns the `Stage0`, attaches it to the app's player, spawns one skirmisher at the post once
  its ground is resident, tracks the last settlement entered, respawns the player there a few seconds after
  death (the manual retry button still works), and answers the HUD (health, stamina). It also builds the
  third person view: Exile and Bull-Warden instances, the camera, and the stage0 telegraph lines.
- App wiring in `main.cpp` (small): the `--combat`, `--combat-bot SKILL` and `--combat-duel` flags,
  the bridge creation, one call in the view builder, one in the HUD hook, and not registering the first
  person `walk_look` system (the bridge owns steering and the camera in combat mode).
- `Engine::setInputSource`: a hook that lets a function supply the per tick input (used by `--combat-bot`;
  recorded like any input, so a bot fight can be saved and replayed).
- Tests: a Catch2 executable that forges worlds on two seeds, adds terrain physics around the site, and
  checks that a scripted bot wins one fight and a passive player loses one, each with the same outcome and
  state hash on a second run; plus placement and respawn tests. A cmake check runs the app headless twice.

## What is deliberately not built

More than one enemy at once (SEED-648 puts a dungeon room's enemies on the one foe in turns, `DungeonFight`), other
roles, the bow, loot or xp on a kill, a leash
that returns the enemy to its post, a lock-on camera in the world, saving a fight in a campaign, and any
tuning. A foe whose ground cell unloads (the player outruns it by more than the streaming radius) has no
floor; the respawn resets it.

Also not supported: saving or loading a fight (the bridge keeps its phase, last settlement and respawn timer
outside the scene, so `--combat` is ignored with `--load` and `--load-slot`), and bit-identical replays in
live play, where the tick on which the enemy's ground cell arrives depends on the streaming thread (headless
and `--screenshot` runs stream synchronously and are deterministic).

## What gets reworked if the combat design changes

- Tuning numbers or the skirmisher's rhythm: nothing in the bridge. It reads `Tuning` and `Stage0` as they are.
- A different commitment model (cancels, i-frames, parry): `Stage0` changes, the bridge keeps calling
  `attach`, `resetFight` and `tryAttack` paths it does not own. Only the bot test outcomes (which bot seeds
  win) would need re-picking, and the test searches for them instead of pinning them.
- A rebuilt combat loop on the real architecture (the doc 18 step 6): `CombatBridge` and `combat_site` are
  the part to keep; `attach` and the three stage0 setters go away with stage0.
- Stage0 killed outright: delete `game/combat_bridge`, the `--combat` block in `main.cpp`, the engine input
  source hook can stay (it is generic), and the `spawnAtSettlement` split can stay.
