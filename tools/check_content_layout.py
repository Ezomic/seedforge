#!/usr/bin/env python3
"""Content layout check (SEED-273): folder layout and file naming of data/ and art/.

Content is organised by feature, not by asset type: data/progression/trainers.json, not
data/json/trainers.json. Fails when, under data/ or art/:
  1. a folder is named after an asset type (textures, audio, meshes, ...).
  2. a folder name is not lower snake_case, or the tree is nested deeper than MAX_DEPTH.
  3. a data/ file is not a snake_case .json (plus .gitkeep, and README.md at the data/ root).
  4. an art/ file breaks its naming rule: textures T_PascalCase.png, meshes SM_/SK_PascalCase.fbx,
     audio A_PascalCase.wav/.ogg, fonts Family-Style.ttf/.otf with a Family-OFL.txt licence,
     Blender sources and scripts snake_case. Backups (.blend1) and unknown extensions are rejected.
  5. two paths differ only by case (they collide on Windows and macOS).
  6. tools/content_layout_baseline.txt lists a violation that no longer exists, or the checker
     finds one it does not list. The baseline is the known debt: shrink it, never grow it.

Usage: check_content_layout.py [--root DIR]    check the tree (default: this repository)
       check_content_layout.py --self-test     prove every rule goes red on a deliberate violation
"""
import argparse
import re
import sys
from pathlib import Path

CONTENT_ROOTS = ("data", "art")
MAX_DEPTH = 4
BASELINE = "tools/content_layout_baseline.txt"

TYPE_FOLDERS = {
    "textures", "texture", "audio", "sounds", "sound", "meshes", "mesh", "models", "model",
    "images", "image", "sprites", "scripts", "shaders", "fonts", "json", "jsons", "fbx", "pngs",
}

SNAKE = r"[a-z][a-z0-9]*(?:_[a-z0-9]+)*"
PASCAL = r"[A-Z][A-Za-z0-9]*"
PASCAL_NAME = rf"{PASCAL}(?:_{PASCAL})*"

SKIP_DIRS = {".git", "node_modules", "build", "__pycache__"}

ART_RULES = {
    ".png": (rf"T_{PASCAL_NAME}(?:_view)?", "textures are T_PascalCase.png (or T_PascalCase_view.png for a preview render)"),
    ".fbx": (rf"S[MK]_{PASCAL_NAME}", "meshes are SM_ (static) or SK_ (skeletal) PascalCase.fbx"),
    ".wav": (rf"A_{PASCAL_NAME}", "audio is A_PascalCase.wav"),
    ".ogg": (rf"A_{PASCAL_NAME}", "audio is A_PascalCase.ogg"),
    ".ttf": (rf"{PASCAL}(?:-{PASCAL})?", "fonts are Family-Style.ttf"),
    ".otf": (rf"{PASCAL}(?:-{PASCAL})?", "fonts are Family-Style.otf"),
    ".blend": (SNAKE, "Blender sources are snake_case.blend"),
    ".py": (SNAKE, "scripts are snake_case.py"),
    ".txt": (rf"{PASCAL}-OFL", "the only .txt in art/ is a font licence, Family-OFL.txt"),
}


def stem_and_ext(name):
    stem, dot, ext = name.rpartition(".")
    return (stem, "." + ext) if dot and stem else (name, "")


def check_file(parts):
    top, name = parts[0], parts[-1]
    if name == ".gitkeep":
        return []
    stem, ext = stem_and_ext(name)
    if top == "data":
        if len(parts) == 2 and name == "README.md":
            return []
        if len(parts) == 2:
            return [("data-root", "data/ holds only feature folders (and README.md), not content files")]
        game_ui = parts[1] == "ui" and ext in (".rml", ".rcss")
        if (ext != ".json" and not game_ui) or not re.fullmatch(SNAKE, stem):
            return [("data-file", "data files are snake_case.json")]
        return []
    if ext == ".blend1":
        return [("art-backup", "Blender backups (.blend1) are not content: delete and ignore them")]
    rule = ART_RULES.get(ext)
    if rule is None:
        return [("art-extension", f"unknown art extension '{ext or name}': add a naming rule before committing it")]
    pattern, hint = rule
    if not re.fullmatch(pattern, stem):
        return [("art-file", hint)]
    return []


def check_paths(paths):
    errors = []
    seen = {}
    for rel in sorted(paths):
        parts = rel.split("/")
        if parts[0] not in CONTENT_ROOTS:
            continue
        for folder in parts[1:-1]:
            if folder.lower() in TYPE_FOLDERS:
                errors.append((rel, "type-folder", f"'{folder}' is an asset type: organise by feature"))
            elif not re.fullmatch(SNAKE, folder):
                errors.append((rel, "folder-name", f"folder '{folder}' is not lower snake_case"))
        if len(parts) - 1 > MAX_DEPTH:
            errors.append((rel, "depth", f"nested {len(parts) - 1} folders deep (max {MAX_DEPTH})"))
        for rule, msg in check_file(parts):
            errors.append((rel, rule, msg))
        key = rel.lower()
        if key in seen and seen[key] != rel:
            errors.append((rel, "case-collision", f"differs only by case from {seen[key]}"))
        seen.setdefault(key, rel)
    return sorted(set(dict.fromkeys(errors)))


def dedupe_type_folders(errors):
    """One report per folder, not per file inside it."""
    out, seen = [], set()
    for rel, rule, msg in errors:
        if rule in {"type-folder", "folder-name"}:
            folder = msg.split("'")[1]
            parts = rel.split("/")
            key = (rule, "/".join(parts[: parts.index(folder) + 1]))
            if key in seen:
                continue
            seen.add(key)
            rel = key[1]
        out.append((rel, rule, msg))
    return out


def violations(paths):
    return dedupe_type_folders(check_paths(paths))


def keys(errors):
    return {f"{rule} {rel}" for rel, rule, _ in errors}


def parse_baseline(text):
    return {line.strip() for line in text.splitlines() if line.strip() and not line.startswith("#")}


def check(paths, baseline):
    errors = violations(paths)
    known = keys(errors)
    out = [f"{rel}: {msg} [{rule}]" for rel, rule, msg in errors if f"{rule} {rel}" not in baseline]
    out += [f"{BASELINE}: '{entry}' is no longer a violation: delete the line"
            for entry in sorted(baseline - known)]
    return out


def walk(root):
    root = Path(root)
    paths = []
    for base in CONTENT_ROOTS:
        top = root / base
        if not top.is_dir():
            continue
        for p in top.rglob("*"):
            rel = p.relative_to(root)
            if p.is_file() and SKIP_DIRS.isdisjoint(rel.parts):
                paths.append(rel.as_posix())
    return paths


def load_baseline(root):
    p = Path(root) / BASELINE
    return parse_baseline(p.read_text(encoding="utf-8")) if p.is_file() else set()


CLEAN = [
    "data/README.md",
    "data/axioms/.gitkeep",
    "data/axioms/last_favour.json",
    "data/progression/power_growth.json",
    "data/factions/guilds/archaists_2.json",
    "art/textures_free/.gitkeep",
    "art/hoplite/T_Bronze.png",
    "art/hoplite/T_Bronze_view.png",
    "art/hoplite/T_Helmet_AO.png",
    "art/hoplite/SM_Aspis.fbx",
    "art/hoplite/SK_Hoplite.fbx",
    "art/combat/A_WindupHeavy.wav",
    "art/combat/A_Hit.ogg",
    "art/ui/Cinzel.ttf",
    "art/ui/AlegreyaSans-Bold.ttf",
    "art/ui/Cinzel-OFL.txt",
    "art/hoplite/build_kit.py",
    "art/hoplite/ichor_kit.blend",
]


def self_test():
    base = violations(CLEAN)
    if base:
        print("self-test needs a clean sample first:\n  " + "\n  ".join(map(str, base)))
        return 1

    def add(*p):
        return lambda paths: paths + list(p)

    cases = [
        ("data organised by asset type", "type-folder", add("data/json/trainers.json")),
        ("art organised by asset type", "type-folder", add("art/Textures/hoplite/T_Bronze.png")),
        ("type folder nested in a feature", "type-folder", add("art/hoplite/meshes/SM_Aspis.fbx")),
        ("camelCase folder", "folder-name", add("data/factionRules/x.json")),
        ("folder with a space", "folder-name", add("art/boat parts/T_Hull.png")),
        ("tree too deep", "depth", add("data/a/b/c/d/e/x.json")),
        ("content file at the data root", "data-root", add("data/loose.json")),
        ("data file in CamelCase", "data-file", add("data/axioms/LastFavour.json")),
        ("data file with a dash", "data-file", add("data/axioms/last-favour.json")),
        ("data file that is not json", "data-file", add("data/axioms/notes.txt")),
        ("rml outside data/ui", "data-file", add("data/axioms/screen.rml")),
        ("texture without prefix", "art-file", add("art/hoplite/Bronze.png")),
        ("texture in snake_case", "art-file", add("art/hoplite/t_bronze.png")),
        ("mesh with the texture prefix", "art-file", add("art/hoplite/T_Aspis.fbx")),
        ("audio without prefix", "art-file", add("art/combat/windup.wav")),
        ("blend in PascalCase", "art-file", add("art/hoplite/IchorKit.blend")),
        ("stray licence text", "art-file", add("art/ui/notes.txt")),
        ("blender backup committed", "art-backup", add("art/hoplite/ichor_kit.blend1")),
        ("unknown art extension", "art-extension", add("art/hoplite/thing.psd")),
        ("extensionless art file", "art-extension", add("art/hoplite/Makefile")),
        ("case-only collision", "case-collision", add("data/axioms/Last_Favour.json", "data/axioms/last_favour.JSON")),
    ]
    missed = 0
    for name, rule, mutate in cases:
        caught = any(r == rule for _, r, _ in violations(mutate(CLEAN)))
        print(f"  {name}: {'red as expected' if caught else 'STAYED GREEN'}")
        missed += not caught

    folder_once = [v for v in violations(CLEAN + ["data/json/a.json", "data/json/b.json"]) if v[1] == "type-folder"]
    ok = len(folder_once) == 1
    print(f"  a bad folder is reported once, not per file: {'ok' if ok else 'FAILED'}")
    missed += not ok

    errs = check(CLEAN + ["data/json/a.json"], set())
    ok = bool(errs)
    print(f"  a violation outside the baseline fails the check: {'red as expected' if ok else 'STAYED GREEN'}")
    missed += not ok
    errs = check(CLEAN + ["data/json/a.json"], {"type-folder data/json"})
    ok = not errs
    print(f"  a baselined violation is tolerated: {'ok' if ok else 'FAILED'}")
    missed += not ok
    errs = check(CLEAN, {"type-folder data/json"})
    ok = any("no longer a violation" in e for e in errs)
    print(f"  a stale baseline line fails the check: {'red as expected' if ok else 'STAYED GREEN'}")
    missed += not ok

    if missed:
        print(f"self-test: {missed} rule(s) did not behave")
        return 1
    print(f"self-test: all {len(cases) + 4} checks passed")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", default=str(Path(__file__).resolve().parent.parent))
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--print-baseline", action="store_true", help="print the current violations as baseline lines")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    if args.print_baseline:
        for line in sorted(keys(violations(walk(args.root)))):
            print(line)
        return 0
    errors = check(walk(args.root), load_baseline(args.root))
    if errors:
        print("Content layout violations (SEED-273):")
        for e in errors:
            print(f"  - {e}")
        return 1
    print("content layout: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
