# 10 — NPCs

The cheapest way to make a generated world feel authored, and the one most procedural RPGs
get wrong.

## The failure mode

Procedural NPCs default to **many, shallow, interchangeable** — generated names, generic
barks, no memory, no relationships. Players learn within twenty minutes that people are
scenery, and the world is uninhabited thereafter no matter how many bodies are in it.

**Invert it: few, deep, persistent.** A hundred people the player comes to recognise beats
a thousand they cannot.

## Four tiers

| Tier | Count | What they get |
|---|---|---|
| **Principals** | 10–15 | Hand-authored voice, real arcs, VO, exposed faces. Cast into roles by the seed |
| **Regulars** | 60–100 | Named, homed, relationships, wants, memory. Systemic dialogue |
| **Residents** | ~300 | Named, located, minimal interaction. Population and casualties |
| **Crowd** | ambient | Unnamed. Atmosphere only |

Principals are faction leaders, Custodians, and synergy-specific characters — the Auditor
who explains the ledger, the Custodian who knows the god is dead. Written once, **cast** by
the seed.

---

## Anatomy of a Regular

Generate a **person embedded in a situation**, not a personality quiz:

- **An economic role**, drawn from the settlement's actual economy layer — the salt-panner
  exists because the axioms made salt abundant here
- **Two or three traits** from a pool of ~20
- **Three to five relationships**: family, rival, creditor, lover, grudge
- **One concrete want**, tied to live world state — *"wants her brother off the dive crews,"*
  not *"seeks adventure"*
- **One fear**
- **A memory record** keyed to the [consequence ledger](04-quests.md#the-consequence-ledger)

> The relationship web is **load-bearing.** A person alone is a stat block; a person inside
> a web is a character. It is also what makes the ledger bite: kill Harl Vance and his
> sister Ilse reacts, Ilse's employer reacts, and the Compact she works for adjusts — from
> one death and a graph nobody had to author.

## Dialogue: author lines, select by state

> **Never procedurally generate dialogue text.** Generated prose reads as generated
> instantly, and the uncanniness costs more than the variety gains.

Write a large pool of lines tagged by **trait × topic × relationship × world-state**, and
let each NPC select what matches their actual situation. Every line is hand-written; the
*combination* is unique.

A salt-panner with the `bitter` trait whose brother works a dive crew, in a town short of
grain because the player sank a hull, has plenty to say — all of it authored. **Specificity
comes from the state, not from a text generator.**

### On LLM-generated dialogue

Use it as an **authoring accelerator at development time**, not a runtime system. Have it
produce variations on authored lines in a character's voice, then read them and keep the
good ones. Bake the output; ship the text.

Runtime generation means latency, per-player cost, tone drift, and — fatally — no guarantee
the NPC will not confidently state something false about game state.

## Memory, kept small

NPCs read a handful of facts from the ledger:

- Have you helped or harmed **them**
- Have you helped or harmed their faction or their relations
- Did they, or someone they trust, witness something
- Your standing, reputation, and debts

**Legibility matters more than depth.** A player who cannot work out *why* someone is cold
to them experiences a bug, not a consequence.

## Death and role succession

The hardest problem in a persistent world: essential NPCs make it fragile, killable NPCs
break quests.

> **Roles are the durable thing; people are instances.**

When a Regular dies, their role passes to someone in their relationship web — an apprentice,
a sibling, a rival who has been waiting. The successor **remembers how the position became
vacant.**

The quest binder rebinds to whoever currently holds the role, so nothing breaks — and a town
where the new harbourmaster is the dead one's brother, and he knows what you did, is worth
far more than an invulnerable questgiver.

This is the same pattern as [faction roles](03-factions.md), [enemy roles](05-combat.md#enemies-roles-not-species),
and [dungeon tiers](09-dungeons.md). It is the project's core design language.

## Schedules: shallow, deliberately

Full Ultima VII-style simulation is a large cost players mostly cannot perceive.

**Four states** — home, work, social, sleep — driven by time of day. **No offline
simulation.** NPCs are simply where they should be when the player looks.

Spend the savings on reactivity. Players notice being *remembered* far more than they notice
being *simulated*.

## Production

**Voice:** Principals only. Everyone else gets greeting vocalisations, effort sounds, and
text. Full VO for a hundred Regulars would consume the project — and a systemic dialogue
pool is fundamentally incompatible with recorded lines anyway, which is a design argument
for text as much as a budget one.

**Faces:** a modest base set with procedural variation, plus per-region tinting so isolated
Sundering valleys look isolated. MetaHuman is tempting and expensive at this population
count — measure before committing.

> Most faces are **covered** — helmets, hoods, masks, weather gear — which the fiction fully
> justifies and which removes facial rigging, animation, lipsync, and uncanny valley from
> the critical path. See [16](16-art-direction.md#characters-and-the-trick-the-fiction-hands-you).

## Why this layer matters

Recall the [Kessen Reach quest](04-quests.md#worked-example): save the library or feed the
town. That choice is only heavy if "the town" means *Ilse, who you have met four times,
whose brother dives for the Compact, and who told you last month she was worried about the
grain.*

Everything else in the design generates situations. **The NPC layer is what makes anyone
care about them.**
