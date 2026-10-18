# forge-cli

Command-line wrapper around libworldforge: PNGs, manifest, batch runs.

This is what the batch seed harness drives when it pushes a thousand seeds through the
phases and reports validation-gate failures by cause - the point being to find
over-constrained axioms before a player does.

    ichor_forge_cli --seed 10 --seed-end 19 --size 256 --phase tectonics --out out/
    ichor_forge_cli --seed 7              # manifest to stdout, nothing written
    ichor_forge_cli --list                # registered phases
    ichor_forge_cli --seed 7 --data data  # phase 11 binds the item, NPC and dungeon ids found in data/

Each seed writes `out/seed_N/<layer>.png` and one `out/manifest.json` lists every layer with its
size and a 64-bit FNV-1a hash, so a changed phase shows up as a changed hash. A seed whose phase
fails (for example a size outside the phase's limits) gets an `error` in the manifest and the exit
code is 1; the rest of the batch still runs.

Every phase runs on the shared `worldforge::forgeWorld` pipeline: a batch forges each seed once, up
to the latest phase any selected phase needs, and each phase only draws layers from that world. The
registry follows the phase numbers and names of doc 08: tectonics (1), erosion (2), hydrology (3),
climate (4), biomes (5), resources (6), settlements (7), roads (8), politics (9), pois (10) and
binding (11); `region_tiers` is derived from phase 1 and has no number. Phase 0 (axioms) is not
here yet, so `defaultForgeConfig()` stands in for the axiom-resolved config (library defaults,
settlement and POI spacing scaled to the grid, two fixed factions). `--data DIR` scans
`catalogue/` and `notable_items/` for item ids, `npcs/` for NPC archetypes (the closest table until
a dedicated archetype table exists) and `dungeon_skeletons/` for dungeon archetypes; without it
phase 11 binds empty pools. Bake (12) needs the engine and is not a phase here.

Adding a phase is one entry in `phaseRegistry()` (`src/registry.cpp`): its `needs` phase and a
function from the `ForgedWorld` to layers. PNGs use stored deflate, so
there is no compressor dependency and the files are byte-identical on every platform. The library
(`ichor_forgecli`) includes only libworldforge, never engine code.

## Map viewer

`viewer/` is a static, offline map viewer: no build step, no server needed, no network access
(a CSP blocks it). Open `viewer/index.html` in a browser and choose an output folder (the one with
`manifest.json`). It offers a seed picker, a toggle plus opacity and blend mode per layer (drawn in
manifest order), the raw value of every visible layer under the cursor, and the selected layer's
manifest hash, recomputed from the decoded pixels so a mismatch shows in red. If you do serve the
folder, `index.html?src=fixture/` loads it without the picker.

    node forge-cli/viewer/check.js [out/]   # smoke test: parser plus JS hash equals the C++ hash

`viewer/fixture/` is real forge-cli output (seed 1, size 64); regenerate it with
`ichor_forge_cli --seed 1 --size 64 --out forge-cli/viewer/fixture` when a phase changes, since
`check.js` fails on a stale hash. Deferred: drag-and-drop of a folder, and overlays for roads,
territory, POIs and contested cells, which appear automatically once forge-cli writes them as layers.

## Forge budget

`forgecli/budget.h` states what the full forge may cost at `kForgeTargetSize` (1024, the largest size
every phase accepts) and `test_forge_budget.cpp` fails CI when the model goes over. The check is
deterministic: it counts cell passes and bytes per cell from a documented model, never a clock or
an allocator, so it gives the same answer on every machine.

Committed budget (`kForgeBudget`), against the model at 1024 x 1024:

| Limit | Budget | Model today | Meaning |
| --- | --- | --- | --- |
| cell passes | 260,000,000 | about 202,000,000 | every phase off one shared forge: each stage paid once; one pass is one stage visiting one cell |
| peak bytes | 96 MiB | about 83 MiB | every stage's kept maps together, plus the largest scratch and the largest layer set |
| largest allocation | 24 MiB | 16 MiB | the biggest single vector, a guard against one buffer blowing up |

The model (`defaultCostModel()` in `src/budget.cpp`) lists per-cell factors for each shared stage
(tectonics, erosion, hydrology, climate, biomes, resources, settlements, roads, politics, pois) and which stages each registered phase lists; the full forge pays each listed stage once.
The factors are estimates read off the code (vector element sizes, loop counts, a heap or sort step
counted as about 8 passes), not profiler output. Erosion is the exception: it costs
`erosionCellPasses()` from `erosion_budget.h` with the model's `ErosionParams`, so tuning erosion
iterations moves the total. A phase with no entry in the model fails
`every registered phase has a cost entry`, so a new phase cannot slip in unbudgeted.

To change the numbers:

- A phase got slower or bigger on purpose: edit its stage factors in `defaultCostModel()`, then raise
  `kForgeBudget` in `budget.h` if the test says the total is over. Keep 25% to 30% headroom (now 28% for
  passes, 16% for peak bytes, 50% for the largest allocation); the test `the budget is tight enough
  to notice growth` fails if the budget is more than 50% above the model.
- A new phase: add a `PhaseModel` (and a `Stage` if it adds shared work). The output-bytes test
  checks the model against the layers the phase really writes.
- Sizes above 1024: raise `kTectonicsMaxSize` first; until then `planErosionBudget` with
  `forgeErosionBudgetCellPasses()` shows 4096 eroding at a 1024 work size (downscale shift 2).

A coarse wall-clock guard (`[wallclock]` test) runs the full forge at size 256 and fails past 120 s.
That is well over ten times the expected time under the Linux ASan/UBSan job (a few seconds), so a
loaded runner will not trip it but a runaway loop will. It is a backstop, not the budget.
