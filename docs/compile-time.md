# Compile time budget (SEED-275)

Measured on the shared 12-thread build machine (MSVC 14.44, Ninja `windows-ci`, Release, `/Z7`, sccache). The machine runs about 15 sessions at once, so absolute seconds carry roughly +-30% noise; the ratios between rows measured back to back are the reliable part. Everything below was measured, not estimated, unless it says "derived".

## What the build is

| Item | Number |
| --- | --- |
| Translation units in the `windows-ci` graph | 538 object files (500 `.cpp` in the tree, 570 including tools and game targets by include scan) |
| Of which in `ichor_tests` | 262 to 273 (about 62% of all compile CPU time) |
| Compile CPU time in one cold-ish build log (`.ninja_log`, under load) | 5471 s over 538 objects, 3421 s of it in test objects |
| Median per-TU time across 11 sibling worktree logs (mostly sccache hits) | 2.4 s |
| Link steps | all `.exe` and `.lib` links together take about 82 s (median over 11 logs) |
| sccache shared cache | 84.6% hit rate, but the cache is at its 10 GiB cap, so eviction is already happening |

Per module, summed object time from the cold-ish log: tests 3190 s, stage0 448 s, world 250 s, data 239 s, ui 170 s, graph 170 s, factions 125 s, gfx 116 s, progression 115 s, save 97 s, assets 91 s.

## Where one translation unit's time goes

Six representative TUs, compiled directly with `cl /Bt+` (front end `c1xx` = parsing and template instantiation, back end `c2` = optimiser and codegen):

| TU | c1xx | c2 | total |
| --- | --- | --- | --- |
| tests/test_xref | 5.9 s | 3.8 s | 10.1 s |
| tests/test_determinism | 9.4 s | 4.6 s | 14.5 s |
| tests/test_data | 5.2 s | 1.9 s | 8.7 s |
| data/axiom | 5.9 s | 4.9 s | 11.4 s |
| world/streamer | 5.0 s | 1.8 s | 7.1 s |
| graph/quest_expansion | 5.0 s | 2.4 s | 7.6 s |

An empty TU compiles in 0.1 s, so most of the front-end time is header parsing, and the back end is 25 to 45% of a TU.

## Heaviest headers

Parse cost of one header included alone, `cl /Zs`, minimum of 3 runs (empty TU is 0.1 s). `#TUs` is how many TUs reach it transitively, from a static include scan of the whole tree.

| Header | Parse | #TUs | Derived CPU in a cold full build |
| --- | --- | --- | --- |
| `nlohmann/json.hpp` | 5.2 s | 45 | about 230 s |
| `catch2/catch_test_macros.hpp` | 3.2 s | 289 | about 900 s |
| `<filesystem>` | 3.3 s (about 2 s over `<string>`) | 284 | about 570 s |
| `<format>` | 2.3 s | 51 | about 120 s |
| `glm/mat4x4.hpp` | 2.1 s | 116 | about 240 s |
| `glm/gtc/quaternion.hpp` | 2.0 s | 91 | about 180 s |
| `glm/glm.hpp` | 1.9 s | 1 (`cloud_shadow.h`) | small |
| `entt/entity/registry.hpp` | 1.7 s | 57 (all via `sim/scene.h`) | about 95 s |
| `<string>` / `<vector>` | 1.1 s / 0.8 s | 488 / 513 | the unavoidable floor |

Derived CPU overlaps (these headers share std includes), so do not add the column up.

The repo's own headers are already lean. The biggest in-repo header by reach times size is `sim/scene.h` (57 TUs, 6.6 kB); the most widely included are `core/reflect.h` (144 TUs), `core/hash.h` (132), `assets/bundle.h` (112) and `data/data_set.h` (109), all under 3.2 kB with no heavy includes. No repo header includes `nlohmann/json.hpp`, `<regex>`, `<iostream>` or `<random>`. The cost is third-party and standard headers, not our own header layering. `<filesystem>` is pulled into about 38 headers because `std::filesystem::path` is in their signatures (`input.h`, `data_set.h`, `axiom.h`, `save/*`, `assets/*`), so it cannot be removed without changing those contracts.

## Top 10 offenders

Slowest objects. Wall time comes from the `.ninja_log` of this worktree (loaded machine); "median" is the median over the sibling logs that rebuilt it (mostly measured on a loaded machine too).

| # | Object | Log (s) | Median (s) | Notes |
| --- | --- | --- | --- | --- |
| 1 | tests/test_determinism | 42.4 | | c1xx 9.4 s, heaviest test TU even unloaded (14.5 s) |
| 2 | stage0/src/combat.cpp | 35.8 | | |
| 3 | app/src/main.cpp | 34.8 | 24.9 | |
| 4 | tests/test_capability_loot_access | 33.6 | | |
| 5 | data/src/axiom.cpp | 33.5 | 18.6 | nlohmann/json heavy |
| 6 | tests/test_codec_fuzz | 30.3 | | |
| 7 | data/src/conflicts.cpp | 28.4 | 15.3 | nlohmann/json heavy |
| 8 | tests/test_faction_questlines | 28.1 | | |
| 9 | tests/test_custodian_mapping | 27.9 | | |
| 10 | assets/src/material_limits.cpp | 27.1 | 16.5 | |

Also high: `data/src/npc_tiers.cpp` (18.2 s median), `sim/src/scene.cpp` (16.3 s), `graph/src/quest_expansion.cpp`, `data/src/ue_export.cpp`, `data/src/synergy.cpp`, `data/src/data_set.cpp`.

Links (median over the sibling logs): `tests/ichor_tests.exe` 14.9 s (25 to 28 s under load), `ichor_gfx_tests.exe` 5.8 s, `app/ichor.exe` 5.7 s, `stage0.exe` 4.7 s, `stage0_tests.exe` 4.6 s. `catch_discover_tests` runs `ichor_tests.exe --list-tests` after the link, measured at 0.17 s warm, so it is not a cost. The single `ichor_tests.exe` link is the daily tax: it is serial and re-runs after every test edit.

## Warm and cold, and what PCH does here

Same six TUs plus a PCH variant. `sccache cold` is `SCCACHE_RECACHE=1`; `warm` is a cache hit. PCH figures use a PCH built from `engine/ichor_pch.h`.

| TU | direct | sccache cold | sccache warm | with PCH (excluding the one-off PCH build) |
| --- | --- | --- | --- | --- |
| test_xref | 14.7 s | 14.7 s | 1.7 s | 7.9 s |
| test_determinism | 21.1 s | 18.1 s | 3.0 s | 11.5 s |
| test_data | 10.3 s | 9.0 s | 2.0 s | 3.2 s |
| data/axiom | 19.9 s | 18.3 s | 1.7 s | 8.7 s |
| world/streamer | 14.3 s | 11.7 s | 2.4 s | 3.4 s |
| graph/quest_expansion | 12.6 s | 16.0 s | 1.7 s | 4.9 s |
| **sum** | **92.9 s** | **87.8 s** | **12.5 s** | **39.6 s** |

(The direct and cache columns were run on a busier machine than the `/Bt+` table above, hence the larger numbers.) The PCH itself took 7 to 9 s to build per target.

Reading this:

- Warm sccache is 7x faster than a plain compile and 3x faster than a PCH compile. The daily loop is therefore decided by the cache hit rate, not by header tuning.
- A cold compile with sccache costs the same as no cache, and a PCH halves it, but `CMakeLists.txt` turns the PCH off whenever a compiler launcher is set, and both `bin/build` and `engine.yml` set sccache. So the PCH (`ichor_pch.h`) is only live in the Visual Studio `windows` preset. Nothing in the standard flows benefits from it.
- Because PCH is off, `ichor_pch.h` contents (including the 5 s `nlohmann/json.hpp`) are never parsed in the standard flows; each TU pays only for what it includes.

## Ranked fixes

Ordered by expected payoff per risk. "Done" is only what this PR changes (nothing in code).

1. **Cache capacity (config, no code).** The shared sccache is at its 10 GiB cap with an 84.6% hit rate. With 15 worktrees and cold-to-warm swings of 7x per TU, evictions turn into full recompiles. Raise `SCCACHE_CACHE_SIZE` for `/e/build-queue/sccache` (disk permitting). Not done: it is machine config owned by the build queue.
2. **Batch the test TUs (unity or grouped).** Catch2 costs about 3.1 s of the roughly 5 s front end in each of the 273 test TUs (about 900 s CPU cold). CMake `UNITY_BUILD` with `UNITY_BUILD_BATCH_SIZE` 4 on `ichor_tests` would cut that to a quarter. Risks: anonymous-namespace and `TEST_CASE` name collisions between files, and a coarser sccache key (editing one test recompiles its batch on a miss). Needs a collision audit first, so not "clearly safe and local". Measured in the SEED-468 spike below: 31 to 38% of the test files clash, and the warm loop does not improve, so not adopted.
3. **Split `ichor_tests.exe` into 3 or 4 executables by domain.** Cuts the 15 to 28 s serial link on every test edit to the part you touched, and lets links run in parallel. A CMake change that affects test names in `ctest`; not done.
4. **Build tests with `/O1` or `/Od`.** Measured with direct compiles, same TUs, best of 2: `/O2 /Ob2` 8.2, 10.3, 4.5, 8.3, 8.6, 7.7 s; `/O1` 6.9, 9.7, 3.9, 9.1, 6.0, 6.7 s; `/Od` 4.7, 7.9, 3.2, 6.5, 5.6, 5.4 s (test_xref, test_determinism, test_data, axiom, streamer, quest_expansion). That is about 30% off at `/Od`. It changes codegen and runtime of the determinism and fuzz tests, and CI's Linux job already covers the sanitizer side, so it needs a runtime comparison first. Not done.
5. **Keep `<filesystem>` and `<format>` out of the next widest headers.** `<filesystem>` is in 38 headers because `path` is in their signatures; a `std::string_view` or an `ichor::Path` wrapper would remove it but changes those contracts. `<format>` is already confined to `.cpp` files through `log.h`. Not done (contract change).
6. **Move `entt/entity/registry.hpp` off the editor headers.** `editor/picking.h` and `editor/session.h` only need `Scene&` and `components.h`, so a forward declaration of `Scene` works. Tried: it builds with `scene.h` added to `picking.cpp`, `session.cpp` and `tests/test_editor.cpp`, but it only saves the roughly 1.7 s entt parse in about 7 TUs (about 12 s CPU), and any sibling branch whose new test includes `session.h` and uses `Scene` would start failing after merge. Reverted as not worth the churn.
7. **Do not widen `ichor_pch.h`.** Neither CI nor `bin/build` uses it. If the PCH is ever to matter, the options are to drop the launcher for Debug-only builds or to move to a sccache-friendly scheme (for example unity, item 2).

## What this PR changes

Only this document. No source, CMake or workflow edits: the one candidate source fix (item 6) was measured as marginal and risky for parallel branches, and the larger wins (items 1 to 4) change build structure or machine config and need their own tickets.

## Reproducing

- Slowest objects and links: parse `engine/build/windows-ci/.ninja_log` (start ms, end ms, ..., output).
- Header parse cost: a one-line TU (`#include <header>` plus `int x;`) compiled with the flags from `build.ninja` plus `/Zs`.
- Front end vs back end: `cl /Bt+` with the target's `DEFINES`, `FLAGS`, `INCLUDES` from `build.ninja`.
- Cold vs warm sccache: run the same compile through `sccache cl` with `SCCACHE_RECACHE=1`, then without; use a private `SCCACHE_DIR` and `SCCACHE_SERVER_PORT` so the shared cache is untouched.

## Spike: unity batches and a Catch2 PCH for the test executables (SEED-468)

Question: do CMake `UNITY_BUILD` / `UNITY_BUILD_BATCH_SIZE`, or a precompiled header for Catch2 alone, pay off on the four domain executables from SEED-467 (`ichor_tests_world` 98 files, `_sim` 100, `_ui` 67, `_core` 27; 292 test files)? Measured with throwaway CMake switches (not kept): `set_target_properties(<t> PROPERTIES UNITY_BUILD ON UNITY_BUILD_BATCH_SIZE <n>)`, `SKIP_UNITY_BUILD_INCLUSION` on files that clash, and `target_precompile_headers(<t> PRIVATE <catch2/catch_test_macros.hpp>)`.

### Compile CPU per translation unit (the reliable number)

Eight clash-free test files, `cl /Bt+` with the real flags (`/O2 /Ob2 /Z7`), best of 2, c1xx plus c2, same files in every row:

| Variant | CPU (8 files) | vs separate TUs |
| --- | --- | --- |
| Separate TUs (today) | 40.0 s | |
| Unity, batch 4 (2 TUs) | 16.9 s | -58% |
| Unity, batch 8 (1 TU) | 11.7 s | -71% |
| Catch2 PCH, each file compiled against it | 15.5 s | -61% |
| Catch2 PCH build itself | 2.8 s, once per executable | |

So both ideas remove the repeated Catch2 and standard-library parse, as predicted. The ceiling for a clean batch of 4 is roughly 2.4x less compile CPU, and the PCH reaches about the same without touching any test source.

### Whole-build wall time (noisy, read with care)

`core` + `ui` executables (94 files), private sccache dir per variant, `bin/build` at 3 compile jobs. The machine load swung by more than the effect: the same baseline measured 668 s, 401 s and 477 s cold. Treat these as direction only; the CPU table above is the number to trust.

| Variant | Cold | Warm (all cache hits) | After editing one test |
| --- | --- | --- | --- |
| Baseline | 401 to 668 s | 53 to 117 s | 7 to 12 s |
| Unity, batch 4 (21% of files excluded for clashes) | 420 to 510 s | 55 to 60 s | 12 to 15 s |
| Unity, batch 8 (about 38% excluded) | 425 s (772 s in a loaded rerun) | 56 s | 15 s |
| Catch2 PCH | 451 s | 118 s | 6 s |

Link time did not decide anything: `ichor_tests_core` 1.9 to 4.6 s and `ichor_tests_ui` 3.5 to 8.9 s in the baseline, 2.1 to 2.5 s and 3.7 to 5.0 s with unity (fewer, larger objects), all inside the load noise.

### Unity clashes

Compiling all four executables with batch 4 and `-k 0`, then excluding every file the compiler named and repeating until the build was green:

| Batch size | Files that had to be excluded | Share of 292 |
| --- | --- | --- |
| 4 | 90 | 31% |
| 8 | 111 | 38% |

The first pass at batch 4 alone reported 75 distinct clashing names across 40 files (36 of about 74 batches failed). Typical clashes:

- Same helper or constant in each file's anonymous namespace: `kSeed`, `kGrid`, `kDt`, `kNaN`/`kNan`, `kInf`, `readJson`, `split`, `shipped`, `mentions`, `collectStrings`, `problems`, `text`, `Fixture`, `Loaded`, `World`, `Skeleton`, `Finding`.
- Families written together share copied helpers: `test_worldforge_*`, `test_forged_*`, `test_notable_items_*`, `test_synergy_*`, `test_setpiece_quests_*`, `test_faction_questlines_*`, `test_dungeon_skeletons_*`.
- Two real headers define `ichor::Capability` (`traversal/verbs.h` and `progression/capabilities.h`), so any batch that mixes a traversal test with a progression test fails even without anonymous namespaces.
- 286 of 294 test files have a file-scope `using namespace`, and 18 define macros; these leak into every later file of a batch and are the second wave of errors once the obvious redefinitions are excluded.

The excluded files still compile on their own, so the measured wall numbers above already include the loss: 21 to 38% of the files get no benefit.

### Why the fix is not cheap

- The mechanical fix is wrapping each file in a unique namespace (or renaming each helper). That touches roughly a third of 292 files, and every parallel session adds test files, so it would conflict constantly.
- Clashes appear non-locally. Batches follow glob order, so adding `test_foo.cpp` can break the build of an unrelated neighbour, and only on a configuration with unity on. Without a CI job that builds unity, it regresses silently; with one, it adds a build that costs as much as the thing it saves.
- A new or removed file shifts every later batch boundary in its executable, which changes the key of every following unity TU. A one-file change then costs a cache miss for each later batch under sccache, while today it costs one TU.
- Editing one test rebuilds its whole batch on a miss: 12 to 15 s measured against 7 to 12 s for a separate TU, and it grows with the batch size.

### Why the PCH does not help here

`CMakeLists.txt` already leaves PCH off when a compiler launcher is set, because sccache cannot cache a compile that uses `/Yu`. This was checked directly: one `/Yc` and two `/Yu` compiles through a private sccache gave 0 hits, 0 misses and 3 non-cacheable calls. Under `bin/build` and CI the Catch2 PCH would therefore turn every test TU into an uncacheable compile: the warm build with PCH took 118 s, against a 53 to 117 s warm baseline whose spread is load noise, and a warm baseline is all cache hits where the PCH build recompiles everything. A one-test edit was fast (6 s), but only because it compiles one TU against an existing PCH, which a separate TU already does at 7 to 12 s. The PCH is a win only where there is no cache (the Visual Studio `windows` preset, which already includes Catch2 through `ichor_pch.h`). In the flows that matter it trades the cache for a 61% cheaper cold compile, so nothing changes.

### Conclusion and recommendation

- Compile CPU per TU falls by 58% (batch 4) to 71% (batch 8), but only for files that can be batched. After the clash exclusions the whole-build cold gain measured about 12 to 36% with large noise, below the 30% bar, and it depends on fixing a third of the test files.
- The warm path, which is the daily loop, did not improve (about 55 s with unity, 53 to 117 s baseline depending on load), and a one-test edit got slower, because the cache hit rate decides it and unity coarsens the cache key.
- Decision: do not enable unity or a test PCH. No CMake change is kept; this PR is the document only.
- If cold CI time ever becomes the bottleneck, the order to try is: (1) item 1 above (cache capacity), since a warm cache beats every compile-time trick measured here; (2) a one-off codemod that puts each test file in its own namespace, as its own ticket, enabling unity for just the executable with the lowest clash count (`ichor_tests_core`: 4 of 27 files clash) and a CI job that fails on a new clash; (3) only then batch size above 4, which saves a further 13 points of CPU but doubles the cost of every test edit.
- Reproduce: set the three properties above in the executable loop of `engine/tests/CMakeLists.txt`, build with `-k 0` through Ninja inside the MSVC environment to list every clash at once, and compare `cl /Bt+` on the same files against a concatenated `#include` TU.
