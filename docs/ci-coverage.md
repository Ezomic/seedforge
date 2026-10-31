# What CI proves (SEED-272)

Audit of the three promises SEED-272 names: determinism on every commit, golden images, save round-trip. Evidence is from `.github/workflows/engine.yml` and the CI log of a green run on main (3309 tests in the Windows job, 3275 in the Linux job, 100% passed in both).

## The two jobs

| Job | Preset | What runs |
|---|---|---|
| `windows runtime + tests` | `windows-ci` (Release, Vulkan runtime on) | `ctest --preset windows-ci`, then `tools/check_smoke_hash.py` (runs `ichor.exe --headless --frames 120`: it must exit cleanly and its logged scene state hash must equal `engine/tests/data/app_smoke_hash.txt`) |
| `linux headless + sanitizers` | `linux-headless-asan` (Debug, clang, ASan/UBSan, `ENGINE_RUNTIME=OFF`) | `ctest --preset linux-headless-asan` |

Both required names report on every pull request; what runs behind them depends on the changed paths (see "Required checks on every PR" at the end of this file).

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

## Axiom pair sweep (SEED-714)

A fourth nightly job, `axiom-sweep`, forges every pair of axioms that may share a world. `tools/axiom_sweep.py` reads
`data/axioms` and `data/conflicts/conflicts.json`, lists every unordered pair, and sets aside the pairs that share a slot,
declare each other in `conflicts` or sit on the blocklist (the rule of doc 01 and of `pinWorldConfig`, so the counts match
`docs/axiom-coverage.md`: 231 pairs, 42 excluded, 189 forged). Each forged pair runs
`ichor_forge_cli --seed 1 --seed-end 8 --size 128 --data data --axioms a,b --validate --out axiom-sweep/a+b`, several at a
time (one per CPU). A pair fails when forge-cli refuses the pin, a seed errors, a seed is rejected after forge-cli's own
rerolls (its failed gates are listed as `gate: cause`), or the manifest does not record the pair.

The job summary carries the report: a counts table, every failing pair with its seeds and gates, and two folds, one with
every forged pair (seconds and land share per seed) and one with the excluded pairs and why. The same report plus
`sweep.json` (every pair's verdict) and the per-pair `manifest.json` and `validation.json` are uploaded as
`nightly-axiom-sweep`, on success too. The night fails when any forged pair fails. A manual run takes `sweep_seeds`
(default `1-8`) and `size` (shared with the sanity report, default 128).

Phase 0 is carried only (SEED-713: forge-cli records the pinned axioms in the manifest, no phase reads the directives
yet), so today every pair forges the same default world and the sweep proves two things: every non-excluded pair pins,
and the default world under every pair passes the gates on seeds 1 to 8 (the sanity report's seeds). It starts telling pairs apart the moment the
axiom-to-params mapping lands, with no change to the job. The build is Release without sanitizers, like `forged-walk`,
and builds only `ichor_forge_cli`. Locally (Windows Release, 8 forge-cli runs at a time) the 189 pairs on seeds 1-8 took 98 s of wall time for 772 s of forge-cli time, about 4 s per pair; the dispatched run's figure is in the SEED-714 PR.

Run it locally with `bin/build ichor_forge_cli`, then
`python tools/axiom_sweep.py engine/build/windows-ci/forge-cli/ichor_forge_cli.exe --out out/sweep`; add
`--shard 1/4` to forge a quarter of the pairs. `python tools/axiom_sweep.py --self-test` proves the pair rules and the
verdicts, and runs in the `python tools + content layout` PR job.

## Test-time budget (SEED-658)

Main went red twice because one test ran too close to the sanitizer timeout (the SEED-492 walk test on the SEED-556 worlds, and the SEED-650 live-worlds probe, which timed out at 1500 s). `tools/test_times.py` reads the per-test durations of a ctest run, prints the 20 slowest cases, and fails when a case exceeds its budget:

| Profile | Where | Default budget per case |
|---|---|---|
| `sanitizer` | `linux headless + sanitizers` | 120 s |
| `release` | `windows runtime + tests`, local runs | 30 s |

A case may run longer only when `tools/test_time_allowlist.json` lists it under that profile with its own cap in seconds and a reason. Going over the cap fails too, so an allowlisted test cannot quietly double. A ctest timeout is always over the cap.

Run it locally after any ctest run: `bin/test-times` (preset `windows-ci`, profile `release`; `-p linux-headless-asan` picks `sanitizer`). It reads `Testing/Temporary/LastTest.log` of the build tree. The script also accepts a JUnit file (`ctest --output-junit ctest.xml`) and captured ctest console output, so a CI log can be checked offline.

Two ctest cases run in both required jobs and need no timing data: `test_time_allowlist` (the file parses, has no duplicate keys, is sorted, every cap is above the default budget and at most the 1500 s ctest timeout, every entry has a reason, and every name appears in a test source so a renamed test cannot leave a dead entry) and `test_time_budget_selftest` (a synthetic slow case turns the check red, an allowlisted one stays green, one over its own cap is red again, and the three input formats parse).

Regenerate the allowlist after an intended slowdown: run the suite, then `python tools/test_times.py <input> --profile <profile> --update --reason "why it is slow"`. This adds every case over budget with a cap of 1.5 x the observed time rounded up to 30 s (never above the 1500 s ctest timeout), raises a cap that was exceeded, and drops entries that now run within the default budget. Edit the reason of a new entry in the file if the shared one does not fit, and commit it. When a slow test is made fast, delete its entry; the script prints a note for each entry that is no longer needed.

The first allowlist was seeded from the CI logs of runs 40052978212 and 40054121237. It includes `live worlds: the probe finds a spawn, ...` and `a showcase run ranks the seeds, ...`, which time out or come close on the Linux job today; remove those entries when their fix lands.

The step now lives in the workflows: the Windows job and every Linux shard run `tools/test_times.py` after ctest (SEED-664). It also checks the tiers: a case that is in no tier list (so it is fast) and runs over 5 s in Release or 90 s under the sanitizers fails and names `engine/tests/test_tiers.json` as the place to move it.

## Test tiers and the 15 minute PR budget (SEED-664, 666, 670, 671, 672)

Each required check has a hard 15 minute `timeout-minutes`. `docs/ci-time-baseline.md` has the measurements behind the design. Every ctest case is in exactly one tier, listed in `engine/tests/test_tiers.json` and applied as a ctest label when ctest loads the tests (`engine/tests/apply_test_tiers.cmake`; the three app tests that are not Catch2 cases are labelled in `engine/tests/CMakeLists.txt`). A case in no list is fast and has no label.

| Tier | Rule | Runs on a PR | Runs on main and nightly |
|---|---|---|---|
| fast | under about 1 s in Release and under 90 s under the sanitizers (the guard fails a fast case over either; a test that is cheap in Release but heavy under ASan goes to medium) | Windows Release, and the Linux sanitizer shards | everywhere |
| medium | about 1 s to 10 s in Release | Windows Release only | also the Linux sanitizer shards |
| soak | the full variant of a heavy test | never | `linux soak tests` job after every merge to main, and the nightly `soak` job |

A heavy test has a small PR variant in the fast or medium tier that fails for the same breakage (fewer seeds, smaller grid), and the original case, unchanged, is listed under soak. The four converted so far:

| Soak (full) | Small PR variant |
|---|---|
| the coverage reserve is what lifts the weakest seeds over the gate (3 weak seeds) | the coverage reserve lifts the first weak seed over the gate |
| from the spawn toward the nearest settlement on 8 seeds ... | ... on 3 seeds ... (2 must arrive, 5 of 8 in the full one) |
| fuzz: a modest seed range over every phase has no violations (64 seeds) | fuzz: a small seed range ... (8 seeds) |
| tectonics: seeds 1 to 64 land between 20 and 70 percent ... at 128, 256 and 1024 | tectonics: seeds 1 to 16 ... at 128 and 256 |

Each required check and where its time goes:

| Check name (unchanged) | How it is built | Preset |
|---|---|---|
| `windows runtime + tests` | one job, 15 min | `windows-ci`: everything except soak |
| `linux headless + sanitizers` | four jobs `linux sanitizers shard N of 4` (each takes every fourth ctest index, `-I N,,4`), plus `linux gpu goldens`; a final job with the required name passes only when all five passed | PR: `linux-headless-asan-fast` (no medium, no soak). Push to main: `linux-headless-asan` (no soak) |

Because the required name belongs to the last job, branch protection needs no change. A cancelled or skipped shard fails it.

Nothing stops being tested: medium runs under the sanitizers on every merge to main, soak runs under the sanitizers after every merge to main and in the nightly (`soak` job in `nightly.yml`, preset `linux-headless-asan-soak`; `windows-ci-soak` runs the same tests in Release locally), and the nightly stress job still runs every other test three times in random order (`bin/ctest-stress` leaves soak out unless `-S` is given, since the soak job covers it). The Windows job runs the medium tier on every PR.

The time guard has a budget per tier. Without an allowlist entry a fast case may run 5 s in Release or 90 s under the sanitizers, a medium case 30 s or 300 s, a soak case 60 s or 600 s (`TIER_BUDGETS` in `tools/test_times.py`); an allowlist entry replaces the tier budget with its own cap. The main shards run fast + medium under ASan, so the medium budget is what they are held to there.

### Adding or moving a test

- A new test is fast by default. If it takes over about 1 s in Release, add its name to `medium` in `engine/tests/test_tiers.json` (the time guard says so when it is over 5 s). `python tools/ci_tiers.py write-tiers <ctest log>` rewrites the medium list from a Release run and keeps the soak list.
- A new heavy test needs a small PR variant and a full variant under `soak`. `ctest --preset windows-ci-soak` runs the soak tests locally, `ctest --preset windows-ci -L medium` the medium ones.
- `test_tiers_consistent` fails when a name in the file is in two tiers, unsorted, contains a `;`, or is in no test source.

### Cache and cancel changes (SEED-671)

- vcpkg is checked out at the `builtin-baseline` commit (`VCPKG_COMMIT` in `engine.yml`; change it together with the baseline in `engine/vcpkg.json`) so the binary cache can hit. Cache keys carry `v2` to leave the stale entries behind.
- vcpkg also hashes the PowerShell version and `cl.exe` into every package ABI, so a Windows runner image that updates either restores 0 packages from an older cache (SEED-716: image 20261025.346.1 moved PowerShell from 7.6.8 to 7.6.9). The Windows key therefore carries both (`vcpkg-windows-v3-pwsh<version>-cl<hash>-...`), and the job saves it right after configure, so a cold run that later hits the 15 minute limit still warms it.
- The Linux GPU golden steps are their own job and share nothing with the sanitizer shards, so they overlap them.
- The forged world test cache (SEED-665, `<build>/forged-world-cache`) is restored by `actions/cache`, keyed on the sources its generator digest hashes (and on the shard number, since each shard forges different worlds and a cache key is written once). A cold run is slower for the tests that use it (the cache stores whole forges), so the first run after a generator change is the slow one.
- The 15 minute limit applies to pull requests only. A run on main (push, or a manual `workflow_dispatch`) starts with cold caches, because a cache saved on a PR branch is not visible to main, and a job killed by its limit saves no cache; so on main the Windows, shard and GPU jobs get 60 minutes and the soak job 90. The first main run after a cache eviction is therefore slow (about 35 minutes for the whole run) and the next ones are warm. Run `engine.yml` by hand from the Actions tab to warm main on purpose.
- To warm the caches after they are lost or renamed: run `engine.yml` from the Actions tab on main (`workflow_dispatch`, 60 minute limits). The pull request limit is 15 minutes (search `PR_LIMIT` in `engine.yml`; main run 40204448268 on 4de0045 had warm caches and every job was under 12 minutes). After a cache eviction or a key rename a cold PR run needs about 25 minutes: raise the limit to 30 until a main run has saved the caches again, then back to 15.
- The ccache keys are `ccache-asan-`, `ccache-gpu-` and `ccache-walk-`: restore keys match by prefix, and the old shared `ccache-linux-` prefix let an ASan job restore the newest Release GPU cache (no hits, a 15 minute cold build).
- A new push to a pull request cancels the run of the previous commit (`cancel-in-progress` was already set for PRs and stays); runs on main queue.

## Required checks on every PR (SEED-548, SEED-545)

`engine.yml` has no path filter on `pull_request` any more. A PR that touched only `art/`, `docs/` or `tools/` used to start no workflow, so the two required checks never reported, GitHub showed BLOCKED and the merge needed an admin bypass (art batch 6 went in that way and broke the Exile mesh test, SEED-506). Now both names report on every PR, and a `changes` job decides the scope from `git diff HEAD^1 HEAD` of the merge commit:

| Changed paths | `heavy` | `art` | What runs |
|---|---|---|---|
| anything in `engine/`, `game/`, `data/`, `bin/`, `art/audio/`, `art/fonts/`, the workflows, any other folder, a `.md` below the root, or one of `tools/check_smoke_hash.py`, `test_times.py`, `test_time_allowlist.json`, `ci_tiers.py` | yes | n/a | everything, as before |
| `art/` (other than audio and fonts), maybe with `docs/` and `tools/` | no | yes | the python checks and the Windows art tests |
| only `docs/`, other `tools/`, root `*.md` | no | no | the python checks only |
| push to main, manual run | yes | no | everything (the push path filter is unchanged) |

Anything not named as light is heavy, so a new folder runs the full suite until it is classified in the `changes` job.

The required names stay: `windows runtime + tests` and `linux headless + sanitizers` are now small aggregator jobs on `ubuntu-latest`.

- `linux headless + sanitizers` passes when `changes` succeeded and, if heavy, every shard and the GPU goldens succeeded; if not heavy it requires them to be `skipped`. A failed, cancelled or unexpectedly skipped job fails it.
- `windows runtime + tests` needs `changes`, `python tools + content layout` and the job `windows build + tests`. That job runs when heavy or art; the aggregator requires it to succeed when it was needed and to be skipped when it was not.
- `python tools + content layout` runs on every PR (about 30 s): `check_content_layout.py` (and `--self-test`), `check_data.py`, `check_deps.py`, `ci_tiers.py check`, `test_times.py --check-allowlist` and `--self-test`. These are the python tests the ctest suite registers.

### The Windows art tests

For an art-only PR the Windows job builds `ichor_tests_ui ichor_tests_world stage0_tests` (not the runtime) and runs `ctest -R` over the tests that open files under `art/` (with `--no-tests=error`, so a rename cannot make it vacuous), then `ichor_tests_ui "[exile]"`:

| Test file | What it reads from `art/` |
|---|---|
| `engine/tests/test_animation_exile.cpp` | `art/exile/SK_Exile.glb`: skeleton, metric mesh, root travel, clips, props |
| `engine/tests/test_animation_exile_avatar.cpp` | the Exile clips through the avatar (tag `[exile]`) |
| `game/stage0/tests/test_exile_player.cpp` | the Exile and its costume GLBs |
| `game/stage0/tests/test_bull_warden.cpp` | `art/monsters/bull_warden` |
| `engine/tests/test_placed_settlement_kit.cpp` | "the exported GLBs cook to the sizes the kit table says": `art/settlement_kit` |
| `engine/tests/test_cooker.cpp` | the cooker (SEED-545) on `engine/tests/data/kit.glb`, so a cooker change is covered, a bad GLB in `art/` is covered by the tests above |
| `content_layout` | `tools/check_content_layout.py` |

Audio and fonts are in the heavy set already. A new test that reads another `art/` folder must be added to the regex in the `Art tests` step of `engine.yml`.

## Linux package and headless smoke run (SEED-663)

`tools/package_linux.sh` is the Linux counterpart of `bin/package` (SEED-644). It builds the `ichor` app, assembles `ichor-linux/` (the binary, any vcpkg shared libraries beside it in `lib/`, `content/`, `shaders/`, `scripts/`, `data/`, `art/` and `ichor.sh`, which starts `ichor --forged` with the default seed 1 and grid 128) and packs `ichor-linux.tar.gz`. With `--smoke` it unpacks that tarball into a fresh folder and runs the unpacked binary `--headless --forged --frames 30` from a different directory. The run must exit 0, log `packaged build: assets are read from <the unpacked folder>` (a fallback to the compiled-in build-tree paths also runs, but does not log that line) and log `exit after 30 frames`.

- **Preset.** The `linux-headless-asan` preset has `ENGINE_RUNTIME=OFF`, so it never builds the `ichor` app and cannot be packaged. The script defaults to `linux-gpu` (Release, clang, `ENGINE_RUNTIME=ON`), whose vcpkg and ccache caches the `linux gpu goldens` job already keeps warm. `-p` picks another preset and `-b` a build directory.
- **Where it runs.** Job `linux package + smoke` in `engine.yml`, on every merge to main and by `workflow_dispatch`, never on a pull request, and it uploads the tarball as the `ichor-linux-package` artifact (7 days). It runs beside the other jobs and is not in the `linux headless + sanitizers` aggregate, so no required check name or duration changed. Measured on branch `claude/seed-663-linux-package-00aeb1` with warm caches: 2 min 16 s in all (about 1.5 minutes of build); the first dispatch restored the `ccache-gpu-` and `vcpkg-linux-gpu-v2-` caches of the goldens job. Cold it compiles SDL3 and the renderer (the goldens job reports 11 to 12 minutes), which a PR's 15 minute limit does not allow, hence main only. Its own `ccache-package-` key keeps it from evicting the goldens job's cache.
- **The ctest case.** `packaged_app_smoke` (medium tier, about 11 s, `engine/tests/packaged_app_check.cmake`) stages a folder the same way, runs the staged executable from another directory and asserts the `packaged build` line names the staged folder. It runs wherever `ENGINE_RUNTIME` is on, which is the `windows-ci` job; no Linux ctest preset builds the app.

What was checked where:

| Check | Where |
|---|---|
| `shellcheck` clean, `bash -n` | Locally on Windows (shellcheck 0.11 from pip) |
| Packaging, tarball layout and modes (`ichor` and `ichor.sh` 755, owner 0), `ichor.sh` content, unpack, run from another directory, the failing path (a binary that does not log the packaged line fails the smoke), the refusal of an output inside the repository sources and of a non-numeric seed | Locally on Windows, against a fake build tree whose `ichor` is a shell script (`-b`, `--no-build`) |
| `packaged_app_smoke` | Locally on Windows with the real `ichor.exe`, and in the `windows-ci` job |
| Building the app on Linux, the `ldd` step (unresolved libraries fail the script, vcpkg libraries are copied to `lib/`), `LD_LIBRARY_PATH` in `ichor.sh`, the real `/proc/self/exe` resolution, the smoke run of a real Linux binary | Only the CI job (it passed on a `workflow_dispatch` run of this branch: the real ELF binary linked, the tarball unpacked elsewhere and the unpacked binary logged the packaged-build line) |

The tarball needs a glibc at least as new as the runner's (ubuntu-latest) and, to open a window, a Vulkan loader (`libvulkan1`) on the player's machine; `--headless` needs neither a GPU nor a display.

## LFS cache (SEED-687)

Every job used to check out with `lfs: true`, so each of them downloaded every LFS file (about 205 MB, about 520 MB with the batch 7 art). GitHub free allows 10 GiB of LFS bandwidth a month, and one PR run has six such jobs. The jobs now check out with `lfs: false` and call `.github/actions/lfs-cache`:

1. `.lfs-assets-id` is written from `git lfs ls-files -l` (the object ids of the checked out ref, sorted).
2. `.git/lfs` is restored from the cache key `lfs-<hash of that file>`; when the exact key is missing, the newest `lfs-` cache is restored, so a changed asset set fetches only the new objects.
3. `git lfs pull` fetches what the cache lacks and writes real files over the pointers.
4. On a miss on main the cache is saved straight away, so a later failing step does not lose it. PR runs only restore.

Objects are content addressed, so Windows and Linux share one key family (`enableCrossOsArchive`). A run on a PR can read caches saved on main but not the other way round, so the first run after a merge to main seeds the cache once.

To clear it: `gh cache list --key lfs-` and `gh cache delete <id>` (or `gh cache delete --all --key lfs-`). The next run downloads once and saves again.

If a job shows pointer files instead of assets (a test fails on a file that starts with `version https://git-lfs`), look for the `Fetch the missing LFS objects` step: it must have run `git lfs pull` without error. Delete the `lfs-` caches and re-run; if it still fails, check the LFS bandwidth quota on github.com/settings/billing, because an exhausted quota makes the fetch fail.

## Compiler caches (SEED-688)

The sccache and ccache entries are keyed by commit and are 20 MB to 1 GB each. They used to be saved by every PR run, which filled the 10 GB Actions cache limit and made GitHub evict the vcpkg caches. Only main saves them now; PR runs restore the newest one from main. Check the total with `gh api repos/Ezomic/seedforge/actions/cache/usage` and list the biggest with `gh cache list --sort size_in_bytes --order desc`.

`cache-prune.yml` runs after the engine and nightly workflows (and by hand with `gh workflow run cache-prune`). It keeps the newest main entry of each compiler and LFS cache family and deletes the caches of closed pull requests, so main's one-entry-per-commit saves cannot grow past one set.
