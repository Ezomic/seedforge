# 07 — Progression

## The constraint that decides everything

**Cap total power growth at roughly 2× over a full campaign**, not 20×.

> The test: **an ordinary bandit should still be able to kill you in act three if you are
> careless.**

That single constraint pays for itself repeatedly:

- Forge-time region tiers stay meaningful for the whole game
- **No level-scaling is ever needed** — which everyone can feel and everyone hates
- Consumables, positioning, and preparation remain relevant at hour sixty
- The generator never has to solve "this whole biome is now trivial"

Progression is **horizontal**, with a thin vertical spine. The player does not get bigger
numbers; they get more answers.

## What grows

**Access** — the primary axis. Diving gear, climbing kits, languages, keys, faction writs.
Progress is measured in *where you can go*, the most satisfying unit available in a
generated world.

**Capability** — discrete verbs, never percentages. *"You can pick locks of this class,"
"you can read pre-Sundering script," "you can cast while moving."* About **30–40 unlocks
total, with no ranks** — a rank is a delay pretending to be a choice.

**Standing** — faction reputation gates trainers, stock, bases, and safe passage. The only
progression axis that can go *negative*.

**Knowledge** — mostly player-side. Which telegraph means what, which merchant pays best,
what a ley node looks like from a ridge. Partly resets per seed, which is exactly why new
seeds stay interesting.

**The spine** — four stats only: **health, stamina, carry, attunement.** Each grows
50–100% over the campaign. That is all.

## How it is earned — not from kills

Kill-XP is incoherent with a game where combat is rare and avoidance is valid; it would
punish the exact play the design is built for.

**1. Discovery.** XP for reaching places, first-time events, solving things, mapping
regions. This rewards precisely what the game is about — and the world generator becomes
the XP dispenser, since sites are already scored for remoteness and difficulty, so the
reward tunes itself.

**2. Milestones.** Quests, set pieces, and faction ranks grant specific named unlocks
rather than points. This is where capability actually arrives, and it keeps the quest layer
load-bearing.

**3. Trainers.** Buy skills with coin and *time*, from people who must be found and whose
factions must tolerate you. The best economy sink, it gates progression behind access and
standing, and it makes settlements worth returning to.

**Deliberately excluded:** kill-XP, and use-based grinding. Use-based systems look thematic
and immediately teach players to jump into walls for an hour — worse, they reward
repetition in a game whose entire pitch is that generated worlds must avoid repetition.

## The Law axiom decides what "stronger" means

| Law | Progression is… |
|---|---|
| **Ore-bound** | **Logistics.** Carry capacity, refining efficiency, supply lines. Power is infrastructure |
| **Blood-price** | **Vitality.** HP pool *is* mana pool — one stat governs what you survive and what you can do |
| **Written** | **Collection.** Your library is your character sheet; loadout size is the curve |
| **Ley-bound** | **Cartography.** Attunement to node types plus knowing where they are. Your map is your build |
| **Dying** | **Conservation.** You do not get stronger, you get *cheaper*. Efficiency as progression |
| **Borrowed** | **Your debt ceiling.** More power available, more collectors interested |

Blood-price collapsing health and mana into one stat is the most elegant of these.
Dying-as-efficiency is the one nobody else is doing.

## Respec — required here specifically

Normally build commitment is right. Not in a generated world: **the player cannot plan
around content they have not seen.** Investing in diving before knowing whether this seed
has anything underwater is a blind bet, and punishing it is unfair.

**Decision:** allow respec as an expensive in-world service — a trainer, a faction favour,
a ritual. It costs real money (another sink), takes travel, and is therefore a decision
rather than a menu toggle.

## Failure that is not death

The [consequence ledger](04-quests.md#the-consequence-ledger) makes non-lethal failure
states viable: losing a fight can cost cargo, standing, a companion, a route, or a week of
world-clock time while the Pressure advances.

Non-lethal failure is what makes a lethal combat system bearable over sixty hours, and it
is far more interesting than a reload.

## Across seeds

**Carry over nothing mechanical.** A new seed must be a real new start, or the world's
danger evaporates on run two.

What carries: a **codex** of axioms encountered, synergies discovered, worlds forged, seeds
completed. Informational and cosmetic. The player's own knowledge is the meta-progression,
and it is the only kind that does not cheapen the next world.

Surfaced on the [world screen](13-ui-hud.md#one-screen-worth-adding-the-world).
