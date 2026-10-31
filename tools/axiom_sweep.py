#!/usr/bin/env python3
"""Axiom pair sweep (SEED-714): forge every pair of axioms that may share a world and name the gates each pair fails.

docs/axiom-coverage.md lists every pair of the axioms under data/axioms, but the forge only ever ran on the default
config. This runs ichor_forge_cli once per pair with `--axioms a,b --validate`, so a new or edited axiom that the forge
cannot pin, or that pushes a fixed set of seeds over a design check, is red the next night instead of in a player's hands.

Pairs: every unordered pair of the axioms under <data>/axioms, sorted by id. A pair is excluded (listed, never forged)
when both share a slot, either lists the other in `conflicts`, or <data>/conflicts/conflicts.json pairs them: the rule
of doc 01 rule 3, the same one the coverage matrix and pinWorldConfig apply. Every other pair is forged:

    ichor_forge_cli --seed B --seed-end E --size S --data DATA --axioms a,b --validate --out DIR/a+b

up to --jobs at a time. --shard k/n forges only the k-th of n equal slices of the included pairs (1-based), for a
matrix of runners; every shard still lists the whole exclusion set.

Verdict per pair: pass when every seed is accepted (after forge-cli's own rerolls). Fail when forge-cli refuses the
pin (exit 2), when a seed errors, when a seed is rejected (its last attempt's failed gates are listed as `gate: cause`),
or when the manifest does not record the pair that was asked for.

Usage: axiom_sweep.py <path-to-ichor_forge_cli> [--data DIR] [--size 128] [--seeds 1-8] [--jobs N] [--out DIR]
                      [--summary FILE] [--json FILE] [--shard k/n]
       axiom_sweep.py --self-test

The report is Markdown on stdout (and in --summary), for $GITHUB_STEP_SUMMARY: the counts, every failing pair with
its seeds and gates, the excluded pairs under a fold. --json writes every pair's verdict. Progress goes to stderr.
Exit: 0 every forged pair passed, 1 some pair failed, 2 usage or setup error.
"""
import argparse
import io
import json
import os
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


class InputError(Exception):
    pass


def parse_range(text):
    first, _, last = text.partition("-")
    begin = int(first)
    end = int(last) if last else begin
    if begin > end:
        raise ValueError(text)
    return begin, end


def parse_shard(text):
    k, _, n = text.partition("/")
    k, n = int(k), int(n)
    if n < 1 or not 1 <= k <= n:
        raise ValueError(text)
    return k, n


def load_axioms(data):
    folder = Path(data) / "axioms"
    axioms = []
    for path in sorted(folder.glob("*.json")):
        doc = json.loads(path.read_text(encoding="utf-8"))
        for key in ("id", "slot"):
            if not isinstance(doc.get(key), str) or not doc[key]:
                raise InputError(f"{path}: missing '{key}'")
        axioms.append({"id": doc["id"], "slot": doc["slot"], "conflicts": set(doc.get("conflicts", []))})
    if len(axioms) < 2:
        raise InputError(f"{folder}: fewer than two axioms")
    ids = [a["id"] for a in axioms]
    if len(set(ids)) != len(ids):
        raise InputError(f"{folder}: repeated axiom id")
    axioms.sort(key=lambda a: a["id"])
    blocklist = set()
    conflicts = Path(data) / "conflicts" / "conflicts.json"
    if conflicts.is_file():
        for pair in json.loads(conflicts.read_text(encoding="utf-8")).get("pairs", []):
            if not (isinstance(pair, list) and len(pair) == 2):
                raise InputError(f"{conflicts}: a pair is not a list of two ids")
            blocklist.add(frozenset(pair))
    return axioms, blocklist


def exclusion(a, b, blocklist):
    """Why a and b may not share a world, or None when they may."""
    if a["slot"] == b["slot"]:
        return f"same slot ({a['slot']})"
    if b["id"] in a["conflicts"] or a["id"] in b["conflicts"]:
        return "declared conflict"
    if frozenset((a["id"], b["id"])) in blocklist:
        return "blocklist"
    return None


def enumerate_pairs(axioms, blocklist):
    """Every unordered pair in id order: (a_id, b_id, exclusion reason or None)."""
    pairs = []
    for i, a in enumerate(axioms):
        for b in axioms[i + 1:]:
            pairs.append((a["id"], b["id"], exclusion(a, b, blocklist)))
    return pairs


def shard_of(items, shard):
    if shard is None:
        return items
    k, n = shard
    return [item for i, item in enumerate(items) if i % n == k - 1]


def pair_dir(a, b):
    return f"{a}+{b}"


def forge_command(cli, a, b, seeds, size, data, out):
    begin, end = seeds
    return [str(cli), "--seed", str(begin), "--seed-end", str(end), "--size", str(size), "--data", str(data),
            "--axioms", f"{a},{b}", "--validate", "--out", str(out)]


def verdict(a, b, seeds, returncode, stderr, manifest_text):
    """One pair's result as {pair, a, b, ok, problems: [str], seeds: {seed: {ok, causes|error}}}."""
    result = {"pair": pair_dir(a, b), "a": a, "b": b, "ok": True, "problems": [], "seeds": {}}

    def problem(text):
        result["ok"] = False
        result["problems"].append(text)

    tail = stderr.strip().splitlines()[-1] if stderr.strip() else f"exit {returncode}"
    if manifest_text is None:
        problem(("axioms refused: " if returncode == 2 else "forge-cli wrote no manifest: ") + tail)
        return result
    try:
        manifest = json.loads(manifest_text)
    except json.JSONDecodeError as e:
        problem(f"manifest.json unreadable: {e}")
        return result
    recorded = manifest.get("axioms", {}).get("ids", "")
    if set(recorded.split(",")) != {a, b}:
        problem(f"manifest records axioms '{recorded}', not {a},{b}")
    begin, end = seeds
    expected = list(range(begin, end + 1))
    runs = {r.get("seed"): r for r in manifest.get("runs", [])}
    for seed in expected:
        run = runs.get(seed)
        if run is None:
            problem(f"seed {seed}: not in the manifest")
            result["seeds"][str(seed)] = {"ok": False, "error": "not run"}
            continue
        if "error" in run:
            problem(f"seed {seed}: forge error: {run['error']}")
            result["seeds"][str(seed)] = {"ok": False, "error": run["error"]}
        elif run.get("validation", {}).get("rejected"):
            causes = list(run["validation"].get("causes", []))
            problem(f"seed {seed}: rejected: " + "; ".join(causes))
            result["seeds"][str(seed)] = {"ok": False, "causes": causes}
        elif "validation" not in run:
            problem(f"seed {seed}: not validated")
            result["seeds"][str(seed)] = {"ok": False, "error": "not validated"}
        else:
            result["seeds"][str(seed)] = {"ok": True, "land_share_permille": run["validation"].get("land_share_permille")}
    if result["ok"] and returncode != 0:
        problem(f"forge-cli exited {returncode} with every seed accepted: {tail}")
    return result


def run_pair(cli, a, b, seeds, size, data, out_root):
    out = Path(out_root) / pair_dir(a, b)
    out.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    run = subprocess.run(forge_command(cli, a, b, seeds, size, data, out), capture_output=True, text=True)
    manifest = out / "manifest.json"
    text = manifest.read_text(encoding="utf-8") if manifest.is_file() else None
    result = verdict(a, b, seeds, run.returncode, run.stderr, text)
    result["seconds"] = round(time.monotonic() - started, 1)
    return result


def render_report(results, excluded, seeds, size, elapsed_s, jobs, shard=None, total_pairs=None):
    begin, end = seeds
    seed_text = f"seed {begin}" if begin == end else f"seeds {begin}-{end}"
    failed = [r for r in results if not r["ok"]]
    out = io.StringIO()
    scope = f" (shard {shard[0]} of {shard[1]})" if shard else ""
    print(f"## Axiom pair sweep: size {size}, {seed_text}{scope}", file=out)
    print(file=out)
    print("| | Pairs |", file=out)
    print("|---|---|", file=out)
    print(f"| Forged | {len(results)} |", file=out)
    print(f"| Passed | {len(results) - len(failed)} |", file=out)
    print(f"| Failed | {len(failed)} |", file=out)
    print(f"| Excluded (same slot or declared conflict) | {len(excluded)} |", file=out)
    print(f"| Total | {total_pairs if total_pairs is not None else len(results) + len(excluded)} |", file=out)
    print(file=out)
    forge_s = sum(r.get("seconds", 0) for r in results)
    print(f"Wall time {elapsed_s:.0f} s with {jobs} forge-cli runs at a time ({forge_s:.0f} s of forge-cli time in all).", file=out)
    print(file=out)
    if failed:
        print(f"### Failing pairs ({len(failed)})", file=out)
        print(file=out)
        for r in failed:
            print(f"- `{r['a']}` + `{r['b']}`", file=out)
            for p in r["problems"]:
                print(f"  - {p}", file=out)
        print(file=out)
    else:
        print("Every forged pair passed every gate.", file=out)
        print(file=out)
    print("<details><summary>Forged pairs (seconds, land share permille per seed)</summary>", file=out)
    print(file=out)
    for r in results:
        shares = ", ".join(
            f"{seed}: {'FAIL' if not s['ok'] else s.get('land_share_permille')}" for seed, s in sorted(r["seeds"].items(), key=lambda kv: int(kv[0])))
        mark = "ok" if r["ok"] else "FAIL"
        print(f"- {mark} `{r['a']}` + `{r['b']}` ({r.get('seconds', 0)} s) {shares}", file=out)
    print(file=out)
    print("</details>", file=out)
    print(file=out)
    print(f"<details><summary>Excluded pairs ({len(excluded)})</summary>", file=out)
    print(file=out)
    for a, b, why in excluded:
        print(f"- `{a}` + `{b}`: {why}", file=out)
    print(file=out)
    print("</details>", file=out)
    return out.getvalue()


def sweep(args, out=sys.stdout, err=sys.stderr):
    cli = Path(args.cli)
    if not cli.is_file():
        print(f"axiom_sweep: no such forge-cli: {cli}", file=err)
        return 2
    try:
        seeds = parse_range(args.seeds)
        shard = parse_shard(args.shard) if args.shard else None
    except ValueError:
        print(f"axiom_sweep: bad --seeds '{args.seeds}' or --shard '{args.shard}'", file=err)
        return 2
    try:
        axioms, blocklist = load_axioms(args.data)
    except (InputError, OSError, json.JSONDecodeError) as e:
        print(f"axiom_sweep: {e}", file=err)
        return 2
    pairs = enumerate_pairs(axioms, blocklist)
    excluded = [p for p in pairs if p[2]]
    included = shard_of([p for p in pairs if not p[2]], shard)
    out_root = Path(args.out) if args.out else Path(tempfile.mkdtemp(prefix="axiom-sweep-"))
    out_root.mkdir(parents=True, exist_ok=True)
    jobs = max(1, args.jobs or os.cpu_count() or 1)
    print(f"axiom_sweep: {len(axioms)} axioms, {len(pairs)} pairs, {len(excluded)} excluded, forging {len(included)} "
          f"at size {args.size}, seeds {args.seeds}, {jobs} at a time, into {out_root}", file=err, flush=True)
    started = time.monotonic()
    results = []
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        futures = [pool.submit(run_pair, cli.resolve(), a, b, seeds, args.size, Path(args.data).resolve(), out_root) for a, b, _ in included]
        for i, future in enumerate(futures, 1):
            r = future.result()
            results.append(r)
            print(f"[{i}/{len(included)}] {'ok  ' if r['ok'] else 'FAIL'} {r['pair']} ({r['seconds']} s)", file=err, flush=True)
    elapsed = time.monotonic() - started
    report = render_report(results, excluded, seeds, args.size, elapsed, jobs, shard, total_pairs=len(pairs))
    out.write(report)
    if args.summary:
        Path(args.summary).write_text(report, encoding="utf-8")
    if args.json:
        Path(args.json).write_text(json.dumps({
            "size": args.size, "seeds": args.seeds, "shard": args.shard, "wall_seconds": round(elapsed, 1),
            "excluded": [{"a": a, "b": b, "why": why} for a, b, why in excluded], "results": results}, indent=1), encoding="utf-8")
    failed = sum(1 for r in results if not r["ok"])
    print(f"axiom_sweep: {len(results)} pairs forged in {elapsed:.0f} s, {failed} failed", file=err)
    return 1 if failed else 0


def self_test(out):
    checks = 0

    def check(cond, what):
        nonlocal checks
        checks += 1
        if not cond:
            print(f"self-test: FAILED: {what}", file=out)
            return False
        return True

    ok = True
    with tempfile.TemporaryDirectory() as tmp:
        data = Path(tmp) / "data"
        (data / "axioms").mkdir(parents=True)
        (data / "conflicts").mkdir()
        axioms = [("c.one", "c", []), ("c.two", "c", []), ("l.oath", "l", ["p.x"]), ("p.x", "p", []), ("p.y", "p", []), ("a.z", "a", [])]
        for ident, slot, conflicts in axioms:
            (data / "axioms" / f"{ident}.json").write_text(json.dumps({"id": ident, "slot": slot, "conflicts": conflicts}), encoding="utf-8")
        (data / "conflicts" / "conflicts.json").write_text(json.dumps({"pairs": [["a.z", "c.one"]]}), encoding="utf-8")
        loaded, blocklist = load_axioms(data)
        pairs = enumerate_pairs(loaded, blocklist)
        ok &= check(len(pairs) == 15, f"6 axioms give 15 pairs, got {len(pairs)}")
        ok &= check([p[0] for p in pairs[:5]] == ["a.z"] * 5, "pairs are sorted by id")
        reasons = {(a, b): why for a, b, why in pairs}
        ok &= check(reasons[("c.one", "c.two")] == "same slot (c)", "same slot is excluded")
        ok &= check(reasons[("p.x", "p.y")] == "same slot (p)", "same slot is excluded")
        ok &= check(reasons[("l.oath", "p.x")] == "declared conflict", "a conflicts entry excludes, whichever side lists it")
        ok &= check(reasons[("a.z", "c.one")] == "blocklist", "conflicts.json excludes")
        ok &= check(reasons[("a.z", "p.y")] is None, "an unrelated pair is forged")
        excluded = [p for p in pairs if p[2]]
        ok &= check(len(excluded) == 4, f"4 exclusions, got {len(excluded)}")
        included = [p for p in pairs if not p[2]]
        shards = [shard_of(included, (k, 3)) for k in (1, 2, 3)]
        ok &= check(sorted(sum(shards, [])) == sorted(included) and all(shards), "3 shards cover the included pairs once")

        good = json.dumps({"axioms": {"ids": "c.one,l.oath"}, "runs": [
            {"seed": 1, "validation": {"rejected": False, "land_share_permille": 400, "causes": []}},
            {"seed": 2, "validation": {"rejected": False, "land_share_permille": 410, "causes": []}}]})
        v = verdict("c.one", "l.oath", (1, 2), 0, "", good)
        ok &= check(v["ok"] and v["seeds"]["2"]["land_share_permille"] == 410, "two accepted seeds pass")
        rejected = json.dumps({"axioms": {"ids": "c.one,l.oath"}, "runs": [
            {"seed": 1, "validation": {"rejected": True, "land_share_permille": 90, "causes": ["land_share: 90 permille below 300"]}},
            {"seed": 2, "error": "roads: no settlement sites to connect"}]})
        v = verdict("c.one", "l.oath", (1, 2), 1, "seed 1 rejected by validation\n", rejected)
        ok &= check(not v["ok"] and len(v["problems"]) == 2, "a rejected seed and an errored seed are two problems")
        ok &= check("land_share: 90 permille below 300" in v["problems"][0], "the gate cause is named")
        ok &= check(v["seeds"]["2"]["error"].startswith("roads"), "the forge error is kept per seed")
        v = verdict("c.one", "l.oath", (1, 2), 2, "forge_cli: --axioms: unknown axiom id 'l.oath'\n", None)
        ok &= check(not v["ok"] and v["problems"] == ["axioms refused: forge_cli: --axioms: unknown axiom id 'l.oath'"], "a refused pin names the reason")
        v = verdict("c.one", "l.oath", (1, 2), 0, "", json.dumps({"axioms": {"ids": "c.one"}, "runs": [{"seed": 1, "validation": {"rejected": False, "causes": []}}]}))
        ok &= check(not v["ok"] and any("records axioms" in p for p in v["problems"]) and any("seed 2: not in the manifest" in p for p in v["problems"]),
                    "a manifest without the pair or a seed fails")
        v = verdict("c.one", "l.oath", (1, 1), 0, "", json.dumps({"axioms": {"ids": "c.one,l.oath"}, "runs": [{"seed": 1, "layers": []}]}))
        ok &= check(not v["ok"] and v["problems"] == ["seed 1: not validated"], "a run without validation fails")
        v = verdict("c.one", "l.oath", (1, 1), 1, "boom\n", json.dumps({"axioms": {"ids": "c.one,l.oath"}, "runs": [{"seed": 1, "validation": {"rejected": False, "causes": []}}]}))
        ok &= check(not v["ok"] and v["problems"][0].startswith("forge-cli exited 1 with every seed accepted"), "a nonzero exit with every seed accepted fails")

        results = [verdict("c.one", "l.oath", (1, 2), 0, "", good), verdict("c.one", "p.x", (1, 2), 1, "", rejected)]
        for r in results:
            r["seconds"] = 1.5
        report = render_report(results, excluded, (1, 2), 128, 12.0, 4, total_pairs=15)
        ok &= check("| Forged | 2 |" in report and "| Failed | 1 |" in report and "| Excluded (same slot or declared conflict) | 4 |" in report
                    and "| Total | 15 |" in report, "the counts table")
        ok &= check("### Failing pairs (1)" in report and "- `c.one` + `p.x`" in report and "land_share: 90 permille below 300" in report, "failing pairs list their gates")
        ok &= check("- `c.one` + `c.two`: same slot (c)" in report, "excluded pairs are listed with the reason")
        clean = render_report(results[:1], excluded, (3, 3), 64, 3.0, 1, shard=(2, 3), total_pairs=15)
        ok &= check("Every forged pair passed every gate." in clean and "seed 3 (shard 2 of 3)" in clean, "a clean shard says so")
    if not ok:
        return 1
    print(f"self-test: all {checks} checks passed", file=out)
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("cli", nargs="?")
    ap.add_argument("--data", default=str(ROOT / "data"))
    ap.add_argument("--size", type=int, default=128)
    ap.add_argument("--seeds", default="1-8")
    ap.add_argument("--jobs", type=int, default=0, help="forge-cli runs at a time (default: the CPU count)")
    ap.add_argument("--out", help="output root, one folder per pair (default: a temp dir)")
    ap.add_argument("--summary", help="also write the Markdown report here")
    ap.add_argument("--json", help="write every pair's verdict here")
    ap.add_argument("--shard", help="k/n: forge only the k-th of n slices of the included pairs")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args(argv)
    if args.self_test:
        return self_test(sys.stdout)
    if not args.cli:
        print("axiom_sweep: need the path to ichor_forge_cli (or --self-test)", file=sys.stderr)
        return 2
    return sweep(args)


if __name__ == "__main__":
    sys.exit(main())
