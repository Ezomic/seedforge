# What CI proves (SEED-272)

Audit of the three promises SEED-272 names: determinism on every commit, golden images, save round-trip. Evidence is from `.github/workflows/engine.yml` and the CI log of a green run on main (3309 tests in the Windows job, 3275 in the Linux job, 100% passed in both).

## The two jobs

| Job | Preset | What runs |
|---|---|---|
| `windows runtime + tests` | `windows-ci` (Release, Vulkan runtime on) | `ctest --preset windows-ci`, then `tools/check_smoke_hash.py` (runs `ichor.exe --headless --frames 120`: it must exit cleanly and its logged scene state hash must equal `engine/tests/data/app_smoke_hash.txt`) |
| `linux headless + sanitizers` | `linux-headless-asan` (Debug, clang, ASan/UBSan, `ENGINE_RUNTIME=OFF`) | `ctest --preset linux-headless-asan` |

Both are path-filtered to `engine/`, `game/`, `data/`, `art/audio/`, `art/fonts/`, `bin/` and the workflow file. A change that touches only `docs/` or `art/` (other than audio and fonts) runs neither.

## Coverage table

| Promise | Test | Runs in | Gap |
|---|---|---|---|
| Determinism: same seed, same world and sim on every OS | `engine/tests/test_determinism.cpp`: "the forged world and the simulation match the checked-in values on every platform" and "the walked physics scene matches across platforms", against `engine/tests/data/determinism.json` (seeds 0, 1, 42, u64 max: heightfield digest, hydrology bytes hash, engine state hash after 120 frames, physics hash at ticks 1, 30, 60, 120, 240). The same file is compared on both OSes. | Both jobs, on every PR and push to main | Pins only the heightfield, hydrology, engine and physics. Later forge phases are pinned by their own per-module golden hashes (`test_worldforge_*`, "golden world hashes for fixed seeds", "erosion golden hash is stable", "biome golden hash is stable", "climate golden hash is stable"), each on both OSes, but there is no single end-to-end world digest. That belongs to SEED-410 and is not duplicated here. |
| Determinism: RNG and scene hash pinned | `engine/tests/test_golden.cpp` ("golden: pcg32 reference stream", "golden: scene state hash after 120 ticks"), `test_rng.cpp`, `test_hash.cpp`, `test_dmath.cpp`, `test_entity_id.cpp` ("entityId golden values are pinned") | Both jobs | None found |
| Determinism: no hidden nondeterminism sources | `test_unordered_audit.cpp`, `test_save_pointer_audit.cpp` | Both jobs | None found |
| Golden images, CPU (forge phase pictures) | `engine/tests/test_forge_goldens.cpp` ("forge golden: ..."), PNGs in `engine/tests/data/golden/forge_*.png` | Both jobs | None found; the harness itself is tested (missing golden, LFS pointer, wrong size, changed picture) |
| Golden images, GPU (terrain, sky, UI, HUD, props, 28 cases) | `engine/tests/test_golden_image.cpp`, own target `ichor_gfx_tests`, PNGs in `engine/tests/data/golden/` | The Linux job (SEED-471): preset `linux-gpu` builds `ichor_gfx_tests` and `ctest --preset linux-gpu` runs it on lavapipe (software Vulkan, `ICHOR_GOLDEN_SOFTWARE=1`, `ICHOR_REQUIRE_GPU=1` so a missing device fails instead of skipping). The Windows job builds it but its runner has no GPU, so every case skips there. | Software tolerance: mean abs diff under 2.0, under 0.2% of pixels off by more than 16, and no 16x16 tile with 40 or more such pixels (a missing draw call piles up in a tile; driver noise spreads thin). Cases lavapipe draws differently from a real GPU (clouds, rain, the underwater shot) keep a second picture in `engine/tests/data/golden/software/`, used only on lavapipe. A new lavapipe/Mesa release can move those; regenerate with `ICHOR_GOLDEN_SOFTWARE=1 ICHOR_UPDATE_GOLDEN=1` and look at each picture. The metric is pinned by "golden image tolerance: ...", and removing any single draw call turned the suite red (SEED-471 PR). |
| Save round-trip: slots, ledger, standing, wallet, capabilities, chronicle, settings | `engine/tests/test_save_roundtrip.cpp` (save, load, save is byte-identical; digests move when state moves; limits; truncation and bit-flip at every position; older-format migration; wrong seed refused), `test_save.cpp`, `test_save_migration.cpp`, `test_snapshot_tail.cpp`, `test_delta_store.cpp`, `test_ledger_codec_format.cpp` | Both jobs | None found. Uses real game data (`data/`) for the capability and standing catalogues. |
| Save round-trip: whole campaign folder, crash during write, corruption | `engine/tests/test_campaign_roundtrip.cpp` (touches every save module, crash at every write stage, every file truncated or flipped, fuzzed reads) | Both jobs | The forge cache part used patterned bytes, not a forged world (closed below). |
| Save round-trip: forged world data through the forge cache | `engine/tests/test_save_forged_world.cpp` (new, this PR) | Both jobs | Closed by this PR. |
| Hydrology cache (a derived file of the forged world) | `engine/tests/test_hydrology_cache.cpp` | Both jobs | None found |

## Gaps closed in this PR

- `test_save_forged_world.cpp`: real forged hydrology bytes (seeds 7 and 42) go through `ForgeCache` put, restart, scan, get, then `decodeHydrologyCache`, and must be bit-identical to computing it. A flipped byte at 64 positions across a real cache file is never served, and a payload for one world is refused against another world's heights.

## Gaps left as proposed tickets

1. **Closed by SEED-471:** the GPU golden images compare on lavapipe in the Linux job. See the table.
2. **Closed by SEED-410:** `engine/tests/test_forge_determinism.cpp` pins the chained world hash and every phase hash of `forgeWorld` for 52 (size, seed) pairs against `engine/tests/data/forge_world_digests.json`, on both OSes. After an intended phase change, regenerate with `ICHOR_UPDATE_FORGE_DIGESTS=1 bin/build -t "forge world digests"` and commit the file.
3. **The headless smoke run's hash is compared** (SEED-472). `tools/check_smoke_hash.py` checks the shipped executable's end-of-run state against `engine/tests/data/app_smoke_hash.txt`. The app populates its own scene, so this value differs from `determinism.json`'s `engine` entry on purpose. After an intended sim change, build `windows-ci` and run `python tools/check_smoke_hash.py engine/build/windows-ci/app/ichor.exe --update`.
4. **A save of a fully forged world (not just the hydrology) is not round-tripped**, because the world is not yet baked to disk as in `docs/14-save-system.md`. Revisit when the campaign `world/` folder is written for real.

## Nightly run (SEED-494)

`.github/workflows/nightly.yml` runs every night at 02:17 UTC (and on demand: Actions, nightly, Run workflow, with optional `rounds` and `size` inputs). It is not a required check and is not path filtered; it runs against the default branch. One Linux job (`linux-headless-asan` preset, 100 minute timeout, because one sanitizer round of the suite takes about 23 minutes on the hosted runner and the first scheduled run to get past the 126 error was cancelled at 30 minutes in round 2) builds once, then runs two steps, and the second runs even when the first fails so one night reports both problems:

| Step | Command | Catches |
|---|---|---|
| ctest-stress | `bin/ctest-stress -p linux-headless-asan -n 3` (SEED-462) | Tests that pass alone but fail under parallel, random-order execution: shared scratch files, fixed paths, order dependence |
| World sanity report | `python3 tools/world_sanity.py <ichor_forge_cli> --seeds 1-8 --size 128` (SEED-413) | World generator changes that push a fixed seed over a design check (`forge-cli --validate`, gates from `world_validation.h`) |

On failure the job summary lists the flaky tests with their failure count per test, and for the world report each failing seed with the design check and cause (`FAIL seed 3: rejected by validation`, then `design check failed: <gate id>: <cause>`). The logs, `manifest.json` and `validation.json` are uploaded as the `nightly-logs` artifact.

Run the same commands locally: `bin/build`, then `bin/ctest-stress -n 2` and `python tools/world_sanity.py engine/build/windows-ci/forge-cli/ichor_forge_cli.exe`. The seeds are fixed on purpose so a red night means a code change, not luck; change the range in the workflow when the world generator is retuned and the set should widen. `nightly.yml` is in the path filter of `engine.yml`, so editing it runs both required checks.

Why seeds 1-8 at size 128: with the SEED-416 defaults, seeds 3, 4 and 5 forge nothing usable at size 64 (`roads: no settlement sites to connect`, tracked by SEED-497), so the report would be red from day one. At sizes 128 and 256 all eight seeds are accepted, and 128 is the cheaper. Revisit the size when SEED-497 lands.

### Why the first scheduled run failed in the stress step

Run 39387287353 died with exit code 126 in `bin/ctest-stress`: the file was committed with mode 100644, so the Linux runner could not execute it (`.gitattributes` already pinned LF for `bin/*`, which was not the cause). It is now committed as 100755 like the other scripts in `bin/`. A new script under `bin/` needs the executable bit in the index (`git add --chmod=+x`), or the same failure returns on Linux only.

## Forged walk bot at 256 and 1024 (SEED-501)

A second job in `nightly.yml`, `forged-walk`, runs in parallel with the first (own 30 minute timeout, same preset, builds only `ichor_tests_world`). It runs the one test case `nightly: the walk bot on big forged worlds arrives or names what stopped it` (`engine/tests/test_forged_walk_nightly.cpp`, sharing the walk setup of the PR suite's 129-grid test through `engine/tests/forged_walk_support.h`). The test is skipped in every normal `ctest` run and only runs when `ICHOR_NIGHTLY_WALK_GRIDS` is set; the workflow sets it to `256,1024` (the `walk_grids` input of a manual run changes it).

For seeds 1-8 on each grid the bot walks from the spawn toward the nearest other settlement (the SEED-492 walk bot, `forged_walk_bot.h`) and prints one line per seed, for example `WALK grid 1024 seed 2: ok blocked, trip 2744 m, path no, walked 166 m in 124 s sim, 2591 m short, wall 35.2 s | blocker: stuck at ... too steep (needs climbing)`: arrival or blocker reason, and wall time. A blocked walk with a named blocker is honest and does not fail the night; arrival is not required (a third of the seeds are blocked on steep ground, which is the data this run collects).

The night fails when a seed is silently stuck:

- the walk timed out while still moving;
- it stopped without a blocker and a report, or without having walked;
- no seed on a grid had a trip to walk, or the 1200 second budget (`ICHOR_NIGHTLY_WALK_BUDGET_S`) ran out before every seed ran (that seed prints as `SILENT not run`).

A seed with no second settlement 60 m or more from the spawn settlement prints `no trip` and does not fail (seeds 5 and 6 at 256 today); it is visible in the log, not a verdict. Local timings on a Release build: all 8 seeds took about 60 s at 256 and about 190 s at 1024. The first CI run used the sanitizer Debug build and took about 190 s per seed at 1024 (the budget ran out after 6 seeds), so this job configures the same preset as Release with sanitizers off. The budget guard stays so a slow night fails loudly instead of timing out.

On failure the job summary lists the `WALK` lines, and `walk.log` is uploaded as `nightly-walk-log` (also on success). Run it locally with `bin/build ichor_tests_world`, then `ICHOR_NIGHTLY_WALK_GRIDS=256,1024 engine/build/windows-ci/tests/ichor_tests_world "nightly: the walk bot*"`.
