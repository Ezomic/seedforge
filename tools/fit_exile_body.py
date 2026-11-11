#!/usr/bin/env python3
"""Puts a generated body mesh on the Exile's skeleton (SEED-793).

The result carries the skeleton and the skin and no clips: where the Exile's clips came from is not recorded, so they
are not copied into a new file, and the provenance sidecar written beside the result says so. --clips keeps them, for
looking at the body in motion; that file is not for the repository.

Inputs:
  --body      the textured static mesh (TRELLIS.2 through ComfyUI, tools/comfy/exile_body_trellis.py)
  --rig       the same mesh auto-rigged by UniRig with its articulationxl template (tools/comfy/exile_body_unirig.py),
              turned from FBX into GLB by tools/comfy/fbx_to_glb.py
  --skeleton  the GLB whose skeleton, skin and clips are kept byte for byte (art/exile/SK_Exile.glb)
  --out       the result

UniRig says where the joints sit in the new body and how each vertex follows them. The body is scaled and moved so its
joints lie as close to the Exile's as one scale and one offset allow, and then each joint carries its share of the
vertices onto the Exile's joint: turned by the smallest turn that lines its bone up with the Exile's, and stretched
along the bone by the ratio of the two lengths. A vertex is the weighted blend of where its joints put it, with
UniRig's own weights. Fingers follow the hand (see UNIRIG_ORDER).

Needs numpy. Same inputs give the same output.
"""
import argparse
import json
import struct
import sys

import numpy as np

import provenance

SOURCE = ("Generated locally by the owner: concept image with Z-Image Turbo, mesh and textures with TRELLIS.2, joints and "
          "skin weights with UniRig, fitted to the skeleton of art/exile/SK_Exile.glb with tools/fit_exile_body.py. No clips.")
COMPONENT = {5120: "i1", 5121: "u1", 5122: "<i2", 5123: "<u2", 5125: "<u4", 5126: "<f4"}
WIDTH = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}
CHILD = {"Hips": "Spine", "Spine": "Spine1", "Spine1": "Spine2", "Spine2": "Neck", "Neck": "Head"}
for _side in ("Left", "Right"):
    CHILD.update({f"{_side}Shoulder": f"{_side}Arm", f"{_side}Arm": f"{_side}ForeArm", f"{_side}ForeArm": f"{_side}Hand",
                  f"{_side}UpLeg": f"{_side}Leg", f"{_side}Leg": f"{_side}Foot", f"{_side}Foot": f"{_side}ToeBase"})
# A joint without a child of its own turns as the bone that leads to it and is not stretched.
END = {"Head": "Neck", "LeftHand": "LeftForeArm", "RightHand": "RightForeArm", "LeftToeBase": "LeftFoot", "RightToeBase": "RightFoot"}
PREFIX = "mixamorig:"


class Glb:
    def __init__(self, path):
        blob = open(path, "rb").read()
        length = struct.unpack_from("<I", blob, 12)[0]
        self.doc = json.loads(blob[20:20 + length])
        self.bin = blob[20 + length + 8:]

    def read(self, index):
        accessor = self.doc["accessors"][index]
        view = self.doc["bufferViews"][accessor["bufferView"]]
        kind = np.dtype(COMPONENT[accessor["componentType"]])
        width = WIDTH[accessor["type"]]
        start = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
        stride = view.get("byteStride") or kind.itemsize * width
        raw = np.frombuffer(self.bin, dtype=np.uint8, count=stride * (accessor["count"] - 1) + kind.itemsize * width, offset=start)
        if stride != kind.itemsize * width:
            rows = np.lib.stride_tricks.as_strided(raw, shape=(accessor["count"], kind.itemsize * width), strides=(stride, 1))
            raw = np.ascontiguousarray(rows).reshape(-1)
        values = raw.view(kind).reshape(accessor["count"], width)
        if accessor.get("normalized"):
            values = values.astype(np.float64) / np.iinfo(kind).max
        return values

    def view_bytes(self, index):
        view = self.doc["bufferViews"][index]
        return self.bin[view.get("byteOffset", 0):view.get("byteOffset", 0) + view["byteLength"]]

    def primitive(self):
        return self.doc["meshes"][0]["primitives"][0]

    def joints(self):
        """Joint names in skin order and their positions in the skin's bind space."""
        skin = self.doc["skins"][0]
        inverse = self.read(skin["inverseBindMatrices"]).astype(np.float64).reshape(-1, 4, 4).transpose(0, 2, 1)
        names = [self.doc["nodes"][j].get("name", "") for j in skin["joints"]]
        return names, np.linalg.inv(inverse)[:, :3, 3]

    def parents(self):
        return {c: i for i, node in enumerate(self.doc["nodes"]) for c in node.get("children", [])}


# UniRig's own humanoid skeleton ("articulationxl"): 34 bones named bone_0 to bone_33, in this order. Its "mixamo" mode
# is not used: it hands the names out to the wrong bones and moves the joints to a T-pose that the mesh is not in.
# Each hand has six finger bones (a thumb chain and one chain for the four fingers). They stay unnamed, so the fingers
# follow the hand: the Exile's clips hold its thumb and index bones at one fixed curl, up to 52 degrees off the bind,
# and never move them, so putting the fingers on those bones only bends the generated hand out of shape.
UNIRIG_ORDER = (["Hips", "Spine", "Spine1", "Spine2", "Neck", "Head", "LeftShoulder", "LeftArm", "LeftForeArm", "LeftHand"] + [None] * 6
                + ["RightShoulder", "RightArm", "RightForeArm", "RightHand"] + [None] * 6
                + ["LeftUpLeg", "LeftLeg", "LeftFoot", "LeftToeBase", "RightUpLeg", "RightLeg", "RightFoot", "RightToeBase"])


def name_rig(rig):
    """Gives UniRig's numbered bones the Exile's names, where they have one."""
    joints = rig.doc["skins"][0]["joints"]
    names = [rig.doc["nodes"][j].get("name", "") for j in joints]
    if names != [f"bone_{i}" for i in range(len(UNIRIG_ORDER))]:
        raise SystemExit(f"the rig is not UniRig's 34 bone humanoid: {names[:4]} ... ({len(names)} joints)")
    for node, name in zip(joints, UNIRIG_ORDER):
        if name:
            rig.doc["nodes"][node]["name"] = PREFIX + name


def nearest(points, targets, chunk=512):
    """Index of the nearest target for every point."""
    out = np.empty(len(points), dtype=np.int64)
    squared = (targets ** 2).sum(axis=1)
    for start in range(0, len(points), chunk):
        block = points[start:start + chunk]
        distance = (block ** 2).sum(axis=1)[:, None] - 2.0 * block @ targets.T + squared[None, :]
        out[start:start + chunk] = distance.argmin(axis=1)
    return out


def turn(a, b):
    """The smallest rotation that takes unit vector a onto unit vector b."""
    axis = np.cross(a, b)
    sine, cosine = np.linalg.norm(axis), float(np.dot(a, b))
    if sine < 1e-9:
        return np.eye(3)
    k = axis / sine
    cross = np.array([[0, -k[2], k[1]], [k[2], 0, -k[0]], [-k[1], k[0], 0]])
    return np.eye(3) + sine * cross + (1.0 - cosine) * cross @ cross


def rig_weights(rig, skeleton_names):
    """Per rig vertex: weights over the skeleton's joints. Finger bones and anything unnamed follow the nearest named ancestor."""
    names, _ = rig.joints()
    skin = rig.doc["skins"][0]
    parents = rig.parents()
    index = {name: i for i, name in enumerate(skeleton_names)}
    target = []
    for node in skin["joints"]:
        while rig.doc["nodes"][node].get("name", "") not in index:
            if node not in parents:
                raise SystemExit(f"the rig's joint {rig.doc['nodes'][node].get('name')} has no ancestor in the skeleton")
            node = parents[node]
        target.append(index[rig.doc["nodes"][node]["name"]])
    target = np.array(target)
    primitive = rig.primitive()["attributes"]
    joints = rig.read(primitive["JOINTS_0"]).astype(np.int64)
    weights = rig.read(primitive["WEIGHTS_0"]).astype(np.float64)
    dense = np.zeros((len(joints), len(skeleton_names)))
    for k in range(4):
        np.add.at(dense, (np.arange(len(joints)), target[joints[:, k]]), weights[:, k])
    return dense / dense.sum(axis=1, keepdims=True)


def fit(body, rig, skeleton):
    """Positions, normals, tangents, joints and weights of the body on the skeleton, and a report."""
    lines = []
    attributes = body.primitive()["attributes"]
    position = body.read(attributes["POSITION"]).astype(np.float64)
    normal = body.read(attributes["NORMAL"]).astype(np.float64)
    tangent = body.read(attributes["TANGENT"]).astype(np.float64)

    rig_position = rig.read(rig.primitive()["attributes"]["POSITION"]).astype(np.float64)
    rig_names, rig_joints = rig.joints()
    # The rig's mesh is the body's own, in another scale and place: one scale per axis box and one offset bring it back.
    scale = (position.max(axis=0) - position.min(axis=0)) / (rig_position.max(axis=0) - rig_position.min(axis=0))
    if scale.max() / scale.min() > 1.02:
        raise SystemExit(f"the rig's mesh is not the body's: box ratios {scale}")
    offset = position.min(axis=0) - rig_position.min(axis=0) * scale
    rig_position = rig_position * scale + offset
    rig_joints = rig_joints * scale + offset
    match = nearest(position, rig_position)
    gap = np.linalg.norm(position - rig_position[match], axis=1).max()
    lines.append(f"body vertex to rig vertex: largest gap {gap * 1000:.2f} mm of {np.ptp(position[:, 1]):.2f} m height")

    names, bind = skeleton.joints()
    weights = rig_weights(rig, names)[match]
    at = {name: i for i, name in enumerate(names)}
    source = {name[len(PREFIX):]: rig_joints[i] for i, name in enumerate(rig_names) if name.startswith(PREFIX)}
    target = {name[len(PREFIX):]: bind[i] for i, name in enumerate(names) if name.startswith(PREFIX)}
    shared = sorted(set(source) & set(target))
    if source["LeftArm"][0] < source["RightArm"][0]:
        raise SystemExit("the body faces the other way from the skeleton")

    # One scale and one offset, by least squares over the shared joints.
    a = np.array([source[n] for n in shared])
    b = np.array([target[n] for n in shared])
    size = ((a - a.mean(axis=0)) * (b - b.mean(axis=0))).sum() / ((a - a.mean(axis=0)) ** 2).sum()
    move = b.mean(axis=0) - size * a.mean(axis=0)
    position = position * size + move
    source = {n: p * size + move for n, p in source.items()}
    miss = {n: np.linalg.norm(source[n] - target[n]) for n in shared}
    worst = max(miss, key=miss.get)
    lines.append(f"scaled {size:.3f}: joints {np.mean(list(miss.values())) * 100:.1f} cm off on average before the limbs are carried, {worst} {miss[worst] * 100:.1f} cm")

    moved = np.zeros_like(position)
    turned_normal = np.zeros_like(normal)
    turned_tangent = np.zeros((len(tangent), 3))
    for name in shared:
        j = at[PREFIX + name]
        w = weights[:, j]
        if not w.any():
            continue
        start, end = (name, CHILD[name]) if name in CHILD else (END[name], name)
        from_bone = source[end] - source[start]
        to_bone = target[end] - target[start]
        u = from_bone / np.linalg.norm(from_bone)
        rotation = turn(u, to_bone / np.linalg.norm(to_bone))
        stretch = np.eye(3)
        if name in CHILD:
            stretch += (np.linalg.norm(to_bone) / np.linalg.norm(from_bone) - 1.0) * np.outer(u, u)
        carried = (position - source[name]) @ (rotation @ stretch).T + target[name]
        moved += w[:, None] * carried
        turned_normal += w[:, None] * (normal @ rotation.T)
        turned_tangent += w[:, None] * (tangent[:, :3] @ rotation.T)
    covered = weights[:, [at[PREFIX + n] for n in shared]].sum(axis=1)
    if covered.min() < 0.999:
        raise SystemExit(f"a vertex follows a joint the fit does not carry ({covered.min():.3f} covered)")
    turned_normal /= np.linalg.norm(turned_normal, axis=1, keepdims=True)
    turned_tangent /= np.linalg.norm(turned_tangent, axis=1, keepdims=True)
    lines.append(f"fitted body: {np.ptp(moved[:, 1]):.2f} m tall, feet at y = {moved[:, 1].min():+.3f} m, {np.ptp(moved[:, 0]):.2f} m wide")

    order = np.argsort(-weights, axis=1)[:, :4]
    top = np.take_along_axis(weights, order, axis=1)
    top /= top.sum(axis=1, keepdims=True)
    order[top == 0.0] = 0
    return moved, turned_normal, np.hstack([turned_tangent, tangent[:, 3:4]]), order, top, lines


class Writer:
    def __init__(self):
        self.views, self.accessors, self.chunks, self.size = [], [], [], 0

    def view(self, data, target=None):
        self.chunks.append(data + b"\0" * (-len(data) % 4))
        view = {"buffer": 0, "byteOffset": self.size, "byteLength": len(data)}
        if target:
            view["target"] = target
        self.size += len(self.chunks[-1])
        self.views.append(view)
        return len(self.views) - 1

    def accessor(self, values, kind, shape, target=None, bounds=False, extra=None):
        array = np.ascontiguousarray(values, dtype=np.dtype(COMPONENT[kind]))
        entry = {"bufferView": self.view(array.tobytes(), target), "componentType": kind, "count": len(array), "type": shape}
        if bounds:
            entry["min"] = [float(v) for v in array.min(axis=0)]
            entry["max"] = [float(v) for v in array.max(axis=0)]
        entry.update(extra or {})
        self.accessors.append(entry)
        return len(self.accessors) - 1

    def copy(self, glb, index):
        """An accessor of another file, its bytes repacked tight."""
        source = dict(glb.doc["accessors"][index])
        values = glb.read(index) if not source.get("normalized") else None
        if values is None:
            raise SystemExit("normalized accessors are not copied")
        source.pop("byteOffset", None)
        source["bufferView"] = self.view(np.ascontiguousarray(values).tobytes())
        self.accessors.append(source)
        return len(self.accessors) - 1


def build(body, skeleton, fitted, name, clips):
    position, normal, tangent, joints, weights = fitted
    doc = json.loads(json.dumps(skeleton.doc))
    if not clips:
        doc.pop("animations", None)
    writer = Writer()
    remap = {}

    def kept(index):
        if index not in remap:
            remap[index] = writer.copy(skeleton, index)
        return remap[index]

    for skin in doc["skins"]:
        skin["inverseBindMatrices"] = kept(skin["inverseBindMatrices"])
    for animation in doc.get("animations", []):
        # The body's idle holds the arms as the bind pose does, out from the sides as in its concept art. The other
        # clips, and the rest of the idle, are the skeleton file's own.
        held = {}
        if animation.get("name") == "idle":
            for channel in animation["channels"]:
                node = doc["nodes"][channel["target"]["node"]]
                if channel["target"]["path"] == "rotation" and node.get("name", "").endswith(("Arm", "ForeArm", "Hand")):
                    held[channel["sampler"]] = node.get("rotation", [0.0, 0.0, 0.0, 1.0])
        for index, sampler in enumerate(animation["samplers"]):
            sampler["input"] = kept(sampler["input"])
            if index in held:
                count = skeleton.doc["accessors"][sampler["output"]]["count"]
                sampler["output"] = writer.accessor(np.tile(held[index], (count, 1)), 5126, "VEC4")
            else:
                sampler["output"] = kept(sampler["output"])

    primitive = body.primitive()
    attributes = {
        "POSITION": writer.accessor(position, 5126, "VEC3", 34962, bounds=True),
        "NORMAL": writer.accessor(normal, 5126, "VEC3", 34962),
        "TANGENT": writer.accessor(tangent, 5126, "VEC4", 34962),
        "TEXCOORD_0": writer.accessor(body.read(primitive["attributes"]["TEXCOORD_0"]), 5126, "VEC2", 34962),
        "JOINTS_0": writer.accessor(joints, 5121, "VEC4", 34962),
        "WEIGHTS_0": writer.accessor(weights, 5126, "VEC4", 34962),
    }
    indices = writer.accessor(body.read(primitive["indices"]).reshape(-1), 5125, "SCALAR", 34963)
    doc["meshes"] = [{"name": name, "primitives": [{"attributes": attributes, "indices": indices, "material": 0}]}]

    doc["images"] = []
    for image in body.doc["images"]:
        doc["images"].append({"mimeType": image["mimeType"], "bufferView": writer.view(body.view_bytes(image["bufferView"]))})
    doc["samplers"] = body.doc["samplers"]
    doc["textures"] = body.doc["textures"]
    material = json.loads(json.dumps(body.doc["materials"][0]))
    material["name"] = name
    # Skin and cloth: the metal channel the bake wrote is ignored.
    material["pbrMetallicRoughness"]["metallicFactor"] = 0.0
    doc["materials"] = [material]
    doc["accessors"] = writer.accessors
    doc["bufferViews"] = writer.views
    doc["buffers"] = [{"byteLength": writer.size}]
    doc["asset"]["generator"] = "tools/fit_exile_body.py"
    text = json.dumps(doc, separators=(",", ":")).encode()
    text += b" " * (-len(text) % 4)
    binary = b"".join(writer.chunks)
    return b"glTF" + struct.pack("<II", 2, 28 + len(text) + len(binary)) + struct.pack("<I4s", len(text), b"JSON") + text \
        + struct.pack("<I4s", len(binary), b"BIN\0") + binary


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    for flag in ("--body", "--rig", "--skeleton", "--out"):
        parser.add_argument(flag, required=True)
    parser.add_argument("--name", default="SK_ExileBody")
    parser.add_argument("--clips", action="store_true",
                        help="keep the skeleton file's clips, to look at the body in motion; the result must not be committed")
    args = parser.parse_args()
    body, rig, skeleton = Glb(args.body), Glb(args.rig), Glb(args.skeleton)
    name_rig(rig)
    *fitted, lines = fit(body, rig, skeleton)
    for line in lines:
        print(line)
    data = build(body, skeleton, fitted, args.name, args.clips)
    open(args.out, "wb").write(data)
    print(f"wrote {args.out}: {len(data) / 1e6:.1f} MB, {'with' if args.clips else 'without'} the skeleton file's clips")
    if not args.clips:
        sidecar = provenance.write_sidecar(args.out, source=SOURCE, licence="own-work", source_file=args.body, clip_origins={})
        print(f"wrote {sidecar}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
