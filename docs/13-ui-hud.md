# 13 — UI & HUD

## Principle: the world is the interface

Landmark silhouettes, audio telegraphs, guide lighting, biome identity, audible territory
borders — this is a game that communicates through itself. **Every HUD element is a player
looking at the corner of the screen instead of at the thing you made.**

But not full diegetic. Dead Space-style is expensive, frequently worse, and a sixty-hour RPG
has too much information to hide in the fiction.

> **Decision: abstract UI with diegetic styling.** Real screens, but the map is paper and
> the journal is handwriting.

## Combat HUD — five elements

1. **Health** — always visible. Low TTK means the player must never be surprised by their own state
2. **Stamina** — governs everything; must read in peripheral vision
3. **The Law resource** — see below
4. **Damage direction indicator** — non-negotiable in first person
5. **Current target's stagger state** — a minimal reticle-adjacent tick, not a floating bar

**No enemy health bars. No damage numbers.** Both are inconsistent with progression that is
capability rather than arithmetic, and both train the player to read the UI instead of the
animation. Stagger is the currency of melee — that is what deserves the pixel.

## The Law axiom reconfigures the HUD

One data-driven HUD, six configurations.

| Law | On screen |
|---|---|
| **Blood-price** | **Nothing extra.** Health *is* mana. One bar teaches the entire game |
| **Dying** | A count that only ever decreases. Watching it fall permanently is the design in one element |
| **Ore-bound** | Charge count beside carried weight — magic and encumbrance visibly competing |
| **Written** | Loadout slots with remaining uses. The build is on screen and finite |
| **Ley-bound** | No bar — an ambient screen effect intensifying with local strength, so the player reads *the world* |
| **Borrowed** | A debt gauge that fills and does not empty easily |

Blood-price is the standout: the most powerful HUD in the set is the one with the fewest
elements.

## The map is the biggest UI investment — and marketing

Since the map is a [diegetic object filled in through play](11-traversal-map.md#the-map-is-an-object-not-a-ui):

- **Hand-drawn aesthetic** — paper, ink, staining. Undiscovered land is **blank paper**, not
  grey fog
- **Player annotations** — pins with typed notes. Essential, because quest markers are gone
  and the player needs somewhere to put *"the smith said the ford is east of here"*
- **NPC directions write themselves onto it**
- **Region and territory overlays**, toggleable — including the ley map if that axiom rolled

> **A beautiful generated map is what sells a seeded game.** It is the artifact of a seed,
> it is what people screenshot, and it is what makes `KRAKEN-77` mean something to a
> stranger. Over-invest deliberately.

## The journal is the player's memory

With no quest markers, the journal is load-bearing. Each entry records **who asked, what
they actually said, where they said to go, and what you decided** — as prose, not objectives.

It also surfaces the [consequence ledger](04-quests.md#the-consequence-ledger) in readable
form. A *"what I've done and what happened"* view is how the player **perceives** reactivity;
without it, half the systems work invisibly and get no credit. Backed by the
[chronicle](14-save-system.md#snapshot-plus-tail).

## Inventory

Encumbrance is a real system, so weight must be effortless to read: a list with a weight
column, sorting, filtering, and an unmistakable over-capacity state.

**No Tetris grids.** Charming for eight hours, hostile for sixty.

## Character screen — deliberately thin

Four stats, capability unlocks, traversal verbs, and faction standing — **all on one
screen.** A pleasant consequence of [shallow vertical progression](07-progression.md): there
is no tree to render because there is not one.

Faction standing must state plainly what each tier unlocks, since standing gates trainers,
stock, and passage.

## One screen worth adding: the world

A *"what world is this?"* view — discovered axioms, active synergies, the Pressure's advance,
faction territory, region status.

It is where the seed's identity lives, it is the surface for the
[cross-seed codex](07-progression.md#across-seeds), and it turns "I'm in the Age of Salt"
from flavour text into something consultable.

## Accessibility

Standard provisions apply, plus one obligation the combat design creates:

> **Audio telegraphs carry combat fairness, so every audio cue needs a visual fallback.**
> Directional indicators for off-screen wind-ups; a visible tell for every attack that
> currently only sounds. This is a requirement, not a stretch goal.

Also:

- Precise waypoints as an option, off by default
- Colourblind-safe faction colours, since territory is colour-coded
- Full subtitles for Principal VO

## Cut list

- **Minimap** — it deletes landmark navigation, which is most of what makes the world worth
  generating
- **Damage numbers and XP popups**
- **Loot rarity colours** — there are no rarity tiers in the [loot design](06-loot-economy.md),
  so there is nothing to colour
- **Crafting UI** — there is no crafting tree
- **Quest markers by default**

## Implementation

UMG is fine. Keep the HUD data-driven so the Law swap is configuration rather than six
implementations. The usual performance trap is complex widgets ticking every frame — bind to
events, invalidate deliberately, and check it early rather than at the end.
