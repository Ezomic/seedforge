# 17 — Tech Stack & Project Structure

## The move that shapes everything

> Build **`libworldforge`** as pure C++ with **zero Unreal dependencies** — no `FVector`, no
> `UObject`, no engine headers. Compile it twice:
>
> - into a **CLI tool** for iteration, testing, and batch runs
> - into a **UE module** for the shipped forge step

Same code, two hosts. This buys two-second compile cycles and image-diffable output while
designing world generation, and a single implementation at ship time.

The deciding constraint is **embeddability**, not speed: the forge runs on the player's
machine, so a separate executable plus IPC is awkward. A library solves it cleanly.

The alternative — writing generation inside UE from day one — means a two-minute
compile-and-launch for every erosion constant tweak. There will be hundreds of those.

## Layout

```
seedforge/
  docs/                 this design document
  libworldforge/        pure C++, no UE. Phases 0–11
    src/                tectonics, erosion, hydrology, climate,
                        biomes, settlement, roads, politics, poi, binding
    tests/              golden-image + determinism tests
  forge-cli/            CLI wrapper → PNGs, manifest, batch runs
  data/                 SHARED source of truth (JSON)
    axioms/  synergies/  catalogue/  kits/  grammars/
  viewer/               local web map viewer (Node)
  game/                 UE 5.8 project
    Plugins/WorldForge/ UE wrapper around libworldforge + Phase 12 bake
    Source/             Core, Gameplay, WorldRuntime, UI
    Content/
  tools/                data validation, JSON→DataAsset import
  art/                  Blender sources, generated textures and audio
  tracker/              the backlog, as JSON
```

`art/` and `tracker/` are not in the original sketch but sit at the root for the same
reason `data/` does: both are source material read by tooling rather than build output, and
both are pure text or small binaries that behave under Git.

**`data/` being shared is the important bit.** Axioms are read by *both* the generator and
the game; two copies will diverge within a month.

## Languages and data

**Generator:** prototype in Python/NumPy — terrain work is all array operations and iteration
speed matters more than runtime. Then port phase by phase to C++, diffing debug images as you
go. The [immutable-artifact structure](08-world-generation.md#shape-a-batch-compiler) makes
the port incremental and verifiable.

**Game:** UE C++ for anything per-frame or per-entity — the ledger, quest binding, combat, AI.
Blueprint for composition, UI, and designer-facing tuning.
> **No gameplay *logic* in Blueprint** — it cannot be diffed and cannot be tested.

**Data:** author in **JSON** — readable, diffable, tool-agnostic, and parseable by the
generator. Import to UE DataAssets at build time so the editor experience stays good.
> Do not author in DataAssets; the generator cannot read them and you are back to two sources
> of truth.

## Version control

**Git + LFS** to start. The separation helps: `libworldforge/`, `data/`, `tools/`, and
`viewer/` are pure text and behave perfectly. Only `game/` is binary-heavy.

**Perforce** is the industry answer and free up to five users; the honest trigger to switch is
when LFS operations cost minutes rather than seconds — typically a few tens of GB of content.
Do not pre-emptively adopt it solo.

Ignore from day one: `Binaries/`, `Intermediate/`, `Saved/`, `DerivedDataCache/`, `.vs/`.

## UE structure

**Split into modules early** — `Core`, `Gameplay`, `WorldRuntime`, `UI` — rather than one
monolith. The cheapest compile-time win available, and retrofitting module boundaries into an
established codebase is miserable.

**Organise Content by feature, not asset type.** `/Content/Dungeons/CryptKit/` beats
`/Content/Meshes/`. Type-based folders stop scaling around the thousandth asset.

Put the forge wrapper in a **plugin** so it compiles separately and stays cleanly bounded.

## Iteration speed — the daily tax

Over a multi-year project this matters more than almost any technical decision.

- Keep the generator out of UE (2 s vs 2 min)
- Module boundaries and forward declarations; watch header bloat
- Live Coding is useful for function bodies, unreliable for structural changes — know which
  you are doing
- Blueprint for things tweaked fifty times a day

## Testing — and the tool that will save you most

The generator is unusually testable: deterministic, pure, image-comparable.

- **Golden-image tests per phase**
- **A determinism test on every commit** — same seed twice, byte-identical output. Determinism
  regressions are silent and devastating
- **Save/load round-trip** — the one game-side test worth insisting on. Everyone skips it and
  everyone regrets it

The high-value one, specific to this project:

> **A batch seed harness.** Run 1,000 seeds through phases 0–11 and report
> [validation-gate](08-world-generation.md#validation-gates) failures by cause.

It catches over-constrained axiom directives, unreachable regions, and starved settlement
placement before they reach a player. It is how the rejection rate gets tuned, and how a new
axiom's bad interaction with an old one gets caught. Build it as soon as the validation gates
exist.

## The map viewer

Have the CLI emit a manifest plus phase PNGs, and build a small **local web viewer** to render
them with toggleable overlays: elevation, biomes, rivers, roads, territory, POIs, contested
cells.

Node 22 is already installed. Canvas plus a few hundred lines gets the single most useful tool
in the project — one that will be stared at for hundreds of hours. Building it in a browser
rather than in-engine keeps it fast to change.

## Plugins are engine-upgrade liabilities

Every third-party plugin is something that can block a 5.8 → 5.9 migration. Take them
deliberately and sparingly.

Given decisions already made — MetaSounds over Wwise, Landscape over custom mesh terrain,
custom save format, no crafting or inventory frameworks — this project may ship with almost
none. That is a good position.

**The policy is not "no plugins".** It is that every one is a decision somebody made on
purpose and wrote down. `tools/check_plugins.py` is that written-down list, and it fails if
the `.uproject` enables anything the list does not account for — not because a new plugin is
necessarily wrong, but because it arrived without anyone recording why.

It also enforces **scope**. A plugin used only at edit time must be restricted to the
`Editor` target rather than shipping in the game: a player never runs it, and a plugin
absent from the runtime binary cannot block a runtime engine upgrade. The check caught
`GeometryScripting` enabled for every target on its first run — it generates the arena
props, and its output is baked StaticMesh assets, so nothing at runtime needed it.

Current position — three plugins, all first-party Epic, all editor-only:

| Plugin | Why |
|---|---|
| `PythonScriptPlugin` | Every asset here is generated by a script, not authored by hand |
| `ModelingToolsEditorMode` | The editor half of Geometry Script; ships with the engine |
| `GeometryScripting` | Generates the arena props and the earlier weapon pass |

Nothing third-party. Nothing in the shipping binary. That is the position worth protecting.

## Order of work

1. **UE vertical slice on a hand-made 500 m square.** Combat, movement, one dungeon, loot, one
   quest. No procedural anything.
   → **Is one ordinary fight tense?** ([05](05-combat.md#the-gate))
2. **NumPy generator, phases 1–6**, with PNG output. Get terrain looking believable
3. **The web viewer**, as soon as there is anything to look at
4. **Phases 7–11** — settlement, roads, politics, POIs, binding
5. **Port to C++**, phase by phase, diffing images
6. **Phase 12 bake** — the UE import pipeline. First real world
7. **Batch harness and validation tuning**
8. Content volume — the rest of the project

> Steps 2–4 are engine-independent and could start today.

## Environment notes

- UE 5.8; Visual Studio 2022 lives on `E:\VS2022`
- Repositories root: `E:\Repositories`
- Node 22 available natively for `viewer/` and `tools/`
