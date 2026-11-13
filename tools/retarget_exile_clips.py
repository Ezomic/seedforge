#!/usr/bin/env python3
"""Puts clips of the two Quaternius Universal Animation Libraries on the Exile's skeleton (SEED-795).

    retarget_exile_clips.py --source UAL1_Standard_RM.glb UAL2_Standard_RM.glb [--exile art/exile/SK_Exile.glb] [--out FILE] [--report]

The Exile's own clips were broken at the source and of unrecorded origin. Both libraries are CC0 1.0 and stand on one
ordinary humanoid rig (pelvis, three spine bones, neck, head, clavicle, upper arm, forearm, hand, thigh, calf, foot,
ball) that lines up bone for bone with the Exile's. The Exile's mesh, skin, skeleton and textures keep their bytes;
only the animations are replaced.

How a clip is carried over. The two rigs rest in different poses, so a joint's rotation cannot be copied as it is: that
is the mistake the old clips were made with. Each rig is first brought to one canonical pose in thought: arms level to
the sides, legs straight down, feet pointing ahead. For every joint that gives the world rotation it has in that pose
(its rest rotation, turned by the smallest turn that lines its bone up with the canonical direction). A source joint's
world rotation in a clip, taken against its canonical one, is a turn in the world; mirrored into the Exile's axes and
put in front of the Exile joint's canonical rotation, it is that joint's world rotation in the clip. The hips, spine,
neck, head and collarbones are not straightened first: their turns are taken from each rig's own rest, so the Exile
keeps its own posture. The hips' travel is scaled by the ratio of the two rigs' hip heights. The root joint takes the horizontal travel and
the hips keep the rest, as the engine expects (docs/exile-import.md).

Thrust is Punch_Jab, which was not made for a spear, played left for right so the spear hand drives. The swing and the
block come from the second library. The idle is stood as the bind pose with the arms hanging, and the run is stretched
in time to the game's speed with the arms brought in.

Needs numpy. Same inputs give the same output.
"""
import argparse
import json
import math
import struct
import sys
from pathlib import Path

import numpy as np

import provenance

FPS = 30
# Metres per second the game moves the Exile at: the clip is stretched in time until its travel matches.
SPEED = {"run": 5.2}
# Degrees each arm is brought in toward the body and ahead: the library's jog carries its elbows wide and behind.
TUCK = {"run": {"Left": (20.0, 10.0), "Right": (20.0, 10.0)}}
PREFIX = "mixamorig:"
# Clips played left for right: the library's jab is thrown with the left hand and the Exile's spear is in the right.
MIRRORED = ("thrust",)
FLIP = np.diag([-1.0, 1.0, 1.0])
# Exile clip -> library clip, or clips played one after the other, joined by "+". The swing is the second library's
# strike followed by its recovery: the first library's only sword attack rolls the chest until a shoulder is at the ear.
CLIPS = {"idle": "Idle_Loop", "walk": "Walk_Loop", "run": "Jog_Fwd_Loop", "swing": "Sword_Regular_A+Sword_Regular_A_Rec",
         "thrust": "Punch_Jab", "block": "Idle_Shield_Loop", "dodge": "Roll", "hit": "Hit_Chest", "death": "Death01"}
# Exile clips whose library clips are in the second library.
SECOND = ("swing", "block")
LOOPS = ("idle", "walk", "run", "block")
# Exile joint -> (library joint, the child whose joint gives the bone's direction on the Exile, on the library rig, canonical direction).
# Directions are in (left, up, ahead). "yaw" keeps the bone's own slope and only turns it to point ahead; None takes the parent's turn.
BONES = {
    # The trunk, the head and the clavicles are carried as turns from each rig's own rest: both rigs rest in a neutral
    # standing posture there, and the Exile's mesh is built around its own (spine 7 degrees back, clavicles 20 degrees
    # down). Lining these bones up with the library's would lean the Exile 7 degrees further forward than the clip
    # means and lift its shoulders out of the shape the idle has.
    "Hips": ("pelvis", None, None, "rest"),
    "Spine": ("spine_01", None, None, "rest"),
    "Spine1": ("spine_02", None, None, "rest"),
    "Spine2": ("spine_03", None, None, "rest"),
    "Neck": ("neck_01", None, None, "rest"),
    "Head": ("Head", None, None, "rest"),
}
for _side, _s, _sign in (("Left", "l", 1), ("Right", "r", -1)):
    BONES.update({
        f"{_side}Shoulder": (f"clavicle_{_s}", None, None, "rest"),
        f"{_side}Arm": (f"upperarm_{_s}", f"{_side}ForeArm", f"lowerarm_{_s}", (_sign, 0, 0)),
        f"{_side}ForeArm": (f"lowerarm_{_s}", f"{_side}Hand", f"hand_{_s}", (_sign, 0, 0)),
        f"{_side}Hand": (f"hand_{_s}", None, None, f"{_side}ForeArm"),
        f"{_side}UpLeg": (f"thigh_{_s}", f"{_side}Leg", f"calf_{_s}", (0, -1, 0)),
        f"{_side}Leg": (f"calf_{_s}", f"{_side}Foot", f"foot_{_s}", (0, -1, 0)),
        f"{_side}Foot": (f"foot_{_s}", f"{_side}ToeBase", f"ball_{_s}", "yaw"),
        f"{_side}ToeBase": (f"ball_{_s}", None, None, f"{_side}Foot"),
    })
COMPONENT = {5120: "i1", 5121: "u1", 5122: "<i2", 5123: "<u2", 5125: "<u4", 5126: "<f4"}
WIDTH = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


def unit(v):
    return v / np.linalg.norm(v)


def quat_matrix(q):
    x, y, z, w = q
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def matrix_quat(m):
    trace = m[0, 0] + m[1, 1] + m[2, 2]
    if trace > 0:
        s = np.sqrt(trace + 1.0) * 2.0
        q = [(m[2, 1] - m[1, 2]) / s, (m[0, 2] - m[2, 0]) / s, (m[1, 0] - m[0, 1]) / s, s / 4.0]
    else:
        i = int(np.argmax([m[0, 0], m[1, 1], m[2, 2]]))
        j, k = (i + 1) % 3, (i + 2) % 3
        s = np.sqrt(1.0 + m[i, i] - m[j, j] - m[k, k]) * 2.0
        q = [0.0, 0.0, 0.0, (m[k, j] - m[j, k]) / s]
        q[i] = s / 4.0
        q[j] = (m[j, i] + m[i, j]) / s
        q[k] = (m[k, i] + m[i, k]) / s
    return unit(np.array(q))


def turn(a, b):
    """The smallest rotation that takes unit vector a onto unit vector b."""
    axis = np.cross(a, b)
    sine, cosine = np.linalg.norm(axis), float(np.dot(a, b))
    if sine < 1e-9:
        return np.eye(3)
    k = axis / sine
    cross = np.array([[0, -k[2], k[1]], [k[2], 0, -k[0]], [-k[1], k[0], 0]])
    return np.eye(3) + sine * cross + (1.0 - cosine) * cross @ cross


class Glb:
    def __init__(self, path):
        blob = open(path, "rb").read()
        length = struct.unpack_from("<I", blob, 12)[0]
        self.doc = json.loads(blob[20:20 + length])
        self.bin = blob[20 + length + 8:]
        self.nodes = self.doc["nodes"]
        self.parent = {c: i for i, n in enumerate(self.nodes) for c in n.get("children", [])}
        self.index = {n.get("name", ""): i for i, n in enumerate(self.nodes)}

    def read(self, index):
        """An accessor's values as stored (no normalisation), one row per element."""
        accessor = self.doc["accessors"][index]
        view = self.doc["bufferViews"][accessor["bufferView"]]
        kind = np.dtype(COMPONENT[accessor["componentType"]])
        width = WIDTH[accessor["type"]]
        size = kind.itemsize * width
        start = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
        stride = view.get("byteStride") or size
        raw = np.frombuffer(self.bin, dtype=np.uint8, count=stride * (accessor["count"] - 1) + size, offset=start)
        if stride != size:
            raw = np.ascontiguousarray(np.lib.stride_tricks.as_strided(raw, shape=(accessor["count"], size), strides=(stride, 1))).reshape(-1)
        return raw.view(kind).reshape(accessor["count"], width)

    def view_bytes(self, index):
        view = self.doc["bufferViews"][index]
        return self.bin[view.get("byteOffset", 0):view.get("byteOffset", 0) + view["byteLength"]]

    def local(self, node):
        n = self.nodes[node]
        if "matrix" in n:
            raise SystemExit(f"{n.get('name')}: a node matrix is not supported")
        return (np.array(n.get("translation", [0.0, 0.0, 0.0])), np.array(n.get("rotation", [0.0, 0.0, 0.0, 1.0])), np.array(n.get("scale", [1.0, 1.0, 1.0])))

    def order(self):
        """Every node, parents first."""
        depth = {}
        for i in range(len(self.nodes)):
            d, p = 0, i
            while p in self.parent:
                p = self.parent[p]
                d += 1
            depth[i] = d
        return sorted(range(len(self.nodes)), key=lambda i: depth[i])

    def world(self, pose=None):
        """Per node: (position, rotation matrix with the scale taken out) for a pose {node: (t, q, s)}; rest where a node has none."""
        out = {}
        full = {}
        for i in self.order():
            t, q, s = (pose or {}).get(i) or self.local(i)
            m = np.eye(4)
            m[:3, :3] = quat_matrix(q) * s
            m[:3, 3] = t
            if i in self.parent:
                m = full[self.parent[i]] @ m
            full[i] = m
            rotation = m[:3, :3] / np.linalg.norm(m[:3, :3], axis=0)
            out[i] = (m[:3, 3].copy(), rotation)
        return out


def sample(glb, name):
    """A clip sampled at FPS: the times and, per frame, {node: (t, q, s)} for the nodes it animates."""
    animation = next((a for a in glb.doc.get("animations", []) if a.get("name") == name), None)
    if animation is None:
        raise SystemExit(f"the source has no {name} clip")
    tracks = []
    end = 0.0
    for channel in animation["channels"]:
        sampler = animation["samplers"][channel["sampler"]]
        times = glb.read(sampler["input"]).astype(np.float64).reshape(-1)
        values = glb.read(sampler["output"]).astype(np.float64)
        if sampler.get("interpolation", "LINEAR") == "CUBICSPLINE":
            values = values[1::3]
        tracks.append((channel["target"]["node"], channel["target"]["path"], times, values, sampler.get("interpolation", "LINEAR")))
        end = max(end, float(times[-1]))
    count = int(round(end * FPS)) + 1
    frames = []
    for f in range(count):
        t = min(f / FPS, end)
        pose = {}
        for node, path, times, values, mode in tracks:
            k = int(np.searchsorted(times, t, side="right")) - 1
            k = max(0, min(k, len(times) - 1))
            value = values[k]
            if mode != "STEP" and k + 1 < len(times) and times[k + 1] > times[k]:
                u = (t - times[k]) / (times[k + 1] - times[k])
                other = values[k + 1]
                if path == "rotation" and np.dot(value, other) < 0:
                    other = -other
                value = value * (1 - u) + other * u
            local = pose.setdefault(node, list(glb.local(node)))
            if path == "rotation":
                local[1] = unit(value)
            elif path == "translation":
                local[0] = value
            elif path == "scale":
                local[2] = value
        frames.append({node: tuple(v) for node, v in pose.items()})
    return np.arange(count) / FPS, frames


class Rig:
    """A rig's axes and, per Exile joint name, the world rotation its joint has in the canonical pose."""

    def __init__(self, glb, names, child):
        self.glb = glb
        self.names = names
        rest = glb.world()
        self.rest = rest
        at = lambda name: rest[glb.index[names[name]]][0]
        left = at("LeftUpLeg") - at("RightUpLeg")
        left[1] = 0.0
        self.left = unit(left)
        self.up = np.array([0.0, 1.0, 0.0])
        self.ahead = np.cross(self.left, self.up)
        self.axes = np.column_stack([self.left, self.up, self.ahead])
        self.aim = {}
        self.canonical = {}
        for name, (_, _, _, kind) in BONES.items():
            node = glb.index[names[name]]
            if isinstance(kind, str) and kind not in ("rest", "yaw"):
                continue
            if kind == "rest":
                self.aim[name] = np.eye(3)
            else:
                bone = unit(rest[glb.index[child[name]]][0] - rest[node][0])
                if kind == "yaw":
                    flat = bone - self.up * np.dot(bone, self.up)
                    target = unit(self.ahead * np.linalg.norm(flat) + self.up * np.dot(bone, self.up))
                else:
                    target = unit(self.axes @ np.array(kind, dtype=float))
                self.aim[name] = turn(bone, target)
        for name, (_, _, _, kind) in BONES.items():
            if isinstance(kind, str) and kind not in ("rest", "yaw"):
                self.aim[name] = self.aim[kind]
            self.canonical[name] = self.aim[name] @ rest[glb.index[names[name]]][1]


def quat_mul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return np.array([aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
                     aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz])


def quat_inverse(q):
    return np.array([-q[0], -q[1], -q[2], q[3]])


# The idle's hanging arm: how much of the arm's length the hang uses, how far out from and ahead of the shoulder the
# hand hangs (m), and how far out the elbow points for each unit it points back.
HANG, HANG_OUT, HANG_AHEAD, ELBOW_OUT = 0.97, 0.04, 0.08, 0.3


def square_idle(exile, rig, frames):
    """Stands the idle as the bind pose does, with the arms hanging, and keeps the library clip's motion about it.

    The library's idle stands turned with the weight on one leg. The Exile's idle faces the way it will walk: every
    joint takes its bind rotation times its key's turn from the clip's mean, and the hips' translation its bind value
    plus the key's offset from the mean. The upper arm and forearm then hang the hand beside the thigh, straight down
    from the shoulder with the elbow pointing back, and the wrist keeps its bind rotation.
    """
    root = exile.index["root"]
    hips = exile.index[PREFIX + "Hips"]
    for node in frames[0]:
        if node == root:
            continue
        keys = np.array([f[node][1] for f in frames])
        keys[np.sum(keys * keys[0], axis=1) < 0] *= -1.0
        mean = unit(keys.sum(axis=0))
        rest = exile.local(node)[1]
        previous = rest
        for f, q in zip(frames, keys):
            new = unit(quat_mul(rest, quat_mul(quat_inverse(mean), q)))
            if np.dot(new, previous) < 0:
                new = -new
            previous = new
            f[node] = (f[node][0], new)
    travel = np.array([f[hips][0] for f in frames])
    for f, t in zip(frames, travel):
        f[hips] = (exile.local(hips)[0] + (t - travel.mean(axis=0)), f[hips][1])
    rest = rig.rest
    for side, sign in (("Left", 1.0), ("Right", -1.0)):
        arm, fore, hand = (exile.index[PREFIX + side + part] for part in ("Arm", "ForeArm", "Hand"))
        shoulder = rest[arm][0]
        upper, lower = np.linalg.norm(rest[fore][0] - shoulder), np.linalg.norm(rest[hand][0] - rest[fore][0])
        wrist = shoulder + rig.left * sign * HANG_OUT + rig.ahead * HANG_AHEAD - rig.up * HANG * (upper + lower)
        reach = np.linalg.norm(wrist - shoulder)
        line = (wrist - shoulder) / reach
        pole = rig.left * sign * ELBOW_OUT - rig.ahead
        pole = unit(pole - line * np.dot(pole, line))
        along = (upper * upper - lower * lower + reach * reach) / (2.0 * reach)
        elbow = shoulder + line * along + pole * np.sqrt(max(0.0, upper * upper - along * along))
        arm_world = turn(unit(rest[fore][0] - shoulder), unit(elbow - shoulder)) @ rest[arm][1]
        fore_world = turn(unit(rest[hand][0] - rest[fore][0]), unit(wrist - elbow)) @ rest[fore][1]
        parent = rest[exile.parent[arm]][1]
        hung = {arm: matrix_quat(parent.T @ arm_world), fore: matrix_quat(arm_world.T @ fore_world), hand: exile.local(hand)[1]}
        for f in frames:
            for node, q in hung.items():
                f[node] = (f[node][0], q)


def tucked(rig, chest, bone, inward, forward):
    """The world turn that brings one arm `inward` degrees toward the body and `forward` degrees ahead, in the chest's frame."""
    side = 1.0 if bone.startswith("Left") else -1.0
    a, f = math.radians(inward), math.radians(forward)
    down = np.array([0.0, -1.0, 0.0])
    tuck = turn(down, unit(np.array([-side * math.sin(a), -math.cos(a), 0.0])))
    swing = turn(down, unit(np.array([0.0, -math.cos(f), math.sin(f)])))
    frame = chest @ rig.rest[rig.glb.index[PREFIX + "Spine2"]][1].T @ rig.axes
    return frame @ swing @ tuck @ frame.T


def joined(sources, exile, name):
    """One Exile clip from its library clips in turn, each from the file that has it, the root's travel carried on."""
    root = exile.index["root"]
    times, frames = np.zeros(0), []
    for clip in CLIPS[name].split("+"):
        source = next((s for s in sources if any(a.get("name") == clip for a in s.doc.get("animations", []))), None)
        if source is None:
            raise SystemExit(f"{clip} is in none of the source files")
        part_times, part = retarget(source, exile, name, clip)
        if frames:
            carried = frames[-1][root][0] - part[0][root][0]
            for keys in part:
                keys[root] = (keys[root][0] + carried, keys[root][1])
            part_times = part_times + times[-1] + 1.0 / FPS
        times = np.concatenate([times, part_times])
        frames += part
    return times, frames


def retarget(source, exile, name, clip):
    """Times and, per frame, {exile node: (translation or None, quaternion)} for one library clip as the Exile clip `name`."""
    source_names = {n: b[0] for n, b in BONES.items()}
    exile_names = {n: PREFIX + n for n in BONES}
    src = Rig(source, source_names, {n: b[2] for n, b in BONES.items()})
    dst = Rig(exile, exile_names, {n: PREFIX + b[1] for n, b in BONES.items() if b[1]})
    mirror = dst.axes @ src.axes.T
    times, frames = sample(source, clip)
    joints = exile.doc["skins"][0]["joints"]
    order = [i for i in exile.order() if i in joints]
    by_node = {exile.index[PREFIX + n]: n for n in BONES}
    root = exile.index["root"]
    hips = exile.index[PREFIX + "Hips"]
    s_root, s_hips = source.index["root"], source.index["pelvis"]
    scale = dst.rest[hips][0][1] / src.rest[s_hips][0][1]
    root_rest = dst.rest[root]
    parent_of_root = dst.rest[exile.parent[root]] if root in exile.parent else (np.zeros(3), np.eye(3))
    out = []
    previous = {}
    for pose in frames:
        world = source.world(pose)
        rotation = {}
        keys = {}
        travel = mirror @ (world[s_root][0] - src.rest[s_root][0]) * scale
        travel[1] = 0.0
        hips_moved = mirror @ (world[s_hips][0] - src.rest[s_hips][0]) * scale
        if name in MIRRORED:
            travel, hips_moved = FLIP @ travel, FLIP @ hips_moved
        root_at = root_rest[0] + travel
        hips_at = dst.rest[hips][0] + hips_moved
        for node in order:
            parent = exile.parent.get(node)
            parent_rotation = rotation.get(parent, dst.rest[parent][1] if parent is not None else np.eye(3))
            if node in by_node:
                bone = by_node[node]
                taken = bone
                if name in MIRRORED:
                    taken = "Right" + bone[4:] if bone.startswith("Left") else "Left" + bone[5:] if bone.startswith("Right") else bone
                turned = mirror @ (world[source.index[source_names[taken]]][1] @ src.canonical[taken].T) @ mirror.T
                if name in MIRRORED:
                    turned = FLIP @ turned @ FLIP
                rotation[node] = turned @ dst.canonical[bone]
                side = bone[:4] if bone[:4] == "Left" else bone[:5]
                if side in TUCK.get(name, {}) and ("Arm" in bone or "Hand" in bone):
                    rotation[node] = tucked(dst, rotation[exile.index[PREFIX + "Spine2"]], bone, *TUCK[name][side]) @ rotation[node]
            else:
                rotation[node] = parent_rotation @ quat_matrix(exile.local(node)[1])
            q = matrix_quat(parent_rotation.T @ rotation[node])
            if node in previous and np.dot(q, previous[node]) < 0:
                q = -q
            previous[node] = q
            translation = None
            if node == root:
                translation = parent_of_root[1].T @ (root_at - parent_of_root[0])
            elif node == hips:
                translation = rotation[root].T @ (hips_at - root_at)
            keys[node] = (translation, q)
        out.append(keys)
    if name == "idle":
        square_idle(exile, dst, out)
    if name in LOOPS:
        # A loop's last key is its first, with the root's travel kept.
        last, first = out[-1], out[0]
        for node in last:
            translation = last[node][0] if node == root else first[node][0]
            last[node] = (translation, first[node][1])
    if name in SPEED:
        travelled = float(np.linalg.norm(out[-1][root][0] - out[0][root][0]))
        times = times * (travelled / times[-1] / SPEED[name])
    return times, out


class Writer:
    def __init__(self):
        self.views, self.accessors, self.chunks, self.size = [], [], [], 0

    def view(self, data, extra=None):
        self.chunks.append(data + b"\0" * (-len(data) % 4))
        view = {"buffer": 0, "byteOffset": self.size, "byteLength": len(data)}
        view.update(extra or {})
        self.size += len(self.chunks[-1])
        self.views.append(view)
        return len(self.views) - 1

    def accessor(self, values, kind, shape, bounds=False):
        array = np.ascontiguousarray(values, dtype=np.dtype(COMPONENT[kind]))
        entry = {"bufferView": self.view(array.tobytes()), "componentType": kind, "count": len(array), "type": shape}
        if bounds:
            entry["min"] = [float(v) for v in np.atleast_1d(array.min(axis=0))]
            entry["max"] = [float(v) for v in np.atleast_1d(array.max(axis=0))]
        self.accessors.append(entry)
        return len(self.accessors) - 1

    def copy(self, glb, index):
        source = dict(glb.doc["accessors"][index])
        old = glb.doc["bufferViews"][source["bufferView"]]
        source.pop("byteOffset", None)
        source["bufferView"] = self.view(np.ascontiguousarray(glb.read(index)).tobytes(), {"target": old["target"]} if "target" in old else None)
        self.accessors.append(source)
        return len(self.accessors) - 1


def build(exile, clips):
    """The Exile's file with its animations replaced by `clips`: {name: (times, frames)}."""
    doc = json.loads(json.dumps(exile.doc))
    writer = Writer()
    remap = {}

    def kept(index):
        if index not in remap:
            remap[index] = writer.copy(exile, index)
        return remap[index]

    for mesh in doc["meshes"]:
        for primitive in mesh["primitives"]:
            primitive["attributes"] = {k: kept(v) for k, v in primitive["attributes"].items()}
            if "indices" in primitive:
                primitive["indices"] = kept(primitive["indices"])
    for skin in doc["skins"]:
        skin["inverseBindMatrices"] = kept(skin["inverseBindMatrices"])
    for image in doc.get("images", []):
        image["bufferView"] = writer.view(exile.view_bytes(image["bufferView"]))
    doc["animations"] = []
    for name, (times, frames) in clips.items():
        stamp = writer.accessor(times.reshape(-1, 1), 5126, "SCALAR", bounds=True)
        animation = {"name": name, "channels": [], "samplers": []}
        for node in frames[0]:
            tracks = [("rotation", np.array([f[node][1] for f in frames]), "VEC4")]
            if frames[0][node][0] is not None:
                tracks.append(("translation", np.array([f[node][0] for f in frames]), "VEC3"))
            for path, values, shape in tracks:
                animation["samplers"].append({"input": stamp, "output": writer.accessor(values, 5126, shape), "interpolation": "LINEAR"})
                animation["channels"].append({"sampler": len(animation["samplers"]) - 1, "target": {"node": node, "path": path}})
        doc["animations"].append(animation)
    doc["accessors"] = writer.accessors
    doc["bufferViews"] = writer.views
    doc["buffers"] = [{"byteLength": writer.size}]
    text = json.dumps(doc, separators=(",", ":")).encode()
    text += b" " * (-len(text) % 4)
    binary = b"".join(writer.chunks)
    return b"glTF" + struct.pack("<II", 2, 28 + len(text) + len(binary)) + struct.pack("<I4s", len(text), b"JSON") + text \
        + struct.pack("<I4s", len(binary), b"BIN\0") + binary


def describe(exile, name, times, frames):
    """One line on where the clip puts the hands and how far it travels."""
    root = exile.index["root"]
    hips = exile.index[PREFIX + "Hips"]
    rest = exile.world()
    left = unit(rest[exile.index[PREFIX + "LeftUpLeg"]][0] - rest[exile.index[PREFIX + "RightUpLeg"]][0])
    out = {"Left": [], "Right": []}
    for keys in frames[:: max(1, len(frames) // 12)]:
        pose = {}
        for node, (translation, q) in keys.items():
            t, _, s = exile.local(node)
            pose[node] = (translation if translation is not None else t, q, s)
        world = exile.world(pose)
        for side, sign in (("Left", 1.0), ("Right", -1.0)):
            out[side].append(sign * float(np.dot(world[exile.index[PREFIX + side + "Hand"]][0] - world[hips][0], left)))
    travel = np.linalg.norm(frames[-1][root][0] - frames[0][root][0])
    return (f"{name:7s} from {CLIPS[name]:35s} {times[-1]:.2f} s, {len(frames)} keys, travels {travel:.2f} m, "
            f"hands out from the midline: left {np.mean(out['Left']):+.2f} m, right {np.mean(out['Right']):+.2f} m")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--source", required=True, nargs="+",
                        help="UAL1_Standard_RM.glb and UAL2_Standard_RM.glb from the two libraries' Standard zips")
    parser.add_argument("--exile", default=str(Path(__file__).resolve().parents[1] / "art" / "exile" / "SK_Exile.glb"))
    parser.add_argument("--out")
    parser.add_argument("--report", action="store_true")
    args = parser.parse_args()
    sources, exile = [Glb(path) for path in args.source], Glb(args.exile)
    clips = {}
    for name in CLIPS:
        times, frames = joined(sources, exile, name)
        clips[name] = (times, frames)
        print(describe(exile, name, times, frames))
    if not args.report:
        out = Path(args.out or args.exile)
        out.write_bytes(build(exile, clips))
        print(f"wrote {out}")
        origins = {name: {"source": f"Quaternius Universal Animation Library{' 2' if name in SECOND else ''}, Standard: {clip.replace('+', ' then ')}"
                          + (", played left for right" if name in MIRRORED else ""), "licence": "CC0-1.0"}
                   for name, clip in CLIPS.items()}
        sidecar = provenance.write_sidecar(out, source="Mesh, skin and skeleton: first-party, generated locally by the owner. Clips: Quaternius Universal "
                                           "Animation Library (https://quaternius.com/packs/universalanimationlibrary.html) and, for the swing and the block, "
                                           "Universal Animation Library 2 (https://quaternius.com/packs/universalanimationlibrary2.html), "
                                           "retargeted by tools/retarget_exile_clips.py. The hashed source file is the first library's.",
                                           licence="own-work", source_file=args.source[0], clip_origins=origins)
        print(f"wrote {sidecar}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
