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
