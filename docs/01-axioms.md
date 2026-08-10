# 01 — World Axioms

The seed's first and most consequential roll. Axioms are the world's premises; geography,
politics, economy, and combat economics are all downstream of them.

## Slot architecture

Axioms are drawn from **five independent slots**, not one pool. Slots answer different
questions and therefore cannot contradict each other, which removes almost all conflict
handling.

| Slot | Question | Drives |
|---|---|---|
| **A. Cataclysm** | What broke? | Ruin types, dungeon kits, terrain scars, tone, traversal verbs |
| **B. Law** | How does power work? | Loot, economy, combat resource, progression meaning, HUD |
| **C. Powers** | Who rules? | Factions, settlement density, road quality, quest structures |
| **D. Pressure** | What is getting worse? | Endgame clock, enemy escalation, world-state change |
| **E. Anomaly** | The weird thing (~33% of seeds) | High-variance identity |

Six options per slot yields **7,776 configurations from 30 authored axioms**. That ratio
is the entire argument for the system.

**Ship with three per slot** (243 combinations), not six. Each axiom needs real content
support and every added axiom multiplies against everything already authored — making
axioms the most natural content-update unit in the project.

---

## Slot A — Cataclysm

| Axiom | Premise | Key consequences |
|---|---|---|
| **The Gods Died** | ~200 years ago; divine magic simply stopped | Temples are tombs, priesthoods are relic-scavengers, healing is alchemical so herbalism is an economy, crypt kit everywhere |
| **The Drowning** | The sea rose 200 m | Settlements only above waterline (hard generator constraint), drowned cities as dungeons, salt-blight biome, diving and boats |
| **The Long Winter** | A decade of winter, ended 30 years ago | Thin regrowth over frozen ruin, famine-shaped politics, things nobody discusses |
| **The Sundering** | The land physically broke | Chasms and shear cliffs, **vertical traversal**, isolated regions |
| **The Silence** | Every animal stopped, a century ago | No fauna, no leather, no mounts, no birdsong. Implemented by *removing* content |
| **Nothing Broke** | A genuine golden age | Intrigue rather than survival. Rare. Exists to make the others land harder |

## Slot B — Law of Magic

Most mechanical reach of any slot. Invest here first.

| Axiom | Premise | Key consequences |
|---|---|---|
| **Ore-bound** | Magic lives in a metal forming near volcanic ridges | **Tectonics determines the political map.** Mines are dungeons, ridges are borders |
| **Blood-price** | Magic costs vitality | HP-as-mana. Built-in moral axis. Bottled vitality as currency |
| **Written** | Spells are physical books | Burnable, stealable, forgeable. Libraries are armories. Loadout chosen in town |
| **Ley-bound** | Magical strength varies by location | Magic becomes **positional**. Geography is tactics. The map is the build |
| **Dying** | Magic worked a century ago and is failing | Finite charges that never refill. Deflationary economy. Progression is efficiency |
| **Borrowed** | All magic is a loan from entities keeping ledgers | Debt meter, escalating collection. Best risk/reward dial available |

## Slot C — Powers

| Axiom | Premise | Key consequences |
|---|---|---|
| **Petty kingdoms** | Many small holds, constant low-grade war | High settlement count, no capital, borders everywhere. No single Incumbent |
| **A decaying empire** | One enormous capital, rotting frontier | Infrastructure quality interpolates by distance from centre |
| **Merchant compacts** | Guild-states; everything by contract | Economy-forward; quests are jobs; violence is subcontracted |
| **Occupation** | A foreign power arrived 20 years ago | Collaborator/resistance axis in every settlement — the most generative quest structure |
| **A theocracy** | Church-state, absolute and administrative | — |
| **No one** | Post-state | Isolated settlements, unmaintained roads, dominant wilderness. **No Incumbent** — different faction ecology |

## Slot D — Pressure

The campaign clock. Hidden at world creation; discovered in play.

| Axiom | What advances |
|---|---|
| **The Blight spreads** | One biome consumes its neighbours, visibly, over campaign time |
| **The Wakening** | The dead increasingly don't stay dead, at a climbing rate |
| **The Rival** | Another human power is simply winning a war |
| **The Returning** | Something arrives on a cycle nearing completion |
| **The Hunger** | Resource collapse; settlements fail on a schedule |
| **The Splitting** | The Sundering never stopped |

## Slot E — Anomaly

Rolled on ~33% of seeds. Hidden at creation. Cheap, memorable, disproportionately shared.

- Two moons, and they disagree
- Night here lasts sixty hours
- One person is immortal, knows everything, and answers honestly — if found
- A second inhabited map layer beneath this one
- The world has an edge, and you can walk to it

---

## Data schema

Axioms are **data assets that emit generator directives**, not prose.

```json
{
  "id": "cataclysm.drowning",
  "slot": "cataclysm",
  "title": "The Drowning",
  "tags": ["WATER", "RUIN", "SCARCITY", "VERTICAL_LIMIT"],
  "conflicts": ["cataclysm.long_winter"],

  "terrain":    { "sea_level_offset_m": 200, "coastal_erosion": 2.4 },
  "settlement": { "min_elevation_m": 210, "prefer_slope": "gentle", "density_mult": 0.7 },
  "biomes":     { "inject": ["salt_blight", "tidal_ruin"], "suppress": ["lowland_farm"] },
  "poi":        { "weights": { "drowned_city": 3.0, "lighthouse": 2.0, "farmstead": 0.2 } },
  "enemies":    { "roster_add": ["drowned", "salt_kin"], "roster_remove": ["bandit_farmer"] },
  "traversal":  { "unlock": ["swim_deep", "boat"] },
  "economy":    { "scarce": ["grain", "timber"], "abundant": ["salvage", "salt"] },
  "palette":    { "grade": "drowning", "fog_density": 1.4 },
  "audio":      { "bed_overrides": ["surf_distant"], "music_palette": "drowning" }
}
```

### Rules

1. **Every axiom must touch at least four directive blocks.** Fewer than four and it is
   decoration, not an axiom — cut it or deepen it.
2. Directives merge; conflicts resolved by slot priority A > B > C > D > E.
3. `conflicts` is a hard blocklist. Blocking an incoherent pair is cheaper and better
   than reconciling it.
4. Axiom JSON lives in `data/axioms/` and is read by **both** the generator and the game.
   See [17](17-tech-stack.md).

---

## Presentation

Compose a world title and summary from the roll, shown on the new-game screen:

> **The Age of Salt** — *Seed `KRAKEN-77`*
> The sea took the lowlands two centuries past. The compacts rule what's left of the
> coast, spells are written in books, and the dead are getting restless.

This block is the save-file identity, the marketing, and the thing players screenshot.

**Slots A–C are shown at world creation** — players want to choose their fantasy.
**Slots D and E are hidden** and discovered in play; the Pressure and the Anomaly land far
harder as revelations.

## Related

- Combinations: [02 — Synergies](02-synergies.md)
- How the Law slot reconfigures combat: [05](05-combat.md#the-law-axiom-decides-what-combat-spends)
- How the Law slot reconfigures the economy: [06](06-loot-economy.md#two-currencies)
- How the Cataclysm slot picks traversal verbs: [11](11-traversal-map.md#verbs-and-the-axiom-that-picks-them)
