# 05 — Combat

The system that decides whether this is a game or a tech demo. It is the only thing the
player touches thousands of times.

## Core identity: rare, expensive, fast

**Fights are uncommon and costly.** Every fight spends something that does not come back
for free — charges, bandages, arrows, blood, durability — and the generated wilderness is
mostly *empty of enemies*.

The design argument and the budget argument point the same way:

- It fits the scarcity themes the axioms establish
- It fixes the standard procedural failure: respawning trash filling space the designer
  could not fill with content
- It makes **avoidance a real strategy**, turning the open world into a decision space
- It needs far fewer enemy types, because each fight carries more weight

**Low time-to-kill in both directions.** Three to eight seconds, lethal for everyone.
Short fights need less move-set variety, less animation, smaller health pools, and less AI
depth — while producing *more* tension. Long fights expose every weakness a small team has.

## Animation is the budget

Combat quality in 3D is roughly 80% animation, and that is where small projects die.

- **Deliberate and heavy**, not fast and combo-driven. Long wind-ups, clear commitment, no
  cancels. Fewer frames, better readability, forgiving of imperfect animation.
- **Movesets per weapon *class*, not per weapon.** Five classes; individual weapons differ
  by stats, reach, and effects.
- **One humanoid skeleton** for as much of the roster as possible. Differentiate with
  scale, materials, gear, and speed.
- **Commitment over cancellation** — a design virtue that also avoids authoring dozens of
  transition states.

## The system

**Preparation-forward, commitment-based, lethal.** Most skill lives *before* contact — the
opening shot, the terrain chosen, the consumable burned, whether to engage at all. Melee is
partly a failure state: what happens when preparation did not hold.

- **Stamina governs everything** — attacking, sprinting, blocking. The pacing dial.
- **Pick two of block / dodge / parry.** Decision: **block and dodge**; parry is a rare
  weapon property, not a universal verb.
- **Poise and stagger** are the real currency of melee. Winning is about who gets
  interrupted, not who has more HP.
- **Positioning over rolling.** Short directional steps with real recovery, not
  invincibility spam. Rolling is expensive to animate *and* it deletes terrain from the
  equation — the opposite of what a terrain-driven world wants.
- **Ranged and thrown are first-class** — bows, slings, thrown oil, snares. Cheap to
  animate and they reward the preparation loop.

## The Law axiom decides what combat *spends*

**One ability system. The Law swaps only the power source.** Same abilities, same VFX, same
animations — different economics.

| Law | Power source | Effect on fights |
|---|---|---|
| **Ore-bound** | Physical charges: weighty, lootable | Magic is ammunition. Encumbrance vs. firepower; corpses are resupply |
| **Blood-price** | Your own HP — *no mana bar exists* | Healing and casting draw one pool. Deeper trouble makes escape cost more |
| **Written** | Spellbooks as items with fixed uses | Loadout picked in town and lived with. Preparation-forward by construction |
| **Ley-bound** | Location strength | Magic becomes positional. Holding the node is winning |
| **Dying** | Finite charges that never refill | Every cast is permanent expenditure. Players hoard for 30 hours then spend once, memorably |
| **Borrowed** | Debt meter with escalating consequences | Power now, collectors later |

**Prototype Blood-price and Ley-bound first.** They change how a fight *plays* the most,
and they will stress-test whether the abstraction is real or whether six combat systems
have been accidentally designed.

## Enemies: roles, not species

Six authored roles, reskinned per biome and faction.

| Role | Behaviour |
|---|---|
| **Skirmisher** | Fast, low health, flanks, punishes commitment |
| **Anchor** | Slow, high poise, controls space; handled first or last |
| **Artillery** | Ranged pressure, forces movement, dies fast when reached |
| **Controller** | Debuffs, snares, summons. The priority target that teaches prioritisation |
| **Swarm** | Individually trivial, dangerous in numbers. The reason area effects exist |
| **Ambusher** | The reason you look at terrain |

Compose encounters against a **budget** — pick roles to fill it, then dress them in the
region's faction and biome. One AI implementation, dozens of faces.

> **Constraint: no enemy may require a specific arena.** Test every one in open field,
> dense forest, narrow corridor, steep slope, and shallow water. Steep procedural slopes
> and water edges are where UE navmesh and root-motion attacks reliably fail — and a
> generated world produces those constantly, where a hand-built level would have avoided them.

## Readability without controlled lighting

In a hand-built level a designer places light so the enemy reads. The generator will not.

- **Audio telegraphs carry equal weight to visual ones.** Every attack wind-up needs a
  distinct sound. In fog, rain, dusk, or forest, audio may be all the player gets.
  → Creates an accessibility obligation: see [13](13-ui-hud.md#accessibility).
- **Silhouette must survive clutter.** Test enemy shapes against dense foliage, the most
  common thing the PCG pass produces.
- **Enforce a clear radius at encounter placement** — a cheap generator validation pass
  that refuses to spawn a fight inside unreadable geometry.

## Cut list

Mounted combat · dismemberment · per-weapon movesets · physics-driven hit reactions ·
large-scale battles · a third defensive verb.

Each is a month or more, and none is why anyone will play this.

## The gate

Build all of this on a hand-made 500 m square with three enemy types and zero procedural
content. Then:

> **Is a single fight against one ordinary enemy tense?**

If yes, the seed system multiplies it. If no, nothing downstream — not axioms, not
factions, not quest grammars — rescues it.
