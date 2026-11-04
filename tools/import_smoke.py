#!/usr/bin/env python3
"""Cook and play every GLB under a pack directory with ichor_import_smoke and print a pass or fail table.

    python tools/import_smoke.py C:/Users/robbi/Documents/concept-art-out/pack [--exe PATH] [--filter TEXT]
        [--include-work] [--jobs N] [--timeout S] [--report FILE]

The pack is only read. Folders named `work` or starting with `_` (renders, previews, scratch exports) are skipped
unless --include-work is given. Each GLB runs in its own process, so a crash or a hang fails that one asset only.
Exit status is 1 when any asset fails. Build the checker first: bin/build ichor_import_smoke
"""
import argparse
import concurrent.futures
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
DEFAULT_EXE = ROOT / "engine" / "build" / "windows-ci" / "tools" / "ichor_import_smoke.exe"


def discover(pack, include_work, text):
    found = []
    for path in sorted(pack.rglob("*.glb")):
        parts = path.relative_to(pack).parts[:-1]
        if not include_work and any(p == "work" or p.startswith("_") for p in parts):
            continue
        if text and text.lower() not in path.relative_to(pack).as_posix().lower():
            continue
        found.append(path)
    return found


def run_one(exe, path, timeout):
    try:
        done = subprocess.run([str(exe), str(path)], capture_output=True, text=True, timeout=timeout, errors="replace")
    except subprocess.TimeoutExpired:
        return {"kind": "?", "status": "FAIL", "yaw": "-", "height": "-", "walk": "-", "clips": "-", "why": f"timed out after {timeout}s"}
    for line in done.stdout.splitlines():
        if line.startswith("RESULT\t"):
            f = line.split("\t")
            why = f[9] if len(f) > 9 else ""
            if "cooker rejected" in why:
                errors = [l[len("[error] "):] for l in done.stderr.splitlines() if l.startswith("[error]")]
                why = f"{why}: {errors[-1]}" if errors else why
            return {"status": f[2], "kind": f[3], "yaw": f[4], "height": f[5], "walk": f[6], "clips": f[8], "why": why}
    errors = [l for l in done.stderr.splitlines() if l.startswith("[error]")]
    why = errors[-1][len("[error] "):] if errors else f"no result, exit code {done.returncode}"
    return {"kind": "?", "status": "FAIL", "yaw": "-", "height": "-", "walk": "-", "clips": "-", "why": f"cannot import: {why}"}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("pack", type=pathlib.Path)
    ap.add_argument("--exe", type=pathlib.Path, default=DEFAULT_EXE)
    ap.add_argument("--filter", default="", help="only assets whose pack-relative path contains this text")
    ap.add_argument("--include-work", action="store_true")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--timeout", type=int, default=300)
    ap.add_argument("--report", type=pathlib.Path, help="also write the table to this file")
    args = ap.parse_args()

    if not args.exe.exists():
        sys.exit(f"{args.exe} not found; build it with: bin/build ichor_import_smoke")
    files = discover(args.pack, args.include_work, args.filter)
    if not files:
        sys.exit(f"no GLB under {args.pack}")

    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        results = list(pool.map(lambda p: run_one(args.exe, p, args.timeout), files))

    rows = [("asset", "result", "kind", "yaw used/needed", "height m", "walk dz m", "clips", "problems")]
    for path, r in zip(files, results):
        rows.append((path.relative_to(args.pack).as_posix(), r["status"], r["kind"], r["yaw"], r["height"], r["walk"], r["clips"], r["why"]))
    widths = [max(len(row[i]) for row in rows) for i in range(7)]
    lines = ["  ".join(c.ljust(widths[i]) for i, c in enumerate(row[:7])) + "  " + row[7] for row in rows]

    failed = [(p, r) for p, r in zip(files, results) if r["status"] != "PASS"]
    lines.append("")
    lines.append(f"{len(files) - len(failed)} of {len(files)} assets pass, {len(failed)} fail")
    if failed:
        lines.append("")
        lines.append("failing assets:")
        for p, r in failed:
            lines.append(f"  {p.relative_to(args.pack).as_posix()}: {r['why']}")
    text = "\n".join(lines)
    print(text)
    if args.report:
        args.report.write_text(text + "\n", encoding="utf-8")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
