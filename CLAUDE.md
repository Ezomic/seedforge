# Ichor

- A test of a signal property (seam, smoothness, silence) must use a metric that would go red if the feature were removed or broken. Compare against a typical value such as the mean, not a maximum over the whole run.

## Building

- Build and test with `bin/build` (Git Bash), never `cmake` directly: `bin/build [-p preset] [-c config] [-t ctest-regex] [target ...]` (default `ichor_tests`, preset `windows-ci`).
- The default preset is the Ninja `windows-ci` (Release, output in `engine/build/windows-ci`). `bin/build` imports the MSVC environment itself (vswhere + `vcvars64.bat`), so no Developer prompt is needed. `-p windows` still selects the Visual Studio generator (Debug unless `-c`, output in `engine/build/windows`). Ninja presets are single-config: `-c Debug` reconfigures with that build type.
- It queues machine-wide (FIFO; builds at once = `ICHOR_BUILD_SLOTS`, else the number in `/e/build-queue/slots.conf`, else 2), prints your queue position, and frees its slot on exit or crash.
- It runs the vcpkg-touching configure one session at a time and only when inputs changed, and shares the vcpkg binary cache.
- `sccache` (`winget install Mozilla.sccache`) is used automatically as the compiler launcher with the shared cache dir (`ICHOR_BUILD_CACHE`, default `/e/build-queue/sccache`) and its stats print after each build. Only Ninja presets honour it; the Visual Studio preset ignores it. Without sccache the build still works, uncached.

## Adding files

- New test: just add `engine/tests/test_foo.cpp` (or `game/stage0/tests/test_foo.cpp`). New source in an existing module: just add `src/foo.cpp`. Both are globbed with `CONFIGURE_DEPENDS`, so no CMakeLists edit is needed and none should be made.
- Only a new module directory (`add_subdirectory` in `engine/CMakeLists.txt`), a new target, or a new link dependency edits CMake. Files with their own target (`test_editor_panels.cpp`, `test_golden_image.cpp`, `panels.cpp`, `miniaudio_impl.cpp`, stage0 `main.cpp`) are excluded by name next to the glob.
