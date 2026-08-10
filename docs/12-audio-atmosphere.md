# 12 — Audio & Atmosphere

**The highest value-per-hour system in the project.**

## Why it matters more here

A superb ambience bed is a few days of work, not a few months, and the player is inside it
every second. More specifically:

> In a hand-built level, a designer controls the lighting, the composition, and the
> sightline that makes a place feel like something. The generator does not. **Audio supplies
> the emotional authorship the level designer is not there to provide** — uniformly, across
> a world nobody has ever walked through.

## Layered ambience

Six layers, mixed live:

1. **Biome bed** — long, slow loops. Every biome needs one; it is one of the five things
   that makes a biome a biome rather than a texture swap
2. **Time of day** — dawn chorus, night insects, the specific quiet of pre-dawn
3. **Weather** — driven from the Phase 4 climate fields
4. **Proximity** — water, cliff faces, settlements, ruins. Spatialised, distance-faded
5. **One-shots** — a distant crack, a rockfall, wingbeats. Randomised, spatialised, sparse
6. **Territory** — **you can hear whose land you are in.** A distant work-bell in Compact
   territory, a patrol horn in the Garrison's, nothing at all in the Renouncers'. Crossing
   a border becomes audible before it becomes visible, driven free from the
   [faction flood-fill](03-factions.md#placement--the-terrain-generator-draws-the-political-map)

## Silence is a resource

Most games are wall-to-wall sound, which deafens the player to dynamics. If the wilderness
is genuinely quiet, **a sound means something** — and that is the contract that makes
[rare, lethal combat](05-combat.md) work. You hear the approach. You get to decide.

> **Artemis Withdrew cashes out here.** A world with no animals loses its entire biological
> ambience layer: no birds, no insects, no distant movement. Just wind, water, footsteps, and
> whatever else is out there. It costs nothing to implement — content is *removed* — and it
> will be the most talked-about thing in the game.

## Music: an event, not a bed

Open worlds routinely over-score, and continuous music makes a world feel like a
screensaver. Let music **arrive**: at vistas, at discovery, at combat, at death. Then let it
stop — the silence after a fight does more work than a victory sting.

**The seed picks the palette, not the melody.** Compose modular stems; let axioms select
instrumentation.

| Cataclysm | Palette |
|---|---|
| Deukalion's Flood | Low strings, water-adjacent percussion, things heard through depth |
| Demeter's Grief | Sparse, high, brittle; long decays. Lyre without accompaniment |
| Silence of the Oracles | Processional forms with the chorus removed — a ruined shape of sacred music |
| The Unbinding | Low frequency, tectonic, unstable |
| Artemis Withdrew | Almost none. What exists is a single unaccompanied voice |
| The Age of Kronos | Warm, full, complete — which feels *wrong* to anyone who has played the others |

Base palette is aulos, lyre and kithara over frame drums, in the Greek modes. The point is
that a mode carries an ethos — Dorian steadies, Phrygian excites, Mixolydian mourns — which
is a period-accurate way of doing exactly what an adaptive score needs to do anyway.

Same composition budget, six sensory identities. The cheapest way to make a new seed feel
new before the player has learned anything about it.

## The combat audio contract

> **Every attack wind-up needs a distinct, identifiable sound.**

In fog, at night, in dense foliage, in rain, audio may be the *only* telegraph the player
receives — because lighting is uncontrolled and the generator will place fights in bad
spots. In a lethal combat system this is fairness, not polish.

- Each enemy role gets an audio identity
- Footstep material detection
- A player turned away must still know what is happening behind them

This creates a hard [accessibility obligation](13-ui-hud.md#accessibility): every audio cue
needs a visual fallback.

## Reverb in spaces nobody designed

**Compute room volume and dominant material at dungeon-forge time** and stamp a reverb
preset onto each generated space. The prefab kits know whether they are stone crypt or
timber mine; the layout pass knows the volume. A lookup, not a simulation, and it does most
of the perceptual work.

Outdoors, approximate with occasional raycasts — cliff walls and canyons should return
sound. In an Unbinding world, that is most of the map.

## Audio landmarks solve the fog problem

Navigation is designed around landmark silhouettes; weather is designed to hide them. Audio
is the reconciliation.

**Bells, waterfalls, mills, mine machinery, surf.** Every settlement gets an audible
signature with a long falloff; every major river gets water audible before it is visible.

When visibility fails, sound still navigates — and *"follow the water downhill"* is a lovely
way to find your way home.

## The Pressure should be audible

The clock needs a sensory channel that is not a UI element. As the front approaches a
region, **degrade the ambience**: birds thin out, then vanish; a low drone enters and rises;
the one-shot pool loses its living entries.

A player who notices a region has gone quiet, and realises why, has understood the endgame
without a line of text.

## Weather, time, and the visual grade

Drive weather **per-region from the climate fields**, not a global roll — the rain shadow
generated in Phase 4 should mean the leeward basin is genuinely dry.

Make weather **mechanical, not cosmetic**:

- Rain masks sound, both ways
- Fog defeats landmark navigation
- Night is dangerous
- Cold gates regions under Demeter's Grief

Visually, the highest-leverage tool is a **strong per-axiom global colour grade** plus
per-biome post-process volumes. A committed grade is what makes assets from mixed sources
read as one world. See [16](16-art-direction.md#the-unification-stack).

## Production

- **MetaSounds plus UE submixes** is enough. Wwise is better for complex adaptive music but
  adds a dependency, a licence tier, and integration time — take it only if the music system
  genuinely needs it. Every plugin is an [engine-upgrade liability](17-tech-stack.md#plugins-are-engine-upgrade-liabilities)
- **Commercial sound libraries are excellent and cheap.** Record own foley for the
  distinctive things — the magic system, the creatures, whatever the axioms make unique
- **Set a loudness budget and ducking rules on day one.** Mixing is where amateur projects
  audibly lose it, and retrofitting a mix is miserable
