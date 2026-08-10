# 01 — World Axioms

The seed's first and most consequential roll. Axioms are the world's premises; geography,
politics, economy, and combat economics are all downstream of them.

Setting: **ancient Greece** — poleis, sanctuaries, mystery cults, and a pantheon whose
current status is the central question of every world.

## Slot architecture

Axioms are drawn from **five independent slots**, not one pool. Slots answer different
questions and therefore cannot contradict each other, which removes almost all conflict
handling.

| Slot | Question | Drives |
|---|---|---|
| **A. Cataclysm** | What broke? | Ruin types, dungeon kits, terrain scars, tone, traversal verbs |
| **B. Law** | How does divine power reach the world? | Loot, economy, combat resource, progression meaning, HUD |
| **C. Powers** | Who rules? | Factions, settlement density, road quality, quest structures |
| **D. Pressure** | What is foretold? | Endgame clock, enemy escalation, world-state change |
| **E. Anomaly** | The weird thing (~33% of seeds) | High-variance identity |

Six options per slot yields **7,776 configurations from 30 authored axioms**.

**Ship with three per slot** (243 combinations), not six. Axioms are the most natural
content-update unit in the project — each new one multiplies against everything already
authored.

---

## Slot A — Cataclysm

| Axiom | Premise | Key consequences |
|---|---|---|
| **The Silence of the Oracles** | ~200 years ago the oracles stopped answering. Nobody knows whether the gods died, left, or lost interest | Sanctuaries are tombs, priesthoods are archivists of a dead correspondence, healing is pharmakon rather than prayer. Plutarch wrote an essay on precisely this anxiety — it is attested, not invented |
| **Deukalion's Flood** | The flood receded everywhere except here | Settlements only above the waterline (hard generator constraint), drowned cities as dungeons, salt-blight, diving and boats |
| **Artemis Withdrew** | The mistress of wild things left, and the wild things went with her | No fauna, no hunting, no hides, **no mounts**. Implemented by *removing* content |
| **Demeter's Grief** | Persephone was never returned. The year stopped turning | Perpetual winter over thin regrowth. Famine-shaped politics. Things nobody discusses |
| **The Unbinding** | What was chained beneath the earth is working loose | Chasms and shear cliffs, **vertical traversal**, regions isolated by geology |
| **The Age of Kronos** | The golden age never ended — which is itself the problem | Intrigue rather than survival. Rare. Exists to make the others land harder |

## Slot B — The Law

How divine power reaches the world. Most mechanical reach of any slot; invest here first.

| Axiom | Premise | Key consequences |
|---|---|---|
| **Orichalcum** | Power inheres in a metal that forms near volcanic ridges | **Tectonics determines the political map.** Mines are dungeons, ridges are borders |
| **Hecatomb** | Power is bought with sacrifice | Vitality as the casting cost. Built-in moral axis, culturally load-bearing rather than edgy. **Ichor** is the divine tier of the same substance |
| **The Mysteries** | Power comes by initiation into rites | A "spell" is a rite you have been admitted to — and admission can be revoked. Sanctuaries are armories. Loadout chosen before leaving |
| **Temenos** | Power exists only on consecrated ground | Magic becomes **positional**: sanctuaries, groves, springs. Greek religion was intensely place-bound, making this the most historically accurate slot on the page |
| **The Oath** | Power is borrowed against an oath sworn on the Styx | Oaths bound even gods. Debt meter, escalating consequence, no absolution |
| **The Last Favour** | Divine attention is finite and nearly spent | Finite charges that never refill. Deflationary. Progression is conservation |

## Slot C — Powers

| Axiom | Premise | Key consequences |
|---|---|---|
| **The Poleis** | Many small city-states, constant low-grade war | High settlement count, no capital. No single Incumbent |
| **The Hegemony** | One dominant power in visible decline | Infrastructure quality interpolates by distance from the centre |
| **The League** | A maritime trading compact; everything by contract | Economy-forward, quests are jobs, violence subcontracted |
| **The Satrapy** | A foreign power arrived twenty years ago | Collaborator/resistance axis in every settlement — the most generative quest structure |
| **The Amphictyony** | A league of states jointly governing a great sanctuary | A real institution. Religious authority as political machinery |
| **The Dark Age** | Post-collapse. Writing largely lost | Isolated settlements, unmaintained roads, dominant wilderness. **No Incumbent.** Also attested |

## Slot D — Pressure

**What is foretold.** Hidden at world creation; discovered in play.

The Greek frame's biggest gain: a clock everyone can see coming and nobody can avert is
the actual engine of the tragedies. The Pressure stops being a countdown and becomes a
prophecy.

| Axiom | What advances |
|---|---|
| **The Miasma** | Ritual pollution spreads and must be *purified*, not fought. A real concept with real remedies |
| **The Unburied** | Funeral rites failed somewhere, and the dead are increasingly not staying down |
| **The Foretold** | A conqueror whose coming was announced, and who is winning |
| **The Return** | Something arrives on a cycle nearing completion |
| **The Famine** | Divine punishment on a schedule; settlements fail in order |
| **The Opening** | The Unbinding never stopped |

## Slot E — Anomaly

Rolled on ~33% of seeds. Hidden at creation.

- **The Labyrinth is real**, and it is somewhere on this map
- **Tiresias lives** — blind, immortal, has seen everything, answers honestly if found
- The underworld is **physically reachable**
- One polis where time runs at a different rate
- The sea has a door in it

---

## Data schema

Axioms are **data assets that emit generator directives**, not prose.

```json
{
  "id": "cataclysm.flood",
  "slot": "cataclysm",
  "title": "Deukalion's Flood",
  "tags": ["WATER", "RUIN", "SCARCITY", "VERTICAL_LIMIT"],
  "conflicts": ["cataclysm.demeters_grief"],

  "terrain":    { "sea_level_offset_m": 200, "coastal_erosion": 2.4 },
  "settlement": { "min_elevation_m": 210, "prefer_slope": "gentle", "density_mult": 0.7 },
  "biomes":     { "inject": ["salt_blight", "tidal_ruin"], "suppress": ["lowland_farm"] },
  "poi":        { "weights": { "drowned_polis": 3.0, "pharos": 2.0, "farmstead": 0.2 } },
  "enemies":    { "roster_add": ["drowned", "salt_kin"], "roster_remove": ["bandit_farmer"] },
  "traversal":  { "unlock": ["swim_deep", "boat"] },
  "economy":    { "scarce": ["grain", "timber"], "abundant": ["salvage", "salt"] },
  "palette":    { "grade": "flood", "fog_density": 1.4 },
  "audio":      { "bed_overrides": ["surf_distant"], "music_palette": "flood" }
}
```

### Rules

1. **Every axiom must touch at least four directive blocks.** Fewer than four and it is
   decoration — cut it or deepen it.
2. Directives merge; conflicts resolved by slot priority A > B > C > D > E.
3. `conflicts` is a hard blocklist. Blocking an incoherent pair is cheaper than reconciling it.
4. Axiom JSON lives in `data/axioms/` and is read by **both** the generator and the game.

---

## Presentation

Compose a world title and summary from the roll, shown on the new-game screen:

> **The Salt Years** — *Seed `KRAKEN-77`*
> The flood never fully drew back. The League rules what is left of the coast, power comes
> by initiation, and the dead are not staying buried.

This block is the save identity, the marketing, and the thing players screenshot.

**Slots A–C are shown at world creation.** **Slots D and E are hidden** — the prophecy and
the anomaly land far harder as revelations.

## Related

- Combinations: [02 — Synergies](02-synergies.md)
- How the Law slot reconfigures combat: [05](05-combat.md#the-law-axiom-decides-what-combat-spends)
- How the Law slot reconfigures the economy: [06](06-loot-economy.md#two-currencies)
- How the Cataclysm slot picks traversal verbs: [11](11-traversal-map.md#verbs-and-the-axiom-that-picks-them)
