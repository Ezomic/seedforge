# 11 — Traversal & Map

This system feeds **backwards** into the generator. Traversal decides world scale, not the
other way round, so it must be settled before generation is tuned.

## The scale math

Work from a target rhythm, not a target size:

- **60–90 seconds** between things worth glancing at
- **3–5 minutes** between sites worth entering
- **10–15 minutes** to cross a region on foot

At a run speed of ~4 m/s, 60 seconds ≈ 240 m. Across a 4 × 4 km world that is roughly
**350 points of interest** — about 100 enterable sites (matching the
[dungeon tiers](09-dungeons.md)) and the rest a shrine, a viewpoint, a wrecked cart with a
note, a camp, a body with a story.

That number is the honest cost of "density beats size," and it is why 4 km is right rather
than 16: **multiply the world's edge by four and the content requirement multiplies by
sixteen.**

## Verbs, and the axiom that picks them

The Cataclysm decides traversal identity, via the axiom schema's `traversal.unlock` block.

| Cataclysm | Primary verbs |
|---|---|
| **The Drowning** | Boats and diving. Water is the map; depth is progression |
| **The Sundering** | Climbing and controlled descent. The world is vertical |
| **The Long Winter** | Environmental protection, sledges. Cold gates regions, not walls |
| **The Silence** | **No mounts** — there are no animals. Everything on foot |
| **Gods Died / Nothing Broke** | Conventional: roads, mounts, coaches |

> **World size is a function of the traversal set.** A Silence world has no horses, so it
> must generate smaller and denser or it becomes a walking simulator. Feed the traversal
> block back into [Phase 7](08-world-generation.md#the-phases) settlement spacing and into
> the world bounds themselves.

An axiom rolled in Phase 0 changes the physical size of the continent.

## Mounts — `OPEN`

Honest accounting: mounts cost animation sets, terrain adaptation and IK, mount/dismount
transitions, stabling or summoning, separate pathing, and camera work — and mounted combat
is already cut. Easily one month, likely two.

**If kept:** no mounted combat, dismount to fight, restricted from steep terrain and
interiors, treated as a **speed buff with rules** rather than a character.
**If cut:** shrink the world and lean harder on roads and fast travel.

Either is defensible. Drifting between them is not.

## Fast travel

The decision that most determines whether the world stays worth having built.

- **Discovered settlements only** — never into the wilderness
- **Along secured roads only** — if the Levellers cut the route, that link is gone until
  someone reopens it
- **Costs coin** — passage, guides, caravan fare. A good [economy sink](06-loot-economy.md#sinks--plan-the-drains-before-the-faucets)
- **Costs world-clock time** — the load-bearing part, because the Pressure advances on a
  schedule. Three days on the road is three days the blight moved

The player self-regulates with no artificial restriction, and the road network and faction
control layer become mechanically live rather than decorative.

## The map is an object, not a UI

The player starts with a rough, incomplete chart and fills it in. Detail arrives from:

- **Vantage points** — reaching a ridge or tower reveals terrain actually visible from it.
  The tower-climbing trope, but earned by geography rather than a checklist, and the
  generator already knows sightlines from the Phase 10 scoring pass
- **Charts bought from the Surveyors** — a faction whose reward is information
- **NPC directions**, which write annotations onto the map
- **The player's own marks**

Two players on the same seed end up with differently-shaped maps — a lovely property for a
game whose seeds are shareable. See [13](13-ui-hud.md#the-map-is-the-biggest-ui-investment--and-marketing)
and [15](15-multiplayer.md#tier-1-async-social--build-this).

## Navigation: landmarks over waypoints

**Show the player marker** — going full Morrowind on position is a needlessly hostile bet.
**Drop precise quest pins** in favour of directions and a compass bearing with rough
distance:

> *North past the salt flats, then east where the broken tower stands.*

This is only viable if landmarks are readable, which makes it a **generator requirement**:

> **Phase 10 validation:** sample points across the walkable world and confirm that from
> each, at least one distinctive landmark silhouette is visible or within a short walk.
> Fail the seed if coverage drops below threshold.

Cheap — a visibility test over a few thousand sample points — and it is the concrete link
between navigation design and the world compiler. It also imposes a
[silhouette requirement on art](16-art-direction.md#silhouette-discipline).

**Precise waypoints ship as an accessibility option, off by default.** Some players need
them; the ones who do not will never turn them on.

## Roads

Give roads a real movement bonus — **20–30%** is enough to be felt. The Phase 8 network
becomes a decision surface: fast and watched, or slow and unseen. Bandits know this too,
which is where ambient encounters go.

## Getting lost should be possible

Fog, night, and storms that genuinely reduce visibility, plus terrain that occludes
landmarks. Being lost is one of the few emotions only an open world can produce.

Recovery must always be available: find a road, a river, or high ground. **Rivers are
especially good and free** — the hydrology pass guarantees they flow downhill to somewhere,
so "follow the water" is always a valid last resort.

Audio backs this up when visibility fails — see
[12](12-audio-atmosphere.md#audio-landmarks-solve-the-fog-problem).
