# Models the hoplite kit in Blender and renders previews. Nothing here touches Unreal.
#
#   blender.exe --background --factory-startup --python art/blender/build_kit.py
#
# Source art lives outside the UE project on purpose: the .uasset is a build output, not
# the master copy, and a generated mesh you cannot re-open is a dead end.
#
# Everything is lathed from a profile rather than welded out of primitives. A shield is a
# revolved curve, and that is the difference between something that reads as bronze and
# something that reads as a cylinder with a torus stuck on it.
#
# Units are metres, +Z up, and every piece is modelled at real scale so it can be checked
# against a 1.8m figure rather than eyeballed.

import bpy
import bmesh
import mathutils
import math
import os
import sys

OUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "renders")
BLEND = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ichor_kit.blend")


def log(m):
    print("[ichor-blender] {}".format(m), flush=True)


# ------------------------------------------------------------------ scene setup

def wipe():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def lathe(name, profile, steps=64, axis=(0, 0, 1)):
    """Revolve a 2D profile given as (radius, height) pairs into a solid of revolution."""
    bm = bmesh.new()
    verts = [bm.verts.new((r, 0.0, z)) for r, z in profile]
    for i in range(len(verts) - 1):
        bm.edges.new((verts[i], verts[i + 1]))
    bm.verts.ensure_lookup_table()
    bm.edges.ensure_lookup_table()

    bmesh.ops.spin(bm, geom=bm.verts[:] + bm.edges[:], cent=(0, 0, 0), axis=axis,
                   angle=2.0 * math.pi, steps=steps, use_merge=True)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-5)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)

    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()

    obj = bpy.data.objects.new(name, me)
    bpy.context.collection.objects.link(obj)
    return obj


def shade_smooth(obj):
    """Smooth shading only. use_auto_smooth was removed in 4.1 and its replacement is a
    geometry-node asset that is awkward to reach headlessly - the bevels already give
    these shapes their hard edges, so the split-normal pass buys nothing here."""
    for p in obj.data.polygons:
        p.use_smooth = True
    return obj


def bevel(obj, width=0.004, segments=2):
    m = obj.modifiers.new("bevel", "BEVEL")
    m.width = width
    m.segments = segments
    m.limit_method = 'ANGLE'
    m.angle_limit = math.radians(40)
    return obj


# ------------------------------------------------------------------ materials

def principled(name, base, metallic, roughness):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nt = mat.node_tree
    bsdf = nt.nodes.get("Principled BSDF")
    bsdf.inputs["Base Color"].default_value = (base[0], base[1], base[2], 1.0)
    bsdf.inputs["Metallic"].default_value = metallic
    bsdf.inputs["Roughness"].default_value = roughness
    return mat, nt, bsdf


def bronze():
    """Bronze. Restrained on purpose - the first pass drove roughness from 0.22 to 0.52
    across a scale-45 noise with a 0.12 bump, and it read as mottled sandstone rather than
    metal. Metal is sold by the reflection, not by the surface detail."""
    mat, nt, bsdf = principled("M_Bronze", (0.640, 0.400, 0.190), 1.0, 0.28)

    noise = nt.nodes.new("ShaderNodeTexNoise")
    noise.inputs["Scale"].default_value = 220.0
    noise.inputs["Detail"].default_value = 3.0

    ramp = nt.nodes.new("ShaderNodeValToRGB")
    ramp.color_ramp.elements[0].position = 0.40
    ramp.color_ramp.elements[0].color = (0.24, 0.24, 0.24, 1)
    ramp.color_ramp.elements[1].position = 0.62
    ramp.color_ramp.elements[1].color = (0.34, 0.34, 0.34, 1)
    nt.links.new(noise.outputs["Fac"], ramp.inputs["Fac"])
    nt.links.new(ramp.outputs["Color"], bsdf.inputs["Roughness"])

    bump = nt.nodes.new("ShaderNodeBump")
    bump.inputs["Strength"].default_value = 0.02
    nt.links.new(noise.outputs["Fac"], bump.inputs["Height"])
    nt.links.new(bump.outputs["Normal"], bsdf.inputs["Normal"])
    return mat


def patina_bronze():
    """Bronze with a little verdigris. The mask is narrow and the green is dark - the
    first attempt mixed at full strength across a wide ramp and produced a beach towel."""
    mat, nt, bsdf = principled("M_BronzePatina", (0.640, 0.400, 0.190), 1.0, 0.32)

    noise = nt.nodes.new("ShaderNodeTexNoise")
    noise.inputs["Scale"].default_value = 14.0
    noise.inputs["Detail"].default_value = 6.0

    ramp = nt.nodes.new("ShaderNodeValToRGB")
    ramp.color_ramp.elements[0].position = 0.52
    ramp.color_ramp.elements[1].position = 0.78

    mix = nt.nodes.new("ShaderNodeMixRGB")
    mix.blend_type = 'MIX'
    mix.inputs["Fac"].default_value = 0.0
    mix.inputs["Color1"].default_value = (0.640, 0.400, 0.190, 1)
    mix.inputs["Color2"].default_value = (0.130, 0.280, 0.225, 1)

    dampen = nt.nodes.new("ShaderNodeMath")
    dampen.operation = 'MULTIPLY'
    dampen.inputs[1].default_value = 0.35

    nt.links.new(noise.outputs["Fac"], ramp.inputs["Fac"])
    nt.links.new(ramp.outputs["Color"], dampen.inputs[0])
    nt.links.new(dampen.outputs["Value"], mix.inputs["Fac"])
    nt.links.new(mix.outputs["Color"], bsdf.inputs["Base Color"])

    bump = nt.nodes.new("ShaderNodeBump")
    bump.inputs["Strength"].default_value = 0.03
    nt.links.new(noise.outputs["Fac"], bump.inputs["Height"])
    nt.links.new(bump.outputs["Normal"], bsdf.inputs["Normal"])
    return mat


def painted(name, rgb):
    """Flat pigment with a little tooth - crests and painted devices."""
    mat, nt, bsdf = principled(name, rgb, 0.0, 0.72)
    noise = nt.nodes.new("ShaderNodeTexNoise")
    noise.inputs["Scale"].default_value = 120.0
    bump = nt.nodes.new("ShaderNodeBump")
    bump.inputs["Strength"].default_value = 0.06
    nt.links.new(noise.outputs["Fac"], bump.inputs["Height"])
    nt.links.new(bump.outputs["Normal"], bsdf.inputs["Normal"])
    return mat


def ash_wood():
    """Cornel-wood shaft: grain stretched along the length."""
    mat, nt, bsdf = principled("M_Wood", (0.32, 0.20, 0.11), 0.0, 0.62)
    tex = nt.nodes.new("ShaderNodeTexNoise")
    tex.inputs["Scale"].default_value = 6.0
    tex.inputs["Detail"].default_value = 6.0
    mapping = nt.nodes.new("ShaderNodeMapping")
    mapping.inputs["Scale"].default_value = (14.0, 14.0, 0.4)
    coord = nt.nodes.new("ShaderNodeTexCoord")
    nt.links.new(coord.outputs["Object"], mapping.inputs["Vector"])
    nt.links.new(mapping.outputs["Vector"], tex.inputs["Vector"])

    mix = nt.nodes.new("ShaderNodeMixRGB")
    mix.inputs["Color1"].default_value = (0.30, 0.18, 0.10, 1)
    mix.inputs["Color2"].default_value = (0.42, 0.28, 0.16, 1)
    nt.links.new(tex.outputs["Fac"], mix.inputs["Fac"])
    nt.links.new(mix.outputs["Color"], bsdf.inputs["Base Color"])
    return mat


def assign(obj, mat, slot=0):
    while len(obj.data.materials) <= slot:
        obj.data.materials.append(None)
    obj.data.materials[slot] = mat


# ------------------------------------------------------------------ the kit

def make_aspis():
    """Hoplon. 0.70m across per the gameplay call, not the historical 0.90m.

    Profile runs from the centre boss out to the rolled rim and back under, so the
    bowl has real thickness and the rim catches a highlight all the way round."""
    r = 0.35
    profile = [
        (0.000, 0.075),
        (0.080, 0.072),
        (0.170, 0.062),
        (0.250, 0.044),
        (0.310, 0.022),
        (r - 0.012, 0.004),
        (r, -0.012),
        (r - 0.006, -0.030),
        (r - 0.030, -0.036),
        (0.300, -0.020),
        (0.180, 0.004),
        (0.070, 0.020),
        (0.000, 0.024),
    ]
    obj = lathe("SM_Aspis", profile, steps=96)
    obj.location = (0, 0, 0)
    assign(obj, patina_bronze())
    bevel(obj, 0.002, 2)
    shade_smooth(obj)
    return obj


def make_dory():
    """2.40m spear, gripped at the origin so the model drops onto a hand bone."""
    parts = []

    shaft = lathe("dory_shaft", [(0.0, -0.70), (0.0125, -0.70), (0.0125, 1.62), (0.0, 1.62)], steps=24)
    assign(shaft, ash_wood())
    parts.append(shaft)

    head = lathe("dory_head", [
        (0.000, 1.62), (0.016, 1.645), (0.030, 1.70), (0.034, 1.76),
        (0.028, 1.83), (0.014, 1.88), (0.000, 1.90),
    ], steps=24)
    assign(head, bronze())
    parts.append(head)

    butt = lathe("dory_butt", [
        (0.000, -0.70), (0.020, -0.70), (0.024, -0.66), (0.012, -0.60), (0.000, -0.58),
    ], steps=20)
    assign(butt, bronze())
    parts.append(butt)

    for p in parts:
        shade_smooth(p)
    return parts


def make_helmet():
    """Corinthian helm. A sphere is the skull; the face is cut out of it.

    The eye slots and the mouth gap are booleans rather than modelled openings - the
    silhouette is what carries this at gameplay distance, and the negative space between
    two eye holes IS the nasal."""
    # Sized off a head, not off a bucket: the first pass came out 29 x 33cm, which is
    # wider than a human skull by half. A helm sits about 22cm across.
    skull = lathe("helm_skull", [
        (0.000, 0.130), (0.044, 0.126), (0.077, 0.107), (0.094, 0.070),
        (0.099, 0.022), (0.097, -0.030), (0.088, -0.074), (0.073, -0.104),
        (0.070, -0.110), (0.082, -0.111), (0.094, -0.081), (0.104, -0.022),
        (0.107, 0.030), (0.101, 0.081), (0.079, 0.118), (0.044, 0.136), (0.000, 0.140),
    ], steps=96)
    skull.scale = (1.0, 1.10, 1.0)
    assign(skull, bronze())
    shade_smooth(skull)

    def cutter(name, loc, scale, rot=(0, 0, 0)):
        bpy.ops.mesh.primitive_cube_add(size=1.0, location=loc)
        c = bpy.context.active_object
        c.name = name
        c.scale = scale
        c.rotation_euler = rot
        return c

    eye_l = cutter("cut_eye_l", (0.085, -0.041, 0.030), (0.10, 0.030, 0.020))
    eye_r = cutter("cut_eye_r", (0.085, 0.041, 0.030), (0.10, 0.030, 0.020))
    mouth = cutter("cut_mouth", (0.078, 0.0, -0.068), (0.11, 0.042, 0.058))

    for c in (eye_l, eye_r, mouth):
        m = skull.modifiers.new("cut_" + c.name, "BOOLEAN")
        m.operation = 'DIFFERENCE'
        m.object = c
        c.hide_render = True
        c.hide_viewport = True

    # A crest is a fin, not a solid of revolution. Lathing it around the Y axis produced a
    # zero-thickness sheet - the 0.000m dimension in the log was the tell. This is an
    # ellipsoid squashed into a blade and cut off flat at the bottom.
    bpy.ops.mesh.primitive_uv_sphere_add(segments=48, ring_count=24, radius=1.0,
                                         location=(0.0, 0.0, 0.120))
    crest = bpy.context.active_object
    crest.name = "helm_crest"
    crest.scale = (0.105, 0.016, 0.062)

    # Cut below 0.130, which is under the skull's 0.140 crown - so the crest roots into
    # the dome instead of hovering a few millimetres above it as it did first time.
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0.0, 0.0, 0.085))
    trim = bpy.context.active_object
    trim.name = "cut_crest"
    trim.scale = (0.4, 0.2, 0.09)
    m = crest.modifiers.new("cut_base", "BOOLEAN")
    m.operation = 'DIFFERENCE'
    m.object = trim
    trim.hide_render = True
    trim.hide_viewport = True

    assign(crest, painted("M_CrestRed", (0.29, 0.038, 0.038)))
    shade_smooth(crest)

    return [skull, crest], [eye_l, eye_r, mouth, trim]


# ------------------------------------------------------------------ preview render

def pick_engine():
    for name in ("BLENDER_EEVEE_NEXT", "BLENDER_EEVEE", "CYCLES"):
        try:
            bpy.context.scene.render.engine = name
            log("render engine: " + name)
            return name
        except Exception:
            continue
    log("WARNING: no known engine accepted; leaving default")
    return bpy.context.scene.render.engine


def studio():
    """A lit environment, not three lamps in a void.

    Metal is almost entirely reflection, so a flat dark world gives it nothing to be. The
    gradient sky is doing more work here than any of the lamps: it is what makes bronze
    look like bronze instead of like stone."""
    world = bpy.data.worlds.new("W")
    bpy.context.scene.world = world
    world.use_nodes = True
    wnt = world.node_tree
    bg = wnt.nodes["Background"]
    bg.inputs["Strength"].default_value = 1.0

    grad = wnt.nodes.new("ShaderNodeTexGradient")
    grad.gradient_type = 'EASING'
    mapping = wnt.nodes.new("ShaderNodeMapping")
    mapping.inputs["Rotation"].default_value = (0.0, math.radians(90), 0.0)
    coord = wnt.nodes.new("ShaderNodeTexCoord")

    ramp = wnt.nodes.new("ShaderNodeValToRGB")
    ramp.color_ramp.elements[0].position = 0.30
    ramp.color_ramp.elements[0].color = (0.045, 0.050, 0.060, 1)
    ramp.color_ramp.elements[1].position = 0.85
    ramp.color_ramp.elements[1].color = (0.62, 0.66, 0.72, 1)

    wnt.links.new(coord.outputs["Generated"], mapping.inputs["Vector"])
    wnt.links.new(mapping.outputs["Vector"], grad.inputs["Vector"])
    wnt.links.new(grad.outputs["Color"], ramp.inputs["Fac"])
    wnt.links.new(ramp.outputs["Color"], bg.inputs["Color"])

    # Something for the underside to catch, so shapes are not lit only from above.
    bpy.ops.mesh.primitive_plane_add(size=12.0, location=(0, 0, -0.45))
    ground = bpy.context.active_object
    ground.name = "ground"
    assign(ground, principled("M_Ground", (0.20, 0.19, 0.17), 0.0, 0.85)[0])

    def lamp(name, loc, energy, size):
        d = bpy.data.lights.new(name, 'AREA')
        d.energy = energy
        d.size = size
        o = bpy.data.objects.new(name, d)
        o.location = loc
        bpy.context.collection.objects.link(o)
        o.constraints.new('TRACK_TO')
        return o

    key = lamp("key", (1.6, -1.9, 1.7), 180, 2.5)
    fill = lamp("fill", (-1.9, -1.2, 0.7), 45, 3.0)
    rim = lamp("rim", (-0.9, 1.9, 1.5), 120, 2.0)

    empty = bpy.data.objects.new("aim", None)
    empty.location = (0, 0, 0)
    bpy.context.collection.objects.link(empty)
    for o in (key, fill, rim):
        o.constraints[0].target = empty

    return [ground]


def show_only(subject, everything, keep):
    """Only the subject renders. The first pass never hid anything for the first shot, so
    the shield, the spear and the helmet all appeared in the same frame."""
    for o in everything:
        o.hide_render = o not in subject and o not in keep


def frame_of(objects):
    """Centre and radius of the subject, so the camera is placed from the geometry rather
    than from a distance I guessed and got wrong three times."""
    xs, ys, zs = [], [], []
    for o in objects:
        for corner in o.bound_box:
            w = o.matrix_world @ mathutils.Vector(corner)
            xs.append(w.x)
            ys.append(w.y)
            zs.append(w.z)
    centre = mathutils.Vector(((min(xs) + max(xs)) / 2,
                               (min(ys) + max(ys)) / 2,
                               (min(zs) + max(zs)) / 2))
    radius = max(max(xs) - min(xs), max(ys) - min(ys), max(zs) - min(zs)) * 0.5
    return centre, max(radius, 0.02)


def render_preview(name, subject, everything, keep, filename, lens=80, pad=2.6):
    scene = bpy.context.scene
    show_only(subject, everything, keep)
    centre, radius = frame_of(subject)

    dist = radius * pad + 0.25
    cam_data = bpy.data.cameras.new("cam_" + name)
    cam_data.lens = lens
    cam = bpy.data.objects.new("cam_" + name, cam_data)
    cam.location = (centre.x + dist * 0.72,
                    centre.y - dist * 0.86,
                    centre.z + dist * 0.42)
    bpy.context.collection.objects.link(cam)
    scene.camera = cam

    target = bpy.data.objects.new("t_" + name, None)
    target.location = centre
    bpy.context.collection.objects.link(target)
    cam.constraints.new('TRACK_TO').target = target

    scene.render.resolution_x = 900
    scene.render.resolution_y = 700
    scene.render.film_transparent = False
    scene.render.filepath = os.path.join(OUT_DIR, filename)
    scene.render.image_settings.file_format = 'PNG'

    try:
        scene.eevee.taa_render_samples = 32
    except Exception:
        pass
    try:
        scene.cycles.samples = 48
    except Exception:
        pass

    bpy.ops.render.render(write_still=True)
    log("rendered {} -> {}".format(name, filename))


def main():
    wipe()
    os.makedirs(OUT_DIR, exist_ok=True)

    aspis = make_aspis()
    dory = make_dory()
    helmet, cutters = make_helmet()
    log("modelled: aspis, dory ({} parts), helmet ({} parts)".format(len(dory), len(helmet)))

    subjects = [aspis] + dory + helmet
    for o in subjects:
        d = o.dimensions
        log("  {:<14} {:.3f} x {:.3f} x {:.3f} m".format(o.name, d.x, d.y, d.z))

    pick_engine()
    keep = studio()
    everything = subjects + cutters

    render_preview("aspis", [aspis], everything, keep, "kit_aspis.png", lens=70)
    render_preview("dory", [dory[1]], everything, keep, "kit_dory_head.png", lens=90, pad=3.0)
    render_preview("helmet", helmet, everything, keep, "kit_helmet.png", lens=85, pad=4.0)
    render_preview("kit", subjects, everything, keep, "kit_all.png", lens=55, pad=1.9)

    bpy.ops.wm.save_as_mainfile(filepath=BLEND)
    log("saved " + BLEND)
    log("DONE")


main()
