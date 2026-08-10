# 00 — Overview

## Pitch

A classic open-world RPG, first/third person, where a seed generates an entire continent
and the rules that govern it. The player picks or is given a seed, the game **forges** a
world in a one-time loading step, and that world is theirs for the campaign.

What distinguishes it from other procedurally generated RPGs is that the seed does not
just arrange terrain. It rolls the world's **axioms** — what broke, how magic works, who
rules, what is getting worse — and the geography, politics, economy, loot, and combat
economics all follow from those.

## Design pillars

**1. Premises before geography.**
The generator's first pass is narrative, not terrain. Everything downstream is a
consequence, so a world reads as caused rather than assembled.

**2. Author roles, generate instances.**
Faction roles, enemy roles, NPC roles, quest structures, dungeon tiers — all hand-written
and finite. The seed decides who fills them and where. This is the only reason a world of
this scope is affordable.

**3. Density over size.**
4 × 4 km, not 40 × 40. Empty-huge is the standard failure of procedural open worlds and
the one most likely to kill this project.

**4. Scarcity makes decisions.**
Combat is rare and expensive. Loot is finite and mostly consumable. Power growth is
shallow. Every system is tuned so the player is choosing what to give up.

**5. The world is the interface.**
Landmark silhouettes, audio telegraphs, guide lighting, audible territory borders. The
HUD is minimal because the world communicates.

**6. Atmosphere over fidelity.**
A small team loses a detail-density contest and can win a mood contest. Fog, light,
colour, and sound are the budget.

## The core loop

```
Forge a world  →  read its premises  →  explore outward from the start region
      ↑                                             ↓
      │                              find sites, factions, people, problems
      │                                             ↓
   new seed          ←  campaign ends  ←   choose sides, spend scarce resources,
                                            watch the Pressure advance
```

## Scope targets

| Thing | Target | Source |
|---|---|---|
| World size | 4 × 4 km | [11](11-traversal-map.md) |
| Points of interest | ~350 (≈100 enterable) | [11](11-traversal-map.md) |
| Settlements | 15–25 | [08](08-world-generation.md) |
| Biomes | 6, fully realised | [08](08-world-generation.md) |
| Axioms | 3 per slot at ship (5 slots) | [01](01-axioms.md) |
| Tier-1 synergies | ~8 at ship | [02](02-synergies.md) |
| Factions per world | 5–7 | [03](03-factions.md) |
| Landmark dungeons | 10–15 fully authored | [09](09-dungeons.md) |
| Dungeon kits | 3 at ship, 5–6 eventually | [09](09-dungeons.md) |
| Named NPCs | 10–15 Principals, 60–100 Regulars | [10](10-npcs.md) |
| Unique items | ~60 unique, ~200 notable | [06](06-loot-economy.md) |
| Enemy roles | 6, reskinned per faction/biome | [05](05-combat.md) |
| Forge time | 30–90 s | [08](08-world-generation.md) |
| Campaign power growth | ~2× total | [07](07-progression.md) |

## Cut list — not in v1

Written down deliberately. These survive enthusiastic Tuesdays.

- Mounted combat
- Mounts at all (`OPEN` — see [11](11-traversal-map.md))
- Dismemberment and physics-driven hit reactions
- Per-weapon movesets (weapon *class* movesets only)
- A third defensive verb (pick two of block / dodge / parry)
- Crafting trees
- Loot rarity tiers and random affixes
- Multiplayer (architecture kept open — see [15](15-multiplayer.md))
- Full NPC scheduling simulation
- Facial animation, lipsync, MetaHumans
- Runtime infinite world streaming
- Large-scale battles
- Minimap
- Procedurally generated main quest

## Open questions

| Question | Where | Impact |
|---|---|---|
| Mounts in or out? | [11](11-traversal-map.md) | World size, 1–2 months of work |
| Catalogue uniques in co-op — instanced or contested? | [15](15-multiplayer.md) | Breaks "60 unique items exist" premise if instanced |
| Third-person, first-person, or both? | — | Animation budget, camera work |
| Campaign length target | — | Content volume, Pressure clock pacing |

## The test that gates everything

Before any of this matters, build a hand-made 500 m square with three enemy types and no
procedural content, and answer:

> **Is a single fight against one ordinary enemy tense?**

If not, nothing downstream rescues it.
