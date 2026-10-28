#!/usr/bin/env python3
"""Contact sheet of the art a pull request changes, uploaded as a CI artifact so art is reviewed without a build (SEED-709).

  art_contact_sheet.py --base REF --head REF --out DIR   write DIR/index.html for the assets under art/ changed from REF to REF
  art_contact_sheet.py --self-test                        prove the selection and page rules

Images are copied next to the page. A GLB is written as a script that hands its bytes to <model-viewer>, because a page opened
from disk may not fetch() a local file but may load a <script>; the viewer itself comes from a CDN, so the 3D previews need
network when the page is opened. Other files are listed with their size, deleted ones without a preview. LFS objects are
fetched one by one with `git lfs smudge`, so the job downloads only what the sheet shows.
"""
import argparse
import base64
import html
import os
import subprocess
import sys
from collections import namedtuple

CAP = 48
EXCLUDED = {"art/LICENSES.json"}
IMAGE_EXT = {".png", ".jpg", ".jpeg", ".webp"}
MODEL_EXT = {".glb"}
MODEL_VIEWER = (
    '<script type="module" src="https://cdn.jsdelivr.net/npm/@google/model-viewer@4.0.0/dist/model-viewer.min.js" '
    'integrity="sha384-sr9b4Ux0WhAUGclJ0ym0FSY2zSOMmNSn0bP/SA0e6bNCrpn/5W3QL8mm+LdlQMKw" crossorigin="anonymous"></script>'
)

Change = namedtuple("Change", "status path old")
Asset = namedtuple("Asset", "path kind change old")


CSS = """
:root{--bg:#f5f5f4;--fg:#1c1917;--mut:#6b6560;--card:#fff;--bd:#d6d3d1;--add:#15803d;--mod:#b45309;--del:#b91c1c}
@media (prefers-color-scheme:dark){:root{--bg:#1c1917;--fg:#f5f5f4;--mut:#a8a29e;--card:#292524;--bd:#44403c;--add:#4ade80;--mod:#fbbf24;--del:#f87171}}
body{margin:0;padding:16px;background:var(--bg);color:var(--fg);font:14px/1.45 system-ui,sans-serif}
h1{margin:0 0 4px}h2{margin:28px 0 8px;border-bottom:1px solid var(--bd);padding-bottom:4px}
.mut{color:var(--mut)}
.grid{display:flex;flex-wrap:wrap;gap:10px;align-items:flex-start}
.card{background:var(--card);border:1px solid var(--bd);border-radius:6px;padding:6px;width:240px}
.card img,.card model-viewer{width:100%;height:228px;display:block;border-radius:3px;background:#ddd;object-fit:contain}
.cap{font-size:12px;margin-top:4px;word-break:break-word}
.badge{display:inline-block;font-size:11px;font-weight:600;padding:0 6px;border-radius:8px;border:1px solid currentColor;margin-left:4px}
.added,.renamed{color:var(--add)}.modified{color:var(--mod)}.deleted{color:var(--del)}
button{font:inherit;width:100%;height:228px;border:1px dashed var(--bd);border-radius:3px;background:transparent;color:inherit;cursor:pointer}
table{border-collapse:collapse}td{padding:2px 12px 2px 0}
"""

# Each models/<n>.js calls this with the GLB as base64; the bytes become a blob: URL the viewer can load from a page on disk.
LOADER = """
<script>
function sheetLoad(btn){const s=document.createElement('script');s.src=btn.dataset.src;document.head.appendChild(s);btn.textContent='Loading...';}
function sheetModel(id,b64){const bytes=Uint8Array.from(atob(b64),c=>c.charCodeAt(0));
const url=URL.createObjectURL(new Blob([bytes],{type:'model/gltf-binary'}));const btn=document.getElementById(id);
const mv=document.createElement('model-viewer');mv.setAttribute('camera-controls','');mv.src=url;btn.replaceWith(mv);}
</script>
"""


def parse_name_status(text):
    """Parse `git diff --name-status -z`: a status field, then one path (two for a rename or copy)."""
    fields = text.split("\0")
    changes = []
    i = 0
    while i < len(fields) and fields[i]:
        status = fields[i][0]
        if status in "RC":
            changes.append(Change(status, fields[i + 2], fields[i + 1]))
            i += 3
        else:
            changes.append(Change(status, fields[i + 1], None))
            i += 2
    return changes


def in_art(path):
    return path is not None and path.startswith("art/") and path not in EXCLUDED


def kind_of(path):
    ext = os.path.splitext(path)[1].lower()
    if ext in IMAGE_EXT:
        return "image"
    if ext in MODEL_EXT:
        return "model"
    return "other"


def classify(change):
    if change.status == "D":
        return Asset(change.path, kind_of(change.path), "deleted", None) if in_art(change.path) else None
    if not in_art(change.path):
        return Asset(change.old, kind_of(change.old), "deleted", None) if change.status == "R" and in_art(change.old) else None
    if change.status == "R" and in_art(change.old):
        return Asset(change.path, kind_of(change.path), "renamed", change.old)
    return Asset(change.path, kind_of(change.path), "modified" if change.status in "MT" else "added", None)


def select(changes, cap=CAP):
    """The changed assets under art/, sorted by path; returns the first `cap` and how many more there are."""
    assets = {}
    for change in changes:
        asset = classify(change)
        if asset:
            assets[asset.path] = asset
    ordered = [assets[p] for p in sorted(assets)]
    return ordered[:cap], max(0, len(ordered) - cap)


def lfs_size(blob):
    if blob.startswith(b"version https://git-lfs.github.com/spec/"):
        for line in blob.decode("utf-8", "replace").splitlines():
            if line.startswith("size "):
                return int(line[5:])
    return len(blob)


def human(n):
    for unit in ("B", "KiB", "MiB"):
        if n < 1024 or unit == "MiB":
            return f"{n} B" if unit == "B" else f"{n:.1f} {unit}"
        n /= 1024


def esc(s):
    return html.escape(str(s), quote=True)


def caption(asset):
    old = f'<br><span class="mut">from {esc(asset.old)}</span>' if asset.old else ""
    return f'<div class="cap">{esc(asset.path)}<span class="badge {asset.change}">{asset.change}</span>{old}</div>'


def card(asset, files):
    if asset.kind == "image":
        preview = f'<img loading="lazy" src="{esc(files[asset.path])}" alt="">'
    else:
        # models/<n>.js calls sheetModel("m<n>", ...), so the button id comes from the file name.
        n = os.path.splitext(os.path.basename(files[asset.path]))[0]
        preview = f'<button id="m{esc(n)}" data-src="{esc(files[asset.path])}" onclick="sheetLoad(this)">Show 3D (needs network)</button>'
    return f'<div class="card">{preview}{caption(asset)}</div>'


def render(shown, hidden, files, sizes, base, head, failed=frozenset()):
    previews = [a for a in shown if a.path in files]
    listed = [a for a in shown if a.path not in files]
    parts = [f"<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width\">"
             f"<title>Art contact sheet</title><style>{CSS}</style>"]
    if any(a.kind == "model" for a in previews):
        parts.append(MODEL_VIEWER + LOADER)
    parts.append(f'</head><body><h1>Art contact sheet</h1><p class="mut">{esc(base)}..{esc(head)}: {len(shown) + hidden} changed asset(s)</p>')
    if not shown:
        parts.append("<p>No asset under art/ changed (art/LICENSES.json is not shown).</p>")
    if previews:
        parts.append('<h2>Previews</h2><div class="grid">')
        parts.extend(card(a, files) for a in previews)
        parts.append("</div>")
    if listed:
        parts.append("<h2>Other files</h2><table>")
        for a in listed:
            size = esc(human(sizes[a.path])) if a.path in sizes else "preview unavailable" if a.path in failed else ""
            old = f'<span class="mut">from {esc(a.old)}</span>' if a.old else ""
            parts.append(f'<tr><td>{esc(a.path)}</td><td><span class="badge {a.change}">{a.change}</span></td><td>{size}</td><td>{old}</td></tr>')
        parts.append("</table>")
    if hidden:
        parts.append(f"<p><strong>{hidden} more not shown</strong> (the sheet stops at {CAP} assets, in path order).</p>")
    parts.append("</body></html>\n")
    return "".join(parts)


def git(*args, stdin=None):
    return subprocess.run(["git", *args], input=stdin, stdout=subprocess.PIPE, check=True).stdout


def content(run, head, path):
    blob = run("show", f"{head}:{path}")
    if blob.startswith(b"version https://git-lfs.github.com/spec/"):
        return run("lfs", "smudge", "--", path, stdin=blob)
    return blob


def build(base, head, out, run=git, log=print):
    changes = parse_name_status(run("diff", "--name-status", "-M", "-z", base, head).decode("utf-8", "surrogateescape"))
    shown, hidden = select(changes)
    os.makedirs(os.path.join(out, "assets"), exist_ok=True)
    os.makedirs(os.path.join(out, "models"), exist_ok=True)
    files, sizes, failed = {}, {}, set()
    for n, asset in enumerate(shown):
        if asset.change == "deleted":
            continue
        try:
            if asset.kind == "other":
                sizes[asset.path] = lfs_size(run("show", f"{head}:{asset.path}"))
                continue
            data = content(run, head, asset.path)
        except subprocess.CalledProcessError as error:
            # A symlink, a submodule or an LFS object the server does not hold (a fork's) must not cost the other previews.
            log(f"art-contact-sheet: no preview for {asset.path}: {error}")
            failed.add(asset.path)
            continue
        ext = os.path.splitext(asset.path)[1].lower()
        if asset.kind == "image":
            rel = f"assets/{n}{ext}"
            with open(os.path.join(out, rel), "wb") as fh:
                fh.write(data)
        else:
            rel = f"models/{n}.js"
            with open(os.path.join(out, rel), "w", encoding="ascii") as fh:
                fh.write(f'sheetModel("m{n}", "{base64.b64encode(data).decode("ascii")}");\n')
        files[asset.path] = rel
    page = render(shown, hidden, files, sizes, base, head, failed)
    with open(os.path.join(out, "index.html"), "w", encoding="utf-8") as fh:
        fh.write(page)
    log(f"art-contact-sheet: {len(shown)} shown, {hidden} more not shown, {len(files)} preview(s) in {out}")
    return 0


def self_test(out):
    failures = []

    def check(name, got, want):
        if got != want:
            failures.append(f"{name}: got {got!r}, want {want!r}")

    z = "\0".join(["M", "art/a.png", "D", "art/gone.glb", "R087", "art/old.glb", "art/new.glb", "A", "engine/x.cpp", ""])
    check("parse -z name-status", parse_name_status(z),
          [Change("M", "art/a.png", None), Change("D", "art/gone.glb", None), Change("R", "art/new.glb", "art/old.glb"),
           Change("A", "engine/x.cpp", None)])
    check("parse odd path", parse_name_status("A\0art/a b,c.png\0"), [Change("A", "art/a b,c.png", None)])

    shown, hidden = select([Change("D", "art/props/gone.glb", None)])
    check("deleted asset is listed as deleted", shown, [Asset("art/props/gone.glb", "model", "deleted", None)])

    shown, hidden = select([Change("R", "art/props/new.png", "art/props/old.png")])
    check("renamed asset keeps its old path", shown, [Asset("art/props/new.png", "image", "renamed", "art/props/old.png")])
    shown, hidden = select([Change("R", "art/props/in.glb", "concept/in.glb")])
    check("renamed into art/ is added", shown, [Asset("art/props/in.glb", "model", "added", None)])
    shown, hidden = select([Change("R", "elsewhere/out.glb", "art/props/out.glb")])
    check("renamed out of art/ is deleted", shown, [Asset("art/props/out.glb", "model", "deleted", None)])

    check("only LICENSES.json selects nothing", select([Change("M", "art/LICENSES.json", None)]), ([], 0))
    check("paths outside art/ select nothing", select([Change("M", "engine/a.png", None), Change("M", "artwork/b.png", None)]), ([], 0))

    shown, hidden = select([Change("A", "art/x/a.fbx", None), Change("M", "art/x/b.blend", None), Change("A", "art/x/c.json", None),
                            Change("A", "art/x/D.PNG", None), Change("A", "art/x/e.jpg", None), Change("A", "art/x/f.jpeg", None),
                            Change("A", "art/x/g.webp", None)])
    check("non-image and non-GLB files are other", [(a.path, a.kind) for a in shown],
          [("art/x/D.PNG", "image"), ("art/x/a.fbx", "other"), ("art/x/b.blend", "other"), ("art/x/c.json", "other"),
           ("art/x/e.jpg", "image"), ("art/x/f.jpeg", "image"), ("art/x/g.webp", "image")])

    many = [Change("A", f"art/m/{i:03}.png", None) for i in range(CAP + 5)]
    shown, hidden = select(list(reversed(many)))
    check("cap keeps the first CAP by path", [a.path for a in shown], [c.path for c in many[:CAP]])
    check("cap counts the rest", hidden, 5)
    check("cap is 48", CAP, 48)
    shown, hidden = select([Change("M", "art/a.png", None), Change("M", "art/a.png", None)])
    check("a path is listed once", len(shown), 1)

    pointer = b"version https://git-lfs.github.com/spec/v1\noid sha256:" + b"0" * 64 + b"\nsize 22249024\n"
    check("size of an LFS pointer is the object's", lfs_size(pointer), 22249024)
    check("size of a plain blob is its length", lfs_size(b"{\"a\": 1}\n"), 9)

    page = render([Asset("art/a.png", "image", "modified", None), Asset("art/b.glb", "model", "added", None),
                   Asset("art/c.fbx", "other", "added", None), Asset("art/d.png", "image", "deleted", None)],
                  3, {"art/a.png": "assets/0.png", "art/b.glb": "models/1.js"}, {"art/c.fbx": 2048}, "abc", "def")
    check("page says how many are not shown", "3 more not shown" in page, True)
    check("page shows the image", 'src="assets/0.png"' in page, True)
    check("page loads the model script", "models/1.js" in page, True)
    check("page pins the viewer with SRI", MODEL_VIEWER in page, True)
    check("page lists the other file with its size", "art/c.fbx" in page and "2.0 KiB" in page, True)
    check("deleted image has no preview", "art/d.png" in page and page.count("<img") == 1, True)
    check("page escapes paths", "&lt;x&gt;" in render([Asset("art/<x>.fbx", "other", "added", None)], 0, {}, {}, "a", "b"), True)
    empty = render([], 0, {}, {}, "a", "b")
    check("empty page says no assets changed", "No asset under art/ changed" in empty, True)
    check("empty page does not load the viewer", MODEL_VIEWER in empty, False)

    build_checks(check)

    for f in failures:
        print(f"art-contact-sheet self-test FAILED: {f}", file=out)
    if not failures:
        print("art-contact-sheet self-test: ok", file=out)
    return 1 if failures else 0


def build_checks(check):
    """Run build() against a fake git: what it fetches, what it writes, and that one failed fetch does not fail the sheet."""
    import tempfile

    pointer = b"version https://git-lfs.github.com/spec/v1\noid sha256:" + b"1" * 64 + b"\nsize 4096\n"
    diff = "\0".join(["A", "art/a.glb", "M", "art/b.png", "D", "art/c.png", "A", "art/d.fbx", "A", "art/e.png", "A", "art/LICENSES.json"]
                     + [x for i in range(CAP) for x in ("A", f"art/z/{i:02}.png")] + [""])
    smudged = []

    def fake(*args, stdin=None):
        if args[0] == "diff":
            return diff.encode("utf-8")
        if args[0] == "show":
            if args[1].endswith("e.png"):
                raise subprocess.CalledProcessError(128, "git show")
            return pointer
        smudged.append(args[-1])
        return b"GLB" if args[-1].endswith(".glb") else b"PNG"

    with tempfile.TemporaryDirectory() as out:
        check("build succeeds", build("b", "h", out, run=fake, log=lambda *a: None), 0)
        page = open(os.path.join(out, "index.html"), encoding="utf-8").read()
        model = open(os.path.join(out, "models", "0.js"), encoding="ascii").read()
        image = open(os.path.join(out, "assets", "1.png"), "rb").read()
    check("only shown images and models are fetched", smudged, ["art/a.glb", "art/b.png"] + [f"art/z/{i:02}.png" for i in range(CAP - 5)])
    check("model script carries the bytes for its button", model, f'sheetModel("m0", "{base64.b64encode(b"GLB").decode()}");\n')
    check("model button id matches its script", 'id="m0" data-src="models/0.js"' in page, True)
    check("image is written", image, b"PNG")
    check("deleted image has no file", "art/c.png" in page and 'src="assets/2.png"' not in page, True)
    check("other file shows its pointer size", "art/d.fbx" in page and "4.0 KiB" in page, True)
    check("a failed fetch is listed as unavailable", "art/e.png" in page and "preview unavailable" in page, True)
    check("capped assets are counted", "5 more not shown" in page, True)


def main(argv):
    ap = argparse.ArgumentParser(description="Contact sheet of the changed assets under art/")
    ap.add_argument("--base")
    ap.add_argument("--head")
    ap.add_argument("--out")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args(argv)
    if args.self_test:
        return self_test(sys.stdout)
    if not (args.base and args.head and args.out):
        ap.error("need --base, --head and --out (or --self-test)")
    return build(args.base, args.head, args.out)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
