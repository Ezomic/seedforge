#!/usr/bin/env python3
"""Dialogue coverage lint (SEED-746): the trait by situation lines of data/dialogue (SEED-168).

A cell is one trait (every id in data/npcs) crossed with one situation (every topic that any line
pairs with a trait=). Reports, in a readable markdown table:
  1. empty cells: a trait with no line for a situation, so an NPC of that trait has nothing to say.
  2. repeated lines per trait: two lines of one trait whose text is the same once case, spacing
     and punctuation are ignored.
  3. thin cells: the cell holds exactly one line, which the cooldown makes repeat soonest.
     Informational only, never gated.

Gate: tools/dialogue_coverage_baseline.txt lists the known empty cells and repeats. The check
fails on a regression: an empty cell or repeat not in the baseline, or a baseline entry that no
longer exists (shrink it, never grow it). The coverage numbers themselves never fail the job.

Usage: dialogue_coverage.py [--root DIR] [--report FILE]   check the tree, optionally write the report
       dialogue_coverage.py --self-test                    prove each rule goes red on a deliberate violation
       dialogue_coverage.py --write-baseline               print the baseline for the current tree
"""
import argparse
import json
import re
import sys
import tempfile
from collections import defaultdict
from pathlib import Path

BASELINE = "tools/dialogue_coverage_baseline.txt"
HEADER = "# Known dialogue coverage debt (SEED-746). Shrink this file, never grow it.\n"


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8"))


def condition_values(line, key):
    prefix = key + "="
    return [c[len(prefix):] for c in line.get("conditions", []) if isinstance(c, str) and c.startswith(prefix)]


def normalise(text):
    return " ".join(re.sub(r"[^a-z0-9 ]", "", text.lower()).split())


def analyse(root):
    traits = sorted(r["id"] for r in read_json(root / "data/npcs/traits.json"))
    lines = [r for r in read_json(root / "data/dialogue/lines.json") if isinstance(r, dict)]
    cells = defaultdict(list)
    by_trait = defaultdict(list)
    for line in lines:
        for trait in condition_values(line, "trait"):
            by_trait[trait].append(line)
            for topic in condition_values(line, "topic"):
                cells[(trait, topic)].append(line["id"])
    topics = sorted({topic for _, topic in cells})
    empty = [(t, s) for t in traits for s in topics if not cells.get((t, s))]
    thin = [(t, s) for t in traits for s in topics if len(cells.get((t, s), [])) == 1]
    repeats = []
    for trait in sorted(by_trait):
        seen = defaultdict(list)
        for line in by_trait[trait]:
            seen[normalise(line.get("text", ""))].append(line["id"])
        repeats += [(trait, sorted(ids)) for ids in seen.values() if len(ids) > 1]
    return {"traits": traits, "topics": topics, "cells": cells, "empty": empty, "thin": thin,
            "repeats": repeats, "total": len(lines), "trait_lines": sum(len(v) for v in by_trait.values())}


def entries(result):
    found = [f"empty-cell {t} {s}" for t, s in result["empty"]]
    found += [f"repeat {t} {','.join(ids)}" for t, ids in result["repeats"]]
    return sorted(found)


def read_baseline(root):
    path = root / BASELINE
    if not path.exists():
        return set()
    return {l.strip() for l in path.read_text(encoding="utf-8").splitlines() if l.strip() and not l.startswith("#")}


def regressions(result, baseline):
    found = set(entries(result))
    problems = [f"new: {e}" for e in sorted(found - baseline)]
    problems += [f"stale baseline entry (fixed, remove it): {e}" for e in sorted(baseline - found)]
    return problems


def report(result, baseline):
    traits, topics, cells = result["traits"], result["topics"], result["cells"]
    total = len(traits) * len(topics)
    filled = total - len(result["empty"])
    out = ["# Dialogue coverage (SEED-746)", ""]
    out.append(f"- Lines: {result['total']} total, {result['trait_lines']} trait-conditioned.")
    out.append(f"- Cells: {filled} of {total} filled ({len(traits)} traits x {len(topics)} situations: {', '.join(topics)}).")
    out.append(f"- Empty cells: {len(result['empty'])}. Thin cells (one line): {len(result['thin'])}. "
               f"Repeated lines: {len(result['repeats'])}. Baselined debt: {len(baseline)}.")
    out += ["", "## Lines per trait and situation", "", "| trait | " + " | ".join(topics) + " |",
            "|---|" + "---|" * len(topics)]
    for trait in traits:
        counts = [str(len(cells.get((trait, s), []))) for s in topics]
        out.append(f"| {trait} | " + " | ".join(c if c != "0" else "**0**" for c in counts) + " |")
    out += ["", "## Empty cells", ""]
    out += [f"- {t} / {s}" for t, s in result["empty"]] or ["None."]
    out += ["", "## Repeated lines per trait", ""]
    out += [f"- {t}: {', '.join(ids)}" for t, ids in result["repeats"]] or ["None."]
    out += ["", "## Thin cells", ""]
    out += [f"- {t} / {s}: {cells[(t, s)][0]}" for t, s in result["thin"]] or ["None."]
    return "\n".join(out) + "\n"


def run(root, report_path):
    result = analyse(root)
    baseline = read_baseline(root)
    text = report(result, baseline)
    if report_path:
        Path(report_path).write_text(text, encoding="utf-8")
    problems = regressions(result, baseline)
    print(text.split("\n## ")[0].strip())
    for problem in problems:
        print(problem)
    if problems:
        print(f"dialogue coverage: {len(problems)} regression(s)")
        return 1
    print("dialogue coverage: ok")
    return 0


def self_test():
    def build(root, lines, traits=("bitter", "brave")):
        (root / "data/npcs").mkdir(parents=True)
        (root / "data/dialogue").mkdir(parents=True)
        (root / "tools").mkdir()
        (root / "data/npcs/traits.json").write_text(json.dumps([{"id": t} for t in traits]))
        (root / "data/dialogue/lines.json").write_text(json.dumps(lines))

    def line(i, trait, topic, text):
        return {"id": i, "text": text, "conditions": [f"trait={trait}", f"topic={topic}"]}

    full = [line("a", "bitter", "work", "One."), line("b", "brave", "work", "Two.")]
    cases = [
        ("clean tree passes", full, "", False),
        ("empty cell fails", full[:1], "", True),
        ("repeat fails", full + [line("c", "brave", "work", "two!")], "", True),
        ("baselined empty cell passes", full[:1], "empty-cell brave work\n", False),
        ("stale baseline fails", full, "empty-cell brave work\n", True),
    ]
    failed = 0
    for name, lines, baseline, want_red in cases:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            build(root, lines)
            (root / BASELINE).write_text(baseline)
            red = bool(regressions(analyse(root), read_baseline(root)))
            ok = red == want_red
            print(("ok   " if ok else "FAIL ") + name)
            failed += not ok
    return 1 if failed else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", default=str(Path(__file__).resolve().parent.parent))
    parser.add_argument("--report", help="write the markdown report to this file")
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--write-baseline", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    root = Path(args.root)
    if args.write_baseline:
        sys.stdout.write(HEADER + "".join(e + "\n" for e in entries(analyse(root))))
        return 0
    return run(root, args.report)


if __name__ == "__main__":
    sys.exit(main())
