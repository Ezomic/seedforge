# 19 — Patrons & Trials

> **Status: PROPOSAL, not a decision.** Every other document in this set states decisions.
> This one does not yet. It exists because the idea is worth keeping and because parts of it
> conflict with decisions already made — see [What this would overturn](#what-this-would-overturn)
> before building anything here.

A layer that gives the player a **named divine patron** and gives the world a set of
**trials** that earn patronage. Originally sketched against Valheim's boss ladder; most of
that structure does not survive contact with this design, and what remains is stated below.

---

## The idea in one line

The pantheon's status is already the central question of every world ([00](00-overview.md)).
Patrons make that question personal: **you pick a side within a pantheon whose existence is
in doubt, and the world tells you whether you chose well.**

## Why it fits — author roles, generate instances

The recurring pattern ([README](README.md#how-to-read-this)) applies cleanly:

- **Patron roles are authored.** A finite hand-written set of gods, each with a domain, a
  voice, a herald, and a set of capability unlocks.
- **The seed places them.** Which patrons are present, which biome holds each sanctuary,
  which trial guards it, and what order they become reachable in are all forge-time
  decisions — Phase 10 (POIs) and Phase 11 (Binding) in [08](08-world-generation.md).

A fixed god-to-biome-to-boss ladder — the Valheim shape — is the thing that does *not* fit.
Premises come before geography here; a hardcoded progression spine would be the one authored
sequence in a game whose pitch is that the sequence is rolled.

> **Rule: the ladder is a set of roles and a partial order, never a fixed list of pairs.**

## The patron roles

Ten authored, **five to seven present per world**, matching the faction count in
[03](03-factions.md). Domain drives which trials, sanctuaries, and capability unlocks attach.

| Patron | Domain | Unlocks tend toward | Herald |
|---|---|---|---|
| **Artemis** | Wild places, the hunt | Remote traversal, tracking, forage | Hind |
| **Apollo** | Prophecy, plague, distance | Foreknowledge, ranged, the Pressure | Hawk |
| **Athena** | Craft, strategy, the polis | Locks, languages, siege, standing | Owl |
| **Hermes** | Roads, thresholds, theft | Fast travel, borders, stealth, trade | — |
| **Hephaestus** | Forge, fire, the made thing | Repair, mechanisms, heat, ruins | — |
| **Poseidon** | Sea, earthquake, horse | Water, boats, unstable ground | Horse |
| **Demeter** | Grain, seasons, the year | Supply, hunger, safe passage | — |
| **Dionysus** | Frenzy, disguise, the crowd | Fear, mobs, infiltration | Panther |
| **Hera** | Trial, marriage, sovereignty | Survival, endurance, oath-work | Peacock |
| **Hades** | The dead, the below | Descent, the unburied, funeral rite | — |

Not every world offers every patron, and the [Cataclysm axiom](01-axioms.md#slot-a--cataclysm)
already removes some outright:

- **Artemis Withdrew** — Artemis is not available. Her sanctuaries are empty, and the design
  intent of that axiom (implemented by *removing* content) extends here for free.
- **Demeter's Grief** — Demeter is present but does not answer. A patron you can swear to and
  get nothing back from is a better expression of that axiom than a missing entry.
- **The Silence of the Oracles** — Apollo's trials exist; the herald never speaks. The player
  works out what the blessing does by using it.

> Patron availability is an **axiom directive block**, not a separate system. It belongs in
> the existing `patrons: { suppress: [...], mute: [...] }` shape alongside `enemies` and
> `traversal` in the [axiom schema](01-axioms.md#data-schema).

## Trials

A trial is a **landmark dungeon or set-piece encounter** ([09](09-dungeons.md)) that stands
between the player and a patron's favour. It is not a boss tier and it does not gate a
material tier.

- Composed from the six authored [enemy roles](05-combat.md#enemies-roles-not-species) against
  a raised encounter budget, dressed in the region's biome and faction. **No new AI.**
- Constrained by the same rule as every other encounter: **no trial may require a specific
  arena.** A trial that only works on flat ground cannot be placed by a generator.
- The reward is an **access or capability unlock** in the sense of
  [07](07-progression.md#what-grows) — a verb, a key, a permission. Never a stat, never a gear
  tier, never a rank.

Trials are ordered by *remoteness and difficulty scoring*, which Phase 10 already computes.
The world therefore has a natural progression order without anyone authoring one.

## Patronage rides the Law — it is not a second power system

The single most important constraint. [Slot B](01-axioms.md#slot-b--the-law) already owns
"how divine power reaches the world," and it reaches into combat, economy, progression and
HUD. A patron blessing that introduced its own currency would be a second Law slot competing
with the first.

**The patron chooses the flavour of the Law's power, never its economics.**

| Law | What a patron is, under this Law |
|---|---|
| **Orichalcum** | Whose ore you refine. Patron changes what the charges *do*, not what they cost |
| **Hecatomb** | Who receives the offering. Same vitality cost, different return |
| **The Mysteries** | **Who initiated you.** The closest natural fit — a patron *is* a body of rites |
| **Temenos** | Which sanctuary types you are attuned to. Patron reshapes your map |
| **The Last Favour** | Whose attention is nearly spent. Which god still answers is the whole question |
| **The Oath** | Who holds the debt, and who comes to collect |

The Mysteries and The Last Favour are the two where patronage adds the most and costs the
least. Prototype against those.

## The herald — per-player narrative with no dialogue system

Each patron sends a **herald**: a bird or beast that appears at a distance, speaks in one or
two lines, and leaves. Non-blocking, no camera control, no conversation UI.

This is the delivery mechanism the design already wants — it fits
[the world is the interface](00-overview.md#design-pillars), the minimal HUD in
[13](13-ui-hud.md), and the audio-first posture in [12](12-audio-atmosphere.md). It is also
the piece that survives into co-op unchanged: heralds are per-character, so two players in
one world hear different gods comment on the same event without any shared cutscene.

> Writing cost is one to two lines per herald per trigger, not a dialogue tree. Budget it as
> barks.

## Prometheus

The conversation this came from put Prometheus at the centre. He does not belong in the
patron table — he is not available to swear to, and that is the point.

**Proposed placement: [Slot E — Anomaly](01-axioms.md#slot-e--anomaly)**, alongside *Tiresias
lives* and *the underworld is physically reachable*, which are the same shape: a mythic figure
who is actually here, hidden at world creation, discovered in play.

> **The Titan is chained here.** Somewhere on this map is a man on a rock and a bird that
> returns. He knows what is foretold — including the part the Pressure has not revealed — and
> he will trade it.

Two reasons this is the right slot rather than a Pressure:

1. It makes the [Slot D](01-axioms.md#slot-d--pressure) prophecy **negotiable** by an
   in-world agent rather than merely readable, which is the tragedy machinery the overview
   claims as the setting's payoff.
2. Freeing him is the kind of world-state consequence the
   [consequence ledger](04-quests.md#the-consequence-ledger) already handles.

The mythological hook worth keeping: in Aeschylus, **Hephaestus forged the chains** and hated
doing it. If Hephaestus is a present patron, his trial line and this anomaly want to meet.

---

## What this would overturn

Honest accounting. These are committed decisions elsewhere, and the proposal is not adoptable
until each is resolved.

| Conflict | Where | Resolution |
|---|---|---|
| **Material tier ladder** (flint → bronze → iron → adamant) | Cut list ([00](00-overview.md#cut-list--not-in-v1)) cuts crafting trees and loot rarity tiers; [07](07-progression.md) caps growth at ~2× | **Dropped.** Trials pay in access and capability. The Valheim gear spine is incompatible with this progression model and is not worth reopening |
| **Seven biomes** | Scope target is **6, fully realised** ([00](00-overview.md#scope-targets)) | **Dropped.** Patron count is decoupled from biome count — several patrons share a biome, placed by remoteness instead |
| **Shrine-building as allegiance** | There is no base building anywhere in this design | **Dropped.** Allegiance is expressed by which sanctuaries you consecrate and hold, which [Temenos](01-axioms.md#slot-b--the-law) already supports |
| **Boss-gated linear progression** | "Premises before geography"; horizontal access progression | **Converted** to remoteness-scored trials with a partial order |
| **Per-player patron in co-op** | Co-op is Tier 2, **post-launch** ([15](15-multiplayer.md)) | **Compatible, deferred.** Patron is character state, and guests already bring character and inventory only. Costs nothing now; do not build for it now |

## `OPEN` questions

| Question | Impact |
|---|---|
| **Is patronage exclusive?** One patron per campaign, or many with competing standing? | Exclusive is stronger and cheaper; it also makes [respec](07-progression.md#respec--required-here-specifically) load-bearing, since a generated world may not contain content for the patron you picked blind |
| **Can a patron be lost?** | Standing is the only axis that can go negative ([07](07-progression.md#what-grows)). Losing a patron mid-campaign is dramatic and risks stranding the player with unlocks they can no longer use |
| **Does this belong in v1 at all?** | It is a content layer over systems that do not exist yet. Nothing here is buildable before the [vertical slice](18-vertical-slice.md) answers its gate |

> **Do not schedule any of this against Milestone 1.** The gate is still *"is one ordinary
> fight tense?"* — patrons are a reason to keep playing a game that already works, not a
> reason one works.

## Related

- Power source this rides on: [01 — Slot B](01-axioms.md#slot-b--the-law), [05](05-combat.md#the-law-axiom-decides-what-combat-spends)
- What trials may pay in: [07 — What grows](07-progression.md#what-grows)
- Where trials get placed: [08 — Phase 10](08-world-generation.md#the-phases), [09](09-dungeons.md)
- Herald delivery: [12](12-audio-atmosphere.md), [13](13-ui-hud.md)
- Co-op implications: [15](15-multiplayer.md)
