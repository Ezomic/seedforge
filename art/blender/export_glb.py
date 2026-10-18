"""Turns the FBX files under art/ into GLB files the Ichor cooker (cgltf + ozz) can take.

  blender.exe -b --python export_glb.py -- exile <art/exile dir> <out dir> [Walk Run ...]
  blender.exe -b --python export_glb.py -- static <in.fbx> <out dir> [--ground] [--scale S]

exile: imports SK_Exile.fbx (mesh + rig) and every SK_Exile_<Clip>.fbx, and
  1. drops the whole rig so the mesh stands with its lowest vertex at z = 0 (Ichor puts the feet on the origin),
  2. adds a "root" bone at the origin above mixamorig:Hips (Ichor treats joint 0 as the root-motion joint,
     subtracts its whole translation from the pose, and keeps only its x/z travel),
  3. copies each clip onto the mesh rig by world-space bone orientation (the clip FBX rigs have a
     different rest pose from the mesh rig, and a cooked clip is only valid against its own bind),
  4. moves the horizontal hips travel onto "root", leaving the vertical hips bounce on the hips,
  5. exports ONE glTF binary: +Y up, one skin, one animation per clip named in lower case (walk, run ...),
     linear keys, every bone kept, tangents written.
prop <in.fbx> <out dir> <spear|shield> [--grip M]: re-exports a hand prop with a grip/centre pivot
static: re-exports a static mesh as GLB (+Y up), optionally standing it on z = 0 and scaling it.
"""
import bpy
import math
import os
import sys
from mathutils import Matrix, Quaternion, Vector

ROOT = "root"


def log(msg):
    print("[fbx-fix] " + msg, flush=True)


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def import_fbx(path):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=path)
    return [o for o in bpy.data.objects if o not in before]


def first(objs, kind):
    return next(o for o in objs if o.type == kind)


def bone_order(arm):
    order = []

    def walk(bone):
        order.append(bone.name)
        for child in bone.children:
            walk(child)

    for bone in arm.data.bones:
        if bone.parent is None:
            walk(bone)
    return order


def lower_mesh_to_floor(arm, mesh):
    zmin = min((mesh.matrix_world @ v.co).z for v in mesh.data.vertices)
    dz = -zmin
    mesh.data.transform(Matrix.Translation((0.0, 0.0, dz)))
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.mode_set(mode='EDIT')
    for eb in arm.data.edit_bones:
        eb.use_connect = False
    for eb in arm.data.edit_bones:
        eb.head.z += dz
        eb.tail.z += dz
    bpy.ops.object.mode_set(mode='OBJECT')
    return dz


def add_root_bone(arm):
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.mode_set(mode='EDIT')
    eb = arm.data.edit_bones.new(ROOT)
    eb.head = Vector((0.0, 0.0, 0.0))
    eb.tail = Vector((0.0, 0.1, 0.0))
    eb.roll = 0.0
    hips = [b for b in arm.data.edit_bones if b.parent is None and b.name != ROOT]
    for h in hips:
        h.parent = eb
        h.use_connect = False
    bpy.ops.object.mode_set(mode='OBJECT')


def rest_rel(arm, name):
    bone = arm.data.bones[name]
    if bone.parent is None:
        return bone.matrix_local.copy()
    return bone.parent.matrix_local.inverted() @ bone.matrix_local


def capture_clip(path):
    """World-space rotation of every bone and world position of the hips, per frame."""
    objs = import_fbx(path)
    rig = first(objs, 'ARMATURE')
    action = rig.animation_data.action
    fs, fe = int(round(action.frame_range[0])), int(round(action.frame_range[1]))
    scene = bpy.context.scene
    frames = []
    for f in range(fs, fe + 1):
        scene.frame_set(f)
        rot = {}
        for pb in rig.pose.bones:
            rot[pb.name] = (rig.matrix_world @ pb.matrix).to_quaternion()
        hips = (rig.matrix_world @ rig.pose.bones['mixamorig:Hips'].matrix).translation.copy()
        heads = {pb.name: (rig.matrix_world @ pb.matrix).translation.copy() for pb in rig.pose.bones}
        frames.append((rot, hips, heads))
    rest_hips = frames[0][1].copy()
    # Position of every joint head, to check the retarget afterwards.
    scene.frame_set(fs)
    heads0 = {pb.name: (rig.matrix_world @ pb.matrix).translation.copy() for pb in rig.pose.bones}
    for o in objs:
        bpy.data.objects.remove(o, do_unlink=True)
    bpy.data.actions.remove(action)
    return fs, frames, rest_hips, heads0


def child_dir_pairs(arm):
    """For each bone, the child heads whose mean direction defines the bone's swing."""
    pairs = {}
    for bone in arm.data.bones:
        kids = [c for c in bone.children if c.name != ROOT]
        if bone.name == 'mixamorig:Hips':
            kids = [c for c in kids if c.name == 'mixamorig:Spine']
        pairs[bone.name] = [c.name for c in kids]
    return pairs


def frame_from(up, lat):
    z = up.normalized()
    x = (lat - z * lat.dot(z)).normalized()
    y = z.cross(x)
    return Matrix((x, y, z)).transposed().to_quaternion()


def target_rotations(arm, rest_heads, rest_rot, pairs, heads):
    """World rotation per bone that reproduces the clip's joint directions on the mesh rig.
    Swing only: the roll of a bone about its own axis follows the rest pose."""
    out = {}
    order = bone_order(arm)
    for n in order:
        if n == ROOT:
            continue
        kids = pairs[n]
        if n == 'mixamorig:Hips':
            up_r = rest_heads['mixamorig:Spine'] - rest_heads[n]
            lat_r = rest_heads['mixamorig:LeftUpLeg'] - rest_heads['mixamorig:RightUpLeg']
            up_c = heads['mixamorig:Spine'] - heads[n]
            lat_c = heads['mixamorig:LeftUpLeg'] - heads['mixamorig:RightUpLeg']
            out[n] = frame_from(up_c, lat_c) @ frame_from(up_r, lat_r).inverted() @ rest_rot[n]
        elif kids:
            d_r = sum(((rest_heads[k] - rest_heads[n]) for k in kids), Vector()).normalized()
            d_c = sum(((heads[k] - heads[n]) for k in kids), Vector()).normalized()
            out[n] = d_r.rotation_difference(d_c) @ rest_rot[n]
        else:
            parent = arm.data.bones[n].parent
            swing = out[parent.name] @ rest_rot[parent.name].inverted()
            out[n] = swing @ rest_rot[n]
    return out


def bake_clip(arm, name, fs, frames, clip_rest_hips):
    scene = bpy.context.scene
    order = [n for n in bone_order(arm)]
    rel_rot = {n: rest_rel(arm, n).to_quaternion() for n in order}
    rest_rot = {n: arm.data.bones[n].matrix_local.to_quaternion() for n in order}
    mesh_rest_hips = arm.data.bones['mixamorig:Hips'].head_local.copy()
    offset = mesh_rest_hips - clip_rest_hips

    for pb in arm.pose.bones:
        pb.rotation_mode = 'QUATERNION'
    if arm.animation_data is None:
        arm.animation_data_create()
    action = bpy.data.actions.new(name)
    arm.animation_data.action = action

    rest_heads = {n: arm.data.bones[n].head_local.copy() for n in order}
    pairs = child_dir_pairs(arm)
    for i, (_, hips, heads) in enumerate(frames):
        frame = i
        rot = target_rotations(arm, rest_heads, rest_rot, pairs, heads)
        world = {}
        for n in order:
            bone = arm.data.bones[n]
            pb = arm.pose.bones[n]
            if n == ROOT:
                p = hips + offset
                pb.location = Vector((p.x - mesh_rest_hips.x, p.y - mesh_rest_hips.y, 0.0))
                pb.rotation_quaternion = Quaternion((1, 0, 0, 0))
                world[n] = Quaternion((1, 0, 0, 0))
                pb.keyframe_insert('location', frame=frame)
                pb.keyframe_insert('rotation_quaternion', frame=frame)
                continue
            parent_world = world[bone.parent.name] if bone.parent else Quaternion((1, 0, 0, 0))
            base = parent_world @ rel_rot[n]
            basis = base.inverted() @ rot[n]
            pb.rotation_quaternion = basis
            world[n] = parent_world @ rel_rot[n] @ basis
            pb.keyframe_insert('rotation_quaternion', frame=frame)
            if n == 'mixamorig:Hips':
                p = hips + offset
                root_t = Vector((p.x - mesh_rest_hips.x, p.y - mesh_rest_hips.y, 0.0))
                loc = p - root_t - bone.head_local
                pb.location = rest_rot[n].inverted() @ loc
                pb.keyframe_insert('location', frame=frame)
    return action


def export_glb(path, extra=None):
    kwargs = dict(
        filepath=path,
        export_format='GLB',
        export_yup=True,
        export_apply=True,
        export_texcoords=True,
        export_normals=True,
        export_tangents=True,
        export_materials='EXPORT',
        export_image_format='AUTO',
        export_cameras=False,
        export_lights=False,
        export_skins=True,
        export_def_bones=False,
        export_animations=True,
        export_animation_mode='ACTIONS',
        export_force_sampling=True,
        export_optimize_animation_size=False,
        export_anim_single_armature=True,
        export_reset_pose_bones=True,
    )
    if extra:
        kwargs.update(extra)
    bpy.ops.export_scene.gltf(**kwargs)
    log("wrote {} ({} bytes)".format(path, os.path.getsize(path)))


def run_exile(src, out, clips):
    reset()
    mesh_objs = import_fbx(os.path.join(src, "SK_Exile.fbx"))
    arm = first(mesh_objs, 'ARMATURE')
    mesh = first(mesh_objs, 'MESH')
    dz = lower_mesh_to_floor(arm, mesh)
    log("lowered rig and mesh by {:.3f} m, mesh min z is now 0".format(dz))
    add_root_bone(arm)
    log("joints: {} (root first: {})".format(len(arm.data.bones), [b.name for b in arm.data.bones if b.parent is None]))

    scene = bpy.context.scene
    scene.render.fps = 24
    actions = []
    clip_heads = {}
    clip_off = {}
    last = 1
    for clip in clips:
        fs, frames, clip_rest_hips, _ = capture_clip(os.path.join(src, "SK_Exile_{}.fbx".format(clip)))
        action = bake_clip(arm, clip.lower(), fs, frames, clip_rest_hips)
        action.use_fake_user = True
        actions.append((clip.lower(), len(frames)))
        clip_heads[clip.lower()] = [f[2] for f in frames]
        clip_off[clip.lower()] = arm.data.bones['mixamorig:Hips'].head_local - clip_rest_hips
        last = max(last, len(frames) - 1)
        log("clip {}: {} frames".format(clip.lower(), len(frames)))
    arm.animation_data.action = None
    scene.frame_start = 0
    scene.frame_end = last

    # Report where the hips go and where the feet are, per clip, to prove the retarget.
    report = []
    for name, n in actions:
        act = bpy.data.actions[name]
        arm.animation_data.action = act
        scene.frame_set(0)
        r0 = (arm.matrix_world @ arm.pose.bones[ROOT].matrix).translation.copy()
        h0 = (arm.matrix_world @ arm.pose.bones['mixamorig:Hips'].matrix).translation.copy()
        scene.frame_set(n - 1)
        r1 = (arm.matrix_world @ arm.pose.bones[ROOT].matrix).translation.copy()
        feet_z = []
        for f in range(0, n, max(1, n // 12)):
            scene.frame_set(f)
            for side in ("LeftFoot", "RightFoot"):
                feet_z.append((arm.matrix_world @ arm.pose.bones["mixamorig:" + side].matrix).translation.z)
        worst = (0.0, "")
        for f in range(0, n, max(1, n // 12)):
            scene.frame_set(f)
            for pb in arm.pose.bones:
                if pb.name == ROOT:
                    continue
                want = clip_heads[name][f][pb.name] + clip_off[name]
                err = (arm.matrix_world @ pb.matrix).translation - want
                if err.length > worst[0]:
                    worst = (err.length, "{}@{}".format(pb.name.split(':')[1], f))
        report.append("{}: max joint position error vs source clip {:.3f} m ({}); root travel {} (len {:.2f} m), hips z at start {:.3f}, foot head z min {:.3f} max {:.3f}".format(
            name, worst[0], worst[1], tuple(round(v, 2) for v in (r1 - r0)), (r1 - r0).length, h0.z, min(feet_z), max(feet_z)))
    arm.animation_data.action = None
    for line in report:
        log(line)

    os.makedirs(out, exist_ok=True)
    export_glb(os.path.join(out, "SK_Exile.glb"))


def run_static(path, out, ground, scale):
    reset()
    objs = import_fbx(path)
    meshes = [o for o in objs if o.type == 'MESH']
    for o in meshes:
        bpy.context.view_layer.objects.active = o
        o.select_set(True)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    if scale != 1.0:
        for o in meshes:
            o.data.transform(Matrix.Scale(scale, 4))
    if ground:
        lo = Vector((1e9, 1e9, 1e9))
        hi = Vector((-1e9, -1e9, -1e9))
        for o in meshes:
            for v in o.data.vertices:
                for i in range(3):
                    lo[i] = min(lo[i], v.co[i])
                    hi[i] = max(hi[i], v.co[i])
        shift = Matrix.Translation((-(lo.x + hi.x) / 2, -(lo.y + hi.y) / 2, -lo.z))
        for o in meshes:
            o.data.transform(shift)
    os.makedirs(out, exist_ok=True)
    export_glb(os.path.join(out, os.path.splitext(os.path.basename(path))[0] + ".glb"))


def run_prop(path, out, kind, grip):
    """Spear: origin on the shaft `grip` metres from the butt, length along +Y in the GLB.
    Shield: origin at the centre, face normal along +Y in the GLB (outer face assumed to be Blender -Y)."""
    reset()
    objs = import_fbx(path)
    meshes = [o for o in objs if o.type == 'MESH']
    for o in meshes:
        bpy.context.view_layer.objects.active = o
        o.select_set(True)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    if kind == "shield":
        for o in meshes:
            o.data.transform(Matrix.Rotation(math.radians(-90), 4, 'X'))
    lo = Vector((1e9, 1e9, 1e9))
    hi = Vector((-1e9, -1e9, -1e9))
    for o in meshes:
        for v in o.data.vertices:
            for i in range(3):
                lo[i] = min(lo[i], v.co[i])
                hi[i] = max(hi[i], v.co[i])
    cx, cy = (lo.x + hi.x) / 2, (lo.y + hi.y) / 2
    cz = lo.z + grip if kind == "spear" else (lo.z + hi.z) / 2
    shift = Matrix.Translation((-cx, -cy, -cz))
    for o in meshes:
        o.data.transform(shift)
    os.makedirs(out, exist_ok=True)
    export_glb(os.path.join(out, os.path.splitext(os.path.basename(path))[0] + ".glb"))


def main():
    args = sys.argv[sys.argv.index("--") + 1:]
    mode = args[0]
    if mode == "exile":
        clips = args[3:] or ["Idle", "Walk", "Run", "Swing", "Thrust", "Block", "Dodge", "Hit", "Death"]
        run_exile(args[1], args[2], clips)
    elif mode == "prop":
        grip = float(args[args.index("--grip") + 1]) if "--grip" in args else 0.9
        run_prop(args[1], args[2], args[3], grip)
    elif mode == "static":
        scale = float(args[args.index("--scale") + 1]) if "--scale" in args else 1.0
        run_static(args[1], args[2], "--ground" in args, scale)
    else:
        raise SystemExit("mode must be exile or static")


main()
