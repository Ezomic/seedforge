# libworldforge

Pure C++, no engine. Phases 0-11 of the world generator, header-only.

Kept separate from the engine deliberately: doc 17 puts iteration speed above almost every other
technical decision, and a generator that rebuilds in two seconds instead of two minutes is the
difference between trying an idea and not bothering. It depends on the standard library only;
`tools/check_deps.py` enforces that in ctest.

    CMakeLists.txt        INTERFACE library `worldforge` (alias `worldforge::worldforge`)
    include/worldforge/   one header per phase: tectonics, erosion, hydrology, climate, biomes,
                          resources, settlements, roads, politics, poi, region tier, landmark
                          coverage, content manifest, subseed

Consumers link `worldforge` for the include path and C++20. Two hosts use it today: `forge-cli`
(`ichor_forgecli`) for iteration and batch runs, and the engine tests. Tests live in
`engine/tests/test_worldforge_*.cpp`, not here.

Prototype each phase in Python/NumPy first - terrain work is array operations and iteration
speed matters more than runtime - then port phase by phase, diffing debug images as you go.
