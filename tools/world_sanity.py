#!/usr/bin/env python3
"""Run ichor_forge_cli --validate over a fixed seed range and name every seed and design check that failed.

    python tools/world_sanity.py <path-to-ichor_forge_cli> [--seeds 1-8] [--size 128] [--data data] [--out DIR]

Prints each seed's land share (land_share seed N: P%) before the verdicts.
Exit: 0 every seed accepted, 1 some seed rejected or failed to forge, 2 usage or setup error.
"""
import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def parse_range(text: str) -> tuple[int, int]:
    first, _, last = text.partition("-")
    begin = int(first)
    end = int(last) if last else begin
    if begin > end:
        raise ValueError(text)
    return begin, end


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("cli")
    parser.add_argument("--seeds", default="1-8")
    parser.add_argument("--size", type=int, default=128)
    parser.add_argument("--data", default=str(ROOT / "data"))
    parser.add_argument("--out")
    args = parser.parse_args()
    try:
        begin, end = parse_range(args.seeds)
    except ValueError:
        print(f"world_sanity: bad --seeds '{args.seeds}', expected N or N-M", file=sys.stderr)
        return 2

    out = Path(args.out) if args.out else Path(tempfile.mkdtemp(prefix="world-sanity-"))
    cmd = [str(Path(args.cli).resolve()), "--seed", str(begin), "--seed-end", str(end), "--size", str(args.size),
           "--data", args.data, "--validate", "--out", str(out)]
    print("world_sanity:", " ".join(cmd), flush=True)
    run = subprocess.run(cmd, capture_output=True, text=True)
    sys.stdout.write(run.stdout)
    sys.stderr.write(run.stderr)

    manifest = out / "manifest.json"
    if not manifest.is_file():
        print(f"world_sanity: no manifest.json in {out}, forge-cli exited {run.returncode}")
        return 1
    runs = json.loads(manifest.read_text())["runs"]
    expected = end - begin + 1
    bad = 0
    for r in runs:
        seed = r["seed"]
        share = r.get("validation", {}).get("land_share_permille", -1)
        if share >= 0:
            print(f"land_share seed {seed}: {share / 10:.1f}%")
        if "error" in r:
            print(f"FAIL seed {seed}: forge error: {r['error']}")
            bad += 1
        elif r.get("validation", {}).get("rejected"):
            print(f"FAIL seed {seed}: rejected by validation")
            for cause in r["validation"]["causes"]:
                print(f"  design check failed: {cause}")
            bad += 1
    if len(runs) != expected:
        print(f"FAIL only {len(runs)} of {expected} seeds ran")
        bad += 1
    if bad == 0 and run.returncode != 0:
        print(f"FAIL forge-cli exited {run.returncode} with every seed accepted")
        bad = 1
    if bad:
        print(f"world_sanity: {bad} problem(s) across seeds {begin}-{end} at size {args.size}")
        return 1
    print(f"world_sanity: seeds {begin}-{end} at size {args.size} all accepted")
    return 0


if __name__ == "__main__":
    sys.exit(main())
