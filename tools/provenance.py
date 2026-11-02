"""Provenance sidecars (SEED-774): `<asset>.provenance.json` next to a model, rig or animation file.

Stdlib only, so the art pipeline scripts (Blender, retargeting, clip jobs) can import it:

    sys.path.insert(0, str(Path(repo) / "tools"))
    import provenance
    provenance.write_sidecar("art/exile/SK_Exile.glb", source="Quaternius Universal Animation Library",
                             licence="CC0-1.0", source_file="downloads/UAL.glb",
                             clip_origins={"Idle": {"source": "UAL idle", "licence": "CC0-1.0"}})

The sidecar holds source (where it came from), licence (CC0-1.0 or own-work), source_file_sha256 (sha256 of the
file it was derived from) and clip_origins (clip name -> {source, licence}). tools/check_assets_licensed.py
validates every sidecar and fails on any other licence or on any mention of HY-Motion.
"""
import hashlib
import json
import re
from pathlib import Path

SUFFIX = ".provenance.json"
SIDECAR_ASSET_EXTS = (".glb", ".gltf", ".fbx", ".bvh")
ALLOWED_LICENCES = ("CC0-1.0", "own-work")
FIELDS = ("source", "licence", "source_file_sha256", "clip_origins")
BANNED = re.compile(r"hy[\s_-]*motion", re.IGNORECASE)
HEX64 = re.compile(r"[0-9a-f]{64}")


def sidecar_path(asset):
    asset = Path(asset)
    return asset.with_name(asset.name + SUFFIX)


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def build_sidecar(source, licence, source_file_sha256, clip_origins=None):
    return {"source": source, "licence": licence, "source_file_sha256": source_file_sha256,
            "clip_origins": dict(clip_origins or {})}


def write_sidecar(asset, source, licence, source_file=None, source_file_sha256=None, clip_origins=None):
    """Write `<asset>.provenance.json`; pass the file the asset was derived from (hashed here) or its digest."""
    if source_file is not None:
        source_file_sha256 = sha256_file(source_file)
    data = build_sidecar(source, licence, source_file_sha256, clip_origins)
    problems = sidecar_problems(data)
    if problems:
        raise ValueError(f"{asset}: " + "; ".join(problems))
    out = sidecar_path(asset)
    out.write_text(json.dumps(data, indent=2, ensure_ascii=False, sort_keys=True) + "\n", encoding="utf-8",
                   newline="\n")
    return out


def strings(value):
    if isinstance(value, str):
        yield value
    elif isinstance(value, dict):
        for k, v in value.items():
            yield from strings(k)
            yield from strings(v)
    elif isinstance(value, list):
        for v in value:
            yield from strings(v)


def licence_problem(value, where="licence"):
    if value in ALLOWED_LICENCES:
        return None
    return f"{where} {value!r} is not allowed (allowed: {', '.join(ALLOWED_LICENCES)})"


def sidecar_problems(data):
    if not isinstance(data, dict):
        return ["sidecar must be a JSON object"]
    problems = []
    if any(BANNED.search(s) for s in strings(data)):
        problems.append("names HY-Motion, whose licence excludes the EU")
    for field in FIELDS:
        if field not in data:
            problems.append(f"missing field '{field}'")
    if not isinstance(data.get("source"), str) or not data.get("source", "").strip():
        problems.append("source must be a non-empty string")
    if "licence" in data:
        problem = licence_problem(data["licence"])
        if problem:
            problems.append(problem)
    digest = data.get("source_file_sha256")
    if "source_file_sha256" in data and not (isinstance(digest, str) and HEX64.fullmatch(digest)):
        problems.append("source_file_sha256 must be a 64-digit lowercase hex digest")
    clips = data.get("clip_origins")
    if "clip_origins" in data and not isinstance(clips, dict):
        problems.append("clip_origins must map clip name to {source, licence}")
    elif isinstance(clips, dict):
        for name, origin in clips.items():
            if not isinstance(origin, dict) or not isinstance(origin.get("source"), str) \
                    or not origin["source"].strip():
                problems.append(f"clip_origins[{name!r}] needs a source string")
                continue
            problem = licence_problem(origin.get("licence"), f"clip_origins[{name!r}] licence")
            if problem:
                problems.append(problem)
    return problems


def load_problems(path):
    try:
        data = json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, ValueError) as e:
        return [f"not readable JSON: {e}"]
    return sidecar_problems(data)


def scan(files):
    """files: relative posix paths. Returns (errors as (path, rule, msg), assets that have no sidecar)."""
    present = set(files)
    errors, missing = [], []
    for f in sorted(present):
        if f.endswith(SUFFIX):
            if f[:-len(SUFFIX)] not in present:
                errors.append((f, "provenance", "sidecar has no asset next to it: delete it"))
        elif f.lower().endswith(SIDECAR_ASSET_EXTS) and f + SUFFIX not in present:
            missing.append(f)
    return errors, missing
