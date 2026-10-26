"""Builds the settlement kit's Kenney pilot pieces (SEED-681) from the CC0 Fantasy Town Kit 2.0 GLBs.

  blender.exe -b --python build_kit_cc0.py -- <Fantasy Town Kit "Models/GLB format" dir> <out dir>

Each output is ONE mesh node (the kit cooker wants exactly one mesh per file) standing on y = 0, centred on the
origin, with the node conventions of docs/art-pipeline.md: a SOCKET_door empty on the houses and a UBX_ box that
is never rendered. The source modules are 1 m cells; the pieces are scaled here, once, so nothing is scaled at run time.
Kenney's walls sit on the +x edge of a cell, so an edge is placed by turning a wall about the cell centre.
"""
import math
import os
import sys

import bpy
from mathutils import Matrix, Vector

SRC = sys.argv[sys.argv.index("--") + 1]
OUT = sys.argv[sys.argv.index("--") + 2]

templates = {}
shared = {}


def shared_material(material):
    """Every module uses the one colormap atlas; importing each GLB makes colormap.001, .002 ... so fold them into one slot.
    Kenney's roughness 1 is outside the renderer's clamp, so the shared material is set to 0.8."""
    if "colormap" not in shared:
        shared["colormap"] = material
        material.name = "colormap"
        bsdf = material.node_tree.nodes.get("Principled BSDF")
        if bsdf is not None:
            bsdf.inputs["Roughness"].default_value = 0.8
            bsdf.inputs["Metallic"].default_value = 0.0
    return shared["colormap"]


def load(name):
    if name in templates:
        return templates[name]
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=os.path.join(SRC, name + ".glb"))
    meshes = [(o, o.matrix_world.copy()) for o in bpy.data.objects if o not in before and o.type == "MESH"]
    for obj, _ in meshes:
        for slot in obj.material_slots:
            slot.material = shared_material(slot.material)
    templates[name] = meshes
    return meshes


def place(name, matrix, parts):
    for obj, world in load(name):
        copy = obj.copy()
        copy.data = obj.data.copy()
        copy.parent = None
        bpy.context.scene.collection.objects.link(copy)
        copy.matrix_world = matrix @ world
        parts.append(copy)


def turn(deg):
    return Matrix.Rotation(math.radians(deg), 4, "Z")


def scale(sx, sy, sz):
    return Matrix.Diagonal((sx, sy, sz, 1.0))


def join(parts, name):
    bpy.ops.object.select_all(action="DESELECT")
    for p in parts:
        p.select_set(True)
    bpy.context.view_layer.objects.active = parts[0]
    bpy.ops.object.join()
    obj = bpy.context.view_layer.objects.active
    obj.name = name
    obj.data.name = name
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    return obj


def bounds(obj):
    pts = [obj.matrix_world @ v.co for v in obj.data.vertices]
    return [min(p[i] for p in pts) for i in range(3)], [max(p[i] for p in pts) for i in range(3)]


def add_extras(obj, socket_at, name):
    lo, hi = bounds(obj)
    box = bpy.data.objects.new("UBX_" + name, bpy.data.meshes.new("UBX_" + name))
    cx, cy = (lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2
    corners = [(x, y, z) for x in (lo[0], hi[0]) for y in (lo[1], hi[1]) for z in (0.0, hi[2])]
    faces = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]
    box.data.from_pydata(corners, [], faces)
    bpy.context.scene.collection.objects.link(box)
    box.parent = obj
    if socket_at is not None:
        socket = bpy.data.objects.new("SOCKET_door", None)
        socket.empty_display_type = "ARROWS"
        socket.location = socket_at
        bpy.context.scene.collection.objects.link(socket)
        socket.parent = obj
    return cx, cy


def ridge_along_x(name):
    top = []
    for obj, world in load(name):
        pts = [world @ v.co for v in obj.data.vertices]
        peak = max(p.z for p in pts)
        top += [p for p in pts if p.z > 0.8 * peak]
    return max(p.x for p in top) - min(p.x for p in top) > max(p.y for p in top) - min(p.y for p in top)


def house(name, wall, door, window, roof, along, across, height, roof_height):
    """Two cells along x (the ridge), one across; the door faces -y, which glTF exports as +z."""
    parts = []
    cells = (-0.5, 0.5)
    for cx in cells:
        for sign, piece in ((-1, door if cx > 0 else window), (1, wall)):
            m = Matrix.Translation((cx * along, sign * (across / 2 - 0.5 * along), 0)) @ scale(along, along, height) @ turn(270.0 if sign < 0 else 90.0)
            place(piece, m, parts)
    for sign in (-1, 1):
        m = Matrix.Translation((sign * 0.5 * along, 0, 0)) @ scale(along, across, height) @ turn(0.0 if sign > 0 else 180.0)
        place(wall, m, parts)
    roof_turn = turn(0.0 if ridge_along_x(roof) else 90.0)
    for cx in cells:
        place(roof, Matrix.Translation((cx * along, 0, height)) @ scale(along, across / 1.1, roof_height) @ roof_turn, parts)
    return join(parts, name)


def main():
    os.makedirs(OUT, exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    results = {}

    specs = [
        ("SM_KenneyHouseWood", dict(wall="wall-wood", door="wall-wood-door", window="wall-wood-window-shutters", roof="roof-high-gable", along=1.8, across=3.3, height=1.8, roof_height=0.9)),
        ("SM_KenneyHouseStone", dict(wall="wall", door="wall-door", window="wall-window-shutters", roof="roof-gable", along=1.8, across=3.3, height=1.8, roof_height=1.8)),
    ]
    for name, spec in specs:
        obj = house(name, **spec)
        lo, hi = bounds(obj)
        add_extras(obj, (0.9, lo[1] - 0.3, 0.0), name)
        results[name] = (lo, hi)
        export(name)

    parts = []
    for i, cx in enumerate((-0.5, 0.5)):
        for row in range(2):
            m = Matrix.Translation((cx * 2.0, -0.9, row * 1.5)) @ Matrix.Diagonal((2.0, 2.0, 1.5, 1.0)) @ turn(90.0)
            place("wall", m, parts)
    obj = join(parts, "SM_KenneyWallBay")
    add_extras(obj, None, "SM_KenneyWallBay")
    results["SM_KenneyWallBay"] = bounds(obj)
    export("SM_KenneyWallBay")

    parts = []
    place("stall-red", Matrix.Diagonal((2.0, 2.0, 2.0, 1.0)), parts)
    obj = join(parts, "SM_KenneyStall")
    add_extras(obj, None, "SM_KenneyStall")
    results["SM_KenneyStall"] = bounds(obj)
    export("SM_KenneyStall")

    for name, (lo, hi) in results.items():
        print("RESULT", name, "half", round((hi[0] - lo[0]) / 2, 2), round((hi[1] - lo[1]) / 2, 2), "height", round(hi[2], 2), "centre", round((hi[0] + lo[0]) / 2, 2), round((hi[1] + lo[1]) / 2, 2), flush=True)


def export(name):
    """Exports only this piece (mesh, its socket and its box), then removes it so the next piece starts clean."""
    keep = [o for o in bpy.data.objects if o.name == name or (o.parent is not None and o.parent.name == name)]
    bpy.ops.object.select_all(action="DESELECT")
    for o in keep:
        o.select_set(True)
    bpy.ops.export_scene.gltf(filepath=os.path.join(OUT, name + ".glb"), export_format="GLB", use_selection=True, export_yup=True)
    for o in keep:
        bpy.data.objects.remove(o, do_unlink=True)


main()
