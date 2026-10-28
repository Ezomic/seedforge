#!/usr/bin/env python3
"""Golden-image diff page (SEED-704): one HTML page that shows each failed golden's expected, actual and diff side by side.

When a golden image case fails, test_golden_image.cpp leaves its pictures in test-output/golden-failures:
NAME.expected.png (a copy of the golden, absent when there is none), NAME.actual.png, NAME.diff.png (absent when the
sizes differ or the golden is missing) and NAME.txt (why it failed). This writes DIR/index.html next to them, so the CI
artifact opens on one page. A missing picture shows as a labelled gap, not a broken image.

Usage: golden_diff_page.py DIR        write DIR/index.html; a missing or empty DIR writes nothing
       golden_diff_page.py --self-test
Exit: 0 written or nothing to write, 2 usage error.
"""
import html
import sys
import tempfile
import urllib.parse
from pathlib import Path

KINDS = ("expected", "actual", "diff")
SUFFIXES = tuple(f".{kind}.png" for kind in KINDS) + (".txt",)


def collect(folder):
    cases = {}
    for path in folder.iterdir():
        for suffix in SUFFIXES:
            if path.is_file() and path.name.endswith(suffix) and len(path.name) > len(suffix):
                cases.setdefault(path.name[: -len(suffix)], {})[suffix] = path.name
                break
    return cases


def render(cases, folder):
    rows = []
    for name in sorted(cases):
        files = cases[name]
        cells = []
        for kind in KINDS:
            file = files.get(f".{kind}.png")
            if file is None:
                body = f'<div class="gap">no {kind} image</div>'
            else:
                src = urllib.parse.quote(file)
                body = f'<a href="{src}"><img src="{src}" alt="{html.escape(name)} {kind}"></a>'
            cells.append(f"<figure><figcaption>{kind}</figcaption>{body}</figure>")
        reason = ""
        if ".txt" in files:
            reason = (folder / files[".txt"]).read_text(encoding="utf-8", errors="replace").strip()
        rows.append(f'<section><h2>{html.escape(name)}</h2><p>{html.escape(reason)}</p><div class="row">{"".join(cells)}</div></section>')
    return f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8"><title>Golden image failures</title>
<style>
body {{ font-family: system-ui, sans-serif; margin: 16px; background: #fff; color: #111; }}
.row {{ display: flex; gap: 8px; flex-wrap: wrap; }}
figure {{ margin: 0; flex: 1 1 300px; }}
figcaption {{ font-weight: 600; }}
img {{ width: 100%; image-rendering: pixelated; border: 1px solid #888; }}
.gap {{ border: 1px dashed #888; padding: 32px; text-align: center; color: #666; }}
</style></head>
<body><h1>Golden image failures: {len(cases)}</h1>
{"".join(rows)}
</body></html>
"""


def run(argv, out):
    if len(argv) != 1:
        print(__doc__, file=out)
        return 2
    folder = Path(argv[0])
    cases = collect(folder) if folder.is_dir() else {}
    if not cases:
        print(f"no golden failures under {folder}", file=out)
        return 0
    (folder / "index.html").write_text(render(cases, folder), encoding="utf-8")
    print(f"{len(cases)} golden failure(s): {folder / 'index.html'}", file=out)
    return 0


class _Sink:
    def write(self, _):
        pass


def self_test(out):
    failures = []

    def expect(label, got, want):
        if got != want:
            failures.append(f"{label}: got {got!r}, want {want!r}")

    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        expect("usage: no argument exits 2", run([], _Sink()), 2)

        missing = root / "gone"
        expect("a missing folder exits 0", run([str(missing)], _Sink()), 0)
        expect("a missing folder writes nothing", missing.exists(), False)

        empty = root / "empty"
        empty.mkdir()
        (empty / "notes.log").write_text("x", encoding="utf-8")
        expect("no failures exits 0", run([str(empty)], _Sink()), 0)
        expect("no failures writes no page", (empty / "index.html").exists(), False)

        runs = root / "run"
        runs.mkdir()
        for kind in KINDS:
            (runs / f"kit.{kind}.png").write_bytes(b"png")
            (runs / f"ui hud.{kind}.png").write_bytes(b"png")
        (runs / "kit.txt").write_text("kit: mean abs diff 9.000 <too high>", encoding="utf-8")
        (runs / "sized.expected.png").write_bytes(b"png")
        (runs / "sized.actual.png").write_bytes(b"png")
        (runs / "sized.txt").write_text("the golden is 320x180, the render is 640x360", encoding="utf-8")
        (runs / "new_case.actual.png").write_bytes(b"png")
        (runs / "new_case.txt").write_text("no golden image", encoding="utf-8")
        expect("failures exit 0", run([str(runs)], _Sink()), 0)
        page = (runs / "index.html").read_text(encoding="utf-8") if (runs / "index.html").exists() else ""

        expect("several failures: every case has a section", page.count("<section>"), 4)
        expect("the count is in the heading", "Golden image failures: 4" in page, True)
        at = [page.find(f"<h2>{name}") for name in ("kit", "new_case", "sized", "ui hud")]
        expect("cases are sorted", -1 not in at and at == sorted(at), True)
        expect("expected, actual and diff are linked", all(f'src="kit.{kind}.png"' in page for kind in KINDS), True)
        expect("a space in a name is quoted in the link", 'src="ui%20hud.actual.png"' in page, True)
        expect("a space in a name stays readable in the heading", "<h2>ui hud</h2>" in page, True)
        expect("the reason is shown and escaped", "&lt;too high&gt;" in page, True)
        sized = page[at[2]:at[3]] if -1 not in at else ""
        expect("different sizes: no diff image, a labelled gap", ("no diff image" in sized, 'src="sized.diff.png"' in sized), (True, False))
        new_case = page[at[1]:at[2]] if -1 not in at else ""
        expect("a missing golden: a labelled gap for expected", "no expected image" in new_case, True)
        expect("a missing golden: the actual is still shown", 'src="new_case.actual.png"' in new_case, True)

    for f in failures:
        print(f"golden-diff-page self-test FAILED: {f}", file=out)
    if not failures:
        print("golden-diff-page self-test: ok", file=out)
    return 1 if failures else 0


if __name__ == "__main__":
    if sys.argv[1:] == ["--self-test"]:
        sys.exit(self_test(sys.stdout))
    sys.exit(run(sys.argv[1:], sys.stdout))
