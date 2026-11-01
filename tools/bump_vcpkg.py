#!/usr/bin/env python3
"""Monthly vcpkg baseline bump (SEED-764).

The vcpkg commit is pinned in every workflow that checks vcpkg out (VCPKG_COMMIT in engine.yml,
`ref:` of the vcpkg checkout in nightly.yml) and as `builtin-baseline` in engine/vcpkg.json. They
must move together or the manifest resolves different ports than the CI checkout provides.

Usage: bump_vcpkg.py --root DIR [--commit SHA]   rewrite every pin to SHA (default: latest vcpkg master)
       bump_vcpkg.py --root DIR --check          fail unless every pin is the same commit
       bump_vcpkg.py --self-test                 run the bump against fixture copies of the files

Prints `old new` when it changed something and `unchanged <sha>` when already current.
"""
import argparse
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

SHA = re.compile(r"[0-9a-f]{40}")
MANIFEST = "engine/vcpkg.json"
BASELINE = re.compile(r'("builtin-baseline"\s*:\s*")([0-9a-f]{40})(")')
ENV_PIN = re.compile(r"(^\s*VCPKG_COMMIT:\s*)([0-9a-f]{40})\b", re.M)
CHECKOUT_PIN = re.compile(r"(repository:\s*microsoft/vcpkg\s*\n\s*ref:\s*)([0-9a-f]{40})\b")
CHECKOUT_REF = re.compile(r"repository:\s*microsoft/vcpkg\s*\n\s*ref:[ \t]*(.+?)[ \t]*$", re.M)
ENV_REF = "${{ env.VCPKG_COMMIT }}"
FIXTURES = Path(__file__).resolve().parent / "bump_vcpkg_fixtures" / "repo"
TARGET_FILES = (".github/workflows/engine.yml", ".github/workflows/nightly.yml", MANIFEST)


def workflows(root: Path) -> list[Path]:
    return sorted((root / ".github" / "workflows").glob("*.yml"))


def mentions_vcpkg(path: Path) -> bool:
    text = path.read_text(encoding="utf-8")
    return "microsoft/vcpkg" in text or "VCPKG_COMMIT" in text


def patterns_for(path: Path) -> list[re.Pattern[str]]:
    return [BASELINE] if path.suffix == ".json" else [ENV_PIN, CHECKOUT_PIN]


def pins(path: Path) -> list[str]:
    text = path.read_text(encoding="utf-8")
    return [m.group(2) for pattern in patterns_for(path) for m in pattern.finditer(text)]


def vcpkg_files(root: Path) -> list[Path]:
    return [root / MANIFEST] + [w for w in workflows(root) if mentions_vcpkg(w)]


def verify(root: Path, expected: str | None = None) -> list[str]:
    """Return the problems: a file with no pin, an unpinned checkout, a stale pin, or pins that disagree."""
    problems = []
    distinct = set()
    for path in vcpkg_files(root):
        name = path.relative_to(root).as_posix()
        found = pins(path)
        distinct.update(found)
        if not found:
            problems.append(f"{name}: mentions vcpkg but has no recognised pin")
        if path.suffix != ".json":
            text = path.read_text(encoding="utf-8")
            has_env = bool(ENV_PIN.search(text))
            for ref in CHECKOUT_REF.findall(text):
                if not (SHA.fullmatch(ref) or (has_env and ref == ENV_REF)):
                    problems.append(f"{name}: vcpkg checkout is not pinned to a commit")
        stale = [s for s in found if expected is not None and s != expected]
        if stale:
            problems.append(f"{name}: still pins {stale[0]}")
    if expected is None and len(distinct) > 1:
        problems.append("pins disagree: " + ", ".join(sorted(distinct)))
    return problems


def current(root: Path) -> str:
    found = pins(root / MANIFEST)
    if len(found) != 1:
        raise SystemExit(f"{MANIFEST}: expected exactly one builtin-baseline, found {len(found)}")
    return found[0]


def rewrite(path: Path, new: str) -> bool:
    text = path.read_text(encoding="utf-8")
    out = text
    for pattern in patterns_for(path):
        out = pattern.sub(lambda m: m.group(1) + new + (m.group(3) if m.lastindex == 3 else ""), out)
    if out == text:
        return False
    path.write_bytes(out.encode("utf-8"))
    return True


def bump(root: Path, new: str) -> list[str]:
    if not SHA.fullmatch(new):
        raise SystemExit(f"not a full commit sha: {new}")
    changed = [p.relative_to(root).as_posix() for p in vcpkg_files(root) if rewrite(p, new)]
    json.loads((root / MANIFEST).read_text(encoding="utf-8"))
    problems = verify(root, new)
    if problems:
        raise SystemExit("bump incomplete:\n  " + "\n  ".join(problems))
    return changed


def latest_vcpkg_commit() -> str:
    out = subprocess.run(
        ["gh", "api", "repos/microsoft/vcpkg/commits/master", "--jq", ".sha"],
        check=True, capture_output=True, text=True,
    ).stdout.strip()
    if not SHA.fullmatch(out):
        raise SystemExit(f"unexpected sha from the GitHub API: {out!r}")
    return out


def self_test() -> int:
    old = current(FIXTURES)
    new = "0123456789abcdef0123456789abcdef01234567"
    failures = []

    def expect(ok: bool, what: str) -> None:
        if not ok:
            failures.append(what)

    def fresh(tmp: Path, name: str) -> Path:
        root = tmp / name
        shutil.copytree(FIXTURES, root)
        return root

    with tempfile.TemporaryDirectory() as t:
        tmp = Path(t)

        root = fresh(tmp, "ok")
        expect(verify(root) == [], "fixture is consistent before the bump")
        changed = bump(root, new)
        expect(sorted(changed) == sorted(TARGET_FILES), f"bump touches the manifest and both workflows, got {changed}")
        expect(verify(root, new) == [], "no problems after the bump")
        for path in vcpkg_files(root):
            expect(old not in path.read_text(encoding="utf-8"), f"{path.name} still holds the old sha")
        expect(len(pins(root / ".github/workflows/nightly.yml")) == 2, "both nightly checkouts are rewritten")
        expect(old in (root / ".github/workflows/unrelated.yml").read_text(), "a workflow without vcpkg is left alone")
        expect(bump(root, new) == [], "a second bump is a no-op")

        root = fresh(tmp, "missed-workflow")
        bump(root, new)
        nightly = root / ".github/workflows/nightly.yml"
        nightly.write_text(nightly.read_text().replace(new, old, 1))
        expect(any("nightly.yml" in p for p in verify(root, new)), "a workflow left on the old sha fails verify")
        expect(any("disagree" in p for p in verify(root)), "disagreeing pins fail the --check form")

        root = fresh(tmp, "missed-manifest")
        bump(root, new)
        manifest = root / MANIFEST
        manifest.write_text(manifest.read_text().replace(new, old))
        expect(any("vcpkg.json" in p for p in verify(root, new)), "a manifest left on the old sha fails verify")

        root = fresh(tmp, "unpinned")
        engine = root / ".github/workflows/engine.yml"
        engine.write_text(engine.read_text().replace("VCPKG_COMMIT: " + old, "VCPKG_COMMIT: master"))
        expect(any("engine.yml" in p for p in verify(root)), "an unrecognised pin fails verify")

        root = fresh(tmp, "new-workflow")
        (root / ".github/workflows/extra.yml").write_text(
            "jobs:\n  a:\n    steps:\n      - uses: actions/checkout@v4\n        with:\n"
            "          repository: microsoft/vcpkg\n          ref: master\n"
        )
        expect(any("extra.yml" in p for p in verify(root)), "a new workflow checking out vcpkg unpinned fails verify")
        try:
            bump(root, new)
            expect(False, "bump must refuse when a vcpkg workflow cannot be rewritten")
        except SystemExit:
            pass

    if failures:
        print("bump_vcpkg self-test FAILED:\n  " + "\n  ".join(failures))
        return 1
    print("bump_vcpkg self-test: ok")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    ap.add_argument("--commit", help="full sha to pin (default: latest vcpkg master)")
    ap.add_argument("--check", action="store_true", help="fail unless every pin is the same commit")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()
    if args.self_test:
        return self_test()
    if args.check:
        problems = verify(args.root)
        for problem in problems:
            print(problem)
        return 1 if problems else 0
    old = current(args.root)
    new = args.commit or latest_vcpkg_commit()
    changed = bump(args.root, new) if new != old else []
    print(f"{old} {new}" if changed else f"unchanged {old}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
