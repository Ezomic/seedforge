#!/usr/bin/env python3
"""Run the headless smoke run and compare its scene state hash with engine/tests/data/app_smoke_hash.txt.

    python tools/check_smoke_hash.py <path-to-ichor.exe>           # CI: fail on a mismatch
    python tools/check_smoke_hash.py <path-to-ichor.exe> --update  # record the new value
"""
import re
import subprocess
import sys
from pathlib import Path

EXPECTED = Path(__file__).resolve().parent.parent / "engine" / "tests" / "data" / "app_smoke_hash.txt"
LINE = re.compile(r"exit after (\d+) frames, (\d+) ticks, state ([0-9a-f]{16})")
ARGS = ["--headless", "--frames", "120"]


def main() -> int:
    update = "--update" in sys.argv
    paths = [a for a in sys.argv[1:] if a != "--update"]
    if len(paths) != 1:
        print(__doc__)
        return 2
    exe = Path(paths[0]).resolve()
    run = subprocess.run([str(exe), *ARGS], capture_output=True, text=True, cwd=EXPECTED.parents[2])
    print(run.stdout, end="")
    print(run.stderr, end="", file=sys.stderr)
    if run.returncode != 0:
        print(f"smoke run exited with {run.returncode}")
        return 1
    found = LINE.findall(run.stdout + run.stderr)
    if not found:
        print("smoke run did not log an 'exit after N frames, N ticks, state <hash>' line")
        return 1
    frames, ticks, actual = found[-1]
    observed = f"{frames} frames, {ticks} ticks, state {actual}"
    if update:
        EXPECTED.write_text(observed + "\n")
        print(f"recorded '{observed}' in {EXPECTED}")
        return 0
    expected = EXPECTED.read_text().strip()
    if observed == expected:
        print(f"smoke hash matches: {observed}")
        return 0
    print(
        f"SMOKE HASH MISMATCH\n  expected: {expected}\n  observed: {observed}\n"
        "The shipped executable simulates differently from the checked-in baseline.\n"
        "If the change was meant to alter the simulation, run\n"
        "  python tools/check_smoke_hash.py engine/build/windows-ci/app/ichor.exe --update\n"
        "(after building the windows-ci preset) and commit engine/tests/data/app_smoke_hash.txt\n"
        "together with the regenerated determinism.json. If not, this is a determinism regression: do not update."
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
