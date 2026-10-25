#!/usr/bin/env python3
"""Data lint (SEED-662): content under data/ stays consistent while many agents add files.

Fails, printing file:line of each violation, when under data/:
  1. dup-id: two records share an id inside one folder, or two item records (catalogue,
     notable_items, provisions) share an id across those folders (the tiresias_blind_coin clash).
  2. orphan-ref: a reference names an id that no record defines (loot homes, capability grants,
     trainers, quest factions and landmarks, parley quest givers and reward items, dialogue traits...).
  3. missing-field: a record lacks a field its kind requires.
  4. bad-id: an id breaks the stable id rules (dot-separated [a-z][a-z0-9_]* segments, no
     segment ending in _<digits>), the same rules as engine/data/src/id_audit.cpp.
  5. baseline: tools/data_lint_baseline.txt lists a violation that no longer exists, or the
     checker finds one it does not list. The baseline is the known debt: shrink it, never grow it.

Usage: check_data.py [--root DIR]    check the tree (default: this repository)
       check_data.py --self-test     prove every rule goes red on a deliberate violation
       check_data.py --write-baseline  print the baseline entries for the current tree
"""
import argparse
import json
import re
import shutil
import sys
import tempfile
from pathlib import Path

BASELINE = "tools/data_lint_baseline.txt"
TABLE_FREE_DIRS = ("axioms", "conflicts", "materials", "synergies", "ui")
ITEM_DIRS = ("catalogue", "notable_items", "provisions")
SEGMENT = re.compile(r"[a-z][a-z0-9_]*")
MAX_ID = 64

ITEM_FIELDS = ("name", "tier", "class", "weight", "description", "benefit", "cost", "cost_kinds")

# directory -> (predicate on the record, required fields); the first matching entry applies.
REQUIRED = {
    "catalogue": [(None, ITEM_FIELDS + ("home",))],
    "notable_items": [(None, ITEM_FIELDS + ("home",))],
    "provisions": [(None, ITEM_FIELDS + ("price", "stock"))],
    "dungeon_skeletons": [(None, ("name", "theme", "rooms", "links"))],
    "factions": [(None, ("name",))],
    "heralds": [(None, ("pressure", "name"))],
    "npcs": [(None, ("name", "description", "incompatible"))],
    "dialogue": [(None, ("text", "weight", "cooldown"))],
    "currency": [(None, ("name", "symbol", "earn", "spend", "cap"))],
    "weapon_classes": [(None, ("name", "windup_ticks", "strike_ticks", "recovery_ticks"))],
    "traversal": [(None, ("terrain", "speed_permille", "stamina_per_second"))],
    "progression": [
        ("trainer", ("trainer", "name", "gate", "currency", "sink", "capability", "cost", "days")),
        ("description", ("name", "description")),
    ],
    "quests": [
        ("silhouette", ("name", "silhouette", "roles", "routes")),
        ("archetype", ("title", "summary", "budget", "start", "nodes", "steps")),
        ("faction", ("title", "budget", "steps", "effects")),
    ],
}


def id_problem(value):
    if not isinstance(value, str) or not value:
        return "id is empty or not a string"
    if len(value) > MAX_ID:
        return f"id is longer than {MAX_ID} characters"
    for segment in value.split("."):
        if not SEGMENT.fullmatch(segment):
            return f"segment '{segment}' must be lowercase letters, digits and underscores, starting with a letter"
        if re.search(r"_[0-9]+$", segment):
            return f"segment '{segment}' ends in _<number>, which reads as a list position"
    return ""


def top_level_spans(text):
    """(start, end) character offsets of each element of a top-level JSON array."""
    spans, depth, in_string, escaped, start = [], 0, False, False, None
    for i, ch in enumerate(text):
        if in_string:
            if escaped:
                escaped = False
            elif ch == "\\":
                escaped = True
            elif ch == '"':
                in_string = False
            continue
        if ch == '"':
            in_string = True
        elif ch in "{[":
            depth += 1
            if depth == 2 and ch == "{":
                start = i
        elif ch in "}]":
            if depth == 2 and ch == "}" and start is not None:
                spans.append((start, i + 1))
                start = None
            depth -= 1
    return spans


class Record:
    def __init__(self, file, data, text, span, index=0):
        self.file = file
        self.index = index
        self.data = data
        self.text = text
        self.span = span
        self.id = data.get("id") if isinstance(data, dict) else None

    def line(self, needle=None):
        start, end = self.span
        offset = start
        if needle is not None:
            found = self.text.find(f'"{needle}"', start, end)
            if found >= 0:
                offset = found
        return self.text.count("\n", 0, offset) + 1

    def where(self, needle=None):
        return f"{self.file}:{self.line(needle)}"


def load_records(root):
    """directory -> list of Record, plus load problems as violations."""
    tables, problems = {}, []
    data_root = root / "data"
    for path in sorted(data_root.rglob("*.json")):
        rel = path.relative_to(root).as_posix()
        folder = path.relative_to(data_root).parts[0]
        text = path.read_text(encoding="utf-8")
        try:
            parsed = json.loads(text)
        except ValueError as e:
            problems.append(("parse", f"{rel}:1", rel, f"{rel}:1: not valid JSON: {e}"))
            continue
        if isinstance(parsed, dict) and folder == "axioms":
            tables.setdefault(folder, []).append(Record(rel, parsed, text, (0, len(text))))
            continue
        if not isinstance(parsed, list):
            if folder not in TABLE_FREE_DIRS:
                problems.append(("parse", f"{rel}:1", rel, f"{rel}:1: expected a top-level array of records"))
            continue
        spans = top_level_spans(text)
        if len(spans) != len(parsed) or not all(isinstance(item, dict) for item in parsed):
            problems.append(("parse", f"{rel}:1", rel, f"{rel}:1: every element of the top-level array must be an object"))
            continue
        for index, (item, span) in enumerate(zip(parsed, spans)):
            tables.setdefault(folder, []).append(Record(rel, item, text, span, index))
    return tables, problems


def as_list(value):
    return value if isinstance(value, list) else [value]


def step_actors(record):
    out = []
    if "archetype" not in record.data:
        return out
    for step in record.data.get("steps", []):
        parts = step.split("|") if isinstance(step, str) else []
        if len(parts) > 1 and parts[1]:
            out.append(parts[1])
    return out


def ids_of(tables, folder, predicate=None):
    return {r.id for r in tables.get(folder, []) if isinstance(r.id, str) and (predicate is None or predicate(r))}


def reference_rules(tables):
    """(folder, label, values(record), valid ids). values yields the strings that must resolve."""
    axioms = ids_of(tables, "axioms")
    items = set().union(*(ids_of(tables, d) for d in ITEM_DIRS))
    capabilities = ids_of(tables, "progression", lambda r: "description" in r.data)
    factions = ids_of(tables, "factions")
    landmarks = ids_of(tables, "quests", lambda r: "silhouette" in r.data)
    landmark_parts = {x for r in tables.get("quests", []) if "silhouette" in r.data
                      for x in as_list(r.data.get("roles", [])) + as_list(r.data.get("routes", []))}
    quest_ids = ids_of(tables, "quests")
    slots = {r.data["slot"] for r in tables.get("quests", []) if "slot" in r.data}
    traits = ids_of(tables, "npcs")
    parley_archetypes = {r.data["archetype"] for r in tables.get("parley", []) if "archetype" in r.data}
    currencies = ids_of(tables, "currency")
    key = lambda name: (lambda r: as_list(r.data.get(name, [])))

    def dialogue_traits(r):
        return [c.split("=", 1)[1] for c in r.data.get("conditions", []) if isinstance(c, str) and c.startswith("trait=")]

    return [
        ("catalogue", "home", key("home"), axioms),
        ("notable_items", "home", key("home"), axioms),
        ("catalogue", "grants", key("grants"), capabilities),
        ("traversal", "capability", key("capability"), capabilities),
        ("progression", "capability", key("capability"), capabilities),
        ("progression", "requires", key("requires"), capabilities),
        ("progression", "currency", key("currency"), currencies),
        ("heralds", "pressure", key("pressure"), axioms),
        ("factions", "axiom", key("axiom"), axioms),
        ("npcs", "incompatible", key("incompatible"), traits),
        ("dialogue", "conditions trait=", dialogue_traits, traits),
        ("quests", "faction", key("faction"), factions),
        ("quests", "archetype", key("archetype"), landmarks | parley_archetypes),
        ("quests", "after", key("after"), quest_ids),
        ("quests", "member_of", key("member_of"), slots),
        ("quests", "hostile_to", key("hostile_to"), slots),
        ("quests", "steps actor", step_actors, factions | landmark_parts | {""}),
        ("parley", "giver_archetype", key("giver_archetype"), parley_archetypes),
        ("parley", "reward_item", key("reward_item"), items),
    ]


def violations(root):
    """Sorted list of (rule, location, key detail, message)."""
    tables, found = load_records(root)

    seen_in_folder = {}
    seen_items = {}
    for folder, records in sorted(tables.items()):
        for record in records:
            if "id" not in record.data:
                if folder not in TABLE_FREE_DIRS:
                    found.append(("missing-field", record.where(), f"{record.file} #{record.index} id", f"{record.where()}: record has no id"))
                continue
            problem = id_problem(record.id)
            if problem:
                found.append(("bad-id", record.where("id"), f"{record.file} {record.id}", f"{record.where('id')}: {problem} \"{record.id}\""))
            if not isinstance(record.id, str):
                continue
            first = seen_in_folder.setdefault((folder, record.id), record)
            if first is not record:
                found.append(("dup-id", record.where("id"), f"{record.file} {record.id}",
                              f"{record.where('id')}: duplicate id \"{record.id}\", first defined at {first.where('id')}"))
            if folder in ITEM_DIRS:
                prior = seen_items.setdefault(record.id, record)
                if prior is not record and prior.file.split("/")[1] != folder:
                    found.append(("dup-id", record.where("id"), f"{record.file} {record.id}",
                                  f"{record.where('id')}: item id \"{record.id}\" is also defined at {prior.where('id')}"))

    for folder, rules in sorted(REQUIRED.items()):
        for record in tables.get(folder, []):
            for predicate, fields in rules:
                if predicate is None or predicate in record.data:
                    for field in fields:
                        if field not in record.data:
                            found.append(("missing-field", record.where(), f"{record.file} {record.id} {field}",
                                          f"{record.where()}: \"{record.id}\" is missing required field \"{field}\""))
                    break

    for folder, label, values, valid in reference_rules(tables):
        for record in tables.get(folder, []):
            for value in values(record):
                if isinstance(value, str) and value not in valid:
                    where = record.where(value) if f'"{value}"' in record.text[record.span[0]:record.span[1]] else record.where()
                    found.append(("orphan-ref", where, f"{record.file} {record.id} {label} {value}",
                                  f"{where}: \"{record.id}\" {label} \"{value}\" matches no known id"))
    return sorted(set(found))


def parse_baseline(text):
    return {line.strip() for line in text.splitlines() if line.strip() and not line.startswith("#")}


def check(root, baseline):
    found = violations(root)
    keys = {f"{rule} {detail}" for rule, _, detail, _ in found}
    out = [f"{message} [{rule}]" for rule, _, detail, message in found if f"{rule} {detail}" not in baseline]
    out += [f"{BASELINE}: '{entry}' is no longer a violation: delete the line" for entry in sorted(baseline - keys)]
    return out


def baseline_for(root):
    return "".join(f"{rule} {detail}\n" for rule, _, detail, _ in violations(root))


def read_baseline(root):
    path = root / BASELINE
    return parse_baseline(path.read_text(encoding="utf-8")) if path.is_file() else set()


def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=1), encoding="utf-8")


def sample_tree(root):
    item = {"tier": "common", "class": "gear", "weight": 1, "description": "d", "benefit": "b", "cost": "c", "cost_kinds": ["weight"]}
    write(root / "data/axioms/flood.json", [{"id": "cataclysm.flood"}])
    write(root / "data/progression/capabilities.json", [{"id": "swim", "name": "Swim", "description": "d"}])
    write(root / "data/catalogue/gear.json", [{"id": "lamp", "name": "Lamp", "home": ["cataclysm.flood"], "grants": ["swim"], **item}])
    write(root / "data/notable_items/gear.json", [{"id": "coin", "name": "Coin", "home": ["cataclysm.flood"], **item}])
    write(root / "data/provisions/provisions.json", [{"id": "loaf", "name": "Loaf", "price": 4, "stock": 2, **item}])
    write(root / "data/parley/parley.json", [{"id": "merchant", "archetype": "merchant"},
                                              {"id": "quest.errand", "giver_archetype": "merchant", "reward_item": "loaf"}])


def rule_coverage(repo):
    """Break every reference and required-field rule in a copy of the real data and expect it red."""
    failures = []
    root = Path(tempfile.mkdtemp(prefix="check_data_"))
    try:
        shutil.copytree(repo / "data", root / "data")
        tables, _ = load_records(root)
        for folder, label, values, _valid in reference_rules(tables):
            record = next((r for r in tables.get(folder, []) if values(r)), None)
            if record is None:
                failures.append(f"no {folder} record exercises the reference rule '{label}'")
                continue
            path = root / record.file
            original = path.read_text(encoding="utf-8")
            start, end = record.span
            key = label.split()[0]
            body = original[start:end]
            at = max(body.find(f'"{key}"'), 0)
            match = re.compile(r'(?<=["=|])' + re.escape(values(record)[0]) + r'(?=["|])').search(body, at)
            if match is None:
                failures.append(f"cannot locate {folder} '{label}' value to break")
                continue
            path.write_text(original[:start] + body[:match.start()] + "ghost_x" + body[match.end():] + original[end:], encoding="utf-8")
            if not any(v[0] == "orphan-ref" and label in v[3] and "ghost_x" in v[3] for v in violations(root)):
                failures.append(f"reference rule {folder} '{label}' stayed green on an orphan")
            path.write_text(original, encoding="utf-8")
        for folder, rules in REQUIRED.items():
            for predicate, fields in rules:
                record = next((r for r in tables.get(folder, []) if predicate is None or predicate in r.data), None)
                if record is None:
                    failures.append(f"no {folder} record exercises the required fields for '{predicate}'")
                    continue
                target = next(f for f in fields if f != predicate)
                path = root / record.file
                original = path.read_text(encoding="utf-8")
                parsed = json.loads(original)
                del parsed[record.index][target]
                path.write_text(json.dumps(parsed), encoding="utf-8")
                if not any(v[0] == "missing-field" and f'"{target}"' in v[3] for v in violations(root)):
                    failures.append(f"required field {folder} '{target}' stayed green when missing")
                path.write_text(original, encoding="utf-8")
    finally:
        shutil.rmtree(root, ignore_errors=True)
    return failures


def self_test():
    cases = [
        ("clean tree", lambda r: None, None),
        ("duplicate id in one folder", lambda r: write(r / "data/notable_items/gear_2.json", [{"id": "coin", "name": "Coin", "home": [], "tier": "common", "class": "gear", "weight": 1, "description": "d", "benefit": "b", "cost": "c", "cost_kinds": []}]), "dup-id"),
        ("duplicate item id across folders", lambda r: write(r / "data/provisions/extra.json", [{"id": "lamp", "name": "L", "price": 1, "stock": 1, "tier": "common", "class": "gear", "weight": 1, "description": "d", "benefit": "b", "cost": "c", "cost_kinds": []}]), "dup-id"),
        ("orphan loot home", lambda r: write(r / "data/catalogue/gear.json", [{"id": "lamp", "name": "Lamp", "home": ["cataclysm.nope"], "tier": "common", "class": "gear", "weight": 1, "description": "d", "benefit": "b", "cost": "c", "cost_kinds": []}]), "orphan-ref"),
        ("orphan quest reward item", lambda r: write(r / "data/parley/parley.json", [{"id": "merchant", "archetype": "merchant"}, {"id": "quest.errand", "giver_archetype": "merchant", "reward_item": "ghost_item"}]), "orphan-ref"),
        ("missing required field", lambda r: write(r / "data/provisions/provisions.json", [{"id": "loaf", "name": "Loaf"}]), "missing-field"),
        ("record without an id", lambda r: write(r / "data/currency/currencies.json", [{"name": "c"}]), "missing-field"),
        ("uppercase id", lambda r: write(r / "data/currency/currencies.json", [{"id": "Coin", "name": "c", "symbol": "s", "earn": 1, "spend": 1, "cap": 1}]), "bad-id"),
        ("id ending in a number", lambda r: write(r / "data/currency/currencies.json", [{"id": "coin_2", "name": "c", "symbol": "s", "earn": 1, "spend": 1, "cap": 1}]), "bad-id"),
        ("unparseable file", lambda r: (r / "data/currency").mkdir(parents=True, exist_ok=True) or (r / "data/currency/bad.json").write_text("[{", encoding="utf-8"), "parse"),
    ]
    failures = []
    for name, mutate, rule in cases:
        root = Path(tempfile.mkdtemp(prefix="check_data_"))
        try:
            sample_tree(root)
            mutate(root)
            rules = {v[0] for v in violations(root)}
            if rule is None and rules:
                failures.append(f"{name}: expected no violations, got {sorted(rules)}")
            elif rule is not None and rule not in rules:
                failures.append(f"{name}: expected a {rule} violation, got {sorted(rules)}")
        finally:
            shutil.rmtree(root, ignore_errors=True)

    root = Path(tempfile.mkdtemp(prefix="check_data_"))
    try:
        sample_tree(root)
        write(root / "data/parley/parley.json", [{"id": "merchant", "archetype": "merchant"}, {"id": "quest.errand", "giver_archetype": "merchant", "reward_item": "ghost_item"}])
        known = parse_baseline(baseline_for(root))
        if check(root, known):
            failures.append("baseline: a baselined violation still failed")
        if not check(root, set()):
            failures.append("baseline: an unbaselined violation passed")
        if not check(root, known | {"orphan-ref gone.json x y z"}):
            failures.append("baseline: a stale entry passed")
    finally:
        shutil.rmtree(root, ignore_errors=True)

    failures += rule_coverage(Path(__file__).resolve().parent.parent)

    for failure in failures:
        print(f"SELF-TEST FAILED: {failure}")
    if not failures:
        print(f"data lint self-test: all {len(cases)} cases and every rule behave")
    return 1 if failures else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", default=str(Path(__file__).resolve().parent.parent))
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--write-baseline", action="store_true", help="print the baseline for the current tree")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    root = Path(args.root)
    if args.write_baseline:
        sys.stdout.write(baseline_for(root))
        return 0
    errors = check(root, read_baseline(root))
    for error in errors:
        print(error)
    if errors:
        print(f"data lint: {len(errors)} problem(s)")
        return 1
    print("data lint: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
