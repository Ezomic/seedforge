#!/usr/bin/env python3
"""Test-time budget guard (SEED-658): print the slowest ctest cases and fail when one runs too long.

A single test close to the sanitizer timeout turns main red the day the code under it gets a little slower
(the SEED-492 walk test on the SEED-556 worlds, the SEED-650 live-worlds probe). This reads the per-test
durations of a ctest run, prints the slowest, and fails when a case exceeds its budget:

  profile sanitizer (Linux ASan/UBSan job)  default budget 120 s
  profile release   (Windows job, local)    default budget  30 s

A case may run longer only when tools/test_time_allowlist.json lists it with its own cap in seconds and a
reason; going over that cap fails too, so an allowlisted test cannot quietly double.

Usage: test_times.py INPUT --profile sanitizer|release [--top N] [--allowlist FILE] [--budget S]
                           [--update --reason TEXT]
       test_times.py --check-allowlist [--root DIR]    the allowlist file is well formed and names real tests
       test_times.py --self-test                       prove a slow case goes red and an allowlisted one stays green

INPUT is one of: a JUnit file (ctest --output-junit FILE), a ctest LastTest.log
(<build>/Testing/Temporary/LastTest.log), or captured ctest console output ("Test #12: name ... Passed 3.4 sec").
Several INPUTs are merged keeping the longest time per case.

Regenerate the allowlist after an intended slowdown: run the suite, then
  python tools/test_times.py ctest.xml --profile sanitizer --update --reason "why it is slow"
This adds every case over budget (cap = 1.5 x observed, rounded up to 30 s, at most the 1500 s ctest timeout),
raises a cap that was exceeded, and drops entries that now run within the default budget. Edit the reason of
a new entry in the file if the shared one does not fit, and commit it.

Exit: 0 within budget, 1 a case over budget, 2 usage or input error.
"""
import argparse
import json
import math
import re
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path

PROFILES = {"sanitizer": 120.0, "release": 30.0}
FAST_CAPS = {"sanitizer": 90.0, "release": 5.0}
TIERS = "engine/tests/test_tiers.json"
CTEST_TIMEOUT = 1500.0
CAP_STEP = 30.0
ALLOWLIST = "tools/test_time_allowlist.json"
SOURCE_DIRS = ("engine", "game", "libworldforge", "forge-cli")
SOURCE_SUFFIXES = (".cpp", ".h", ".txt", ".cmake")

CONSOLE = re.compile(r"Test\s+#\d+:\s+(?P<name>.+?)\s+\.{3,}\s*(?:\*{3})?(?P<status>[A-Za-z][A-Za-z ]*?)\s+(?P<sec>\d+(?:\.\d+)?) sec\s*$")
LOG_START = re.compile(r"^\d+/\d+ Testing: (?P<name>.+?)\s*$")
LOG_TIME = re.compile(r"^Test time =\s+(?P<sec>\d+(?:\.\d+)?) sec")


class InputError(Exception):
    pass


def parse_junit(text):
    try:
        root = ET.fromstring(text)
    except ET.ParseError as e:
        raise InputError(f"not valid JUnit XML: {e}")
    times = {}
    for case in root.iter("testcase"):
        name = case.get("name")
        sec = case.get("time")
        if name and sec is not None:
            times[name] = float(sec)
    return times


def parse_last_test_log(text):
    times = {}
    name = None
    for line in text.splitlines():
        m = LOG_START.match(line)
        if m:
            name = m.group("name")
            continue
        m = LOG_TIME.match(line)
        if m and name:
            times[name] = float(m.group("sec"))
            name = None
    return times


def parse_console(text):
    times = {}
    for line in text.splitlines():
        m = CONSOLE.search(line)
        if m:
            times[m.group("name")] = float(m.group("sec"))
    return times


def parse_times(text):
    head = text.lstrip()[:5]
    if head.startswith("<"):
        times = parse_junit(text)
    elif "Test time =" in text:
        times = parse_last_test_log(text)
    else:
        times = parse_console(text)
    return times


def load_times(paths):
    merged = {}
    for path in paths:
        try:
            text = Path(path).read_text(encoding="utf-8", errors="replace")
        except OSError as e:
            raise InputError(f"cannot read {path}: {e}")
        times = parse_times(text)
        if not times:
            raise InputError(f"{path}: no test durations found")
        for name, sec in times.items():
            merged[name] = max(sec, merged.get(name, 0.0))
    return merged


def read_allowlist(path):
    """Returns {profile: {name: {"seconds": float, "reason": str}}}; an absent file is an empty allowlist."""
    p = Path(path)
    if not p.exists():
        return {k: {} for k in PROFILES}
    problems = []

    def no_dupes(pairs):
        d = {}
        for k, v in pairs:
            if k in d:
                problems.append(f"duplicate key {k!r}")
            d[k] = v
        return d

    try:
        data = json.loads(p.read_text(encoding="utf-8"), object_pairs_hook=no_dupes)
    except (OSError, json.JSONDecodeError) as e:
        raise InputError(f"{path}: {e}")
    if problems:
        raise InputError(f"{path}: " + "; ".join(problems))
    if not isinstance(data, dict) or set(data) != set(PROFILES):
        raise InputError(f"{path}: top level must be an object with exactly the keys {sorted(PROFILES)}")
    return data


def allowlist_problems(data):
    problems = []
    for profile, entries in data.items():
        if not isinstance(entries, dict):
            problems.append(f"{profile}: must be an object of test name to entry")
            continue
        names = list(entries)
        if names != sorted(names):
            problems.append(f"{profile}: entries are not sorted by name")
        for name, entry in entries.items():
            where = f"{profile}: {name!r}"
            if not isinstance(entry, dict) or set(entry) != {"seconds", "reason"}:
                problems.append(f"{where}: entry must be exactly {{seconds, reason}}")
                continue
            sec, reason = entry["seconds"], entry["reason"]
            if isinstance(sec, bool) or not isinstance(sec, (int, float)) or not math.isfinite(sec):
                problems.append(f"{where}: seconds must be a finite number")
            elif sec <= PROFILES[profile]:
                problems.append(f"{where}: cap {sec} s is not above the default budget {PROFILES[profile]:g} s, remove the entry")
            elif sec > CTEST_TIMEOUT:
                problems.append(f"{where}: cap {sec} s is above the {CTEST_TIMEOUT:g} s ctest timeout")
            if not isinstance(reason, str) or not reason.strip():
                problems.append(f"{where}: needs a reason")
    return problems


def source_text(root):
    chunks = []
    for d in SOURCE_DIRS:
        base = Path(root) / d
        if not base.is_dir():
            continue
        for f in base.rglob("*"):
            if f.suffix in SOURCE_SUFFIXES and f.is_file() and "build" not in f.relative_to(root).parts:
                chunks.append(f.read_text(encoding="utf-8", errors="replace"))
    return "\n".join(chunks)


def check_allowlist(root, path=None):
    path = path or Path(root) / ALLOWLIST
    data = read_allowlist(path)
    problems = allowlist_problems(data)
    names = {n for entries in data.values() if isinstance(entries, dict) for n in entries}
    if names and not problems:
        text = source_text(root)
        for name in sorted(names):
            if name not in text:
                problems.append(f"{name!r} is not in any test source: renamed or deleted, update the allowlist")
    return problems


def evaluate(times, entries, budget):
    """Returns the cases that broke their budget as (name, seconds, limit, reason or None), slowest first."""
    over = []
    for name, sec in times.items():
        entry = entries.get(name)
        limit = entry["seconds"] if entry else budget
        if sec > limit:
            over.append((name, sec, limit, entry["reason"] if entry else None))
    over.sort(key=lambda o: -o[1])
    return over


def tier_lists(path):
    if not Path(path).exists():
        return None
    try:
        data = json.loads(Path(path).read_text(encoding="utf-8"))
        return set(data["medium"]) | set(data["soak"])
    except (OSError, ValueError, KeyError, TypeError) as e:
        raise InputError(f"{path}: {e}")


def evaluate_tiers(times, slow_tiers, profile, entries):
    """A case in no tier list is fast: over the fast cap it belongs in engine/tests/test_tiers.json (SEED-664)."""
    cap = FAST_CAPS[profile]
    over = []
    for name, sec in times.items():
        if name not in slow_tiers and name not in entries and sec > cap:
            over.append((name, sec, cap, f"fast tier cap, add it to the medium list in {TIERS}"))
    return over


def cap_for(observed):
    return min(CTEST_TIMEOUT, math.ceil(observed * 1.5 / CAP_STEP) * CAP_STEP)


def update_entries(times, entries, budget, reason):
    new = {}
    for name, entry in entries.items():
        sec = times.get(name)
        if sec is None:
            new[name] = entry
        elif sec > budget:
            cap = entry["seconds"] if sec <= entry["seconds"] else max(entry["seconds"], cap_for(sec))
            new[name] = {"seconds": cap, "reason": entry["reason"]}
    for name, sec in times.items():
        if sec > budget and name not in new:
            new[name] = {"seconds": cap_for(sec), "reason": reason}
    return {k: new[k] for k in sorted(new)}


def write_allowlist(path, data):
    text = json.dumps(data, indent=2, ensure_ascii=False) + "\n"
    Path(path).write_text(text, encoding="utf-8", newline="\n")


def report(times, over, budget, top, out):
    print(f"slowest {min(top, len(times))} of {len(times)} test cases (budget {budget:g} s):", file=out)
    for name, sec in sorted(times.items(), key=lambda kv: -kv[1])[:top]:
        print(f"  {sec:9.2f} s  {name}", file=out)
    if not over:
        print("test-times: every case is within its budget", file=out)
        return
    print(f"test-times: {len(over)} case(s) over budget:", file=out)
    for name, sec, limit, reason in over:
        what = f"allowlisted cap {limit:g} s ({reason})" if reason else f"budget {limit:g} s, not allowlisted"
        print(f"  {sec:.2f} s > {what}: {name}", file=out)
    print("test-times: speed the test up, or if it must be slow add it to tools/test_time_allowlist.json "
          "(python tools/test_times.py INPUT --profile P --update --reason TEXT)", file=out)


def run(argv, out=sys.stdout):
    ap = argparse.ArgumentParser(add_help=True, description="ctest per-test time budget guard")
    ap.add_argument("inputs", nargs="*")
    ap.add_argument("--profile", choices=sorted(PROFILES))
    ap.add_argument("--budget", type=float, help="override the profile's default budget in seconds")
    ap.add_argument("--top", type=int, default=20)
    ap.add_argument("--root", default=str(Path(__file__).resolve().parent.parent))
    ap.add_argument("--allowlist")
    ap.add_argument("--tiers", help=f"tier lists (default {TIERS} under --root when present)")
    ap.add_argument("--update", action="store_true")
    ap.add_argument("--reason")
    ap.add_argument("--check-allowlist", action="store_true")
    ap.add_argument("--self-test", action="store_true")
    try:
        args = ap.parse_args(argv)
    except SystemExit as e:
        return int(e.code or 0)
    allowlist = args.allowlist or str(Path(args.root) / ALLOWLIST)
    try:
        if args.self_test:
            return self_test(out)
        if args.check_allowlist:
            problems = check_allowlist(args.root, allowlist)
            for p in problems:
                print(f"test-times: {p}", file=out)
            if not problems:
                print("test-times: allowlist is consistent", file=out)
            return 1 if problems else 0
        if not args.inputs or not args.profile:
            print("test-times: need INPUT and --profile (or --check-allowlist, --self-test)", file=out)
            return 2
        if args.top < 1 or (args.budget is not None and not args.budget > 0):
            print("test-times: --top and --budget must be positive", file=out)
            return 2
        budget = args.budget if args.budget is not None else PROFILES[args.profile]
        times = load_times(args.inputs)
        data = read_allowlist(allowlist)
        entries = data[args.profile]
        if args.update:
            if not (args.reason and args.reason.strip()):
                print("test-times: --update needs --reason", file=out)
                return 2
            data[args.profile] = update_entries(times, entries, budget, args.reason.strip())
            write_allowlist(allowlist, data)
            print(f"test-times: wrote {len(data[args.profile])} {args.profile} entries to {allowlist}", file=out)
            return 0
        over = evaluate(times, entries, budget)
        slow_tiers = tier_lists(args.tiers or str(Path(args.root) / TIERS))
        if slow_tiers is not None and args.budget is None:
            seen = {o[0] for o in over}
            over += [o for o in evaluate_tiers(times, slow_tiers, args.profile, entries) if o[0] not in seen]
            over.sort(key=lambda o: -o[1])
        report(times, over, budget, args.top, out)
        stale = sorted(n for n in entries if n in times and times[n] <= budget)
        for n in stale:
            print(f"test-times: note: {n!r} runs within the default budget now, --update drops its entry", file=out)
        return 1 if over else 0
    except InputError as e:
        print(f"test-times: {e}", file=out)
        return 2


class _Sink:
    def write(self, _):
        pass


def self_test(out):
    failures = []

    def expect(label, got, want):
        if got != want:
            failures.append(f"{label}: got {got!r}, want {want!r}")

    junit = ('<testsuite><testcase name="fast case" time="1.5"/><testcase name="slow case" time="200.0"/>'
             '<testcase name="allowed case" time="300.0"/></testsuite>')
    console = ("12/30 Test  #12: slow case ........................   Passed  200.00 sec\n"
               "13/30 Test  #13: odd, name... with dots ....   Passed    2.50 sec\n"
               "14/30 Test  #14: hung ...........***Timeout 1500.06 sec\n"
               "15/30 Test  #15: broke ..........***Failed    4.00 sec\n")
    log = ("1/2 Testing: slow case\n1/2 Test: slow case\nOutput:\n...\nTest time = 200.00 sec\nTest Passed.\n"
           "2/2 Testing: fast case\nOutput:\nTest time =   1.50 sec\n")
    expect("junit parse", parse_times(junit), {"fast case": 1.5, "slow case": 200.0, "allowed case": 300.0})
    expect("log parse", parse_times(log), {"slow case": 200.0, "fast case": 1.5})
    expect("console parse", parse_times(console),
           {"slow case": 200.0, "odd, name... with dots": 2.5, "hung": 1500.06, "broke": 4.0})

    entries = {"allowed case": {"seconds": 360.0, "reason": "needs a big world"}}
    times = parse_times(junit)
    over = evaluate(times, entries, 120.0)
    expect("synthetic slow case is red", [o[0] for o in over], ["slow case"])
    expect("allowlisted case stays green", "allowed case" in [o[0] for o in over], False)
    expect("allowlisted case over its cap is red",
           [o[0] for o in evaluate({"allowed case": 361.0}, entries, 120.0)], ["allowed case"])
    expect("a case exactly at the budget is green", evaluate({"x": 120.0}, {}, 120.0), [])
    expect("release budget is tighter", [o[0] for o in evaluate({"x": 31.0}, {}, PROFILES["release"])], ["x"])

    expect("a fast case over the fast cap is red", [o[0] for o in evaluate_tiers({"x": 6.0}, set(), "release", {})], ["x"])
    expect("a medium case over the fast cap is green", evaluate_tiers({"x": 6.0}, {"x"}, "release", {}), [])
    expect("a fast case within the cap is green", evaluate_tiers({"x": 4.0}, set(), "release", {}), [])
    expect("cap rounds 1.5x up to 30 s", cap_for(100.0), 150.0)
    expect("cap never passes the ctest timeout", cap_for(1500.0), CTEST_TIMEOUT)
    updated = update_entries({"slow case": 200.0, "allowed case": 100.0, "fast case": 1.0}, entries, 120.0, "why")
    expect("update adds the slow case", updated.get("slow case"), {"seconds": 300.0, "reason": "why"})
    expect("update drops a case back within budget", "allowed case" in updated, False)
    kept = update_entries({"other": 1.0}, entries, 120.0, "why")
    expect("update keeps an entry the run did not cover", kept, entries)

    good = {"sanitizer": {"a": {"seconds": 200, "reason": "r"}}, "release": {}}
    expect("good allowlist has no problems", allowlist_problems(good), [])
    for label, bad in (
        ("empty reason", {"sanitizer": {"a": {"seconds": 200, "reason": " "}}, "release": {}}),
        ("cap at the budget", {"sanitizer": {"a": {"seconds": 120, "reason": "r"}}, "release": {}}),
        ("cap past the ctest timeout", {"sanitizer": {"a": {"seconds": 2000, "reason": "r"}}, "release": {}}),
        ("nan cap", {"sanitizer": {"a": {"seconds": float("nan"), "reason": "r"}}, "release": {}}),
        ("unsorted", {"sanitizer": {"b": {"seconds": 200, "reason": "r"}, "a": {"seconds": 200, "reason": "r"}}, "release": {}}),
        ("extra field", {"sanitizer": {"a": {"seconds": 200, "reason": "r", "x": 1}}, "release": {}}),
    ):
        expect(f"bad allowlist ({label}) is rejected", bool(allowlist_problems(bad)), True)

    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        (root / "engine" / "tests").mkdir(parents=True)
        (root / "engine" / "tests" / "test_x.cpp").write_text('TEST_CASE("a real test") {}\n', encoding="utf-8")
        (root / "tools").mkdir()
        al = root / ALLOWLIST
        al.write_text(json.dumps({"sanitizer": {"a real test": {"seconds": 200, "reason": "r"}}, "release": {}}), encoding="utf-8")
        expect("check passes for a real test name", check_allowlist(root), [])
        al.write_text(json.dumps({"sanitizer": {"gone test": {"seconds": 200, "reason": "r"}}, "release": {}}), encoding="utf-8")
        expect("check fails for a name in no source", len(check_allowlist(root)), 1)
        al.write_text('{"sanitizer": {"a": {"seconds": 200, "reason": "r"}, "a": {"seconds": 300, "reason": "r"}}, "release": {}}', encoding="utf-8")
        try:
            check_allowlist(root)
            failures.append("duplicate key was accepted")
        except InputError:
            pass

        xml = root / "ctest.xml"
        xml.write_text(junit, encoding="utf-8")
        al.write_text(json.dumps({"sanitizer": {"allowed case": {"seconds": 360, "reason": "r"}}, "release": {}}), encoding="utf-8")
        argv = [str(xml), "--profile", "sanitizer", "--root", str(root)]
        expect("cli: slow case exits 1", run(argv, _Sink()), 1)
        xml.write_text(junit.replace('"200.0"', '"20.0"'), encoding="utf-8")
        expect("cli: allowlisted case alone exits 0", run(argv, _Sink()), 0)
        xml.write_text("<testsuite/>", encoding="utf-8")
        expect("cli: input without durations exits 2", run(argv, _Sink()), 2)
        expect("cli: missing profile exits 2", run([str(xml)], _Sink()), 2)

    for f in failures:
        print(f"test-times self-test FAILED: {f}", file=out)
    if not failures:
        print("test-times self-test: ok", file=out)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(run(sys.argv[1:]))
