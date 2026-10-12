# 15 — Multiplayer

> **Multiplayer is the largest scope multiplier available and cannot be retrofitted into an
> open-world RPG.** The real question is not "should there be multiplayer" but "which
> decisions must be made now."

## The advantage already in hand

The architecture is unusually well-suited to networked play in one expensive way:
**terrain never replicates.**

`seed` + `genVersion` reproduces a bit-identical world in a few hundred bytes. In most
open-world co-op games, world synchronisation is the dominant engineering cost. Here it is a
one-time local forge on first join, cached thereafter. A guest downloads a few hundred bytes
plus the host's delta log — not 400 MB.

**Terrain is free. State is the problem**, and every hard question below is a state question.

## The ladder

| Tier | What | Verdict |
|---|---|---|
| **1** | Asynchronous social | **Build this.** Cheap, and fits a seeded game better than almost any other genre |
| **2** | Host-authoritative co-op, 2–4 players | Achievable, expensive. The realistic ceiling. Post-launch |
| **3** | Persistent shared servers | Every system needs conflict resolution. Not this project |
| **4** | MMO-lite | No |

---

## Tier 1: async social — build this

Because everyone on a seed has a **geometrically identical world**, asynchronous features
work better here than in the games that popularised them.

- **Messages left in the world**, Souls-style. In Souls these are approximate; here they are
  exact — a message on a specific ledge is on *that ledge* for everyone
- **Death markers** showing where players on this seed died. A danger map the community
  generates for free
- **Annotated map sharing** — the genuinely novel one. The map is a
  [player-authored artifact](11-traversal-map.md#the-map-is-an-object-not-a-ui) filled in by
  exploration and NPC directions; sharing *your* map of `KRAKEN-77` with a stranger walking
  the same continent is something no other RPG can offer, and it costs an export function
- **Chronicle export** — the run as readable text. See [14](14-save-system.md#sharing)
- **Featured seeds** rather than competitive dailies. An open-world RPG makes a poor
  speedrun leaderboard, but "this week's world" as a shared topic works

**Cost:** small. **Value for a seed-based game:** disproportionate.

---

## Tier 2: co-op — problems and answers

**Whose world?** The host's. Guests bring character and inventory only, and keep no world
progress. Proven (Valheim, Terraria, Souls) and it **dissolves the hardest question
outright** — consequences belong to the host's campaign, so there is no "whose ledger" problem.

**Quest state.** Host's state is the world's. Guests participate and are paid; their journal
records *"I helped with this,"* not ownership.

**The Pressure clock.** Advances only in the host's session. Guests visiting a further-along
world simply arrive further along.

**Fast travel's time cost.** Party travels together; cost applied once.

**Difficulty — conflicts with the combat design.** Two players trivialise an encounter tuned
for one. The answer that preserves the identity:

> **Add enemies. Never inflate health.**

Health scaling makes lethal, low-TTK combat spongy and destroys the "one bandit is dangerous"
contract everything else is built on. More bodies keeps TTK honest in both directions and
keeps positioning and preparation load-bearing.

**Lethality and downtime.** Low TTK plus co-op means frequent deaths and someone watching.
A downed-and-revive state with a real cost — spent charges, lost cargo, world-clock time.
Not a free pickup.

**Loot in a scarcity economy.** Instance the common tier — consumables, charges, materials —
because shared scarcity in co-op generates arguments, not drama.

> `OPEN`: catalogue uniques are finite per world by design. Instancing them breaks the
> "60 unique items exist in this world" premise; contesting them causes friction. Current
> lean: single-instance with host priority or a party roll.

**Replication load.** Modest given no terrain: player transforms, combat events, in-range
NPC state, delta events. Well within what a simple snapshot replication layer handles.

---

## What must be decided now, regardless

If there is any chance of co-op later, four things must be true from the first week:

1. **Run single-player as a one-player listen server.** The single most important item — the
   difference between "add co-op in three months" and "rewrite the game"
2. **Mutate state only through an event/command layer.** The consequence ledger is already
   event-sourced, so the hard part is accidentally done — but keep the discipline everywhere,
   not just for narrative facts
3. **No singleton assumptions about "the player."** Audio, streaming, encounter spawning, and
   the Pressure must not be written against a single global pawn
4. **Server-authoritative shape for gameplay logic**, even when the server is you

**Cost:** roughly 20–30% added to gameplay systems work. **Retrofitting cost:** several times
that, or the project.

## Recommendation

Ship single-player. Build **Tier 1 async social** — messages and shared annotated maps are
the most on-theme feature set this design could have. Architect to the four rules above so
co-op stays open. Build co-op **after the game is proven**, as the post-launch addition that
doubles the audience rather than the feature that prevents the game existing.
