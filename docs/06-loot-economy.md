# 06 — Loot & Economy

## No affix soup

The default procedural answer is stat-rolled gear — *Iron Sword of the Bear +7*. It is
wrong here for a specific reason: **the randomness is already spent on the world.** If the
map is generated *and* the loot is rolled, nothing is placed, and the player stops reading
the world as authored.

Diablo gets away with it because the loot roll *is* the game. Here, exploration is the
game, and exploration only feels good when specific things are in specific places.

## The catalogue principle

Author a **finite catalogue** and let the seed decide only *where things are* and *who
holds them*.

- **~60 unique named items**
- **~200 notable items**
- A fully generic mundane tier below that

The seed distributes them by axiom: the Written world stocks its catalogue in libraries,
the Ore-bound world buries it in mines.

Payoff: *"I found the Ledger Knife in the drowned cathedral this run; it was in a mountain
fort last time"* is a meaningful sentence. Nobody has ever said anything meaningful about
a rolled affix.

## The best loot is a verb

Prefer **capability over numbers**. The most valuable thing a chest can contain answers
*"what can I do now that I couldn't before?"*

- Diving gear, climbing kit, cold-weather rig — these are **map access**, the strongest
  reward available in a generated world
- A key, a writ, a faction seal
- A language, a cipher, a chart
- Map fragments revealing POIs — which is why [the Surveyors](03-factions.md#custodians)
  are worth having as a faction

This also resolves quest pacing: gating regions by traversal verbs is legible, defiable,
and avoids stat walls a generated world cannot reliably tune.

## Class ratios

Because combat is rare and expensive, most loot is **what pays for combat**.

| Class | Share | Role |
|---|---|---|
| Consumables & charges | ~55% | Refill the cost of fighting. The economy's pulse |
| Materials & salvage | ~20% | Crafting inputs, repair, trade goods. Regionally varied |
| Information | ~10% | Maps, ledgers, keys, rumours. Cheap to author, disproportionately loved |
| Coin | ~10% | Mundane transactions only |
| Gear | ~5% | Rare, authored, named, remembered |

5% gear is the right number when every piece is hand-made and permanent.

## Two currencies

**Coin buys bread. Charge buys power.** The world's magic resource is the high-value
currency, and it is the same resource combat spends.

| Law | Hard currency | Economic character |
|---|---|---|
| Ore-bound | Refined ore charges | Heavy — wealth is physically burdensome and stealable |
| Blood-price | Bottled vitality | Gold is human life, and everyone knows it |
| Written | Spellbooks, pages | Wealth is legible, burnable, forgeable |
| Ley-bound | Node deeds and claims | Wealth is *real estate* — not portable at all |
| Dying | Remaining charges | Deflationary; everything costs more every month |
| Borrowed | Transferable debt | You can pay in obligations that are not yours |

One data-driven currency swap changes how looting, trading, hoarding, and fighting feel,
without a second economy implementation.

## Encumbrance

Weight earns its place here because the entire game is built on "you cannot have
everything." It turns looting into a decision instead of a hoover.

Two rules keep it from being miserable:

1. Generous but real limit.
2. **Never make the answer be walking back.** Carts, caches, stash points at settlements.
   The interesting question is *what do I leave behind*, not *how many trips*.

Ore-bound is the standout: when currency is heavy, being rich is a tactical liability.

## Sinks — plan the drains before the faucets

Every RPG economy collapses around hour twenty because designers build faucets and forget
drains.

- **Repair and maintenance** — the reliable baseline
- **Faction dues and bribes** — access is rented, not bought
- **Paid fast travel** — passage, mounts, guides. Free fast travel deletes both the economy
  and the world
- **Rebuilding** — funding a settlement, a bridge, a route. The best sink in the genre,
  because it changes the map visibly
- **Information** — the Surveyors sell knowledge at real prices
- **Casting itself** — under Dying or Blood-price, power *is* the wallet

Target: the player feels **briefly rich two or three times per campaign**, never continuously.

## Merchants

The axiom schema already emits `economy: { scarce, abundant }` per region. Feed it in
directly:

- Stock and price derive from local scarcity, faction control, and route status
- The Drowning coast pays absurdly for grain and gives away salvage
- **When the player cuts a trade route, prices move** — so the quest layer and the economy
  layer are the same system. A Compact hull sunk three weeks ago shows up as a price paid.

That last one is nearly free once the [consequence ledger](04-quests.md#the-consequence-ledger)
exists.

## Verdicts

**Durability:** yes, on weapons only, as a **repair cost** — not breakage. A steady sink and
a reason to visit settlements. Item destruction is just punishment.

**Enemy drops:** minimal. Bandits carry what bandits would carry — a little coin, their
weapon, some food. In a world where fighting is expensive, a fight that does not pay for
itself is a meaningful loss, and that is the point. **Loot lives in places, not corpses.**

## Cut list

No crafting tree with a hundred recipes · no gear tiers named after metals · no random
affixes · no vendor trash that exists only to be sold · **no loot that is strictly better
than what it replaces** — every real gear piece is a trade-off, because a trade-off is a
decision and an upgrade is just a delay.
