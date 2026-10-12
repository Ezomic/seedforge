# 09 — Dungeons

## The honest problem

**Procedural dungeons work brilliantly in 2D and rarely in first-person 3D.** In a
top-down roguelike, layout *is* the experience — the whole room reads at a glance and the
generator's variety is legible. In first person, a dungeon is experienced as a sequence of
nearby surfaces, and generated layouts collapse into "corridor, room, corridor, room." The
variety exists mathematically and is invisible experientially.

So: **three generators, used for different things.**

---

## Three tiers

### Landmark dungeons — 10–15, fully authored

Built as hand-authored scene files with hand-placed encounters, **baked lighting**, and real
architectural intent. The seed decides only *where* they appear and which faction holds
them.

Large practical bonus: as authored scenes they get baked lighting, tuned light probes,
proper occlusion, and art direction. They will look markedly better than anything
generated — that is fine, they are the peaks. They carry the
[set-piece quests](04-quests.md#set-pieces).

### Regional dungeons — 30–50, authored skeleton + generated contents

A hand-made layout template — the shape, the vertical structure, the shortcut topology —
with generated dressing, encounters, loot placement, and faction skinning.

**~70% of the quality for ~20% of the authoring.** Most dungeon hours should go here.

### Minor sites — 100+, fully procedural

Caves, camps, cellars, wrecks, watchtowers. **Five to ten minutes each.** Their job is to
make the wilderness worth crossing, not to be a destination.

> **Ratio insight:** a generated world does not need fifty hour-long dungeons. It needs
> many short sites and a few long ones. **Length is the enemy** — a mediocre procedural
> space is tolerable for six minutes and unbearable for forty.

---

## Generate the mission before the space

For the procedural tiers, the single most important structural decision:

> **Build the mission graph first, then build space to fit it.** Never lay out rooms and
> then decide what is in them — that produces rooms with stuff in them, not a dungeon.

The mission graph is what the player *does*: gate, key, challenge, reward, shortcut,
reveal, boss.

1. **Mission** — `entry → challenge → gate → [branch: key ∥ optional reward] → shortcut-back → climax`
2. **Embed** — assign each node a room type and depth, respecting gate ordering
3. **Layout** — place rooms in 3D, route connections, insert stairs and drops
4. **Instantiate** — select prefabs, dress, light, populate
5. **Validate** — reachability with gates respected, no soft-locks, encounter budget in range

> This is the **same graph-rewriting engine** as the [quest system](04-quests.md#tier-1-generation--four-passes).
> Different ruleset, same code. Design for that deliberately.

---

## The 3D problems the graph will not solve

### Verticality
Generated layouts default to planar, and a flat 3D dungeon reads as a maze. Make vertical
structure an explicit constraint: require N level changes, treat stair and shaft modules as
first-class connectors, and include **open shafts giving sightlines between floors** —
seeing a room you will reach later is the cheapest orientation aid available.

### Getting lost
The signature failure of procedural 3D. Three fixes, borrowed from Souls-style design
because it solved this properly:

- A **hub room** the mission graph must route through more than once, visible from multiple
  approaches
- **One-way drops and unlockable shortcuts** back to known space — generated as first-class
  graph nodes, not decoration
- A **unique visual anchor per branch** — a broken statue, a collapsed roof, a colour — so
  branches are distinguishable by memory rather than by minimap

### Lighting
Cannot be baked for generated spaces, so it must be generated. Attach light sockets to
prefabs with rules, and implement **guide lighting**: a subtle brightness and warmth
gradient toward the objective. Shipped games do this constantly and players never
consciously notice.

Budget realistically for lighting in generated interiors, the main performance risk here, and
another reason to keep procedural sites small. The authored tier sidesteps it entirely.

### Seams
Modular kits must snap to a grid with consistent socket heights and door widths.

> **Pick the grid early — 2 m or 4 m — write it down, and never break it.** Every exception
> costs tenfold later.

---

## Kits — the real cost

The largest art line item in the project.

- **5–6 kits**: crypt, mine, ruin, natural cave, fortification, and one axiom-specific
  (drowned polis under Deukalion's Flood; sealed telesterion under Demeter's Grief)
- **30–50 modules each**: room shells, corridors, junctions, stairs, shafts, doors, plus
  damage and clutter variants
- Which kits appear is decided by the **Cataclysm axiom**

**Ship with three kits.** Every additional kit multiplies against every layout already
authored, making kits the best content-update unit alongside axioms.

## Persistence: forge layouts, instantiate on entry

- **Forge the layout at world-generation time** (Phase 11) and store it in the manifest —
  the quest binder must be able to reference *"the third chamber of Ordmoor"* when building
  a quest, which it cannot do if the dungeon does not exist yet.
- **Instantiate geometry on entry**, deterministically, from the stored layout.
- **Store only the delta**: what was looted, killed, opened, broken.

Result: tiny saves, stable quest references, and a dungeon cleared in hour four still
cleared in hour sixty. See [14](14-save-system.md).

## Anti-patterns

- **Long corridors** — free to generate, miserable to walk, the clearest tell that a space
  was generated. Cap length hard and force a turn, a level change, or a sightline.
- **Symmetry and grid-perfection** — real ruins are not square. Rotate, offset, collapse.
- **Backtracking without shortcuts** — if the graph makes you return, it must generate the
  shortcut.
- **One kit everywhere** — two dungeons in the same kit in the same region are read as the
  same dungeon.
- **Overlength** — if the procedural tier averages over ten minutes, cut length, not count.
