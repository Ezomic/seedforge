# 03 — Factions

Author **roles**, not factions. A mining cartel, a book monopoly, and a blood tithe-house
are one archetype in three costumes.

## The ecology: six roles

A world needs a *shape*, not an assortment. Every seed fills these slots:

| Role | Wants | Bound to |
|---|---|---|
| **Incumbent** | To keep things exactly as they are | Powers axiom |
| **Challenger** ×1–2 | The Incumbent's chair | Powers + Cataclysm |
| **Parasite** | The conflict to continue | Law axiom |
| **Custodian** | To keep a secret | Cataclysm (+ synergies) |
| **Refusenik** | Out, entirely | Law axiom |
| **Herald** | The Pressure to arrive | Pressure axiom |

**5–7 active factions per world.** More and every one becomes mush — the player cannot
hold their positions, so all choices flatten into "which icon is closest."

---

## Incumbent — read off the Powers axiom

| Powers axiom | Incumbent |
|---|---|
| Decaying empire | **the Administration** — not evil, just enormous and slow; cannot reach its own frontier |
| Merchant compacts | **the Compact** — rules by contract; violence subcontracted |
| Occupation | **the Garrison** — 20 years in; some were born here |
| Theocracy | **the Church** |
| Petty kingdoms | *Special case:* 3–4 holds each running the Incumbent script at reduced scale. Horizontal, war-shaped map |
| No one | *No Incumbent.* Parasites and Refuseniks expand into the vacuum — a genuinely different ecology |

## Challengers

- **The Restorationists** — want the pre-Cataclysm world back. Their legitimacy is
  backward-looking, so they are the only faction that can tell the truth about the old
  world and the only one incapable of imagining a new one.
- **The Successors** — a splinter of the Incumbent that believes it should hold the reins.
  Same methods, different hands. Morally the most interesting: joining them changes who
  suffers without changing how much.
- **The Levellers** — want *nobody* in charge. Right about the rot, catastrophic about
  the remedy.

## Parasite — read off the Law axiom

Controls whatever magic runs on, profits from every side, and quietly sabotages peace.

| Law | The Parasite |
|---|---|
| Ore-bound | **The extraction cartel** — owns the ridges, rents ore to both armies |
| Blood-price | **The Tithe-houses** — legal, regulated, immaculate paperwork |
| Written | **The Stationers** — a copying monopoly that decides what gets copied |
| Ley-bound | **The Claim Office** — sells deeds to invisible geography |
| Dying | **The Hoarders** — sitting on the last charges, waiting for the peak |
| Borrowed | **The Debt-brokers** — buy your obligation, sell it on at a markup |

The Parasite is the best quest-giver in the game: they hire the player for anything and
never ask a moral question. They are also the faction most players end up hating most,
which is correct.

## Custodians

- **The Keepers** — know what actually happened during the Cataclysm. When a Tier-1
  synergy fires, this is usually the faction it generates.
- **The Surveyors** — know the *land*. Their reward is **information**: trade with them
  and POIs light up. See [11](11-traversal-map.md#the-map-is-an-object-not-a-ui).

## Refuseniks

- **The Renouncers** — gave up magic on principle. Defenceless, clean-handed, and correct
  in a way that gets them killed.
- **The Wanderers** — no territory, no claim, pass through everyone's land. Mechanically
  the player's lifeline in hostile regions, since they can offer services anywhere.

## Herald — and the rule that matters most

> **The Pressure's faction is people who want it to happen, not monsters.**

| Pressure | Herald |
|---|---|
| Blight | **The Bloom-touched** — let it in; think *you* are the sick ones |
| Wakening | A vigil *hastening* it, having done the maths on grief |
| Hunger | Hoarders who calculated who lives and quietly decided |
| Returning | Heralds preparing a welcome |
| Splitting | A chasm-cult reading the breaking as correction |
| Rival | Simply the rival power, who are people, with reasons |

A political clock can be joined, infiltrated, argued with, or be tempting. A wall of
enemies can only be fought.

---

## Placement — the terrain generator draws the political map

Each faction has terrain affinities and a claim strategy. Run a weighted flood-fill from
home settlements during [Phase 9](08-world-generation.md#the-phases).

| Faction type | Affinity | Claim strategy |
|---|---|---|
| Extraction cartel | Ore nodes, mountain passes | Resource-following; ignores borders |
| Administration | Roads, river confluences | Contiguous; decays with distance from capital |
| Compact | Ports, trade junctions | **Nodes only** — owns towns, not the land between |
| Renouncers | Marginal land, high isolation | Small, defensive, avoids all others |
| Wanderers | None | Routes, not territory |
| Herald | The Pressure's origin | Expanding over campaign time |

Two things fall out for free:

1. **Borders emerge from geology.** Phase 1 placed the ore; the ore places the cartel; the
   cartel's reach places the war. A frontier that follows a ridgeline is a real causal chain.
2. **Overlapping claims mark content sites.** Contested cells are where danger, quests, and
   interesting NPCs go — the generator identifies its own good locations.

## Relationships — computed, not authored

Derive hostility from two axes rather than a hand-written matrix:

- **Old order:** restore ←→ preserve ←→ replace
- **The Law resource:** control ←→ free access ←→ renounce

```
hostility(A, B) = ideological_distance(A, B) + resource_overlap(A, B)
```

Two factions wanting the same mines hate each other regardless of philosophy; two that
agree on everything but disagree about the old world still fight. This produces reasoned
alliances and, more usefully, **odd bedfellows the player can exploit.**

## Rules

1. **Every faction is right about something.** Give each a *true* diagnosis and a *bad*
   prescription. The Levellers are correct that the Compact is strangling the coast; their
   solution kills the coast.
2. **Every faction offers something mechanical and costs something real** — access, a base,
   a service, a gear line, paid for in locked content and enemies made. Reputation without
   teeth is not reputation.
3. **No faction is both fully joinable and fully defeatable.** Pick one per faction, or the
   endgame collapses into a checklist.
4. **At least two factions must want strictly incompatible things.** If everyone can be
   satisfied, there was never a decision.

## Naming

Compose from role + a noun the axioms already produced (salt, ash, ledger, ore, vigil,
tide, charge, ink):

> *The Salt Compact · the Ash-readers · the Ledger House · the Ore Assize · the Deep Guild
> · the Last Vigil*

Because the nouns come from the world's own axioms, names cohere within a seed
automatically and no two seeds sound alike.

## Related

- Faction standing as progression: [07](07-progression.md)
- Territory as an audible layer: [12](12-audio-atmosphere.md#layered-ambience)
- Faction identity through material parameters: [16](16-art-direction.md#faction-identity-through-materials-not-meshes)
