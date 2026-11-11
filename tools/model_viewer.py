#!/usr/bin/env python3
"""Local model viewer for the GLBs under art/ (SEED-790).

  model_viewer.py [--root DIR] [--port N] [--no-browser]   serve the viewer on 127.0.0.1 and open it in the browser
  model_viewer.py --self-test                               prove the listing, path, measuring and reference rules

One page lists every GLB under art/ and shows the chosen one in <model-viewer> with its clips. Beside it stand what the
file measures (triangles, vertices, joints, clips, textures and their resolution, whether it has a normal map) and its
reference images: the concept art under art/concept/ and the _view renders in the model's own folder whose name starts
with the model's (SK_ExileFounder.glb takes T_ExileFounder*.png). A generated model is judged against those.

The viewer script is the build the art contact sheet pins and comes from a CDN, so the 3D view needs network. A GLB or
image that is still a Git LFS pointer is reported as one, not loaded: `git lfs pull` fetches it. Only .glb and image
files under art/ are served, and only to this machine.
"""
import argparse
import json
import os
import struct
import sys
import urllib.parse
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import art_contact_sheet  # noqa: E402

ART = "art"
CONCEPT = "art/concept"
PORT = 8790
POINTER = b"version https://git-lfs.github.com/spec/"
SERVED = {".glb": "model/gltf-binary", ".png": "image/png", ".jpg": "image/jpeg", ".jpeg": "image/jpeg", ".webp": "image/webp"}
TRIANGLES = 4

PAGE_HEAD = """<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Ichor model viewer</title>
<style>
:root{--bg:#f5f5f4;--fg:#1c1917;--mut:#6b6560;--card:#fff;--bd:#d6d3d1;--acc:#b45309;--warn:#b91c1c;--stage:#cfcbc7}
@media (prefers-color-scheme:dark){:root{--bg:#1c1917;--fg:#f5f5f4;--mut:#a8a29e;--card:#292524;--bd:#44403c;--acc:#fbbf24;--warn:#f87171;--stage:#3a3633}}
*{box-sizing:border-box}
[hidden]{display:none!important}
body{margin:0;height:100vh;display:grid;grid-template-columns:270px minmax(0,1fr) 300px;background:var(--bg);color:var(--fg);font:13px/1.45 system-ui,sans-serif}
nav,aside{overflow:auto;padding:10px}
nav{border-right:1px solid var(--bd)}aside{border-left:1px solid var(--bd)}
main{display:flex;flex-direction:column;min-width:0;min-height:0}
header{display:flex;flex-wrap:wrap;gap:10px;align-items:center;padding:8px 12px;border-bottom:1px solid var(--bd)}
h1{font-size:14px;margin:0;word-break:break-all}h2{font-size:12px;margin:14px 0 4px;color:var(--mut);font-weight:600}
input[type=search],select{font:inherit;color:inherit;background:var(--card);border:1px solid var(--bd);border-radius:6px;padding:4px 8px}
input[type=search]{width:100%}
label{display:flex;gap:5px;align-items:center;color:var(--mut)}
nav button{display:block;width:100%;text-align:left;font:inherit;color:inherit;background:none;border:0;border-radius:4px;padding:2px 6px;cursor:pointer}
nav button:hover{background:var(--card)}nav button[aria-current=true]{color:var(--acc);background:var(--card)}
nav button.pointer{color:var(--mut)}
model-viewer{flex:1;width:100%;min-height:0;background:var(--stage)}
#note{padding:24px;color:var(--mut)}
table{border-collapse:collapse;width:100%}td{padding:1px 8px 1px 0;vertical-align:top}td:first-child{color:var(--mut);white-space:nowrap}
.warn{color:var(--warn)}
aside img{width:100%;display:block;border-radius:4px;margin-bottom:2px}aside figure{margin:0 0 10px}figcaption{font-size:11px;color:var(--mut);word-break:break-all}
@media (max-width:900px){body{grid-template-columns:1fr;grid-template-rows:30vh 60vh auto;height:auto}nav,aside{border:0;border-bottom:1px solid var(--bd)}}
</style>
"""

PAGE_BODY = """
</head><body>
<nav><input type="search" id="filter" placeholder="Filter models" aria-label="Filter models"><div id="list"></div></nav>
<main>
<header><h1 id="name">No model chosen</h1>
<label>Clip <select id="clip"></select></label>
<label><input type="checkbox" id="play" checked> Play</label>
<label><input type="checkbox" id="spin"> Turntable</label></header>
<model-viewer id="mv" camera-controls touch-action="pan-y" shadow-intensity="0.6" interaction-prompt="none" hidden></model-viewer>
<div id="note">Choose a model on the left.</div>
</main>
<aside><h2>Measured</h2><table id="stats"></table><h2>References</h2><div id="refs"></div></aside>
<script>
const $ = (id) => document.getElementById(id);
const mv = $('mv');
let models = [];
let shown = null;

const human = (n) => n < 1024 ? n + ' B' : n < 1048576 ? (n / 1024).toFixed(1) + ' KiB' : (n / 1048576).toFixed(1) + ' MiB';
const url = (path) => '/' + path.split('/').map(encodeURIComponent).join('/');

function row(label, value, warn) {
  const tr = $('stats').insertRow();
  tr.insertCell().textContent = label;
  const td = tr.insertCell();
  td.textContent = value;
  if (warn) td.className = 'warn';
}

function note(text) {
  mv.hidden = true;
  $('note').hidden = false;
  $('note').textContent = text;
}

function drawList() {
  const want = $('filter').value.trim().toLowerCase();
  const list = $('list');
  list.replaceChildren();
  let folder = null;
  for (const m of models) {
    if (want && !m.path.toLowerCase().includes(want)) continue;
    const cut = m.path.lastIndexOf('/');
    if (m.path.slice(0, cut) !== folder) {
      folder = m.path.slice(0, cut);
      const h = document.createElement('h2');
      h.textContent = folder;
      list.append(h);
    }
    const b = document.createElement('button');
    b.textContent = m.path.slice(cut + 1);
    b.title = m.pointer ? 'Git LFS pointer: git lfs pull fetches it' : human(m.size);
    if (m.pointer) b.className = 'pointer';
    b.setAttribute('aria-current', m.path === shown);
    b.onclick = () => show(m.path);
    list.append(b);
  }
  if (!list.children.length) list.textContent = models.length ? 'No model matches.' : 'No GLB under art/.';
}

async function show(path) {
  shown = path;
  history.replaceState(null, '', '#' + encodeURIComponent(path));
  drawList();
  $('name').textContent = path;
  $('stats').replaceChildren();
  $('refs').replaceChildren();
  $('clip').replaceChildren();
  note('Loading...');
  const info = await (await fetch('/api/model?path=' + encodeURIComponent(path))).json();
  if (shown !== path) return;
  row('File', human(info.size));
  if (info.error) {
    row('Not shown', info.error, true);
    note(info.error);
  } else {
    const s = info.stats;
    row('Triangles', s.triangles.toLocaleString('en'));
    row('Vertices', s.vertices.toLocaleString('en'));
    row('Meshes', s.meshes + ' (' + s.primitives + ' primitive' + (s.primitives === 1 ? '' : 's') + ')');
    row('Joints', s.joints);
    row('Clips', s.clips.length ? s.clips.join(', ') : 'none');
    row('Materials', s.materials);
    row('Normal map', s.normal_map ? 'yes' : 'none', !s.normal_map);
    for (const t of s.textures) row('Texture', (t.name || 'unnamed') + ', ' + (t.width ? t.width + ' x ' + t.height : 'size unknown') + ', ' + human(t.bytes));
    if (!s.textures.length) row('Texture', 'none', true);
    $('note').hidden = true;
    mv.hidden = false;
    mv.src = url(path);
  }
  for (const ref of info.references) {
    const fig = document.createElement('figure');
    const img = document.createElement('img');
    img.loading = 'lazy';
    img.src = url(ref);
    img.alt = '';
    const cap = document.createElement('figcaption');
    cap.textContent = ref;
    fig.append(img, cap);
    $('refs').append(fig);
  }
  if (!info.references.length) $('refs').textContent = 'No concept art or _view render is named after this model.';
}

function playClip() {
  if (!$('clip').value) return;
  mv.animationName = $('clip').value;
  if ($('play').checked) mv.play(); else mv.pause();
}

mv.addEventListener('load', () => {
  const names = mv.availableAnimations;
  $('clip').replaceChildren(...names.map((n) => new Option(n, n)));
  if (names.includes('idle')) $('clip').value = 'idle';
  playClip();
  const d = mv.getDimensions();
  row('Size', d.x.toFixed(2) + ' x ' + d.y.toFixed(2) + ' x ' + d.z.toFixed(2) + ' m');
});
mv.addEventListener('error', () => note('The viewer could not load this model.'));
$('clip').onchange = playClip;
$('play').onchange = playClip;
$('spin').onchange = () => { mv.autoRotate = $('spin').checked; };
$('filter').oninput = drawList;

function showAsked() {
  const asked = decodeURIComponent(location.hash.slice(1));
  if (asked !== shown && models.some((m) => m.path === asked)) show(asked);
}
window.onhashchange = showAsked;

fetch('/api/models').then((r) => r.json()).then((list) => {
  models = list;
  drawList();
  showAsked();
});
</script>
</body></html>
"""

PAGE = PAGE_HEAD + art_contact_sheet.MODEL_VIEWER + PAGE_BODY


def is_pointer(path):
    """A Git LFS pointer is a short text file; the object it names is fetched by `git lfs pull`."""
    try:
        if os.path.getsize(path) > 1024:
            return False
        with open(path, "rb") as fh:
            return fh.read(len(POINTER)) == POINTER
    except OSError:
        return False


def file_size(path):
    """The size of the file, or of the LFS object when the file is still its pointer."""
    with open(path, "rb") as fh:
        head = fh.read(1024)
    return art_contact_sheet.lfs_size(head) if head.startswith(POINTER) else os.path.getsize(path)


def rel(root, path):
    return os.path.relpath(path, root).replace(os.sep, "/")


def list_models(root):
    """Every .glb under art/, by path."""
    models = []
    for folder, _, names in os.walk(os.path.join(root, ART)):
        for name in names:
            if name.lower().endswith(".glb"):
                path = os.path.join(folder, name)
                models.append({"path": rel(root, path), "size": file_size(path), "pointer": is_pointer(path)})
    return sorted(models, key=lambda m: m["path"])


def resolve(root, asked):
    """The file a request names, or None unless it is a served kind of file that really lies under art/."""
    if os.path.splitext(asked)[1].lower() not in SERVED:
        return None
    art = os.path.realpath(os.path.join(root, ART))
    path = os.path.realpath(os.path.join(root, asked))
    try:
        inside = os.path.commonpath([art, path]) == art
    except ValueError:
        return None
    return path if inside and path != art and os.path.isfile(path) else None


def image_size(data):
    """(width, height) of a PNG or JPEG, else None."""
    if data[:8] == b"\x89PNG\r\n\x1a\n" and len(data) >= 24:
        return struct.unpack(">II", data[16:24])
    if data[:2] == b"\xff\xd8":
        i = 2
        while i + 9 <= len(data) and data[i] == 0xFF:
            marker, length = data[i + 1], struct.unpack(">H", data[i + 2:i + 4])[0]
            if 0xC0 <= marker <= 0xCF and marker not in (0xC4, 0xC8, 0xCC):
                height, width = struct.unpack(">HH", data[i + 5:i + 9])
                return width, height
            i += 2 + length
    return None


def measure(blob):
    """What a GLB holds. Raises ValueError when the bytes are not a GLB."""
    if len(blob) < 20 or blob[:4] != b"glTF" or blob[16:20] != b"JSON":
        raise ValueError("not a GLB file")
    json_length = struct.unpack("<I", blob[12:16])[0]
    try:
        doc = json.loads(blob[20:20 + json_length])
    except ValueError as error:
        raise ValueError(f"unreadable GLB header: {error}")
    binary = blob[20 + json_length + 8:]
    accessors = doc.get("accessors", [])
    triangles = vertices = primitives = 0
    for mesh in doc.get("meshes", []):
        for primitive in mesh.get("primitives", []):
            primitives += 1
            count = accessors[primitive["attributes"]["POSITION"]]["count"]
            vertices += count
            if primitive.get("mode", TRIANGLES) == TRIANGLES:
                triangles += (accessors[primitive["indices"]]["count"] if "indices" in primitive else count) // 3
    textures = []
    for image in doc.get("images", []):
        entry = {"name": image.get("name") or image.get("uri"), "width": None, "height": None, "bytes": 0}
        if "bufferView" in image:
            view = doc["bufferViews"][image["bufferView"]]
            start = view.get("byteOffset", 0)
            entry["bytes"] = view["byteLength"]
            size = image_size(binary[start:start + min(view["byteLength"], 65536)])
            if size:
                entry["width"], entry["height"] = size
        textures.append(entry)
    materials = doc.get("materials", [])
    return {
        "triangles": triangles,
        "vertices": vertices,
        "meshes": len(doc.get("meshes", [])),
        "primitives": primitives,
        "joints": sum(len(skin.get("joints", [])) for skin in doc.get("skins", [])),
        "clips": [clip.get("name") or f"clip {i}" for i, clip in enumerate(doc.get("animations", []))],
        "materials": len(materials),
        "normal_map": any("normalTexture" in material for material in materials),
        "textures": textures,
    }


def references(root, model):
    """Concept art, then the model folder's _view renders, whose name starts with the model's. LFS pointers are left out."""
    stem = os.path.splitext(os.path.basename(model))[0]
    prefix = "T_" + (stem[3:] if stem[:3] in ("SM_", "SK_") else stem)
    found = []
    for folder, _, names in os.walk(os.path.join(root, CONCEPT)):
        found += [os.path.join(folder, n) for n in names if n.startswith(prefix) and n.lower().endswith(".png")]
    concept = sorted(rel(root, p) for p in found if not is_pointer(p))
    own = os.path.join(root, os.path.dirname(model))
    views = [os.path.join(own, n) for n in os.listdir(own) if n.startswith(prefix) and n.endswith("_view.png")]
    return concept + sorted(rel(root, p) for p in views if not is_pointer(p))


def describe(root, asked):
    """The /api/model answer: size, references and either the stats or why the model is not shown. None for a bad path."""
    path = resolve(root, asked)
    if path is None or not path.lower().endswith(".glb"):
        return None
    model = rel(root, path)
    answer = {"path": model, "size": file_size(path), "references": references(root, model)}
    if is_pointer(path):
        answer["error"] = "Git LFS pointer: run `git lfs pull` to fetch the model"
        return answer
    try:
        with open(path, "rb") as fh:
            answer["stats"] = measure(fh.read())
    except (ValueError, KeyError, IndexError, TypeError, struct.error) as error:
        answer["error"] = f"cannot read this GLB: {error}"
    return answer


def handler_for(root):
    class Handler(BaseHTTPRequestHandler):
        def reply(self, status, kind, body):
            self.send_response(status)
            self.send_header("Content-Type", kind)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def reply_json(self, value):
            self.reply(200, "application/json", json.dumps(value).encode("utf-8"))

        def do_GET(self):
            url = urllib.parse.urlsplit(self.path)
            path = urllib.parse.unquote(url.path)
            if path == "/":
                return self.reply(200, "text/html; charset=utf-8", PAGE.encode("utf-8"))
            if path == "/api/models":
                return self.reply_json(list_models(root))
            if path == "/api/model":
                answer = describe(root, urllib.parse.parse_qs(url.query).get("path", [""])[0])
                return self.reply_json(answer) if answer else self.reply(404, "text/plain", b"no such model under art/\n")
            found = resolve(root, path.lstrip("/"))
            if found is None:
                return self.reply(404, "text/plain", b"not found\n")
            with open(found, "rb") as fh:
                return self.reply(200, SERVED[os.path.splitext(found)[1].lower()], fh.read())

        def log_message(self, format, *args):
            pass

    return Handler


def serve(root, port, browser):
    try:
        server = ThreadingHTTPServer(("127.0.0.1", port), handler_for(root))
    except OSError as error:
        print(f"model-viewer: cannot listen on port {port}: {error} (pass --port)", file=sys.stderr)
        return 1
    address = f"http://127.0.0.1:{port}/"
    print(f"model-viewer: {len(list_models(root))} model(s) under {os.path.join(root, ART)}, serving {address} (Ctrl+C stops it)", flush=True)
    if browser:
        webbrowser.open(address)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


def make_glb(doc, binary=b""):
    """A GLB from a glTF document and its binary chunk, for the self-test."""
    text = json.dumps(doc).encode("utf-8")
    text += b" " * (-len(text) % 4)
    binary += b"\0" * (-len(binary) % 4)
    chunks = struct.pack("<I4s", len(text), b"JSON") + text
    if binary:
        chunks += struct.pack("<I4s", len(binary), b"BIN\0") + binary
    return struct.pack("<4sII", b"glTF", 2, 12 + len(chunks)) + chunks


def self_test(out):
    import tempfile
    import threading
    import urllib.error
    import urllib.request

    failures = []

    def check(name, got, want):
        if got != want:
            failures.append(f"{name}: got {got!r}, want {want!r}")

    png = b"\x89PNG\r\n\x1a\n" + struct.pack(">I4sII", 13, b"IHDR", 2048, 1024) + b"\x08\x02\0\0\0"
    jpeg = b"\xff\xd8\xff\xe0\x00\x04\0\0\xff\xc0\x00\x11\x08" + struct.pack(">HH", 600, 800) + b"\x03"
    check("PNG size", image_size(png), (2048, 1024))
    check("JPEG size is width then height", image_size(jpeg), (800, 600))
    check("unknown image has no size", image_size(b"RIFF....WEBP"), None)

    doc = {
        "accessors": [{"count": 30}, {"count": 12}, {"count": 9}, {"count": 4}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "indices": 1}, {"attributes": {"POSITION": 2}}]},
                   {"primitives": [{"attributes": {"POSITION": 3}, "mode": 1}]}],
        "skins": [{"joints": [0, 1, 2]}],
        "animations": [{"name": "idle"}, {}],
        "materials": [{"name": "a"}, {"name": "b"}],
        "images": [{"name": "T_Base", "bufferView": 0}, {"uri": "outside.png"}],
        "bufferViews": [{"byteOffset": 4, "byteLength": len(png)}],
    }
    stats = measure(make_glb(doc, b"\0\0\0\0" + png))
    check("indexed triangles, then unindexed, lines not counted", stats["triangles"], 4 + 3)
    check("vertices of every primitive", stats["vertices"], 43)
    check("meshes and primitives", (stats["meshes"], stats["primitives"]), (2, 3))
    check("joints", stats["joints"], 3)
    check("an unnamed clip is numbered", stats["clips"], ["idle", "clip 1"])
    check("materials", stats["materials"], 2)
    check("no normal map", stats["normal_map"], False)
    check("embedded texture is measured, external one is listed", stats["textures"],
          [{"name": "T_Base", "width": 2048, "height": 1024, "bytes": len(png)},
           {"name": "outside.png", "width": None, "height": None, "bytes": 0}])
    doc["materials"][1]["normalTexture"] = {"index": 0}
    check("a normal map on any material counts", measure(make_glb(doc, b"\0\0\0\0" + png))["normal_map"], True)
    check("an empty GLB measures as nothing", measure(make_glb({}))["triangles"], 0)
    try:
        measure(b"not a model at all, just text")
        check("bytes that are not a GLB are refused", "accepted", "ValueError")
    except ValueError:
        pass

    pointer = POINTER + b"v1\noid sha256:" + b"0" * 64 + b"\nsize 22249024\n"
    with tempfile.TemporaryDirectory() as folder:
        root = os.path.realpath(folder)
        files = {
            "art/exile/SK_Exile.glb": make_glb(doc, b"\0\0\0\0" + png),
            "art/exile/SK_ExileFounder.glb": pointer,
            "art/exile/SM_Broken.GLB": b"glTF but cut short",
            "art/exile/SK_Exile.fbx": b"fbx",
            "art/exile/T_ExileTextured_view.png": png,
            "art/exile/T_ExileBaseColor.png": png,
            "art/exile/T_ExilePointer_view.png": pointer,
            "art/concept/characters/T_ExileFounderFront.png": png,
            "art/concept/characters/T_ExileConquerorSide.png": png,
            "art/concept/gear/T_Exile.png": png,
            "art/concept/gear/T_Spear.png": png,
            "art/notes.txt": b"x",
            "engine/secret.png": png,
            "outside.glb": make_glb({}),
        }
        for name, data in files.items():
            os.makedirs(os.path.dirname(os.path.join(root, name)), exist_ok=True)
            with open(os.path.join(root, name), "wb") as fh:
                fh.write(data)

        check("only GLBs under art/ are listed, by path, a pointer with its object's size", list_models(root),
              [{"path": "art/exile/SK_Exile.glb", "size": len(files["art/exile/SK_Exile.glb"]), "pointer": False},
               {"path": "art/exile/SK_ExileFounder.glb", "size": 22249024, "pointer": True},
               {"path": "art/exile/SM_Broken.GLB", "size": 18, "pointer": False}])

        check("a model under art/ resolves", resolve(root, "art/exile/SK_Exile.glb"),
              os.path.realpath(os.path.join(root, "art/exile/SK_Exile.glb")))
        for bad in ("art/../outside.glb", "outside.glb", "engine/secret.png", "art/exile/../../engine/secret.png",
                    "art/notes.txt", "art/exile/SK_Exile.fbx", "art/exile/SK_Missing.glb", "art", "", "/etc/passwd.png",
                    os.path.join(root, "outside.glb")):
            check(f"{bad!r} is refused", resolve(root, bad), None)

        check("references: concept art by name, then the folder's _view renders, no pointers, no plain textures",
              references(root, "art/exile/SK_Exile.glb"),
              ["art/concept/characters/T_ExileConquerorSide.png", "art/concept/characters/T_ExileFounderFront.png",
               "art/concept/gear/T_Exile.png", "art/exile/T_ExileTextured_view.png"])
        check("a longer model name narrows the references", references(root, "art/exile/SK_ExileFounder.glb"),
              ["art/concept/characters/T_ExileFounderFront.png"])
        check("a model nothing is named after has none", references(root, "art/exile/SM_Broken.GLB"), [])

        check("a model is described with its stats", describe(root, "art/exile/SK_Exile.glb")["stats"]["joints"], 3)
        founder = describe(root, "art/exile/SK_ExileFounder.glb")
        check("a pointer is reported, not measured", ("stats" in founder, founder["error"].startswith("Git LFS pointer")), (False, True))
        check("a pointer still shows its references", founder["references"], ["art/concept/characters/T_ExileFounderFront.png"])
        check("a broken GLB is reported", describe(root, "art/exile/SM_Broken.GLB")["error"], "cannot read this GLB: not a GLB file")
        check("an image is not a model", describe(root, "art/exile/T_ExileTextured_view.png"), None)
        check("a path outside art/ is not described", describe(root, "outside.glb"), None)

        server = ThreadingHTTPServer(("127.0.0.1", 0), handler_for(root))
        threading.Thread(target=server.serve_forever, daemon=True).start()
        base = f"http://127.0.0.1:{server.server_address[1]}"

        def get(path):
            try:
                with urllib.request.urlopen(base + path) as response:
                    return response.status, response.headers["Content-Type"], response.read()
            except urllib.error.HTTPError as error:
                return error.code, error.headers["Content-Type"], error.read()

        status, kind, body = get("/")
        check("the page is served", (status, kind), (200, "text/html; charset=utf-8"))
        check("the page pins the viewer the contact sheet pins", art_contact_sheet.MODEL_VIEWER in body.decode("utf-8"), True)
        check("the model list is served", [m["path"] for m in json.loads(get("/api/models")[2])],
              ["art/exile/SK_Exile.glb", "art/exile/SK_ExileFounder.glb", "art/exile/SM_Broken.GLB"])
        check("a model is described over HTTP", json.loads(get("/api/model?path=art%2Fexile%2FSK_Exile.glb")[2])["stats"]["triangles"], 7)
        check("an unknown model is a 404", get("/api/model?path=art/exile/SK_Missing.glb")[0], 404)
        check("a model outside art/ is a 404", get("/api/model?path=outside.glb")[0], 404)
        check("a GLB is served with its type", get("/art/exile/SK_Exile.glb")[:2], (200, "model/gltf-binary"))
        check("a GLB is served whole", get("/art/exile/SK_Exile.glb")[2], files["art/exile/SK_Exile.glb"])
        check("an image is served", get("/art/concept/gear/T_Exile.png")[:2], (200, "image/png"))
        for bad in ("/art/../outside.glb", "/art/%2e%2e/outside.glb", "/outside.glb", "/engine/secret.png", "/art/notes.txt",
                    "/art/exile/SK_Exile.fbx", "/tools/model_viewer.py"):
            check(f"GET {bad} is a 404", get(bad)[0], 404)
        server.shutdown()
        server.server_close()

    for f in failures:
        print(f"model-viewer self-test FAILED: {f}", file=out)
    if not failures:
        print("model-viewer self-test: ok", file=out)
    return 1 if failures else 0


def main(argv):
    ap = argparse.ArgumentParser(description="Local viewer for the GLB models under art/")
    ap.add_argument("--root", default=str(Path(__file__).resolve().parent.parent), help="repository root (default: this repository)")
    ap.add_argument("--port", type=int, default=PORT)
    ap.add_argument("--no-browser", action="store_true", help="do not open the page in the browser")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args(argv)
    if args.self_test:
        return self_test(sys.stdout)
    if not os.path.isdir(os.path.join(args.root, ART)):
        ap.error(f"{args.root} has no {ART}/ folder")
    return serve(os.path.realpath(args.root), args.port, not args.no_browser)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
