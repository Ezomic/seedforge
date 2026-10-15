# Ichor

- A test of a signal property (seam, smoothness, silence) must use a metric that would go red if the feature were removed or broken. Compare against a typical value such as the mean, not a maximum over the whole run.

## Building

- Build and test with `bin/build` (Git Bash), never `cmake` directly: `bin/build [-p preset] [-c config] [-t ctest-regex] [target ...]` (default `ichor_tests`, Debug).
- It queues machine-wide (FIFO, `ICHOR_BUILD_SLOTS` builds at once, default 2), prints your queue position, and frees its slot on exit or crash.
- It runs the vcpkg-touching configure one session at a time and only when inputs changed, and shares the vcpkg binary cache.
- With `sccache` installed it is used as the compiler launcher, but only Ninja presets (`windows-ci`) honour it; the default Visual Studio preset ignores it.

## Adding files

- New test: just add `engine/tests/test_foo.cpp` (or `game/stage0/tests/test_foo.cpp`). New source in an existing module: just add `src/foo.cpp`. Both are globbed with `CONFIGURE_DEPENDS`, so no CMakeLists edit is needed and none should be made.
- Only a new module directory (`add_subdirectory` in `engine/CMakeLists.txt`), a new target, or a new link dependency edits CMake. Files with their own target (`test_editor_panels.cpp`, `test_golden_image.cpp`, `panels.cpp`, `miniaudio_impl.cpp`, stage0 `main.cpp`) are excluded by name next to the glob.
