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
2. **Batch the test TUs (unity or grouped).** Catch2 costs about 3.1 s of the roughly 5 s front end in each of the 273 test TUs (about 900 s CPU cold). CMake `UNITY_BUILD` with `UNITY_BUILD_BATCH_SIZE` 4 on `ichor_tests` would cut that to a quarter. Risks: anonymous-namespace and `TEST_CASE` name collisions between files, and a coarser sccache key (editing one test recompiles its batch on a miss). Needs a collision audit first, so not "clearly safe and local". Not done.
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
