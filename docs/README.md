# Seedforge — Design Document

> **Working title.** Placeholder only; rename freely.

A first/third-person open-world RPG in Unreal Engine 5.8 where each seed generates a
complete continent — its geography, its history, its politics, and the rules its magic
obeys. Worlds are **forged once** at new-game and baked to disk, not streamed from an
infinite generator.

The central idea: **the seed rolls the world's premises first, and the geography is a
consequence.** Two seeds don't just look different — they have different factions,
different economies, different traversal verbs, and a different definition of what
"getting stronger" means.

---

## Contents

| # | Document | Covers |
|---|---|---|
| 00 | [Overview](00-overview.md) | Pitch, pillars, scope targets, cut list, open questions |
| 01 | [World Axioms](01-axioms.md) | The five-slot premise system and its data schema |
| 02 | [Synergies](02-synergies.md) | Authored axiom combinations and firing rules |
| 03 | [Factions](03-factions.md) | Six roles, territory placement, relationship derivation |
| 04 | [Quests](04-quests.md) | Three tiers, grammar generation, the consequence ledger |
| 05 | [Combat](05-combat.md) | Rare, expensive, lethal; enemy roles; the Law power source |
| 06 | [Loot & Economy](06-loot-economy.md) | Finite catalogue, capability loot, dual currency, sinks |
| 07 | [Progression](07-progression.md) | Shallow vertical spine, horizontal access, earning sources |
| 08 | [World Generation](08-world-generation.md) | The forge pipeline, phase by phase |
| 09 | [Dungeons](09-dungeons.md) | Three tiers, mission-first generation, kits |
| 10 | [NPCs](10-npcs.md) | Four tiers, systemic dialogue, role succession |
| 11 | [Traversal & Map](11-traversal-map.md) | Scale math, fast travel, diegetic map, navigation |
| 12 | [Audio & Atmosphere](12-audio-atmosphere.md) | Ambience layers, silence, per-axiom palettes |
| 13 | [UI & HUD](13-ui-hud.md) | Minimal HUD, Law reconfiguration, map and journal |
| 14 | [Save System](14-save-system.md) | Campaign/slot split, deterministic IDs, versioning |
| 15 | [Multiplayer](15-multiplayer.md) | Async social now, co-op architecture decisions now |
| 16 | [Art Direction](16-art-direction.md) | Atmosphere over fidelity, the unification stack |
| 17 | [Tech Stack](17-tech-stack.md) | `libworldforge`, repo layout, tooling, order of work |

---

## How to read this

Each document states **decisions**, not options. Where a decision is genuinely open it
is marked `OPEN`. Where something is deliberately excluded it appears in that document's
cut list — the cut lists are load-bearing and should be treated as commitments.

A recurring pattern runs through nearly every system: **author roles, generate
instances.** Faction roles, enemy roles, NPC roles, dungeon tiers, and quest structures
are all hand-written; the seed decides who fills them and where. This is what makes a
world of this scope affordable, and it should be the first thing to reach for when a new
system needs designing.

## Status

Design complete, implementation not started. The first milestone is a hand-built 500 m
vertical slice with no procedural content — see [17-tech-stack.md](17-tech-stack.md#order-of-work).
