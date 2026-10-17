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
