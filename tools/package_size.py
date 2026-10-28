#!/usr/bin/env python3
"""Package size budget guard (SEED-708): record the size of the tester package and fail when it outgrows the budget.

The Linux tarball that tools/package_linux.sh builds is what outside testers download. This prints its size and fails
when it is more than MARGIN over the budget in tools/package_size_budget.json, so growth (a new art batch, a texture
cook that stops compressing) is raised on purpose instead of drifting in.

Usage: package_size.py ARCHIVE [--budget FILE]
       package_size.py --self-test

Raise the budget after an intended growth: set "bytes" in tools/package_size_budget.json to the size the
`linux package + smoke` job printed and say why in "note". That job runs on main, not on a PR; to measure a branch
first, run `gh workflow run engine.yml --ref BRANCH` and read its package size line.

Exit: 0 within budget, 1 over budget, 2 usage or input error.
"""
import argparse
import json
import os
import sys
import tempfile
from pathlib import Path

BUDGET = "tools/package_size_budget.json"
# Whole percents so the comparison is exact integer arithmetic on byte counts.
MARGIN_PERCENT = 5
SHRINK_PERCENT = 10


class InputError(Exception):
    pass


def read_budget(path):
    try:
        data = json.loads(Path(path).read_text(encoding="utf-8"))
    except FileNotFoundError:
        raise InputError(f"no budget file at {path}; create it with {{\"bytes\": N, \"note\": \"...\"}}")
    except (OSError, ValueError) as e:
        raise InputError(f"cannot read the budget file {path}: {e}")
    size = data.get("bytes") if isinstance(data, dict) else None
    if type(size) is not int or size <= 0:
        raise InputError(f"{path} needs \"bytes\": a positive whole number")
    return size


def evaluate(size, budget):
    if size * 100 > budget * (100 + MARGIN_PERCENT):
        return "over"
    if size * 100 < budget * (100 - SHRINK_PERCENT):
        return "shrink"
    return "ok"


def mb(n):
    return f"{n / 1e6:.1f} MB ({n:,} bytes)"


def report(name, size, budget, status, out):
    limit = budget * (100 + MARGIN_PERCENT) // 100
    change = (size - budget) * 100 / budget
    print(f"package-size: {name} is {mb(size)}, budget {mb(budget)} ({change:+.1f}%), fails above {mb(limit)}", file=out)
    if status == "over":
        print(f"package-size: FAILED, the package grew more than {MARGIN_PERCENT}% over its budget.", file=out)
        print(f"package-size: if the growth is intended, set \"bytes\": {size} in {BUDGET} and say why in \"note\",", file=out)
        print("package-size: in its own PR so the raise is reviewed. If it is not intended, find what grew (unpack the", file=out)
        print("package-size: ichor-linux-package artifact and compare with `du -sh */` of the previous one).", file=out)
    elif status == "shrink":
        print(f"package-size: note: the package is more than {SHRINK_PERCENT}% under its budget; lower it to keep the guard tight:", file=out)
        print(f"package-size: set \"bytes\": {size} in {BUDGET}", file=out)


def record(name, size, budget, status):
    path = os.environ.get("GITHUB_STEP_SUMMARY")
    if not path:
        return
    with open(path, "a", encoding="utf-8") as f:
        f.write(f"Package size: `{name}` is {mb(size)}, budget {mb(budget)}, {status}\n")


def run(argv, out=sys.stdout):
    ap = argparse.ArgumentParser(description="package size budget guard")
    ap.add_argument("archive")
    ap.add_argument("--budget", default=str(Path(__file__).resolve().parent.parent / BUDGET))
    try:
        args = ap.parse_args(argv)
    except SystemExit as e:
        return 0 if e.code == 0 else 2
    try:
        budget = read_budget(args.budget)
        archive = Path(args.archive)
        if not archive.is_file():
            raise InputError(f"no package at {archive}")
        size = archive.stat().st_size
    except InputError as e:
        print(f"package-size: {e}", file=out)
        return 2
    status = evaluate(size, budget)
    report(archive.name, size, budget, status, out)
    record(archive.name, size, budget, status)
    return 1 if status == "over" else 0


class _Sink:
    def write(self, _):
        pass


def self_test(out):
    failures = []

    def expect(label, got, want):
        if got != want:
            failures.append(f"{label}: got {got!r}, want {want!r}")

    expect("under budget is ok", evaluate(900, 1000), "ok")
    expect("at the budget is ok", evaluate(1000, 1000), "ok")
    expect("exactly at the margin is ok", evaluate(1050, 1000), "ok")
    expect("one byte over the margin is over", evaluate(1051, 1000), "over")
    expect("far over is over", evaluate(2000, 1000), "over")
    expect("exactly at the shrink line is ok", evaluate(900, 1000), "ok")
    expect("far under suggests a shrink", evaluate(899, 1000), "shrink")

    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        archive = root / "pkg.tar.gz"
        archive.write_bytes(b"x" * 1000)
        budget = root / "budget.json"
        argv = [str(archive), "--budget", str(budget)]
        expect("cli: missing budget file exits 2", run(argv, _Sink()), 2)
        budget.write_text(json.dumps({"bytes": 1000, "note": "n"}), encoding="utf-8")
        expect("cli: at the budget exits 0", run(argv, _Sink()), 0)
        budget.write_text(json.dumps({"bytes": 953, "note": "n"}), encoding="utf-8")
        expect("cli: within the margin exits 0", run(argv, _Sink()), 0)
        budget.write_text(json.dumps({"bytes": 900, "note": "n"}), encoding="utf-8")
        expect("cli: over the margin exits 1", run(argv, _Sink()), 1)
        budget.write_text(json.dumps({"bytes": 2000, "note": "n"}), encoding="utf-8")
        expect("cli: far under exits 0", run(argv, _Sink()), 0)
        for label, text in (("not json", "{"), ("no bytes", '{"note": "n"}'), ("zero bytes", '{"bytes": 0}'),
                            ("text bytes", '{"bytes": "1000"}'), ("bool bytes", '{"bytes": true}'), ("a list", "[1000]")):
            budget.write_text(text, encoding="utf-8")
            expect(f"cli: budget {label} exits 2", run(argv, _Sink()), 2)
        budget.write_text(json.dumps({"bytes": 1000}), encoding="utf-8")
        expect("cli: missing archive exits 2", run([str(root / "gone.tar.gz"), "--budget", str(budget)], _Sink()), 2)

        class Capture:
            text = ""

            def write(self, s):
                Capture.text += s

        budget.write_text(json.dumps({"bytes": 900}), encoding="utf-8")
        run(argv, Capture())
        expect("cli: the over message says how to raise the budget", "package_size_budget.json" in Capture.text, True)
        Capture.text = ""
        budget.write_text(json.dumps({"bytes": 2000}), encoding="utf-8")
        run(argv, Capture())
        expect("cli: the shrink note suggests the measured size", '"bytes": 1000' in Capture.text, True)

        summary = root / "summary.md"
        os.environ["GITHUB_STEP_SUMMARY"] = str(summary)
        try:
            budget.write_text(json.dumps({"bytes": 1000}), encoding="utf-8")
            run(argv, _Sink())
        finally:
            del os.environ["GITHUB_STEP_SUMMARY"]
        expect("cli: the size is recorded in the step summary",
               summary.exists() and "1,000 bytes" in summary.read_text(encoding="utf-8"), True)

    for f in failures:
        print(f"package-size self-test FAILED: {f}", file=out)
    if not failures:
        print("package-size self-test: ok", file=out)
    return 1 if failures else 0


if __name__ == "__main__":
    if sys.argv[1:] == ["--self-test"]:
        sys.exit(self_test(sys.stdout))
    sys.exit(run(sys.argv[1:]))
