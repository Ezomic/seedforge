#!/usr/bin/env python3
"""Asset licence ledger check (SEED-680): every shipped asset has a recorded, allowed licence.

The ledger is art/LICENSES.json: {"entries": [{id, path, source_url, author, license, attribution,
date_added}]}. `path` is a glob relative to the repository root (`*` stays inside a folder, `**` crosses
folders). Fails when, under art/ and the audio folders:
  1. unlisted: a file has no covering ledger entry and is not in the first-party baseline.
  2. bad-license: an entry's licence is not allowlisted (CC0-1.0, MIT, OFL-1.1, CC-BY-3.0, CC-BY-4.0,
     and LicenseRef-Proprietary for first-party assets the owner made and keeps all rights to).
     CC-BY-NC*, GPL* and anything unknown are rejected.
  3. bad-entry: an entry lacks a field, repeats an id, has a malformed date, or a licence that needs
     attribution (everything but CC0-1.0 and LicenseRef-Proprietary) has no attribution text.
  4. orphan-entry: an entry matches no file.
  5. baseline: tools/asset_license_baseline.txt lists a file that is gone or is now covered by the ledger.
     The baseline is the first-party assets that predate the ledger: shrink it, never grow it.
  6. credits: CREDITS.md differs from what the ledger generates.

Usage: check_assets_licensed.py [--root DIR]    check the tree (default: this repository)
       check_assets_licensed.py --write-credits  regenerate CREDITS.md from the ledger
       check_assets_licensed.py --write-baseline print the baseline lines for the files the ledger does not cover
       check_assets_licensed.py --self-test     prove every rule goes red on a deliberate violation
"""
import argparse
import json
import re
import sys
from pathlib import Path

LEDGER = "art/LICENSES.json"
BASELINE = "tools/asset_license_baseline.txt"
CREDITS = "CREDITS.md"
ASSET_ROOTS = ("art", "audio", "data/audio", "engine/content/audio", "engine/assets/vendor")
SKIP_DIRS = {".git", "node_modules", "build", "__pycache__"}

ALLOWED = {
    "CC0-1.0": "CC0 1.0 Universal",
    "MIT": "MIT",
    "OFL-1.1": "SIL Open Font License 1.1",
    "CC-BY-3.0": "CC BY 3.0",
    "CC-BY-4.0": "CC BY 4.0",
    "LicenseRef-Proprietary": "Proprietary, all rights reserved (first-party)",
}
NEEDS_ATTRIBUTION = set(ALLOWED) - {"CC0-1.0", "LicenseRef-Proprietary"}
FIELDS = ("id", "path", "source_url", "author", "license", "attribution", "date_added")
DATE = re.compile(r"\d{4}-\d{2}-\d{2}")


def glob_regex(pattern):
    out, i = [], 0
    while i < len(pattern):
        c = pattern[i]
        if pattern.startswith("**/", i):
            out.append("(?:.*/)?")
            i += 3
        elif pattern.startswith("**", i):
            out.append(".*")
            i += 2
        elif c == "*":
            out.append("[^/]*")
            i += 1
        elif c == "?":
            out.append("[^/]")
            i += 1
        else:
            out.append(re.escape(c))
            i += 1
    return re.compile("".join(out))


def license_problem(value):
    if value in ALLOWED:
        return None
    upper = str(value).upper()
    if "-NC" in upper:
        return f"licence '{value}' is non-commercial: not allowed"
    if upper.startswith(("GPL", "AGPL", "LGPL")):
        return f"licence '{value}' is copyleft: not allowed"
    return f"licence '{value}' is not on the allowlist ({', '.join(sorted(ALLOWED))})"


def parse_ledger(text):
    try:
        data = json.loads(text)
    except json.JSONDecodeError as e:
        return None, [(LEDGER, "bad-entry", f"not valid JSON: {e}")]
    entries = data.get("entries") if isinstance(data, dict) else None
    if not isinstance(entries, list):
        return None, [(LEDGER, "bad-entry", 'top level must be {"entries": [...]}')]
    return entries, []


def well_formed(e):
    return isinstance(e, dict) and all(isinstance(e.get(f), str) for f in FIELDS)


def entry_errors(entries):
    errors, ids = [], set()
    for n, e in enumerate(entries):
        where = e["id"] if isinstance(e, dict) and isinstance(e.get("id"), str) else f"entry #{n}"
        if not isinstance(e, dict):
            errors.append((where, "bad-entry", "entry is not an object"))
            continue
        missing = [f for f in FIELDS if not isinstance(e.get(f), str)]
        for f in missing:
            errors.append((where, "bad-entry", f"missing string field '{f}'"))
        if missing:
            continue
        if e["id"] in ids:
            errors.append((where, "bad-entry", "duplicate id"))
        ids.add(e["id"])
        for f in ("id", "path", "source_url", "author"):
            if not e[f].strip():
                errors.append((where, "bad-entry", f"field '{f}' is empty"))
        if not DATE.fullmatch(e["date_added"]):
            errors.append((where, "bad-entry", "date_added must be YYYY-MM-DD"))
        problem = license_problem(e["license"])
        if problem:
            errors.append((where, "bad-license", problem))
        elif e["license"] in NEEDS_ATTRIBUTION and not e["attribution"].strip():
            errors.append((where, "bad-entry", f"licence {e['license']} needs attribution text"))
    return errors


def check_tree(files, entries, baseline, credits_text=None):
    errors = entry_errors(entries)
    matchers = [(e, glob_regex(e["path"])) for e in entries if well_formed(e) and e["path"].strip()]
    covered = set()
    for e, rx in matchers:
        hits = [f for f in files if rx.fullmatch(f)]
        covered.update(hits)
        if not hits:
            errors.append((e["id"], "orphan-entry", f"path '{e['path']}' matches no file"))
    file_set = set(files)
    for f in sorted(file_set - covered):
        if f not in baseline:
            errors.append((f, "unlisted", f"no entry in {LEDGER} covers this file"))
    for line in sorted(baseline):
        if line not in file_set:
            errors.append((BASELINE, "baseline", f"'{line}' no longer exists: delete the line"))
        elif line in covered:
            errors.append((BASELINE, "baseline", f"'{line}' is now covered by the ledger: delete the line"))
    if credits_text is not None and credits_text != render_credits(entries):
        errors.append((CREDITS, "credits", "out of date: run tools/check_assets_licensed.py --write-credits"))
    return errors


def render_credits(entries):
    lines = [
        "# Credits",
        "",
        f"Generated from `{LEDGER}` by `tools/check_assets_licensed.py --write-credits`. Do not edit by hand.",
        "",
    ]
    rows = sorted((e for e in entries if well_formed(e)), key=lambda e: e["id"])
    if not rows:
        lines.append("No third-party assets are shipped yet.")
    for e in rows:
        lines.append(f"## {e['id']}")
        lines.append("")
        lines.append(f"- Author: {e['author']}")
        lines.append(f"- Licence: {ALLOWED.get(e['license'], e['license'])}")
        lines.append(f"- Source: {e['source_url']}")
        lines.append(f"- Files: `{e['path']}`")
        if e["attribution"].strip():
            lines.append(f"- Attribution: {e['attribution'].strip()}")
        lines.append("")
    return "\n".join(lines).rstrip("\n") + "\n"


def walk(root):
    root = Path(root)
    files = []
    for base in ASSET_ROOTS:
        top = root / base
        if not top.is_dir():
            continue
        for p in top.rglob("*"):
            rel = p.relative_to(root)
            if p.is_file() and SKIP_DIRS.isdisjoint(rel.parts) and rel.as_posix() != LEDGER:
                files.append(rel.as_posix())
    return sorted(set(files))


def read_text(root, rel):
    p = Path(root) / rel
    return p.read_text(encoding="utf-8") if p.is_file() else None


def parse_baseline(text):
    return {line.strip() for line in (text or "").splitlines() if line.strip() and not line.startswith("#")}


def check_root(root):
    text = read_text(root, LEDGER)
    if text is None:
        return [(LEDGER, "bad-entry", "ledger is missing")]
    entries, errors = parse_ledger(text)
    if entries is None:
        return errors
    return check_tree(walk(root), entries, parse_baseline(read_text(root, BASELINE)), read_text(root, CREDITS) or "")


def sample_entry(**kw):
    base = {"id": "pilot_kit", "path": "art/pilot/**", "source_url": "https://example.org/pilot",
            "author": "A. Author", "license": "CC0-1.0", "attribution": "", "date_added": "2026-10-26"}
    base.update(kw)
    return base


def self_test():
    files = ["art/pilot/SM_Wall.glb", "art/pilot/T_Wall.png", "art/own/SM_Home.glb"]
    base_ok = {"art/own/SM_Home.glb"}

    def rules(entries, fl=files, baseline=base_ok):
        return {r for _, r, _ in check_tree(fl, entries, baseline, render_credits(entries))}

    ok = [sample_entry()]
    if rules(ok):
        print(f"self-test needs a clean sample first: {rules(ok)}")
        return 1
    cases = [
        ("an unlisted file", "unlisted", lambda: rules(ok, files + ["art/pilot_two/SM_Rock.glb"])),
        ("an unlisted file in an audio folder", "unlisted", lambda: rules(ok, files + ["audio/A_Hit.ogg"])),
        ("a CC-BY-NC licence", "bad-license", lambda: rules([sample_entry(license="CC-BY-NC-4.0", attribution="x")])),
        ("a GPL licence", "bad-license", lambda: rules([sample_entry(license="GPL-3.0")])),
        ("another LicenseRef value", "bad-license", lambda: rules([sample_entry(license="LicenseRef-Other")])),
        ("an unknown licence", "bad-license", lambda: rules([sample_entry(license="Custom")])),
        ("an orphan entry", "orphan-entry", lambda: rules(ok + [sample_entry(id="gone", path="art/gone/**")])),
        ("CC-BY without attribution text", "bad-entry", lambda: rules([sample_entry(license="CC-BY-4.0", attribution=" ")])),
        ("a missing author field", "bad-entry", lambda: rules([{k: v for k, v in sample_entry().items() if k != "author"}])),
        ("a malformed date", "bad-entry", lambda: rules([sample_entry(date_added="yesterday")])),
        ("a duplicate id", "bad-entry", lambda: rules(ok + [sample_entry(path="art/own/**")])),
        ("a baseline line for a deleted file", "baseline", lambda: rules(ok, baseline=base_ok | {"art/own/Gone.glb"})),
        ("a baseline line the ledger now covers", "baseline", lambda: rules(ok, baseline=base_ok | {"art/pilot/SM_Wall.glb"})),
        ("a stale CREDITS.md", "credits", lambda: {r for _, r, _ in check_tree(files, ok, base_ok, "old\n")}),
    ]
    missed = 0
    for name, rule, run in cases:
        caught = rule in run()
        print(f"  {name}: {'red as expected' if caught else 'STAYED GREEN'}")
        missed += not caught
    good = [
        ("CC-BY with attribution", [sample_entry(license="CC-BY-4.0", attribution="Wall by A. Author, CC BY 4.0")]),
        ("MIT with a notice", [sample_entry(license="MIT", attribution="Copyright A. Author")]),
        ("LicenseRef-Proprietary needs no attribution text", [sample_entry(license="LicenseRef-Proprietary")]),
        ("single-star globs", [sample_entry(path="art/pilot/*.glb"), sample_entry(id="tex", path="art/pilot/*.png")]),
    ]
    for name, entries in good:
        passed = not rules(entries)
        print(f"  {name}: {'ok' if passed else 'FAILED'}")
        missed += not passed
    rx = glob_regex("art/*/x.glb")
    passed = bool(rx.fullmatch("art/a/x.glb")) and not rx.fullmatch("art/a/b/x.glb")
    print(f"  '*' stays inside a folder: {'ok' if passed else 'FAILED'}")
    missed += not passed
    if missed:
        print(f"self-test: {missed} rule(s) did not behave")
        return 1
    print(f"self-test: all {len(cases) + len(good) + 1} checks passed")
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", default=str(Path(__file__).resolve().parent.parent))
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--write-credits", action="store_true")
    parser.add_argument("--write-baseline", action="store_true")
    args = parser.parse_args(argv)
    if args.self_test:
        return self_test()
    if args.write_credits or args.write_baseline:
        entries, errors = parse_ledger(read_text(args.root, LEDGER) or "")
        if entries is None:
            print(errors[0][2])
            return 1
        if args.write_credits:
            (Path(args.root) / CREDITS).write_text(render_credits(entries), encoding="utf-8", newline="\n")
            return 0
        rx = [glob_regex(e["path"]) for e in entries if well_formed(e)]
        for f in walk(args.root):
            if not any(r.fullmatch(f) for r in rx):
                print(f)
        return 0
    errors = check_root(args.root)
    if errors:
        print("Asset licence violations (SEED-680):")
        for where, rule, msg in errors:
            print(f"  - {where}: {msg} [{rule}]")
        return 1
    print("asset licences: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
