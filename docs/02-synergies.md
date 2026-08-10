# 02 — Synergies

Random axiom combination produces variety. **Authored synergies produce worlds people
tell stories about.**

## The quality bar: the new-noun test

> Author a synergy only when the pair produces something **neither axiom implies alone** —
> a faction, a dungeon type, a traversal verb, or an item class.

- ❌ *Long Winter + Hunger* → "very hungry." Additive. Not a synergy.
- ✅ *Silence + Blood-price* → no animals means no animal blood, so **every point of magic
  came out of a person.** New economy, new faction, new moral floor.

A second useful signal: a good synergy answers a question the player would otherwise ask
sceptically. *"Why is this ore so contested?"* → because it is under 200 m of water.

**Write the two-sentence description before writing any code.** If you can't, there is no
synergy there and you have saved yourself the implementation.

---

## Tier 1 — flagships

Deep. Each gets a faction, a dungeon type, and a mechanical change. Budget 1–2 weeks
each. **Ship ~8.**

### Cataclysm × Law

**Gods Died + Borrowed** — the gods *were* the lenders; they were killed to void the debt.
Everyone alive descends from defaulters, and something is reconstituting the ledger.
→ Faction: **the Auditors**. Dungeons: reliquary-vaults of contract fragments.
Mechanic: borrowed magic is **traceable** — casting draws collection toward you.

**The Silence + Blood-price** — no animals, so no animal blood. Settlements that still
cast are visibly depopulated.
→ Faction: **the Tithe-houses** (legal, regulated blood levy) vs. free cities that
renounced magic and are defenceless. Currency: bottled vitality. *Gold is human life.*

**The Drowning + Ore-bound** — the ore is all under 200 m of water.
→ Traversal: diving gear as a full progression axis. Dungeons: pressurised wrecks and
flooded mines with air management. Faction: dive-guilds with appalling mortality.
The generator now places the richest sites in the most lethal ones automatically.

**The Sundering + Ley-bound** — the land broke *along the ley lines*. The chasms **are**
the magic.
→ Power is strongest at the most dangerous edges. Vertical traversal and magical
strength become the same axis.

**The Long Winter + Written** — during the winter, people burned books for warmth. Magic
wasn't lost, it was *incinerated*.
→ Every surviving book is one someone chose to freeze beside. Faction: **the Ash-readers**,
reconstructing spells from partial burns. Dungeons: sealed libraries under ice.

**Nothing Broke + Dying** — the golden age is real and quietly ending. Perhaps 40 years
left, and maybe nine people know.
→ Tonal inversion: a **conspiracy**, not a survival game. The people who know are lying
to prevent a panic that would kill more than the shortage will.

### Cataclysm × Powers

**Gods Died + Theocracy** — the church knows, administers rituals to a corpse, and kills
whoever finds out.
→ **The Custodians** (inner circle) vs. the sincere clergy (who would die for the lie).
Main quest becomes an information-control thriller. Dungeon: the crypt with the body.

**The Silence + Occupation** — the occupiers brought their animals. The only living beasts
belong to the invaders.
→ A horse on the horizon means *enemy*, with no UI. The resistance can never have cavalry.
Nearly free to implement; enormously evocative.

**Sundering + No one** — regions cut off by geology, developing in isolation for two
centuries.
→ Structurally valuable: justifies a distinct architecture kit, culture, and dialect per
region. A narrative solution to procedural homogeneity.

**Drowning + Merchant compacts** — the compacts exist *because* drowning made trade the
only survival mechanism.
→ Nothing grows; all food is imported; trade routes are life support. The player can
starve or save a settlement by touching its shipping.

**Long Winter + Petty kingdoms** — the kingdoms are the groups that survived together.
Borders are grain-store borders; alliances are *who fed whom*.

### Law × Powers

**Written + Theocracy** — the church holds a literal monopoly on literacy. Reading is a
crime, every spellbook is contraband, and learning magic *is* becoming a criminal.

**Ore-bound + Occupation** — they invaded for the ore. The mines are the political centre;
resistance work is extraction sabotage.

**Dying + Merchant compacts** — the compacts are cornering the remaining charges. Prices
climb measurably over campaign time; a scroll that cost pocket change in act one costs a
house in act three.

**Ley-bound + Petty kingdoms** — borders drawn around ley nodes rather than rivers. The
map looks irrational until the player sees the overlay.

---

## Tier 2 — Pressure synergies

Text-only or light. Cheap (an afternoon each). Can have dozens. Since Pressure is hidden
at creation, these land as revelations.

- **Drowning + Splitting** — water still rising *and* land still breaking. Settlements
  visited in act one are gone by act three. The strongest possible proof the clock is real.
- **Written + Blight** — the blight eats paper. Magic is erased at a measurable rate;
  every library not reached in time is content permanently lost from that save.
- **Borrowed + Returning** — the thing coming back is the lender. The countdown is a due date.
- **Silence + Wakening** — nothing living moves, but the dead are standing up.
- **Ore-bound + Rival** — the rival is winning because they hold the ridge. Reframes a
  losing war as a logistics problem the player can attack.

## Tier 3 — Anomaly synergies

Rare. Jackpot identity.

- **Immortal witness + Gods Died** — one person watched it happen and has lied for 200 years.
- **Second layer + Sundering** — the sundering exposed it; chasms are the entrances.
- **Two moons + Ley-bound** — ley strength on a dual-lunar cycle. Magical geography shifts
  on a predictable schedule; learning the calendar is real skill expression.
- **Sixty-hour night + Blood-price** — power and danger on a long cycle.
- **World's edge + Drowning** — the sea pours off the edge and is *still rising*. Something
  is refilling it.

## Triples — ~1% of seeds

Author few; the ones you do become legendary.

- **Gods Died + Borrowed + Returning** — the lender is coming back and the shields are dead.
- **Silence + Blood-price + Hunger** — nothing to eat and nothing to bleed.
- **Drowning + Ore-bound + Merchant compacts** — an underwater gold rush with company towns.

---

## Firing rules

1. **Cap active synergies at two** (three absolute maximum). A world running six is noise —
   the player cannot hold the premise, and nothing feels causal.
2. Resolve in priority order: **Cataclysm×Law → Cataclysm×Powers → Law×Powers → Pressure →
   Anomaly.** Once two have fired, stop checking.
3. A fired synergy **overrides** default flavour text and injects its faction, dungeon type,
   and world name.
4. Incoherent pairs go on the axiom `conflicts` blocklist rather than getting a
   reconciliation. Blocking is cheaper and better.

## Content tiers

| Tier | Cost | Contents | Count at ship |
|---|---|---|---|
| 1 | 1–2 weeks | Faction + dungeon type + mechanic + prose | ~8 |
| 2 | An afternoon | World description + dialogue lines | Dozens |

Most players never know which tier they got, because the **world description** is what
forms their first impression.
