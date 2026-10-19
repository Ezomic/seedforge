# Forge pipeline performance budget (SEED-450)

Measured on the shared 12-thread Windows build machine (MSVC, Ninja `windows-ci`, Release), about 15 sessions running at once, so absolute milliseconds carry roughly +-30% noise. The ratios and the allocation counts are the reliable part. Numbers come from `forgeWorld` (`forge_world.h`) with the rich test config (items, NPCs, dungeons, two factions), seed 11, best of three runs.

The vertical slice (doc 18) does not fix a world size; it only says the engine draws one forged region. These tables therefore cover the default test size (256) and the sizes up to `kForgeTargetSize` (1024, the largest every phase accepts, see `forge-cli/include/forgecli/budget.h`). Sizes above that run erosion at a downscaled work size (`erosion_budget.h`) and are not measured here.

## Time per phase (ms)

| Phase | 256 | 512 | 1024 |
| --- | --- | --- | --- |
| tectonics | 10 | 44 | 156 |
| erosion | 570 | 2500 | 8700 |
| hydrology | 7 | 35 | 150 |
| climate | 1.5 | 7.6 | 22 |
| biomes | 2.3 | 10 | 38 |
| resources | 1.9 | 8.6 | 26 |
| settlements | 6.5 | 17 | 55 |
| roads | 87 | 845 | 896 |
| politics | 3.4 | 18 | 58 |
| pois | 12 | 6.6 | 3.5 |
| content | 0.05 | 0.07 | 0.06 |

Erosion is 70% to 80% of the whole forge. Roads depend on how many settlements the size yields, so they are not linear in cell count (and pois also vary with the sites found), which is why those two rows are noisy across sizes.

## Memory per phase

Peak live heap is not observable from inside the shared test binary, so the guard counts what the allocation hook of `test_codec_fuzz_alloc_hook.cpp` can see exactly: bytes requested per phase (churn, not live bytes) and the largest single allocation. Both are deterministic and identical on every machine.

| Size | Bytes requested, all phases | Largest single allocation |
| --- | --- | --- |
| 256 | 127 MB (erosion 114 MB) | 0.5 MiB |
| 512 | 510 MB (erosion 456 MB) | 2 MiB |
| 1024 | 2.0 GB (erosion 1.8 GB) | 8 MiB |

Peak live memory is bounded by the SEED-424 model (`forge-cli/README.md`, "Forge budget"): about 69 MiB at 1024, committed budget 96 MiB. The erosion rows were doubled for the SEED-416 default of 48 rounds (24 before); the 256 allocation is measured, the 512 and 1024 erosion time and allocation rows and the other phases' totals are scaled estimates, not re-measured. That model is a cell-pass and bytes-per-cell estimate, the measured table above is the check that the real run behaves.

## The CI guard

`engine/tests/test_forge_perf.cpp`, tag `[forge_perf]`, runs the forge at 256 (about 2.5 s for three runs) and fails when:

- a phase takes more than 6 times its recorded cost, where cost is the phase time divided by the time of a fixed reference loop measured in the same process (so a slow or loaded runner scales both). Phases cheaper than 2% of the reference loop are compared against that 2% floor instead, because they are too short to time; a phase that small can get up to about 6 times the floor slower before it trips, and the allocation check below still covers its work;
- a phase requests more than 1.5 times its recorded allocation bytes, or the largest single allocation exceeds 1.5 times its record.

No absolute wall clock is asserted. Other cases in the file check that the guard accepts the record, goes red when any single phase is made 10 times slower or allocates twice as much, and that allocation counts repeat exactly between runs.

To refresh the record after an intended change, run the hidden report and copy the 256 rows into `kRecord`:

```bash
engine/build/windows-ci/tests/ichor_tests.exe "[.perf_report]"
```

`kRecord.timeRatio` is the `ratio` column, `allocBytes` the `alloc` column, `maxSingleAlloc` the largest single allocation in bytes.
