# CI time baseline (SEED-670)

Why the two required checks took 35 to 90 minutes (Linux) and 6 to 24 (Windows), measured on a green push to main (run 40124000022, commit 51ab1d6b, 3,972 tests). The target is 15 minutes wall clock per required check, as a hard `timeout-minutes`. `tools/ci_tiers.py baseline` reprints the arithmetic from any two ctest logs.

## Where the minutes go

| Step | Windows job (s) | Linux job (s) |
|---|---|---|
| Checkout (project + vcpkg), toolchain, cache restore | 30 | 83 |
| Configure | 317 | 189 |
| Build | 114 | 44 |
| Test | 114 (ctest -j4) | 1489 (ctest -j4, ASan/UBSan) |
| GPU goldens: configure again, build, run | n/a (skip on the runner) | 289 |
| Total | about 590 | about 2095 |

Both build steps were warm sccache or ccache hits. A change to a widely included header turns them into full rebuilds, so a bad day is longer than this table.

Findings:

1. **The vcpkg binary cache never hit.** Both workflows checked out vcpkg master, so every run had a new tool and port script hash and the log says `Restored 0 package(s)`: each job rebuilt all 32 packages (configure 317 s on Windows, 189 s and then another 262 s for the `linux-gpu` preset on Linux). The workflows now check out the commit that `engine/vcpkg.json` pins as `builtin-baseline`, and the cache keys moved to `v2` so the stale entries stop being restored.
2. **The sanitizer suite costs 14x the Release suite** (5,946 s against 432 s summed). ASan/UBSan costs about 6x on a typical test and 25 to 30x on the world forging tests. 3,904 of the 3,972 tests are fast, and they still cost 2,294 s summed under ASan, mostly process start-up (every Catch2 case is its own process).
3. **Ten tests carry 40% of the sanitizer minutes.** See the table below: the 25 slowest.
4. **Linux had one job**, so nothing ran in parallel, and the GPU golden steps ran after the 25 minute test step.

## The 25 slowest tests

Release seconds are from the Windows job, sanitizer seconds from the Linux job, same run.

| # | Test | Release s | Sanitizer s |
|---|---|---|---|
| 1 | the coverage reserve is what lifts the weakest seeds over the gate | 13.3 | 382 |
| 2 | from the spawn toward the nearest settlement on 8 seeds the walker arrives or names what stopped it | 17.9 | 367 |
| 3 | fuzz: a modest seed range over every phase has no violations | 14.4 | 314 |
| 4 | tectonics: seeds 1 to 64 land between 20 and 70 percent for nearly every seed at 128, 256 and 1024 | 21.5 | 272 |
| 5 | a path over the forged navmesh does not cross open lake water, and one over the plain navmesh does | 10.1 | 223 |
| 6 | a road over the water is a bridge: the path crosses where the road does | 9.5 | 220 |
| 7 | the surface blocks open water, lets a road bridge it, and leaves dry ground walkable | 8.3 | 205 |
| 8 | hydrology survives degenerate and extreme terrain | 8.1 | 199 |
| 9 | the land erosion removes is about the same at 64, 128 and 256 | 5.3 | 135 |
| 10 | landmark coverage passes on the first attempt at 128 and 256 for seeds 1 to 8 | 5.0 | 131 |
| 11 | the forged frame counts stay inside the budget and a doubled instance count turns the guard red | 6.4 | 126 |
| 12 | higher scores never get a smaller tier and the best sites come first | 2.7 | 71 |
| 13 | live worlds: the probe finds a spawn, repeats itself, and the walk reaches a settlement on some seed | 3.0 | 68 |
| 14 | live worlds: a mostly ocean seed found by search scores below a balanced one | 2.8 | 62 |
| 15 | each skill parameter changes the outcome on its own | 1.2 | 57 |
| 16 | coverage is deterministic and bounded | 1.4 | 53 |
| 17 | forge world digests: every phase of the forge matches the checked-in table on every platform | 2.9 | 45 |
| 18 | forge world digests: a moved phase hash is reported by phase with the regenerate command | 2.9 | 44 |
| 19 | every forge phase stays within a generous multiple of its recorded cost | 1.6 | 42 |
| 20 | a stronger skill level wins clearly more fights | 0.8 | 38 |
| 21 | every river runs on to the sea or a lake | 1.4 | 35 |
| 22 | the forge request flag chooses the forged terrain and defaults to the stand-in | 1.5 | 34 |
| 23 | hostile: quest ruleset file through loadRuleset | 1.2 | 33 |
| 24 | hostile: quest expansion rules | 0.8 | 32 |
| 25 | serialised data holds ids, never pointers or runtime handles | 1.3 | 28 |

## Tier arithmetic

Tiers are by Release time: fast under about 1 s, medium above that, soak the full variant of a test that has a small PR variant (`engine/tests/test_tiers.json`). Numbers from the same run, with the tier lists as first written:

| Tier | Tests | Release s (summed) | Sanitizer s (summed) |
|---|---|---|---|
| fast | 3,904 | 148 | 2,294 |
| medium | 64 | 217 | 2,317 |
| soak | 4 | 67 | 1,335 |
| all | 3,972 | 432 | 5,946 |

Per job, with 4 cores each:

| Job | What it runs | Test wall | Plus setup | Projected total |
|---|---|---|---|---|
| Windows PR | fast + medium (soak left out) | (148 + 217) / 4 = 91 s | 230 to 450 s configure, build, restore | 7 to 10 min |
| Linux PR, 1 job | fast, ASan | 2,294 / 4 = 574 s | 420 s | 16.6 min, over budget |
| Linux PR, 4 shards | fast, ASan | 2,294 / 16 = 143 s | 420 s per shard (about 250 s once the vcpkg cache restores) | 6 to 9.5 min |
| Linux main, 4 shards | fast + medium, ASan | 4,611 / 16 = 288 s | 420 s | about 12 min |
| Soak (main, nightly) | soak, ASan | 1,335 / 4 = 334 s | 420 s | about 13 min |

The shard count is the smallest that fits 15 minutes with the 420 s of fixed setup per job: `tools/ci_tiers.py baseline` prints 2 shards as the minimum; 4 leaves room for a cold ccache. The shards take every fourth test by ctest index (`ctest -I N,,4`), which balances them better than splitting by test executable (the executables differ a lot in size) and needs no list to maintain.

The projections for setup rely on the vcpkg cache restoring after the pin, which only a CI run can confirm; the PR description reports what the first runs showed.
