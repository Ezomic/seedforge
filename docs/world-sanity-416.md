# World sanity with the SEED-416 defaults (SEED-497 report)

Report only. No default or threshold was changed. Questions: why do seeds 3, 4 and 5 at size 64 forge worlds that fail every gate, and do the SEED-416 defaults (48 erosion rounds, forge-cli river threshold scaled by area, carve 128, max carve 60) hold up on realistic worlds? Run on `origin/main` (after PR 410) with `ichor_forge_cli --seed 1 --seed-end 16 --size S --data data --validate --out vS`, plus `ichor_forge_cli gallery --seeds 1..16 --size S`, for S = 64, 128, 256 and 1024. Gate numbers below are from the **first attempt** of each seed, because `--validate` rerolls up to 8 times and an "accepted" seed hides how often its own seed fails. CI does not run on the PR that carries this file (docs only).

## Gate results per size (16 seeds, first attempt, seeds as given)

| Size | settlement_density | road_connected | settlement_reach | no_dead_ends | landmark_coverage | quest_items | quest_cast | All 7 | Accepted after rerolls |
|---|---|---|---|---|---|---|---|---|---|
| 64 | 13 | 13 | 13 | 13 | 8 | 13 | 13 | 8 | 13 (seeds 3, 4, 5 never reach validation) |
| 128 | 16 | 16 | 16 | 16 | 15 | 16 | 16 | 15 | 16 |
| 256 | 16 | 16 | 16 | 16 | 13 | 16 | 16 | 13 | 16 |
| 1024 | 0 | 16 | 16 | 16 | 0 | 16 | 16 | 0 | 0 |

Cells are seeds passing out of 16. `axiom_directives_starved` is skipped everywhere (axioms are not resolved yet). The two sanity thresholds the report relies on (`sanityThresholds()` in forge-cli) zero the quest minimums, so quest_items and quest_cast cannot fail.

Failing seeds: size 128, seed 4 (landmark 0). Size 256, seeds 1, 5, 8 (landmark 513, 375, 521 against 550). Size 64, seeds 6, 9, 10, 11, 14 (landmark) and 3, 4, 5 (forge error, below). Size 1024: every seed fails settlement_density (value 74, minimum 500) and landmark_coverage (147 to 432, minimum 550), and all 16 are rejected after 8 attempts.

## Why seeds 3, 4 and 5 fail at size 64

They are not "failing all 7 gates" so much as not forging a world: `roads: no settlement sites to connect`. The biome layer is 100% ocean (4096 of 4096 cells), no rivers, no suitability, no settlements.

Land share (cells at or above 0 m) after each stage, 64 grid:

| Seed | after tectonics | after erosion | after hydrology carve |
|---|---|---|---|
| 3 | 16.7% | 0.0% | 0.0% |
| 4 | 9.7% | 0.0% | 0.0% |
| 5 | 14.9% | 0.0% | 0.0% |
| 6 | 28.2% | 1.7% | 1.7% |
| mean of 16 | 35.3% | 17.8% | 17.8% |

Erosion removes about half of all land at size 64 and everything on the three thin-continent seeds. The same seeds at larger sizes:

| Size | mean land after tectonics | after erosion | seed 3 | seed 4 | seed 5 |
|---|---|---|---|---|---|
| 64 | 35.3% | 17.8% | 0.0% | 0.0% | 0.0% |
| 128 | n/a | 28.1% (biome land) | 26.7% | 4.4% | 2.7% |
| 256 | 33.4% | 31.0% | 18.0% | 7.6% | 3.8% |
| 1024 | 36.7% | 36.8% | 13.1% | 8.5% | 31.7% |

At 1024 erosion is neutral (36.7% to 36.8%); at 256 it costs 2 points; at 64 it costs 17. So the 64 failure is a **small-grid artefact of erosion**: 48 rounds with per-cell rates act on a grid whose cells span far more ground, so a thin continent is planed below sea level. Hydrology carving then changes nothing.

## What is real

1. **Settlement density and landmark coverage cannot pass at 1024 with the current config.** `defaultForgeConfig()` leaves `maxSettlements = 20` and `maxPois = 16`, with a fixed 64 cell view range, while the density gate divides by area. Density is 4768 at 128, 1192 at 256 and 74 at 1024, against a minimum of 500, so the gate passes only up to a size of about 395. Landmark coverage falls the same way (a fixed number of landmarks, a fixed sight range, a bigger world). This is a mismatch between the config, which scales spacing with size but not counts, and the gates, which are per area. It is not terrain. Every 1024 world reaches 20 settlements and 20 POIs and passes road_connected, settlement_reach and no_dead_ends.
2. **Land share varies a lot per seed, at every size.** At 1024: 9.1% (seed 4), 14.2% (seed 3), 22.3% (seed 12) up to 85.7% (seed 16); the mean is 37.9%. Seeds 4 and 3 are thin at every size (8.6% and 13.3% land straight out of tectonics at 1024), so this comes from the tectonic substrate (plate types and heights, SEED-448), not from erosion. At 256 seed 5 has 6.8% land out of tectonics and is the second thinnest world. Whether a 4% to 10% land world is acceptable is a design call; no gate currently measures land share.
3. **Landmark coverage is the only gate that fails first-attempt at 128 and 256**, 1 and 3 seeds out of 16. Rerolling rescues them. It is the marginal gate at every size (values near 375 to 521 against 550).
4. **Rivers and lakes are present at 256 and 1024** for all seeds except thin ones: river cells 7 (seed 4) to 2730 at 256, 504 to 18821 at 1024. Seed 5 at 256 has 0 river cells with 4% land. Lake cells reach 54k at 1024 (seed 16); not checked whether that is a lot.

## Recommendation

- Resolved by SEED-499: forgeWorld now scales the erosion rounds and the thermal rate with side / 256 on grids smaller than 256 (`scaleErosionToGrid`), so the land erosion removes is about the same at 64, 128 and 256 and seeds 3, 4 and 5 forge at 64. Sizes 256 and up are unchanged. The bullet below is the original recommendation.
- Treat the size 64 failures as an artefact. Do not tune erosion to rescue 64. Keep tests off 64 for seeds that are mostly ocean (PR 410 did: gallery uses 6 to 8), or make erosion scale its work to the grid.
- The nightly sanity report (SEED-494) uses seeds 1 to 8 at size 128, where all 8 are accepted and 7 pass on the first attempt. Size 256 is also green after rerolls.
- Decide what "sane at 1024" means. Either scale `maxSettlements` and `maxPois` (and ideally the view range) with area in forge-cli's config, or make the gate density per land area. Until then a 1024 run is red by construction and cannot be used to judge terrain.
- Decide whether a minimum land share gate is wanted. Today seeds with under 10% land can pass every gate (seed 4 at 256, 7.6% land).
- Not checked: how these numbers compare with the world before SEED-416 (no old build was run), and whether the gate thresholds themselves are right.

## Pictures and raw output

Everything is under `C:\Users\Eigenaar\AppData\Roaming\Claude\side-session-notes\local_7ba3bc00-5c64-4d3f-9fdb-424e0d4870da\seed-497\` (not in the repo; the 1024 gallery is 51 MB):

- `gallery64.png`, `gallery128.png`, `gallery256.png`, `gallery1024.png`: seeds 1 to 16, each cell captioned with passed over evaluated gates and the failed gate numbers (`gallery<S>.log` has the text).
- `v64`, `v128`, `v256`, `v1024`: full forge-cli output with `validation.json`, `manifest.json` and every layer PNG per seed.
- `analyze.py`, `layers.py`, `stages.py`: the scripts that produced the tables above.
