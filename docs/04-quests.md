# 04 — Quests

## Why generated quests usually fail

They are generated **verb-first**. `Kill [n] [enemy] at [place]` is a sentence with holes,
and no variation in the holes makes it a story. Players learn the template in twenty
minutes and read all quest text as noise thereafter.

**Generate situation-first.** Start from a tension that already exists in the world, then
find the action. The verb is the last thing decided, not the first.

The world already produces tensions for free: the faction flood-fill created contested
cells, the economy layer created dependencies, the Pressure created a moving front.

---

## Three tiers

### Tier 0 — Contracts
Fully generated, low stakes, bounties and deliveries.
**Rule: let them be honest about what they are.** A job board with a price and no dramatic
framing. Players do not resent repetitive work; they resent repetitive work *dressed as
drama*. Cap the supply or price it so nobody grinds it.

### Tier 1 — Situations
Procedural structure, authored beats, generated from world state. The bulk of the game.
Detailed below.

### Tier 2 — The spine
Hand-authored main quest and faction questlines, **procedurally bound**. The same beats
every seed; different cast, locations, and faction alignments.

> **Do not attempt to generate the main quest.** Nobody has done it well and the failure
> is unrecoverable, because it is the thing every player experiences. A structurally fixed,
> cast-variable main quest reads as authored, because it is.

---

## Tier 1 generation — four passes

**1. Pull a tension.** Sample live world state, weighted by proximity to the player and
narrative pressure:

- a contested cell where two factions' claims overlap
- a settlement with a broken resource dependency
- a hostility pair above threshold
- the Pressure front advancing on something valuable
- a Custodian secret with a party who would pay to keep or break it
- **an unresolved consequence of something the player already did** ← highest value

**2. Expand.** Rewrite abstract nodes into concrete structures, two or three levels deep.
`<resolve tension>` → `<obtain object> + <deal with rival claimant>`. Keep the ruleset to
roughly 30 rules — that covers an enormous space and stays maintainable.

**3. Bind.** Resolve every node to a real entity: this faction, that NPC, this dungeon,
that road. **Binding is where the quest becomes specific, and specificity is the whole
difference between authored-feeling and generated-feeling.**

**4. Validate.** Reachability, no contradictions with the ledger, difficulty inside budget,
and at least one node the player can refuse. Discard failures — generation is cheap, a bad
quest is expensive.

> The same graph-rewriting engine serves [dungeon mission graphs](09-dungeons.md#generate-the-mission-before-the-space).
> Different ruleset, same code.

---

## Worked example

Seed: **The Salt Years** — Deukalion's Flood + The League + The Mysteries, Pressure = The
Miasma. The *Mysteries + Miasma* synergy fired: pollution spreads through the initiated, so
rites are being permanently lost.

The generator pulls three facts off world state, none authored:

- Kessene imports all grain via League shipping *(economy layer — nothing grows)*
- The Isonomoi have been sinking League hulls in the strait *(hostility, contested cells)*
- The miasma front reaches the sanctuary at Ordmoor in ~11 days *(Pressure advance)*

It produces:

> The Telestai will pay extravagantly to evacuate Ordmoor's sanctuary and its initiates
> before the miasma reaches it. The only hulls fast enough are League grain ships, and the
> League will release one — for the right pressure, applied by you. But the ship on the
> schedule is Kessene's.
>
> **Rites that no one else can confer, or a polis that eats next month.**

Two claimants with genuine cases, an irreversible cost on both sides, and a deadline the
world imposed rather than a timer the UI invented.

---

## Making generated quests read as written

- **Localise the stakes.** Not "the region will suffer" — *Elsa's mill floods, and Elsa has
  two children.* Specific beats epic, and specificity is nearly free given persistent NPCs.
- **Two claimants, one object.** The best quest shape in existence. Bias the grammar
  heavily toward it.
- **Bind to persistent NPCs.** Quest-givers who evaporate after one use are the deepest
  reason procedural quests feel disposable. Require binding to *existing* people; an NPC
  appearing in five quests across forty hours becomes a character for free. See [10](10-npcs.md).
- **Plan the complication at generation time**, not at runtime. Runtime surprises
  contradict things the player already saw.
- **Every quest must move a visible number** — territory, supply status, faction strength,
  a route opening or closing. If completion changes nothing observable, it was a chore.
- **Refusal must be a real path**, including walking away and including telling the other
  side. If the only failure state is "not yet done," the quest has no moral weight.

---

## The consequence ledger

One append-only log of world facts, written by every resolution.

```
kessen_reach.starving   = true, since day 41, cause: player
stationers.reputation   = -2
levellers.reputation    = +3
ordmoor_library.status  = saved, 11 volumes recovered
npc.harl_vance.status   = dead, killed_by: player, witnessed_by: [npc.ilse_ward]
salt_compact.hulls      = 6 (was 9)
```

Tier 1 generation **reads this ledger as a tension source.** Ilse Ward saw you kill Harl
Vance, and six weeks later that is a quest. Kessen Reach is starving because of you, and
the quest generated there knows it and says so.

This is the cheapest high-value system in the project — a flat list of facts and some
weighted queries — and it is the difference between a world that reacts and one that resets.

**It is also the save file.** See [14](14-save-system.md).

---

## Pacing without a fixed path

- **Assign region tiers at forge time**, in rings or branches out from the start point,
  baked into the manifest. Tier gates enemy rosters and quest complexity, so difficulty is
  a property of *place* — intuitive to read, and defiable.
- **Budget against the ledger, not against level.** The generator knows the player's actual
  gear, charges, and allies.
- **Gate by access, not by numbers.** Deep sites need diving gear; high sites need climbing.
  Progression through *verbs* is far more legible in an open world than stat walls, and it
  lets a brave player go somewhere early and survive on skill.

## Set pieces

Attach a hand-authored quest to every landmark silhouette — roughly **15–20**, each tied to
a POI *archetype* rather than a location, so they place procedurally but play as designed.
These are what players remember and what makes surrounding Tier 1 material feel like part
of the same game.

## Anti-patterns

- **Infinite quests as a content substitute** — unbounded Tier 0 tells the player nothing matters
- **Fetch chains** — one quest's content spread over three quests' walking
- **Choices that reconverge** — the player notices, and discounts every later choice
- **Quest markers that remove the world** — see [13](13-ui-hud.md) and [11](11-traversal-map.md#navigation-landmarks-over-waypoints)
