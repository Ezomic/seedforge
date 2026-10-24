#!/usr/bin/env python3
"""Test tiers and CI time arithmetic (SEED-670, SEED-664, SEED-666, SEED-672).

Every ctest case is in exactly one tier, kept in engine/tests/test_tiers.json:

  fast    no entry. Under about 1 s in a Release build.
  medium  about 1 s to 10 s in Release. Runs in the Windows PR job, not under the sanitizers on a PR.
  soak    the full variant of a test that has a small PR variant. Runs nightly and on main only.

The file lists the medium and soak names; engine/tests/apply_test_tiers.cmake turns them into the ctest labels
`medium` and `soak` when ctest loads the tests. A case without a label is fast.

  ci_tiers.py baseline RELEASE_LOG ASAN_LOG [--tiers FILE] [--cores N] [--overhead S] [--budget S]
      Per-tier test count, summed Release and sanitizer seconds, and the shard count a 15 minute budget needs.
      The logs are ctest console output (a CI job log works) or a LastTest.log or JUnit file.
  ci_tiers.py write-tiers RELEASE_LOG [--medium-over S] [--tiers FILE]
      Rewrites the medium list from a Release run (every non-soak case slower than S, default 1.0), keeping the soak list.
  ci_tiers.py check [--root DIR] [--tiers FILE]
      The file is well formed, sorted, has no case in two tiers and every name is in a test source.
"""
import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_times  # noqa: E402

TIERS = "engine/tests/test_tiers.json"
TIER_NAMES = ("medium", "soak")


def read_tiers(path):
    try:
        data = json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as e:
        raise test_times.InputError(f"{path}: {e}")
    if not isinstance(data, dict) or set(data) != set(TIER_NAMES):
        raise test_times.InputError(f"{path}: top level must be an object with exactly the keys {list(TIER_NAMES)}")
    return data


def write_tiers(path, data):
    Path(path).write_text(json.dumps({k: sorted(data[k]) for k in TIER_NAMES}, indent=2, ensure_ascii=False) + "\n",
                          encoding="utf-8", newline="\n")


def tier_of(name, tiers):
    for t in TIER_NAMES:
        if name in tiers[t]:
            return t
    return "fast"


def tiers_problems(root, path):
    data = read_tiers(path)
    problems = []
    seen = {}
    for t in TIER_NAMES:
        names = data[t]
        if not isinstance(names, list) or not all(isinstance(n, str) and n for n in names):
            problems.append(f"{t}: must be a list of test names")
            continue
        if names != sorted(names):
            problems.append(f"{t}: not sorted by name")
        for n in names:
            if ";" in n:
                problems.append(f"{n!r}: a ';' in a test name splits the label command, rename the test")
            if n in seen:
                problems.append(f"{n!r}: in both {seen[n]} and {t}")
            seen[n] = t
    if not problems:
        text = test_times.source_text(root)
        for n in sorted(seen):
            if n not in text:
                problems.append(f"{n!r} is not in any test source: renamed or deleted, update {TIERS}")
    return problems


def summarise(times, tiers):
    rows = {t: [0, 0.0] for t in ("fast",) + TIER_NAMES}
    for name, sec in times.items():
        r = rows[tier_of(name, tiers)]
        r[0] += 1
        r[1] += sec
    return rows


def cmd_baseline(args, out):
    tiers = read_tiers(args.tiers)
    rel = test_times.load_times([args.release])
    asan = test_times.load_times([args.asan])
    rrows, arows = summarise(rel, tiers), summarise(asan, tiers)
    print(f"{'tier':8} {'tests':>6} {'Release s':>10} {'sanitizer s':>12}", file=out)
    for t in ("fast",) + TIER_NAMES:
        print(f"{t:8} {rrows[t][0]:6d} {rrows[t][1]:10.0f} {arows[t][1]:12.0f}", file=out)
    total = (sum(r[0] for r in rrows.values()), sum(r[1] for r in rrows.values()), sum(r[1] for r in arows.values()))
    print(f"{'all':8} {total[0]:6d} {total[1]:10.0f} {total[2]:12.0f}", file=out)
    fast_asan = arows["fast"][1]
    per_job = fast_asan / args.cores
    print(f"\nPR sanitizer job (fast tier): {fast_asan:.0f} s summed, {per_job:.0f} s wall on {args.cores} cores, "
          f"plus {args.overhead:.0f} s of checkout, configure and build per job", file=out)
    for shards in range(1, 9):
        wall = args.overhead + per_job / shards
        print(f"  {shards} shard(s): about {wall / 60:4.1f} min", file=out)
        if wall <= args.budget:
            print(f"fewest shards inside {args.budget / 60:.0f} min: {shards}", file=out)
            break
    else:
        print("no shard count up to 8 fits the budget", file=out)
    print(f"Windows PR job test step (fast + medium): {(rrows['fast'][1] + rrows['medium'][1]) / args.cores:.0f} s wall on {args.cores} cores", file=out)
    return 0


def cmd_write_tiers(args, out):
    rel = test_times.load_times([args.release])
    try:
        data = read_tiers(args.tiers)
    except test_times.InputError:
        data = {"medium": [], "soak": []}
    soak = set(data["soak"])
    medium = sorted(n for n, s in rel.items() if s > args.medium_over and n not in soak)
    write_tiers(args.tiers, {"medium": medium, "soak": sorted(soak)})
    print(f"ci_tiers: {len(medium)} medium, {len(soak)} soak written to {args.tiers}", file=out)
    return 0


def main(argv, out=sys.stdout):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", default=str(Path(__file__).resolve().parent.parent))
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("baseline")
    b.add_argument("release")
    b.add_argument("asan")
    b.add_argument("--cores", type=int, default=4)
    b.add_argument("--overhead", type=float, default=420.0)
    b.add_argument("--budget", type=float, default=900.0)
    w = sub.add_parser("write-tiers")
    w.add_argument("release")
    w.add_argument("--medium-over", type=float, default=1.0)
    sub.add_parser("check")
    for p in (b, w, sub.choices["check"]):
        p.add_argument("--tiers")
    args = ap.parse_args(argv)
    args.tiers = args.tiers or str(Path(args.root) / TIERS)
    try:
        if args.cmd == "baseline":
            return cmd_baseline(args, out)
        if args.cmd == "write-tiers":
            return cmd_write_tiers(args, out)
        problems = tiers_problems(args.root, args.tiers)
        for p in problems:
            print(f"ci_tiers: {p}", file=out)
        if not problems:
            print("ci_tiers: tiers file is consistent", file=out)
        return 1 if problems else 0
    except test_times.InputError as e:
        print(f"ci_tiers: {e}", file=out)
        return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
