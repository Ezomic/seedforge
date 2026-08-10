# 03 — Factions

Author **roles**, not factions. A mine cartel, an initiation monopoly, and a sacrifice-house
are one archetype in three costumes.

## The ecology: six roles

| Role | Wants | Bound to |
|---|---|---|
| **Incumbent** | To keep things exactly as they are | Powers axiom |
| **Challenger** ×1–2 | The Incumbent's chair | Powers + Cataclysm |
| **Parasite** | The conflict to continue | Law axiom |
| **Custodian** | To keep a secret | Cataclysm (+ synergies) |
| **Refusenik** | Out, entirely | Law axiom |
| **Herald** | The prophecy to arrive | Pressure axiom |

**5–7 active factions per world.** More and every one becomes mush.

---

## Incumbent — read off the Powers axiom

| Powers axiom | Incumbent |
|---|---|
| The Hegemony | **The Archonate** — not evil, just enormous and slow; cannot reach its own frontier |
| The League | **The Hellenotamiai** — the treasurers. Rule by contract and levy; violence subcontracted |
| The Satrapy | **The Garrison** — twenty years in; some were born here |
| The Amphictyony | **The Council** — religious authority as political machinery |
| The Poleis | *Special case:* three or four poleis each running the Incumbent script at reduced scale. Horizontal, war-shaped map |
| The Dark Age | *No Incumbent.* Parasites and Refuseniks expand into the vacuum |

## Challengers

- **The Archaists** — want the world before the Silence back. Backward-looking legitimacy:
  the only faction that can tell the truth about the old world, and the only one incapable of
  imagining a new one.
- **The Diadochoi** — a splinter of the Incumbent that believes it should hold the reins.
  Same methods, different hands. Joining them changes who suffers, not how much.
- **The Isonomoi** — want power held equally or not at all. Right about the rot, catastrophic
  about the remedy.

## Parasite — read off the Law axiom

Controls whatever power runs on, profits from every side, and quietly sabotages peace.

| Law | The Parasite |
|---|---|
| Orichalcum | **The Metallarchs** — hold the mine leases, rent ore to both armies |
| Hecatomb | **The Hekatombaia** — a legal, regulated levy with immaculate procedure |
| The Mysteries | **The Telestai** — sell initiation, and decide who is never admitted |
| Temenos | **The Horistai** — the boundary-setters, selling claims to consecrated ground |
| The Last Favour | **The Thesauroi** — the treasuries, sitting on what remains, waiting for the price to peak |
| The Oath | **The Horkos-brokers** — buy your obligation, sell it on at a markup |

The Parasite is the best quest-giver in the game: they hire the player for anything and never
ask a moral question. They are also the faction most players end up hating most.

## Custodians

- **The Mnemones** — the rememberers. A real Greek civic office: officials whose job was to
  hold what was not written down. Here, they know what actually happened at the Silence.
  When a Tier-1 synergy fires, this is usually the faction it generates.
- **The Periegetes** — "those who lead around." Also real, and the reason we have Pausanias.
  They know the *land*, and their reward currency is **information**: trade with them and POIs
  light up.

## Refuseniks

- **The Asebeis** — "the impious," a name given by their enemies. They set down the rites on
  principle. Defenceless, clean-handed, and correct in a way that gets them killed.
- **The Xenoi** — strangers moving under guest-right. No territory, no claim, protected by
  *xenia* in everyone's land. Mechanically the player's lifeline in hostile regions.

## Herald — and the rule that matters most

The Pressure's faction is **people who want the prophecy fulfilled**, not monsters:

| Pressure | Herald |
|---|---|
| The Miasma | **The Miastores** — they let it in, and think you are the polluted ones |
| The Unburied | A vigil *hastening* it, having done the arithmetic on grief |
| The Famine | Hoarders who calculated who lives and quietly decided |
| The Return | **The Kerykes** — heralds, a sacred office, preparing a welcome |
| The Opening | A chasm-cult reading the breaking as correction |
| The Foretold | Simply the conqueror's people, who are people, with reasons |

A prophecy can be joined, subverted, fulfilled early, or argued with. A wall of enemies can
only be fought.

---

## Placement — the terrain generator draws the political map

Each faction has terrain affinities and a claim strategy. Weighted flood-fill from home
settlements during [Phase 9](08-world-generation.md#the-phases).

| Faction type | Affinity | Claim strategy |
|---|---|---|
| Metallarchs | Ore nodes, mountain passes | Resource-following; ignores borders |
| Archonate | Roads, river confluences | Contiguous; decays with distance from the centre |
| Hellenotamiai | Ports, trade junctions | **Nodes only** — owns harbours, not the land between |
| Asebeis | Marginal land, high isolation | Small, defensive, avoids all others |
| Xenoi | None | Routes, not territory |
| Herald | The prophecy's origin | Expanding over campaign time |

Two things fall out for free:

1. **Borders emerge from geology.** Phase 1 placed the ore; the ore places the Metallarchs;
   their reach places the war.
2. **Overlapping claims mark content sites** — the generator identifies its own good locations.

## Relationships — computed, not authored

- **Old order:** restore ←→ preserve ←→ replace
- **The Law resource:** control ←→ free access ←→ renounce

```
hostility(A, B) = ideological_distance(A, B) + resource_overlap(A, B)
```

Produces reasoned alliances and, more usefully, **odd bedfellows the player can exploit**.

## Rules

1. **Every faction is right about something.** A *true* diagnosis and a *bad* prescription.
2. **Every faction offers something mechanical and costs something real.** Reputation without
   teeth is not reputation.
3. **No faction is both fully joinable and fully defeatable.**
4. **At least two factions must want strictly incompatible things**, or the player never
   actually chooses.

## Naming

Compose from role plus a noun the axioms produced — salt, ash, bronze, oath, tide, ichor,
laurel, ember:

> *The Salt League · the Ash Mnemones · the Bronze Assize · the Oathkeepers · the Laurel
> Council · the Last Vigil*

Because the nouns come from the world's own axioms, names cohere within a seed automatically.

## Related

- Faction standing as progression: [07](07-progression.md)
- Territory as an audible layer: [12](12-audio-atmosphere.md#layered-ambience)
- Faction identity through material parameters: [16](16-art-direction.md#faction-identity-through-materials-not-meshes)
