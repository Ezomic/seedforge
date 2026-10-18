# libworldforge

Pure C++, no engine. Phases 1-11 of the world generator, header-only. Phase 0 (axioms) is not here yet; `forge-cli` uses a default config. Phase 12 (bake) is engine code in `engine/world/` and is not part of this library.

Kept separate from the engine deliberately: doc 17 puts iteration speed above almost every other
technical decision, and a generator that rebuilds in two seconds instead of two minutes is the
difference between trying an idea and not bothering. It depends on the standard library only;
`tools/check_deps.py` enforces that in ctest.

    CMakeLists.txt        INTERFACE library `worldforge` (alias `worldforge::worldforge`)
    include/worldforge/   one header per phase: tectonics, erosion, hydrology, climate, biomes,
                          resources, settlements, roads, politics, poi, region tier, landmark
                          coverage, content manifest, subseed

Consumers link `worldforge` for the include path and C++20. Two hosts use it today: `forge-cli`
(`ichor_forgecli`) for iteration and batch runs, and the engine tests. The engine proper does not
include it yet: its forge still draws a stand-in terrain until SEED-449. Tests live in
`engine/tests/test_worldforge_*.cpp`, not here.

Prototype each phase in Python/NumPy first - terrain work is array operations and iteration
speed matters more than runtime - then port phase by phase, diffing debug images as you go.
