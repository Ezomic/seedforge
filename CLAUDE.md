# Ichor

- A test of a signal property (seam, smoothness, silence) must use a metric that would go red if the feature were removed or broken. Compare against a typical value such as the mean, not a maximum over the whole run.

## Building

- Build and test with `bin/build` (Git Bash), never `cmake` directly: `bin/build [-p preset] [-c config] [-t ctest-regex] [target ...]` (default `ichor_tests`, Debug).
- It queues machine-wide (FIFO, `ICHOR_BUILD_SLOTS` builds at once, default 2), prints your queue position, and frees its slot on exit or crash.
- It runs the vcpkg-touching configure one session at a time and only when inputs changed, and shares the vcpkg binary cache.
- With `sccache` installed it is used as the compiler launcher, but only Ninja presets (`windows-ci`) honour it; the default Visual Studio preset ignores it.
