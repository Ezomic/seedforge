# 18 — Vertical Slice

**Milestone 1.** A hand-built 500 m square with no procedural content.

> The slice's job is to **answer questions**, not to be a small version of the game.

---

## What it must answer

Scope around the **riskiest** assumptions, not the most enjoyable ones to build.

| Assumption | Risk | Cost if wrong late |
|---|---|---|
| Rare + lethal + low-TTK combat is **tense**, not frustrating | **Highest** | Cascades into progression, loot ratios, enemy density, world size |
| Scarcity is engaging, not tedious | High | Rewrites the economy and loot ratios |
| No-waypoint landmark navigation works | Medium | Changes UI, art requirements, generator validation |
| The Law-swap is one system, not six | Medium | Architectural rework across combat, HUD, economy |

> **World generation is deliberately absent.** It is laborious, not risky — well-understood
> engineering. It does not belong in a slice whose purpose is de-risking.

---

## Stage 0 — the grey-box prototype

**Two to three weeks. One empty box, one enemy, no art, no map, no loot.**

Character controller, camera, one melee weapon, stamina, block, dodge, poise/stagger, and
**audio telegraphs**. Then fight the same skirmisher two hundred times.

This answers the highest-risk question at roughly **10% of the slice's cost**. If a single
fight is not tense in a grey box, it will not become tense because a forest was added
around it. If it *is* tense, everything downstream gets far more confident.

> **Audio telegraphs belong here, not in a later polish pass.** You cannot evaluate whether
> a lethal fight is *fair* without them, and fairness is most of what separates tension from
> frustration. See [12](12-audio-atmosphere.md#the-combat-audio-contract).

---

## The 500 m map is a test fixture

Designed deliberately to contain the five terrain conditions that will break AI once the
generator exists:

1. **Open field**
2. **Dense forest**
3. **A narrow defile or corridor**
4. **A steep slope**
5. **Shallow water and a bank**

Plus a **ridge** for a vantage point, a **river** for navigation-by-water, and **two or
three landmark silhouettes**.

> **Every enemy is validated in all five conditions before the slice is done.** Steep slopes
> and water edges are where UE navmesh and root-motion attacks reliably fall apart. A
> hand-built level would normally avoid them; this one must not, because
> [the generator will produce them constantly](05-combat.md#enemies-roles-not-species).

**Content on the map:** one small settlement (3–4 buildings — merchant, trainer, stash, one
NPC), one regional-tier dungeon of ~15 minutes, and a handful of minor sites.

---

## Scope: in

### Combat
- **One melee weapon class *and* a bow.** Two, not one — the preparation-forward identity is
  "open at range, finish in melee," so a melee-only slice tests half the design
- Stamina, block, dodge, poise/stagger
- **No parry** (a rare weapon property, not a slice feature)
- Target 3–8 second fights, lethal both ways

### Enemies — three roles
| Role | Tests |
|---|---|
| **Skirmisher** | Commitment and punish — the core melee dance |
| **Anchor** | The poise/stagger economy |
| **Artillery** | Positioning and terrain use |

Skip Swarm, Controller, and Ambusher — they need crowd tech, status effects, and perception
tuning respectively, and none tests anything the first three do not.

### Loot & economy
- Consumables, charges, 4–5 catalogue items
- **Encumbrance on.** Not optional — "is scarcity fun or annoying" is a primary question and
  weight is central to it
- One merchant, one repair service

### Progression
- Discovery XP, the four stats, one trainer with 4–5 capability unlocks
- No respec, no faction standing

### Navigation
- **No waypoints, no minimap.** Compass, verbal directions, landmarks
- A functional but ugly map screen — defer the hand-drawn one. That is an art investment;
  this is an engineering question

### Audio
- Combat telegraphs only. No ambience beds, music, or reverb system

### Art
- Megascans, one bought architecture kit, one bought animation set. **Make nothing custom**
- **Do** establish the material-response clamp and one colour grade — the
  [unification stack](16-art-direction.md#the-unification-stack) is cheap to test and
  expensive to retrofit
- **Covered faces from day one**

---

## The Law axiom, and the architecture test

Build the ability system with a **swappable power source**. Ship the slice on
**Blood-price**: health *is* mana, one bar, no extra HUD, and it directly stress-tests
scarcity because casting spends survivability.

Near the end, implement **Dying** (finite charges) as a second configuration.

> **If swapping Blood-price → Dying takes more than a day, the abstraction is wrong** and
> two combat systems have been designed by accident. A decisive architectural test that
> costs a day now and months at content stage.

---

## Scope: out

No world generation · no axiom system (one Law hardcoded, one swap test) · no factions ·
no quest generation (one hand-written quest) · no systemic dialogue (a few authored lines) ·
no consequence ledger · no dungeon generation · no ambience or music · no crafting ·
no fast travel · no save system beyond "it saves".

## Except these four — right from week one

Retrofitting these is what the whole plan exists to avoid. See
[15](15-multiplayer.md#what-must-be-decided-now-regardless).

1. **Run single-player as a one-player listen server.** The most important item here
2. **Mutate state only through an event/command layer**
3. **No singleton assumptions about "the player"** — audio, streaming, spawning
4. **The one-page style guide**, including the 2 m or 4 m snap grid
   ([16](16-art-direction.md#the-style-guide--week-one), [09](09-dungeons.md#seams))

---

## Success criteria — written before building

Otherwise whatever gets built will be rationalised into a pass.

1. One skirmisher, no consumables, full health — **is the fight tense?**
2. Can a fresh player reach the dungeon from a spoken direction, with no waypoint?
3. After clearing the dungeon, does the player feel they **spent** something?
4. Does the Blood-price → Dying swap take under a day?
5. **Does a fresh player die at least once and immediately want to try again?**

Number five is the real test, and it is the one that cannot be answered from inside the
project.

## Playtest with outsiders — non-negotiable

After two hundred hours of building combat, the builder is the worst possible judge of
whether it is tense or merely difficult.

**Five people who have never played it.** Watch without helping. Count how many quit versus
how many retry.

> Tension and frustration feel identical from inside a project and completely different from
> outside. This is the most-skipped step in solo development and the one that most reliably
> decides whether a combat system works.

## Kill criteria — decided in advance

If fights read as frustrating rather than tense after two honest tuning passes, the likely
fix is **raising TTK or lowering lethality** — which cascades into progression curves, loot
ratios, enemy density, and world size. Several documents would need rewriting.

That is acceptable at month four and catastrophic at year three.

> **Write down now what result would make you change the design**, so the decision is not
> made by sunk cost.

---

## Build order

| # | Step | Notes |
|---|---|---|
| 1 | Grey-box: controller, camera, melee, stamina, block/dodge, stagger, audio telegraphs | Stage 0 |
| 2 | **Fight one skirmisher two hundred times** | **Gate** |
| 3 | Bow and the preparation loop | |
| 4 | Anchor and Artillery, validated across all five terrain conditions | |
| 5 | The 500 m map | |
| 6 | Loot, consumables, encumbrance, merchant, repair | Scope creep starts here |
| 7 | The dungeon | |
| 8 | Discovery XP, trainer, capability unlocks | |
| 9 | Navigation, landmarks, the no-waypoint test | |
| 10 | The Law swap test | |
| 11 | **Outside playtest** | |

Steps 1–2 are roughly **40% of the total value**. Everything after step 3 is confirmation
and context; **step 2 is the actual experiment.**

## Estimate

Three to four months solo, assuming reasonable UE C++ fluency.

Standard warning applies: vertical slices routinely take twice their estimate, mostly from
step 6 onward where "just one more system" accumulates. **The gate at step 2 is what
protects against spending that time before knowing whether the premise holds.**

## Next

On passing: [17 — Order of work](17-tech-stack.md#order-of-work), step 2 — the NumPy
generator, which is engine-independent and can proceed in parallel.
