# 14 — Save System

> **The [consequence ledger](04-quests.md#the-consequence-ledger) *is* the save file.** The
> quest system's tension source, NPC memory, and save data are one artifact. That
> convergence is the payoff of everything upstream.

## Three parts

| Part | Size | Lifetime |
|---|---|---|
| **The forged world** | ~200–400 MB | Written once at new-game, immutable forever |
| **The ledger** | ~1–3 MB | Grows slowly, snapshotted per save slot |
| **Player state** | KB | Per slot |

### The structural decision

> **A campaign owns the world; save slots are ledger snapshots inside it.**

```
Saves/
  Campaign_KRAKEN-77/
    world/                    immutable, written once
      manifest.json
      heightmap.r16
      weightmaps/
      dungeon_layouts.bin
      genVersion
    slots/
      autosave_01.sav         ~1 MB
      quicksave.sav
      manual_03.sav
```

Twenty saves in one campaign cost one world folder and twenty tiny files. Without the split,
every quicksave writes 300 MB, which is unshippable.

The alternative — store only the seed and re-forge on load — is tempting for disk space and
fatal in practice: nobody accepts a 60-second forge on every load.

## Deterministic entity IDs

> **Every generated entity needs a stable ID derived from world structure, not spawn order.**

```
chestId = hash(worldSeed, poiId, roomIndex, slotIndex)
npcId   = hash(worldSeed, settlementId, residentIndex)
```

If IDs come from instantiation order, the delta *"chest 47 is looted"* resolves to a
different chest depending on how the level streamed in — producing the classic procedural
bug where loot reappears, doors relock, and dead NPCs return, **intermittently and
unreproducibly.**

Derive IDs from world structure and this entire class of bug never exists.

## Deltas with default-elision

Store only what differs from the forged baseline. A chest never opened has no entry; an NPC
nobody spoke to has no record. This is what keeps a sixty-hour save under a few megabytes.

Six categories cover it:

- **Entity state** — looted, opened, killed, destroyed
- **World state** — territory, prices, Pressure position, settlement status
- **NPC state** — alive, role holders, memory, relationships
- **Quest state**
- **Discovery** — map revealed, POIs found
- **Player**

## Snapshot plus tail

Pure append-only gives perfect history and a replay cost that grows all game. Pure snapshots
give fast loads and no history. Use both — standard event sourcing:

- **In memory:** append-only log
- **On disk:** last snapshot + the tail since it
- **On load:** restore snapshot, replay tail

Keep a second, much smaller **chronicle** — notable events only, human-readable — for the
journal's history view and for [export/sharing](15-multiplayer.md#tier-1-async-social--build-this).
The state ledger holds current truth; the chronicle holds the story.

## Autosave, and save-scumming

The consequence design assumes choices stick, and reloading defeats it. That fight cannot be
won, so do not pick it. What *can* be done:

- **Autosave after consequential moments, not before** — so reloading costs real progress
  rather than five seconds
- **Do not telegraph the reload point.** No *"you have made an enemy"* popup at the moment of
  choice; let it surface later, through an NPC, in the world
- Autosave on settlement entry/exit, dungeon entry, quest resolution, and a timer

### Ironman

Offer **Ironman** as an option: one rolling save, no manual slots. Trivial to implement (a
flag on the save policy), and it is the mode where the ledger, role succession, and the
Pressure clock all finally sing.

**Offer it prominently; never force it.** No permadeath in a classic RPG.

## Versioning

> Because the world is baked to disk, **patching the generator cannot break an existing
> campaign.** Phase 2's erosion constants can change in v1.3 and every in-progress save is
> untouched.

Store `genVersion` in the world folder and never regenerate an existing campaign's terrain.

**Content patches** are harder — new items and quest rules must apply to already-forged
worlds. Two rules make it survivable:

1. **Stable string IDs everywhere, never array indices.** Adding an entry to the item
   catalogue must not change what `item_47` means — because it will, and it will ship, and
   someone's sixty-hour save will fill with the wrong swords.
2. **Version the save format with an explicit migration path**, written before it is needed.

## Corruption safety

Write to a temp file, fsync, atomically rename. Keep three rotating backups. Checksum the
ledger.

Twenty minutes of work that everyone postpones until the first player loses a sixty-hour
campaign — at which point it is the only thing anyone remembers about the game.

## Sharing

Determinism gives this for free: **`seed` + `genVersion` reproduces the identical world** on
another machine in a few hundred bytes. That is the whole community pitch — post a seed, a
stranger walks your continent.

Also worth shipping: **chronicle export** as readable text. *"Here's what happened in my Age
of Salt run"* is shareable content that costs a text serialiser.

## UE notes

- Use `USaveGame` for player state if convenient, but **write the ledger yourself** —
  compressed binary, own schema. UE's reflection-based serialisation is the wrong tool for a
  few hundred thousand small facts
- **Never serialise actor pointers or soft references to generated actors.** Serialise IDs
  and re-resolve on load; generated actors do not exist when the save is read
- **Save/load round-trip is the one automated test worth insisting on.** See [17](17-tech-stack.md#testing--and-the-tool-that-will-save-you-most)
