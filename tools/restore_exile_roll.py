#!/usr/bin/env python3
"""Puts the source clips' bone roll back into art/exile/SK_Exile.glb (SEED-717).

art/blender/export_glb.py copies each clip onto the mesh rig by joint direction only ("swing only"), so every
bone's roll about its own axis followed the bind pose instead of the clip: the elbows bent against the skinned
upper arm and the forearms wrapped up to 156 degrees. The joint directions it wrote are right. This rewrites
the rotation keys of every mixamorig bone from the SK_Exile_<Clip>.fbx files, leaving translations, the root
bone, the mesh and the skin untouched, so the joint positions stay where they were.

The map from the FBX to the GLB: SK_Exile.fbx's BindPose gives each joint's bind rotation in the FBX world, the
GLB's node rest gives it in the GLB, and one fixed rotation takes the first to the second for every joint (the
local bind rotations are identical; the script stops if any joint is more than 0.01 degrees off). A clip's world
rotation from its Lcl curves, turned by that map, is the GLB world rotation; the GLB parent's then gives the key.

Usage: restore_exile_roll.py [--folder art/exile] [--out FILE]   rewrite the GLB (default: in place)
       restore_exile_roll.py --report                              print per clip what would change, write nothing
Same input bytes give the same output bytes. Plain Python, no packages.
"""
import argparse
import json
import math
import struct
import sys
import zlib
from pathlib import Path

TICKS_PER_SECOND = 46186158000
FPS = 24
CLIPS = ("Idle", "Walk", "Run", "Swing", "Thrust", "Block", "Dodge", "Hit", "Death")
UNSUPPORTED = ("PreRotation", "PostRotation", "RotationOffset", "RotationPivot", "ScalingOffset", "ScalingPivot")


# Quaternions are [x, y, z, w].
def qmul(p, q):
    x1, y1, z1, w1 = p
    x2, y2, z2, w2 = q
    return [w1 * x2 + x1 * w2 + y1 * z2 - z1 * y2, w1 * y2 - x1 * z2 + y1 * w2 + z1 * x2,
            w1 * z2 + x1 * y2 - y1 * x2 + z1 * w2, w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2]


def qinv(q):
    return [-q[0], -q[1], -q[2], q[3]]


def qnorm(q):
    n = math.sqrt(sum(x * x for x in q))
    return [x / n for x in q]


def qrot(q, v):
    return qmul(qmul(q, [v[0], v[1], v[2], 0.0]), qinv(q))[:3]


def qangle(p, q):
    return math.degrees(2.0 * math.acos(min(1.0, abs(sum(a * b for a, b in zip(p, q))))))


def euler_xyz(deg):
    """FBX eEulerXYZ: rotate about X, then Y, then Z."""
    out = [0.0, 0.0, 0.0, 1.0]
    for axis in (2, 1, 0):
        half = math.radians(deg[axis]) / 2.0
        q = [0.0, 0.0, 0.0, math.cos(half)]
        q[axis] = math.sin(half)
        out = qmul(out, q)
    return out


def matrix_rotation(m):
    """Rotation of an FBX 4x4 (column-major, columns may carry scale)."""
    cols = []
    for c in range(3):
        v = m[4 * c:4 * c + 3]
        n = math.sqrt(sum(x * x for x in v))
        cols.append([x / n for x in v])
    r = [[cols[c][r] for c in range(3)] for r in range(3)]
    trace = r[0][0] + r[1][1] + r[2][2]
    if trace > 0:
        s = math.sqrt(trace + 1.0) * 2.0
        return qnorm([(r[2][1] - r[1][2]) / s, (r[0][2] - r[2][0]) / s, (r[1][0] - r[0][1]) / s, s / 4.0])
    i = max(range(3), key=lambda k: r[k][k])
    j, k = (i + 1) % 3, (i + 2) % 3
    s = math.sqrt(1.0 + r[i][i] - r[j][j] - r[k][k]) * 2.0
    q = [0.0, 0.0, 0.0, 0.0]
    q[i] = s / 4.0
    q[j] = (r[j][i] + r[i][j]) / s
    q[k] = (r[k][i] + r[i][k]) / s
    q[3] = (r[k][j] - r[j][k]) / s
    return qnorm(q)


def read_fbx(path):
    """Binary FBX 7.x as nested (name, properties, children) tuples."""
    data = Path(path).read_bytes()
    if not data.startswith(b"Kaydara FBX Binary  \x00"):
        raise SystemExit(f"{path}: not a binary FBX")
    version = struct.unpack_from("<I", data, 23)[0]
    wide = version >= 7500
    scalar = {"Y": "<h", "C": "<?", "I": "<i", "L": "<q", "F": "<f", "D": "<d"}
    array = {"f": "f", "d": "d", "l": "q", "i": "i", "b": "?"}

    def prop(o):
        kind = chr(data[o])
        o += 1
        if kind in scalar:
            return struct.unpack_from(scalar[kind], data, o)[0], o + struct.calcsize(scalar[kind])
        if kind in array:
            count, encoding, size = struct.unpack_from("<III", data, o)
            raw = data[o + 12:o + 12 + size]
            if encoding:
                raw = zlib.decompress(raw)
            return list(struct.unpack(f"<{count}{array[kind]}", raw)), o + 12 + size
        if kind in "SR":
            size = struct.unpack_from("<I", data, o)[0]
            return data[o + 4:o + 4 + size], o + 4 + size
        raise SystemExit(f"{path}: unknown property type {kind!r}")

    def node(o):
        end, count, _ = struct.unpack_from("<QQQ" if wide else "<III", data, o)
        o += 24 if wide else 12
        if end == 0:
            return None, o
        name = data[o + 1:o + 1 + data[o]].decode()
        o += 1 + data[o]
        props = []
        for _ in range(count):
            value, o = prop(o)
            props.append(value)
        children = []
        while o < end:
            child, o = node(o)
            if child is None:
                break
            children.append(child)
        return (name, props, children), end

    top, o = [], 27
    while True:
        n, o = node(o)
        if n is None:
            return top
        top.append(n)


def child(n, name):
    return [c for c in n[2] if c[0] == name]


class FbxRig:
    """Joints, parents, Lcl curves and the BindPose of one FBX file."""

    def __init__(self, path):
        top = read_fbx(path)
        objects = next(n for n in top if n[0] == "Objects")[2]
        self.path = path
        self.models, curve_nodes, curves, self.bind = {}, {}, {}, {}
        for o in objects:
            uid = o[1][0]
            if o[0] == "Model":
                props = {}
                for p in child(o, "Properties70")[0][2] if child(o, "Properties70") else []:
                    props[p[1][0].decode()] = p[1][4:]
                for key in UNSUPPORTED:
                    if key in props and any(abs(v) > 1e-6 for v in props[key]):
                        raise SystemExit(f"{path}: {key} on a joint is not supported")
                if props.get("RotationOrder", [0])[0] != 0:
                    raise SystemExit(f"{path}: only XYZ rotation order is supported")
                self.models[uid] = {"name": o[1][1].split(b"\x00")[0].decode(), "props": props, "anim": {}}
            elif o[0] == "AnimationCurveNode":
                curve_nodes[uid] = {}
            elif o[0] == "AnimationCurve":
                keys = {c[0]: c[1][0] for c in o[2] if c[1]}
                curves[uid] = (keys["KeyTime"], keys["KeyValueFloat"])
            elif o[0] == "Pose":
                for pose_node in child(o, "PoseNode"):
                    fields = {c[0]: c[1][0] for c in pose_node[2]}
                    self.bind[fields["Node"]] = fields["Matrix"]
        self.parent = {}
        for c in next(n for n in top if n[0] == "Connections")[2]:
            kind, a, b = c[1][0].decode(), c[1][1], c[1][2]
            if kind == "OO" and a in self.models and b in self.models:
                self.parent[a] = b
            elif kind == "OP" and a in curves and b in curve_nodes:
                curve_nodes[b][c[1][3].decode()] = curves[a]
            elif kind == "OP" and a in curve_nodes and b in self.models:
                self.models[b]["anim"][c[1][3].decode()] = curve_nodes[a]
        self.by_name = {m["name"]: uid for uid, m in self.models.items()}

    def frames(self):
        """Key count of the clip; every rotation curve must hold one key per 1/24 s frame."""
        counts = set()
        starts = set()
        for m in self.models.values():
            for channel in m["anim"].get("Lcl Rotation", {}).values():
                times = channel[0]
                step = TICKS_PER_SECOND // FPS
                if any(b - a != step for a, b in zip(times, times[1:])):
                    raise SystemExit(f"{self.path}: {m['name']} is not keyed once per frame at {FPS} fps")
                counts.add(len(times))
                starts.add(times[0])
        if len(counts) != 1 or len(starts) != 1:
            raise SystemExit(f"{self.path}: rotation curves differ in length {sorted(counts)} or start {sorted(starts)}")
        return counts.pop()

    def local_rotation(self, uid, frame):
        m = self.models[uid]
        value = list(m["props"].get("Lcl Rotation", [0.0, 0.0, 0.0]))
        for axis, letter in enumerate("XYZ"):
            channel = m["anim"].get("Lcl Rotation", {}).get("d|" + letter)
            if channel:
                value[axis] = channel[1][frame]
        return euler_xyz(value)

    def world_rotation(self, uid, frame, cache):
        if uid not in cache:
            local = self.local_rotation(uid, frame)
            p = self.parent.get(uid)
            cache[uid] = qnorm(qmul(self.world_rotation(p, frame, cache), local)) if p in self.models else local
        return cache[uid]


class Glb:
    def __init__(self, path):
        self.data = bytearray(Path(path).read_bytes())
        length = struct.unpack_from("<I", self.data, 12)[0]
        self.gltf = json.loads(self.data[20:20 + length])
        self.bin = 20 + length + 8
        nodes = self.gltf["nodes"]
        self.parent = {c: i for i, n in enumerate(nodes) for c in n.get("children", [])}
        self.index = {n.get("name", ""): i for i, n in enumerate(nodes)}

    def accessor(self, i):
        a = self.gltf["accessors"][i]
        if a["componentType"] != 5126:
            raise SystemExit("only float accessors are supported")
        view = self.gltf["bufferViews"][a["bufferView"]]
        if view.get("byteStride", 0) not in (0, {"SCALAR": 4, "VEC3": 12, "VEC4": 16}[a["type"]]):
            raise SystemExit("interleaved accessors are not supported")
        width = {"SCALAR": 1, "VEC3": 3, "VEC4": 4}[a["type"]]
        offset = self.bin + view.get("byteOffset", 0) + a.get("byteOffset", 0)
        return offset, width, a["count"]

    def read(self, i):
        offset, width, count = self.accessor(i)
        flat = struct.unpack_from(f"<{count * width}f", self.data, offset)
        return [list(flat[k * width:(k + 1) * width]) for k in range(count)]

    def write(self, i, values):
        offset, width, count = self.accessor(i)
        struct.pack_into(f"<{count * width}f", self.data, offset, *[x for v in values for x in v])

    def rest(self):
        return {i: [list(n.get("translation", [0.0, 0.0, 0.0])), list(n.get("rotation", [0.0, 0.0, 0.0, 1.0]))]
                for i, n in enumerate(self.gltf["nodes"])}

    def world(self, i, trs, cache):
        """(position, rotation) of node i; the Exile's scales are 1."""
        if i not in cache:
            t, r = trs[i]
            if i in self.parent:
                pp, pr = self.world(self.parent[i], trs, cache)
                cache[i] = ([a + b for a, b in zip(pp, qrot(pr, t))], qnorm(qmul(pr, r)))
            else:
                cache[i] = (list(t), qnorm(list(r)))
        return cache[i]


def bind_map(glb, mesh):
    """The rotation from the FBX world to the GLB world, checked against every joint's bind."""
    rest = glb.rest()
    hips = mesh.by_name["mixamorig:Hips"]
    fbx_hips = matrix_rotation(mesh.bind[hips])
    glb_hips = glb.world(glb.index["mixamorig:Hips"], rest, {})[1]
    to_glb = qnorm(qmul(glb_hips, qinv(fbx_hips)))
    worst = 0.0
    for name, uid in mesh.by_name.items():
        if not name.startswith("mixamorig:") or uid not in mesh.bind or name not in glb.index:
            continue
        mapped = qmul(to_glb, matrix_rotation(mesh.bind[uid]))
        worst = max(worst, qangle(mapped, glb.world(glb.index[name], rest, {})[1]))
    if worst > 0.01:
        raise SystemExit(f"the FBX bind does not map onto the GLB bind: {worst:.4f} degrees off")
    return to_glb, worst


def restore_clip(glb, animation, rig, to_glb):
    """New rotation keys per GLB accessor, plus the largest roll change and joint move for the report."""
    frames = rig.frames()
    rotation = {}
    translation = {}
    times = None
    for channel in animation["channels"]:
        node = channel["target"]["node"]
        sampler = animation["samplers"][channel["sampler"]]
        if sampler.get("interpolation", "LINEAR") != "LINEAR":
            raise SystemExit(f"{animation['name']}: only linear keys are supported")
        keys = [t[0] for t in glb.read(sampler["input"])]
        if times is None:
            times = keys
        elif keys != times:
            raise SystemExit(f"{animation['name']}: channels have different key times")
        if channel["target"]["path"] == "rotation":
            rotation[node] = sampler["output"]
        elif channel["target"]["path"] == "translation":
            translation[node] = sampler["output"]
    if len(times) != frames or any(abs(t - k / FPS) > 1e-4 for k, t in enumerate(times)):
        raise SystemExit(f"{animation['name']}: {len(times)} GLB keys against {frames} FBX frames at {FPS} fps")

    old = {node: glb.read(acc) for node, acc in rotation.items()}
    moved = {node: glb.read(acc) for node, acc in translation.items()}
    new = {node: [list(q) for q in keys] for node, keys in old.items()}
    order = sorted(rotation, key=lambda n: depth(glb, n))
    roll = 0.0
    for k in range(frames):
        src_cache = {}
        world = {}
        for node in order:
            name = glb.gltf["nodes"][node].get("name", "")
            if not name.startswith("mixamorig:"):
                continue
            if name not in rig.by_name:
                raise SystemExit(f"{rig.path}: no {name}")
            target = qnorm(qmul(to_glb, rig.world_rotation(rig.by_name[name], k, src_cache)))
            parent = glb.parent.get(node)
            parent_world = world.get(parent) if parent in world else animated_world(glb, parent, k, old)
            q = qnorm(qmul(qinv(parent_world), target))
            # Each key on the side of the one before, so a viewer that blends keys componentwise takes the short way.
            if sum(a * b for a, b in zip(q, new[node][k - 1] if k > 0 else old[node][k])) < 0:
                q = [-x for x in q]
            world[node] = target
            roll = max(roll, qangle(q, old[node][k]))
            new[node][k] = q
    return new, roll, joint_shift(glb, old, new, moved, frames)


def depth(glb, node):
    d = 0
    while node in glb.parent:
        node = glb.parent[node]
        d += 1
    return d


def animated_world(glb, node, k, keys):
    """World rotation of a node that is not rewritten (the root bone), from its own keys."""
    if node is None:
        return [0.0, 0.0, 0.0, 1.0]
    local = keys[node][k] if node in keys else glb.rest()[node][1]
    return qnorm(qmul(animated_world(glb, glb.parent.get(node), k, keys), local))


def joint_shift(glb, old, new, moved, frames):
    worst = 0.0
    base = glb.rest()
    for k in range(frames):
        poses = []
        for keys in (old, new):
            trs = {i: [list(v) for v in tr] for i, tr in base.items()}
            for node, q in keys.items():
                trs[node][1] = q[k]
            for node, t in moved.items():
                trs[node][0] = t[k]
            cache = {}
            poses.append({i: glb.world(i, trs, cache)[0] for i in trs})
        for i in poses[0]:
            worst = max(worst, math.dist(poses[0][i], poses[1][i]))
    return worst


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--folder", default=str(Path(__file__).resolve().parents[1] / "art" / "exile"))
    parser.add_argument("--out")
    parser.add_argument("--report", action="store_true")
    args = parser.parse_args()
    folder = Path(args.folder)
    glb = Glb(folder / "SK_Exile.glb")
    mesh = FbxRig(folder / "SK_Exile.fbx")
    to_glb, worst = bind_map(glb, mesh)
    print(f"FBX bind to GLB bind: largest joint error {worst:.4f} degrees")
    animations = {a["name"]: a for a in glb.gltf["animations"]}
    for clip in CLIPS:
        animation = animations.get(clip.lower())
        if animation is None:
            raise SystemExit(f"SK_Exile.glb has no {clip.lower()} clip")
        new, roll, shift = restore_clip(glb, animation, FbxRig(folder / f"SK_Exile_{clip}.fbx"), to_glb)
        print(f"{clip.lower():7s} largest key rotation change {roll:6.1f} degrees, largest joint move {shift * 1000:.2f} mm")
        if not args.report:
            for channel in animation["channels"]:
                node = channel["target"]["node"]
                if channel["target"]["path"] == "rotation" and node in new:
                    glb.write(animation["samplers"][channel["sampler"]]["output"], new[node])
    if not args.report:
        out = Path(args.out) if args.out else folder / "SK_Exile.glb"
        out.write_bytes(bytes(glb.data))
        print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
