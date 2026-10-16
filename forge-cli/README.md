# forge-cli

Command-line wrapper around libworldforge: PNGs, manifest, batch runs.

This is what the batch seed harness drives when it pushes a thousand seeds through the
phases and reports validation-gate failures by cause - the point being to find
over-constrained axioms before a player does.

    ichor_forge_cli --seed 10 --seed-end 19 --size 256 --phase tectonics --out out/
    ichor_forge_cli --seed 7              # manifest to stdout, nothing written
    ichor_forge_cli --list                # registered phases

Each seed writes `out/seed_N/<layer>.png` and one `out/manifest.json` lists every layer with its
size and a 64-bit FNV-1a hash, so a changed phase shows up as a changed hash. A seed whose phase
fails (for example a size outside the phase's limits) gets an `error` in the manifest and the exit
code is 1; the rest of the batch still runs.

Adding a phase is one entry in `phaseRegistry()` (`src/registry.cpp`). PNGs use stored deflate, so
there is no compressor dependency and the files are byte-identical on every platform. The library
(`ichor_forgecli`) includes only libworldforge, never engine code.
