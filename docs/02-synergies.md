# 02 — Synergies

Random axiom combination produces variety. **Authored synergies produce worlds people tell
stories about.**

A recurring bonus of the Greek frame: several synergies land on myths that already exist,
so the combination reads as recovered rather than invented.

## The quality bar: the new-noun test

> Author a synergy only when the pair produces something **neither axiom implies alone** —
> a faction, a dungeon type, a traversal verb, or an item class.

- ❌ *Demeter's Grief + The Famine* → "very hungry." Additive. Not a synergy.
- ✅ *Artemis Withdrew + Hecatomb* → no beasts means no sacrificial animals, so **the
  hundred-ox offering must be paid in something else.** New economy, new faction, new moral floor.

**Write the two-sentence description before writing any code.** If you can't, there is no
synergy there.

---

## Tier 1 — flagships

Deep. Each gets a faction, a dungeon type, and a mechanical change. **Ship ~8.**

### Cataclysm × Law

**Artemis Withdrew + Hecatomb** — there are no animals left to sacrifice, and the rites
specify quantities.
→ Faction: **the Hekatombaia**, who administer a legal, regulated human levy with
immaculate procedure — against the poleis that abandoned sacrifice entirely and are
therefore powerless. Currency: bottled vitality. Iphigenia as precedent rather than tragedy.

**Deukalion's Flood + Orichalcum** — the orichalcum is Atlantean, and Atlantis is under the
water. *This synergy is simply the myth.*
→ Traversal: diving as a full progression axis. Dungeons: pressurised wrecks and flooded
mines with air management. Faction: dive-guilds with appalling mortality.

**Demeter's Grief + The Mysteries** — the Eleusinian Mysteries *were* Demeter's cult, and
their promise was Persephone's return. She did not return.
→ The central rite of the world is a rite that demonstrably failed. Every initiate was sold
an afterlife that isn't coming. Faction: **the Hierophants**, who continue anyway, and the
lapsed who want their years back. Dungeons: the telesterion, sealed.

**The Silence of the Oracles + The Oath** — the gods witnessed and guaranteed oaths. With
them silent, **nobody can be released from one.**
→ Everyone alive is bound by oaths their grandparents swore, and there is no absolution.
Faction: **the Oathkeepers**, who adjudicate an unappealable law. Mechanic: sworn power is
traceable, and the Styx does not forget.

**The Unbinding + Temenos** — the earth broke **along the sacred ways**. The chasms are
where the temene were.
→ Power is strongest at the most dangerous edges. Vertical traversal and divine strength
become one axis.

**The Age of Kronos + The Last Favour** — the golden age persists on a dwindling divine
attention, and perhaps nine people have done the arithmetic.
→ Tonal inversion: a **conspiracy**, not a survival game. The ones who know are lying to
prevent a panic that would kill more than the shortage will.

### Cataclysm × Powers

**The Silence of the Oracles + The Amphictyony** — the league that governs the great
sanctuary knows the oracle has said nothing for two centuries, and has been **composing its
pronouncements** ever since.
→ Faction split: the inner circle who compose, and the sincere priesthood who transmit in
good faith. The main quest becomes an information-control thriller with a real institution
at its centre.

**Artemis Withdrew + The Satrapy** — the occupiers brought their animals. The only living
beasts belong to the invaders.
→ A horse on the horizon means enemy, with no UI. The resistance can never have cavalry.
Cheap to implement, enormously evocative.

**The Unbinding + The Dark Age** — regions cut off by geology, developing in isolation for
two centuries.
→ Structurally valuable: justifies a distinct architecture kit, dialect and cult per region.
A narrative solution to procedural homogeneity.

**Deukalion's Flood + The League** — the League exists *because* the flood made shipping the
only survival mechanism.
→ Nothing grows; all grain is imported; trade routes are life support. The player can starve
or save a polis by touching its shipping.

**Demeter's Grief + The Poleis** — the city-states are the groups that survived the first
winter together. Borders are granary borders; alliances are *who fed whom*.

### Law × Powers

**The Mysteries + The Amphictyony** — initiation is controlled by the temple league, and
unauthorised initiation is **sacrilege**, a capital charge. (Attested: profaning the
Mysteries nearly ended Alcibiades.)
→ Learning power *is* becoming a criminal. One rule, and the whole progression system gains
stakes.

**Orichalcum + The Satrapy** — they invaded for the metal. The mines are the political
centre; resistance work is extraction sabotage.

**The Last Favour + The League** — the League is cornering what favour remains. Prices climb
measurably over campaign time.

**Temenos + The Poleis** — borders drawn around sanctuaries rather than rivers. The map looks
irrational until the player sees the overlay.

---

## Tier 2 — Pressure synergies

Text-only or light. An afternoon each; can have dozens. Since Pressure is hidden, these land
as revelations.

- **Deukalion's Flood + The Opening** — the water is still rising *and* the land still
  breaking. Settlements visited in act one are gone by act three.
- **The Mysteries + The Miasma** — pollution spreads through the initiated. Every sanctuary
  not purified in time is knowledge permanently lost from that save.
- **The Oath + The Return** — the thing returning is the guarantor. The countdown is a
  reckoning.
- **Artemis Withdrew + The Unburied** — nothing living moves, but the dead are standing up.
- **Orichalcum + The Foretold** — the conqueror is winning because he holds the ridge.
  Reframes a losing war as a logistics problem the player can attack.

## Tier 3 — Anomaly synergies

- **Tiresias + The Silence of the Oracles** — one man has been prophesying for two hundred
  years without divine input, and knows exactly what that means.
- **The Labyrinth + The Unbinding** — the breaking exposed it. The chasms are its entrances.
- **The reachable underworld + The Unburied** — the dead are not returning by accident;
  they are walking back out.
- **A polis out of time + The Last Favour** — favour there has not yet run down.
- **The door in the sea + Deukalion's Flood** — the water pours through it and is *still
  rising*. Something is refilling it.

## Triples — ~1% of seeds

- **Silence of the Oracles + The Oath + The Return** — the guarantor is coming back and
  there is no one left to intercede.
- **Artemis Withdrew + Hecatomb + The Famine** — nothing to eat and nothing to offer.
- **Deukalion's Flood + Orichalcum + The League** — an Atlantean gold rush with company towns.

---

## Firing rules

1. **Cap active synergies at two** (three maximum). A world running six is noise.
2. Resolve in priority order: **Cataclysm×Law → Cataclysm×Powers → Law×Powers → Pressure →
   Anomaly.** Once two fire, stop checking.
3. A fired synergy **overrides** default flavour text and injects its faction, dungeon type,
   and world name.
4. Incoherent pairs go on the `conflicts` blocklist rather than getting a reconciliation.

## Content tiers

| Tier | Cost | Contents | Count at ship |
|---|---|---|---|
| 1 | 1–2 weeks | Faction + dungeon type + mechanic + prose | ~8 |
| 2 | An afternoon | World description + dialogue lines | Dozens |

Most players never know which tier they got, because the **world description** is what forms
their first impression.
