# 08 — World Generation

## Shape: a batch compiler

Because worlds are forged once at new-game, the pipeline is a **compiler**: seed in, world
on disk out. That framing brings compiler discipline — ordered passes, immutable
intermediate artifacts, a validation gate at the end.

> **Structural rule: every phase writes its output as an immutable artifact and never
> mutates a previous phase's.** This gives re-run-from-phase-N, seed diffing, and — most
> importantly — the ability to dump every intermediate as an image.

### Why forge-once

- Expensive generation becomes affordable (erosion, multi-pass settlement placement)
- Heightmaps can be baked; runtime Landscape editing in UE is largely editor-only and
  fighting it is a tar pit
- World Partition, HLODs, and baked navmesh all work, because the world is authored-on-disk
- **Generator patches cannot break existing campaigns** — see [14](14-save-system.md#versioning)

Trade-off: switching seeds means a new campaign and a re-forge. For a classic RPG that is
correct — a seed is a campaign, not a level.

---

## The phases

| # | Phase | Output |
|---|---|---|
| 0 | **Axioms** | `WorldConfig` — merged directive blocks |
| 1 | **Tectonics** | Base elevation field |
| 2 | **Erosion** | Eroded elevation |
| 3 | **Hydrology** | River splines, lake polygons, carved channels |
| 4 | **Climate** | Temperature and moisture fields |
| 5 | **Biomes** | Biome ID map, blend weights |
| 6 | **Resources** | Ore nodes, ley nodes, arable score |
| 7 | **Settlements** | Settlement list with positions and size tiers |
| 8 | **Roads** | Road splines, deformed terrain |
| 9 | **Politics** | Territory map, faction data, contested cells |
| 10 | **POIs** | POI list, dungeon archetype assignment, landmarks |
| 11 | **Binding** | Item distribution, NPC cast, quest tensions, manifest |
| 12 | **Bake** | Landscape, PCG inputs, navmesh, WP cells, HLODs |

**Only Phase 12 needs Unreal.** Phases 0–11 are a standalone library — see [17](17-tech-stack.md).

### Phase notes

**0 — Axioms.** Roll slots A–E, resolve synergies (priority order, cap at two), merge
directive blocks. Must be first; must be pure data.

**1 — Tectonics.** Voronoi plates with type and drift vectors; uplift at convergent
boundaries, rifts at divergent.
> Do **not** substitute straight noise. Noise gives blobs; plates give **ranges** — linear
> chains with a direction and a reason. Every downstream layer benefits: ore goes on
> convergent boundaries, rain shadows form leeward, borders follow ridges.

**2 — Erosion.** Hydraulic (droplet or grid) plus thermal for talus. **The highest-value
pass in the pipeline** — the difference between terrain that looks generated and terrain
that looks *old*. Expensive, which is exactly what forge-once buys.

**3 — Hydrology.** Priority-flood depression filling, then D8 or D-infinity flow
accumulation. Rivers above a threshold; lakes in filled depressions; carve channels back
into the heightfield.
> Rivers **must** come from flow accumulation. A river running uphill or ending nowhere is
> the most immersion-breaking thing a generated world can do, and everyone notices.

**4 — Climate.** Temperature from latitude plus elevation lapse rate. Moisture from
prevailing wind with orographic lift, producing **rain shadows** — the single effect that
does most for perceived design intent.

**5 — Biomes.** Whittaker-style temperature × moisture lookup, modulated by slope and
elevation, then axiom injection and suppression.

**6 — Resources.** Ore from tectonic data; ley nodes if the Law calls for them; arable
score from slope and moisture.
> This is where Orichalcum stops being flavour: the ore sits on the convergent boundaries
> Phase 1 produced, so the wars in Phase 9 happen where the geology put them.

**7 — Settlements.** Suitability = fresh water + arable + defensibility + resource
proximity + coastal access, with axiom constraints applied. Greedy placement with
Poisson-disc spacing; size tier from score.

**8 — Roads.** A* between settlements, cost weighted by slope, water crossings, biome.
MST plus ~15% extra edges (a pure MST reads artificially sparse). **Deform the terrain to
the road** — never paint a road onto terrain.

**9 — Politics.** Faction home assignment, weighted territory flood-fill, relationship
matrix, contested cell identification. See [03](03-factions.md#placement--the-terrain-generator-draws-the-political-map).

**10 — POIs.** Placement from contested cells, remoteness, and **sightline scoring**
(raycast from road splines — an entrance nobody can see may as well not exist). Assign
dungeon archetypes; place landmark silhouettes.

**11 — Binding.** Distribute the item catalogue, generate the named NPC cast, seed initial
quest tensions, forge dungeon layouts, write the manifest.

**12 — Bake.** Heightmap and weightmaps into Landscape, PCG biome inputs, navmesh, World
Partition cells, HLODs.

---

## Resolution

| Field | Working resolution |
|---|---|
| Elevation, erosion, hydrology | **4096² at 1 m** (64 MB per float field — trivial offline) |
| Plates, climate | **256²**, upsampled — low-frequency by nature, saves enormous time |
| Landscape target | 4033 × 4033 at 1 m scale ≈ 4 km |

If over performance budget, drop erosion to 2048² and upsample before Phase 3 — flow
accumulation cares about large-scale structure, and fine detail returns from materials and
tessellation.

## Debug output is not optional

Dump a PNG for every field, every phase, every run: elevation, flow accumulation,
temperature, moisture, biome IDs, suitability, territory, road graph, POI placement.

> **Build the image dumper before Phase 1.** You cannot debug a world by walking around in
> it — you will spend an hour flying over terrain to learn what a 200 KB image shows in one
> second.

Rendered by the [map viewer](17-tech-stack.md#the-map-viewer).

## Validation gates

Not every roll is playable. Gate before Phase 12:

- Enough arable land for the settlement count
- All settlements connected by road
- No region unreachable given available traversal verbs
- POI count within range
- Contested cell count above a floor (a world with no friction has no content)
- Landmark visibility coverage above threshold — see [11](11-traversal-map.md#navigation-landmarks-over-waypoints)
- Axiom directives not mutually starved

On failure, reroll with `worldSeed' = hash(worldSeed, attempt)` and log the cause. A
rejected seed is invisible; a broken world is not.

> **Watch the rejection rate.** Above ~10% means the directives are over-constrained. Tune
> with the [batch harness](17-tech-stack.md#testing--and-the-tool-that-will-save-you-most).

## Determinism

The headline rule is hash-derived subseeds — never a shared RNG stream:

```
regionSeed  = splitmix64(worldSeed ^ hash(rx, ry))
dungeonSeed = splitmix64(worldSeed ^ hash(poiId))
lootSeed    = splitmix64(dungeonSeed ^ hash(chestId))
```

The gotchas that actually cost days:

1. **Never iterate an unordered container during generation.** `HashMap` / `TSet` /
   `unordered_map` iteration order is not guaranteed and *will* differ between runs or
   builds. **Sort keys before looping.** This is the number-one source of "the same seed
   made a different world."
2. **Parallelism must be order-independent.** Each cell computes from its own derived seed
   and its own inputs; reductions must be deterministic — never accumulate into a shared
   float in thread-completion order.
3. **Fixed iteration counts.** Never "erode until converged" — erode exactly N times.
4. **Avoid fast-math.** Floating point is not bit-identical across compilers and
   architectures. Only matters for cross-platform seed sharing — but seed sharing is part
   of the pitch, so move critical decisions onto integer hashes rather than float comparisons.
5. **Version the generator.** Store `genVersion` beside `worldSeed`; any phase change
   silently invalidates old seeds.

## Performance

Erosion dominates at 60–70% of total forge time. Target **30–90 s** overall. Run on a
worker thread with progress reporting, and put a map drawing itself on screen while it works.

## Prototype path

Write phases 1–6 in whatever iterates fastest (NumPy — the terrain math is all array
operations), get the world *looking* right, then port phase by phase to C++. The
immutable-artifact structure makes the port incremental and verifiable: port a phase, diff
its PNG against the reference, move on.

The mistake is writing it in UE C++ from day one and paying a two-minute compile-and-launch
for every erosion constant. There will be hundreds of those.
