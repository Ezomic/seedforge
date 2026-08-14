# libworldforge

Pure C++, no Unreal. Phases 0-11 of the world generator.

Kept out of UE deliberately: doc 17 puts iteration speed above almost every other technical
decision, and a generator that rebuilds in two seconds instead of two minutes is the
difference between trying an idea and not bothering.

    src/     tectonics, erosion, hydrology, climate, biomes,
             settlement, roads, politics, poi, binding
    tests/   golden-image and determinism tests

Prototype each phase in Python/NumPy first - terrain work is array operations and iteration
speed matters more than runtime - then port phase by phase, diffing debug images as you go.
