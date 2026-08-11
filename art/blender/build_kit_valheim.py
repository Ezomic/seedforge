# The hoplite kit in the Valheim idiom: cheap geometry, cheap textures, expensive light.
#
#   blender.exe --background --factory-startup --python art/blender/build_kit_valheim.py
#
# Three deliberate inversions from the previous pass:
#
#   Faceted, not smooth   - 12-16 lathe steps and flat shading. The facets ARE the style,
#                           so there is no bevel and no smooth-by-angle.
#   Painted, not procedural - real 64px PNG textures written to disk and sampled with
#                           Closest filtering, so texels stay crisp instead of blurring.
#                           These are assets, not shader graphs, and they ship.
#   Lit, not detailed     - warm low key, cool fill, strong rim. Doc 16's thesis and the
#                           reason a five-person team could ship Valheim.
#
# Textures are generated from a fixed seed, so the same kit comes out of every run - the
# same forge-once principle the world generator is built on.
#
# No fog in these previews on purpose. Fog is a world-space effect and it fights the one
# thing Stage 0 measures, which is whether a wind-up silhouette reads.

import bpy
import bmesh
import mathutils
import numpy as np
import math
import os

HERE = os.path.dirname(os.path.abspath(__file__))
TEX_DIR = os.path.join(HERE, "..", "textures")
OUT_DIR = os.path.join(HERE, "..", "renders")
BLEND = os.path.join(HERE, "ichor_kit_valheim.blend")

TEX = 64
SEED = 0x1C407


def log(m):
    print("[ichor-vh] {}".format(m), flush=True)


# ------------------------------------------------------------------ textures

def blocky(w, h, cells, rng):
    """Smoothly interpolated value noise.

    It used to be nearest-neighbour upscaling, which paints literal axis-aligned squares -
    and squares on top of flat-shaded quads on top of Closest filtering meant every part of
    the surface was rectangles. Low resolution is the Valheim look; hard square edges are
    not, and the two are easy to confuse."""
    g = rng.random((cells + 1, cells + 1))

    ys = np.linspace(0, cells, h)
    xs = np.linspace(0, cells, w)
    y0 = np.floor(ys).astype(int)
    x0 = np.floor(xs).astype(int)
    y1 = np.minimum(y0 + 1, cells)
    x1 = np.minimum(x0 + 1, cells)

    ty = (ys - y0)[:, None]
    tx = (xs - x0)[None, :]
    ty = ty * ty * (3.0 - 2.0 * ty)
    tx = tx * tx * (3.0 - 2.0 * tx)

    a = g[np.ix_(y0, x0)]
    b = g[np.ix_(y0, x1)]
    c = g[np.ix_(y1, x0)]
    d = g[np.ix_(y1, x1)]
    return (a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty


def base(w, h, rgb):
    a = np.zeros((h, w, 4), dtype=np.float32)
    a[:, :, 0], a[:, :, 1], a[:, :, 2], a[:, :, 3] = rgb[0], rgb[1], rgb[2], 1.0
    return a


def tint(arr, mask, rgb, amount):
    for c in range(3):
        arr[:, :, c] = arr[:, :, c] * (1 - mask * amount) + rgb[c] * (mask * amount)
    return arr


def tex_bronze(rng):
    """Bronze: broad tonal blotching, verdigris in patches, a few bright scratch texels."""
    # Less orange than the first pass: 0.62/0.39/0.18 under a warm key read as terracotta.
    a = base(TEX, TEX, (0.60, 0.435, 0.185))
    coarse = blocky(TEX, TEX, 8, rng)
    a[:, :, :3] *= (0.87 + 0.24 * coarse)[:, :, None]

    # Patina as occasional flecks, not continents.
    verdigris = np.clip((blocky(TEX, TEX, 9, rng) - 0.62) * 3.2, 0.0, 1.0)
    a = tint(a, verdigris, (0.19, 0.31, 0.26), 0.36)

    grit = blocky(TEX, TEX, 40, rng)
    a[:, :, :3] *= (0.91 + 0.16 * grit)[:, :, None]

    scratch = (rng.random((TEX, TEX)) > 0.991).astype(np.float32)
    a = tint(a, scratch, (0.86, 0.72, 0.44), 0.40)
    return np.clip(a, 0, 1)


def tex_wood(rng):
    """Shaft: grain as vertical bands of varying width, knots as dark clusters."""
    a = base(TEX, TEX, (0.34, 0.21, 0.115))
    cols = rng.random(TEX)
    band = np.repeat(cols[None, :], TEX, axis=0)
    a[:, :, :3] *= (0.80 + 0.36 * band)[:, :, None]

    streak = blocky(TEX, TEX, 24, rng)
    a[:, :, :3] *= (0.90 + 0.18 * streak)[:, :, None]

    knots = (blocky(TEX, TEX, 12, rng) > 0.88).astype(np.float32)
    a = tint(a, knots, (0.16, 0.09, 0.05), 0.7)
    return np.clip(a, 0, 1)


def tex_crest(rng):
    """Dyed horsehair: deep madder red, coarse tonal variation along the length."""
    a = base(TEX, TEX, (0.36, 0.055, 0.05))
    strands = np.repeat(rng.random(TEX)[:, None], TEX, axis=1)
    a[:, :, :3] *= (0.72 + 0.52 * strands)[:, :, None]
    dark = (blocky(TEX, TEX, 16, rng) > 0.80).astype(np.float32)
    a = tint(a, dark, (0.14, 0.02, 0.02), 0.5)
    return np.clip(a, 0, 1)


def tex_crest_dark(rng):
    """The opponent's crest. Same weave, undyed and weathered - so the two sides differ by
    colour alone and the silhouette stays identical, which is what makes it a fair tell."""
    a = base(TEX, TEX, (0.115, 0.110, 0.125))
    strands = np.repeat(rng.random(TEX)[:, None], TEX, axis=1)
    a[:, :, :3] *= (0.74 + 0.48 * strands)[:, :, None]
    dark = (blocky(TEX, TEX, 16, rng) > 0.80).astype(np.float32)
    a = tint(a, dark, (0.05, 0.05, 0.06), 0.5)
    return np.clip(a, 0, 1)


def tex_marble(rng):
    """Weathered marble: warm bone base, grey veining, dirt settling low."""
    a = base(TEX, TEX, (0.80, 0.78, 0.71))
    a[:, :, :3] *= (0.88 + 0.22 * blocky(TEX, TEX, 16, rng))[:, :, None]

    veins = np.zeros((TEX, TEX), dtype=np.float32)
    for _ in range(5):
        x = rng.integers(0, TEX)
        for y in range(TEX):
            x = int(np.clip(x + rng.integers(-1, 2), 0, TEX - 1))
            veins[y, x] = 1.0
            if x + 1 < TEX and rng.random() > 0.6:
                veins[y, x + 1] = 0.6
    a = tint(a, veins, (0.42, 0.42, 0.40), 0.5)
    return np.clip(a, 0, 1)


def tex_leather(rng):
    """Straps and the shield's inner face."""
    a = base(TEX, TEX, (0.30, 0.19, 0.12))
    a[:, :, :3] *= (0.80 + 0.40 * blocky(TEX, TEX, 20, rng))[:, :, None]
    return np.clip(a, 0, 1)


def write_texture(name, arr):
    """Blender stores pixels bottom-up, so the array is flipped on the way out."""
    h, w, _ = arr.shape
    img = bpy.data.images.new(name, width=w, height=h, alpha=True)
    img.pixels.foreach_set(np.flipud(arr).reshape(-1).astype(np.float32))
    img.filepath_raw = os.path.join(TEX_DIR, name + ".png")
    img.file_format = 'PNG'
    img.save()

    # A 8x nearest-neighbour copy purely so the texture can be looked at. Never shipped.
    big = np.repeat(np.repeat(arr, 8, axis=0), 8, axis=1)
    view = bpy.data.images.new(name + "_view", width=w * 8, height=h * 8, alpha=True)
    view.pixels.foreach_set(np.flipud(big).reshape(-1).astype(np.float32))
    view.filepath_raw = os.path.join(TEX_DIR, name + "_view.png")
    view.file_format = 'PNG'
    view.save()

    log("texture {} ({}x{})".format(name, w, h))
    return img


def build_textures():
    os.makedirs(TEX_DIR, exist_ok=True)
    rng = np.random.default_rng(SEED)
    return {
        "bronze": write_texture("T_Bronze", tex_bronze(rng)),
        "wood": write_texture("T_Wood", tex_wood(rng)),
        "crest": write_texture("T_Crest", tex_crest(rng)),
        "crest_dark": write_texture("T_CrestDark", tex_crest_dark(rng)),
        "marble": write_texture("T_Marble", tex_marble(rng)),
        "leather": write_texture("T_Leather", tex_leather(rng)),
    }


# ------------------------------------------------------------------ materials

def textured(name, image, metallic, roughness, rough_range=None):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nt = mat.node_tree
    bsdf = nt.nodes["Principled BSDF"]
    bsdf.inputs["Metallic"].default_value = metallic
    bsdf.inputs["Roughness"].default_value = roughness

    tex = nt.nodes.new("ShaderNodeTexImage")
    tex.image = image
    # Linear, not Closest. Hard texel edges were half of what read as squares.
    tex.interpolation = 'Linear'
    nt.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])

    if rough_range is not None:
        # On metal the base colour mostly tints reflections, so raising texture contrast
        # does almost nothing visible. Roughness is what shows: dull patches break the
        # reflection up and give the surface something to be, without painting squares.
        bw = nt.nodes.new("ShaderNodeRGBToBW")
        nt.links.new(tex.outputs["Color"], bw.inputs["Color"])

        mr = nt.nodes.new("ShaderNodeMapRange")
        mr.inputs["From Min"].default_value = 0.10
        mr.inputs["From Max"].default_value = 0.70
        mr.inputs["To Min"].default_value = rough_range[0]
        mr.inputs["To Max"].default_value = rough_range[1]
        nt.links.new(bw.outputs["Val"], mr.inputs["Value"])
        nt.links.new(mr.outputs["Result"], bsdf.inputs["Roughness"])
    return mat


# ------------------------------------------------------------------ geometry

def lathe(name, profile, steps=14):
    bm = bmesh.new()
    verts = [bm.verts.new((r, 0.0, z)) for r, z in profile]
    for i in range(len(verts) - 1):
        bm.edges.new((verts[i], verts[i + 1]))
    bm.verts.ensure_lookup_table()
    bm.edges.ensure_lookup_table()
    bmesh.ops.spin(bm, geom=bm.verts[:] + bm.edges[:], cent=(0, 0, 0), axis=(0, 0, 1),
                   angle=2.0 * math.pi, steps=steps, use_merge=True)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-5)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    obj = bpy.data.objects.new(name, me)
    bpy.context.collection.objects.link(obj)
    return obj


def unwrap(obj):
    bpy.ops.object.select_all(action='DESELECT')
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    try:
        bpy.ops.uv.smart_project(angle_limit=math.radians(66), island_margin=0.02)
    except Exception as e:
        log("unwrap failed on {}: {}".format(obj.name, e))
    bpy.ops.object.mode_set(mode='OBJECT')
    obj.select_set(False)
    return obj


def assign(obj, mat, slot=0):
    while len(obj.data.materials) <= slot:
        obj.data.materials.append(None)
    obj.data.materials[slot] = mat


def scale_uv(obj, k):
    """smart_project fits every island into 0-1, so a 64px texture ends up stretched across
    the whole object and the texels read the size of a fist. Scaling the UVs up tiles the
    texture instead, which is what sets texel density. This one number is the difference
    between low-res and just plain wrong."""
    uv = obj.data.uv_layers.active
    if uv is None:
        return obj
    for d in uv.data:
        d.uv = (d.uv[0] * k, d.uv[1] * k)
    return obj


def cylindrical_uv(obj, density=5.0):
    """Wraps the texture round the lathe axis: u is the angle, v is height in metres.

    smart_project cut these shapes into islands, and because the texture tiles, each
    island landed on a different part of it - which is what the pale patches on the helmet
    were. A surface of revolution has one natural seam and needs no islands at all.

    Density is tiles per metre, so texel size is constant across every prop rather than
    depending on how big each object happened to be."""
    me = obj.data
    if not me.uv_layers:
        me.uv_layers.new(name="UVMap")
    uv = me.uv_layers.active

    radius = 0.0
    for v in me.vertices:
        radius = max(radius, math.hypot(v.co.x, v.co.y))
    scale_u = max(2.0 * math.pi * radius * density, 0.001)

    for poly in me.polygons:
        angles = []
        for li in poly.loop_indices:
            co = me.vertices[me.loops[li].vertex_index].co
            angles.append(math.atan2(co.y, co.x) / (2.0 * math.pi))

        # The face that straddles the seam would otherwise wrap the whole texture
        # backwards across itself.
        if max(angles) - min(angles) > 0.5:
            angles = [a + 1.0 if a < 0.0 else a for a in angles]

        for li, a in zip(poly.loop_indices, angles):
            co = me.vertices[me.loops[li].vertex_index].co
            uv.data[li].uv = (a * scale_u, co.z * density)
    return obj


def planar_uv(obj, density=5.0, plane="XY"):
    """Flat projection for things that are discs or slabs rather than revolutions. A
    cylindrical map on the shield would smear the whole face out from the centre."""
    me = obj.data
    if not me.uv_layers:
        me.uv_layers.new(name="UVMap")
    uv = me.uv_layers.active

    pick = {"XY": (0, 1), "XZ": (0, 2), "YZ": (1, 2)}[plane]
    for poly in me.polygons:
        for li in poly.loop_indices:
            co = me.vertices[me.loops[li].vertex_index].co
            uv.data[li].uv = (co[pick[0]] * density, co[pick[1]] * density)
    return obj


def finish(obj, mat, mode="cyl", density=5.0, plane="XY", smooth_angle=20.0):
    """Mapped, textured, and smooth-shaded above an angle threshold.

    Flat shading everything made every facet its own visible quad. Smoothing across gentle
    curves while keeping creases sharp is what lets a 500-triangle mesh read as a curved
    object - the low-poly look should live in the silhouette, not in the shading."""
    for p in obj.data.polygons:
        p.use_smooth = True
    if smooth_angle is not None:
        bpy.ops.object.select_all(action='DESELECT')
        bpy.context.view_layer.objects.active = obj
        obj.select_set(True)
        try:
            bpy.ops.object.shade_smooth_by_angle(angle=math.radians(smooth_angle))
        except Exception as e:
            log("shade_smooth_by_angle unavailable ({}); leaving fully smooth".format(e))
        obj.select_set(False)
    if mode == "cyl":
        cylindrical_uv(obj, density)
    else:
        planar_uv(obj, density, plane)
    assign(obj, mat)
    return obj


def extrude_varying(name, points_xz, thicknesses):
    """Like extrude_profile but with per-point thickness, so a shape can taper along its
    length. A crest is a brush of horsehair - fat where it is bound to the helmet, thin at
    the tip - and a slab of constant thickness reads as cardboard."""
    bm = bmesh.new()
    near, far = [], []
    for (x, z), t in zip(points_xz, thicknesses):
        near.append(bm.verts.new((x, -t, z)))
        far.append(bm.verts.new((x, t, z)))
    bm.faces.new(near)
    bm.faces.new(list(reversed(far)))
    n = len(points_xz)
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new((near[i], near[j], far[j], far[i]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    obj = bpy.data.objects.new(name, me)
    bpy.context.collection.objects.link(obj)
    return obj


def extrude_yz(name, points_yz, half_thickness_x, x_centre=0.0):
    """A closed outline in the YZ plane given depth along X. Used to cut the face opening
    as one shape, so the nasal and the cheeks are whatever bronze is left behind."""
    bm = bmesh.new()
    near = [bm.verts.new((x_centre - half_thickness_x, y, z)) for y, z in points_yz]
    far = [bm.verts.new((x_centre + half_thickness_x, y, z)) for y, z in points_yz]
    bm.faces.new(near)
    bm.faces.new(list(reversed(far)))
    n = len(points_yz)
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new((near[i], near[j], far[j], far[i]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    obj = bpy.data.objects.new(name, me)
    bpy.context.collection.objects.link(obj)
    return obj


def extrude_profile(name, points_xz, half_thickness):
    """A closed 2D outline in XZ, given real thickness in Y. Crests, cheek pieces and
    anything else that is a plate rather than a solid of revolution."""
    bm = bmesh.new()
    front = [bm.verts.new((x, -half_thickness, z)) for x, z in points_xz]
    back = [bm.verts.new((x, half_thickness, z)) for x, z in points_xz]
    bm.faces.new(front)
    bm.faces.new(list(reversed(back)))
    n = len(points_xz)
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new((front[i], front[j], back[j], back[i]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    obj = bpy.data.objects.new(name, me)
    bpy.context.collection.objects.link(obj)
    return obj


def make_aspis(mats):
    r = 0.35
    obj = lathe("SM_Aspis", [
        (0.000, 0.070), (0.120, 0.062), (0.240, 0.040), (0.320, 0.014),
        (r, -0.010), (r - 0.020, -0.034), (0.280, -0.018), (0.120, 0.008), (0.000, 0.020),
    ], steps=24)
    return finish(obj, mats["bronze"], mode="planar", density=6.0, plane="XY")


def make_dory(mats):
    shaft = lathe("dory_shaft", [(0.0, -0.70), (0.0125, -0.70), (0.0125, 1.62), (0.0, 1.62)], steps=8)
    head = lathe("dory_head", [
        (0.000, 1.62), (0.020, 1.66), (0.034, 1.74), (0.024, 1.84), (0.000, 1.90),
    ], steps=8)
    butt = lathe("dory_butt", [(0.000, -0.70), (0.022, -0.68), (0.010, -0.60), (0.000, -0.58)], steps=8)
    return [finish(shaft, mats["wood"], density=7.0),
            finish(head, mats["bronze"], density=6.0),
            finish(butt, mats["bronze"], density=6.0)]


def make_helmet(mats):
    """Corinthian helm as one piece of bronze with a hole in it.

    The previous version added cheek plates onto a dome, which is backwards and read as
    exactly that - bars stuck on a shape. A real helm is a single shell; the nasal is the
    tongue of metal hanging into the opening from the brow, and the cheeks are simply the
    bronze either side of it. Everything here is one lathe and one subtraction."""
    skull = lathe("helm_skull", [
        (0.000, 0.126), (0.026, 0.124), (0.056, 0.114), (0.084, 0.092),
        (0.101, 0.056), (0.109, 0.012), (0.110, -0.036), (0.104, -0.080),
        (0.094, -0.114),
        (0.104, -0.136), (0.121, -0.152),
        (0.115, -0.160),
        (0.096, -0.140), (0.082, -0.112),
        (0.088, -0.074), (0.096, -0.032), (0.095, 0.014), (0.087, 0.058),
        (0.072, 0.092), (0.042, 0.114), (0.000, 0.118),
    ], steps=24)
    skull.scale = (1.0, 1.06, 1.0)
    finish(skull, mats["bronze"], density=6.0)

    # One opening. Points 2-5 dip down and back up, which leaves the nasal hanging from
    # the brow between the eyes; the rest of the outline sweeps out and down so the metal
    # left at the sides becomes the cheeks.
    face_void = extrude_yz("cut_face", [
        (-0.073, 0.034), (-0.064, 0.046), (-0.050, 0.053), (-0.035, 0.055),
        (-0.026, 0.052),
        (-0.019, 0.022), (-0.015, -0.004), (-0.014, -0.023),
        (0.014, -0.023), (0.015, -0.004), (0.019, 0.022),
        (0.026, 0.052),
        (0.035, 0.055), (0.050, 0.053), (0.064, 0.046), (0.073, 0.034),
        (0.071, 0.018), (0.063, 0.003), (0.049, -0.009), (0.035, -0.017),
        (0.027, -0.023),
        (0.025, -0.052), (0.023, -0.092), (0.021, -0.132), (0.017, -0.162),
        (0.010, -0.178),
        (-0.010, -0.178),
        (-0.017, -0.162), (-0.021, -0.132), (-0.023, -0.092), (-0.025, -0.052),
        (-0.027, -0.023),
        (-0.035, -0.017), (-0.049, -0.009), (-0.063, 0.003), (-0.071, 0.018),
    ], 0.16, x_centre=0.10)
    face_void.hide_render = True
    face_void.hide_viewport = True

    # The cutter carries bronze so the faces the boolean creates inherit it. Without this
    # the operand contributes an empty material slot, the newly cut interior walls land in
    # it with no material at all, and the AO bake then reports nothing to bake into for
    # that slot - which is exactly the warning that has been appearing.
    assign(face_void, mats["bronze"])

    m = skull.modifiers.new("cut_face", "BOOLEAN")
    m.operation = 'DIFFERENCE'
    m.object = face_void

    # A crest sweeps up from the brow and overhangs the nape. The old one was a low
    # lozenge that vanished when seen head-on, which is no use when its whole job is
    # telling two identical fighters apart at ten metres.
    crest_outline = [
        (0.078, 0.096), (0.086, 0.130), (0.072, 0.162), (0.040, 0.184),
        (-0.004, 0.192), (-0.048, 0.182), (-0.084, 0.158), (-0.110, 0.120),
        (-0.122, 0.076), (-0.124, 0.042), (-0.108, 0.032),
        (-0.094, 0.060), (-0.078, 0.090), (-0.058, 0.104), (-0.026, 0.116),
        (0.008, 0.120), (0.042, 0.108), (0.066, 0.092),
    ]
    # Every underside point sits a few millimetres inside the dome. The previous version
    # cleared it, and daylight under a crest is the tell that it is a prop balanced on top
    # rather than something bound to the helmet.
    crest_thick = [
        0.017, 0.015, 0.013, 0.012,
        0.011, 0.011, 0.010, 0.009,
        0.008, 0.007, 0.006,
        0.010, 0.013, 0.015, 0.017,
        0.018, 0.017, 0.017,
    ]
    crest = extrude_varying("helm_crest", crest_outline, crest_thick)
    finish(crest, mats["crest"], mode="planar", density=6.0, plane="XZ", smooth_angle=34.0)

    return [skull, crest], [face_void]


def apply_modifiers(obj):
    bpy.ops.object.select_all(action='DESELECT')
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    for m in list(obj.modifiers):
        try:
            bpy.ops.object.modifier_apply(modifier=m.name)
        except Exception as e:
            log("could not apply {} on {}: {}".format(m.name, obj.name, e))
    # Belt and braces: booleans can still leave a slot nothing references.
    try:
        bpy.ops.object.material_slot_remove_unused()
    except Exception:
        pass
    obj.select_set(False)


def join(parts, name):
    bpy.ops.object.select_all(action='DESELECT')
    for o in parts:
        o.select_set(True)
    bpy.context.view_layer.objects.active = parts[0]
    bpy.ops.object.join()
    obj = bpy.context.active_object
    obj.name = name
    bpy.ops.object.select_all(action='DESELECT')
    return obj


def add_bake_uvs(obj, layer="UVAO"):
    """A second UV set, packed into 0-1 and non-overlapping.

    The tiling UVs cannot be baked to - they run far outside 0-1 and repeat, so every
    tile would write over the same texels. Detail comes from UV1, occlusion from UV2."""
    me = obj.data
    if layer not in me.uv_layers:
        me.uv_layers.new(name=layer)
    me.uv_layers.active = me.uv_layers[layer]

    bpy.ops.object.select_all(action='DESELECT')
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    try:
        bpy.ops.uv.smart_project(angle_limit=math.radians(66), island_margin=0.03)
    except Exception as e:
        log("bake unwrap failed: {}".format(e))
    bpy.ops.object.mode_set(mode='OBJECT')
    obj.select_set(False)
    return obj


def bake_ao(obj, size=256, samples=64, hide=()):
    """Bakes ambient occlusion to a per-asset map.

    This is the cheapest way to make geometry read as form: recesses go dark, prominences
    stay bright. Without it every hollow on the helmet is lit exactly like every ridge,
    which is why it looked flat despite having real cheek pieces and a real nasal."""
    scene = bpy.context.scene
    previous_engine = scene.render.engine

    img = bpy.data.images.new("T_Helmet_AO", width=size, height=size)
    img.generated_color = (1.0, 1.0, 1.0, 1.0)

    # Every material on the object needs the target node, and it must be the active one.
    #
    # The list is snapshotted first because assigning into obj.data.materials while
    # iterating it mutates the collection mid-loop - which silently skipped slot 1 and
    # left the crest with no occlusion at all.
    targets = []
    slots = list(obj.data.materials)
    log("material slots on {}: {}".format(
        obj.name, [m.name if m else "<empty>" for m in slots]))

    for slot_index, mat in enumerate(slots):
        if mat is None:
            log("  slot {} is empty - skipped".format(slot_index))
            continue
        copy = mat.copy()
        copy.name = mat.name + "_" + obj.name
        obj.data.materials[slot_index] = copy

        node = copy.node_tree.nodes.new("ShaderNodeTexImage")
        node.image = img
        node.select = True
        copy.node_tree.nodes.active = node
        targets.append((copy, node))

    # Verify against what is actually on the object now, not against what we think we set.
    for slot_index, mat in enumerate(obj.data.materials):
        active = mat.node_tree.nodes.active if mat and mat.use_nodes else None
        ok = active is not None and getattr(active, "image", None) is img
        log("  slot {} ({}): bake target {}".format(
            slot_index, mat.name if mat else "<empty>", "ready" if ok else "MISSING"))

    for o in hide:
        o.hide_render = True

    scene.render.engine = 'CYCLES'
    try:
        scene.cycles.samples = samples
        scene.cycles.bake_type = 'AO'
        scene.render.bake.margin = 8
        scene.render.bake.use_selected_to_active = False
    except Exception as e:
        log("bake settings: {}".format(e))

    bpy.ops.object.select_all(action='DESELECT')
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    try:
        bpy.ops.object.bake(type='AO')
        log("baked AO {}x{} at {} samples".format(size, size, samples))
    except Exception as e:
        log("BAKE FAILED: {}".format(e))
    obj.select_set(False)

    img.filepath_raw = os.path.join(TEX_DIR, "T_Helmet_AO.png")
    img.file_format = 'PNG'
    img.save()

    scene.render.engine = previous_engine
    for o in hide:
        o.hide_render = False

    # Wire it in: tiling detail on UV1, occlusion on UV2, multiplied.
    for mat, node in targets:
        nt = mat.node_tree
        bsdf = nt.nodes["Principled BSDF"]
        detail = None
        for n in nt.nodes:
            if n.type == 'TEX_IMAGE' and n is not node:
                detail = n
                break
        if detail is None:
            continue

        uv1 = nt.nodes.new("ShaderNodeUVMap")
        uv1.uv_map = "UVMap"
        nt.links.new(uv1.outputs["UV"], detail.inputs["Vector"])

        uv2 = nt.nodes.new("ShaderNodeUVMap")
        uv2.uv_map = "UVAO"
        node.interpolation = 'Linear'
        nt.links.new(uv2.outputs["UV"], node.inputs["Vector"])

        mix = nt.nodes.new("ShaderNodeMixRGB")
        mix.blend_type = 'MULTIPLY'
        mix.inputs["Fac"].default_value = 0.85
        nt.links.new(detail.outputs["Color"], mix.inputs["Color1"])
        nt.links.new(node.outputs["Color"], mix.inputs["Color2"])
        nt.links.new(mix.outputs["Color"], bsdf.inputs["Base Color"])

    return img


# ------------------------------------------------------------------ light + render

def tonemap():
    """The highlights were clipping to flat white on every curved bronze surface. That was
    never a material problem - a 200W key half a metre from a 0.7m prop simply overexposes.
    AgX rolls the highlight off instead of chopping it."""
    vs = bpy.context.scene.view_settings
    for tf in ("AgX", "Filmic", "Standard"):
        try:
            vs.view_transform = tf
            log("view transform: " + tf)
            break
        except Exception:
            continue
    try:
        vs.look = "AgX - Base Contrast"
    except Exception:
        pass


def export_fbx(parts, name, out_dir):
    """One FBX per asset. Modifiers are applied on the way out so what Unreal receives is
    the final mesh, not a base mesh plus instructions it cannot read."""
    bpy.ops.object.select_all(action='DESELECT')
    for o in parts:
        o.hide_render = False
        o.hide_viewport = False
        o.select_set(True)
    bpy.context.view_layer.objects.active = parts[0]

    path = os.path.join(out_dir, name + ".fbx")
    try:
        bpy.ops.export_scene.fbx(
            filepath=path,
            use_selection=True,
            object_types={'MESH'},
            use_mesh_modifiers=True,
            mesh_smooth_type='EDGE',
            bake_space_transform=True,
            add_leaf_bones=False,
            apply_unit_scale=True,
            apply_scale_options='FBX_SCALE_NONE',
            global_scale=1.0,
            path_mode='STRIP',
        )
    except Exception as e:
        log("EXPORT FAILED {}: {}".format(name, e))
        return None

    bpy.ops.object.select_all(action='DESELECT')
    size = os.path.getsize(path) if os.path.exists(path) else 0
    if size == 0:
        log("EXPORT FAILED {}: no file written".format(name))
        return None
    log("exported {:<12} {:>7} bytes  ({} tris)".format(
        name, size, sum(len(o.data.polygons) for o in parts)))
    return path


def pick_engine():
    for name in ("BLENDER_EEVEE_NEXT", "BLENDER_EEVEE", "CYCLES"):
        try:
            bpy.context.scene.render.engine = name
            log("engine: " + name)
            return
        except Exception:
            continue


def studio():
    """Warm low sun, cool sky fill, hard rim. This is where the budget goes."""
    world = bpy.data.worlds.new("W")
    bpy.context.scene.world = world
    world.use_nodes = True
    wnt = world.node_tree
    bg = wnt.nodes["Background"]
    bg.inputs["Strength"].default_value = 0.9

    grad = wnt.nodes.new("ShaderNodeTexGradient")
    grad.gradient_type = 'EASING'
    mapping = wnt.nodes.new("ShaderNodeMapping")
    mapping.inputs["Rotation"].default_value = (0.0, math.radians(90), 0.0)
    coord = wnt.nodes.new("ShaderNodeTexCoord")
    ramp = wnt.nodes.new("ShaderNodeValToRGB")
    ramp.color_ramp.elements[0].position = 0.25
    ramp.color_ramp.elements[0].color = (0.055, 0.062, 0.080, 1)
    ramp.color_ramp.elements[1].position = 0.90
    ramp.color_ramp.elements[1].color = (0.42, 0.52, 0.66, 1)
    wnt.links.new(coord.outputs["Generated"], mapping.inputs["Vector"])
    wnt.links.new(mapping.outputs["Vector"], grad.inputs["Vector"])
    wnt.links.new(grad.outputs["Color"], ramp.inputs["Fac"])
    wnt.links.new(ramp.outputs["Color"], bg.inputs["Color"])

    bpy.ops.mesh.primitive_plane_add(size=14.0, location=(0, 0, -0.45))
    ground = bpy.context.active_object
    ground.name = "ground"
    gm = bpy.data.materials.new("M_Ground")
    gm.use_nodes = True
    gm.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.10, 0.105, 0.10, 1)
    gm.node_tree.nodes["Principled BSDF"].inputs["Roughness"].default_value = 0.9
    assign(ground, gm)

    def lamp(name, loc, energy, size, colour):
        d = bpy.data.lights.new(name, 'AREA')
        d.energy = energy
        d.size = size
        d.color = colour
        o = bpy.data.objects.new(name, d)
        o.location = loc
        bpy.context.collection.objects.link(o)
        o.constraints.new('TRACK_TO')
        return o

    key = lamp("key", (1.5, -1.7, 1.1), 70, 0.55, (1.0, 0.90, 0.78))
    fill = lamp("fill", (-1.9, -1.1, 0.8), 22, 3.0, (0.62, 0.72, 1.0))
    rim = lamp("rim", (-0.8, 1.9, 1.6), 80, 1.2, (0.85, 0.92, 1.0))

    aim = bpy.data.objects.new("aim", None)
    bpy.context.collection.objects.link(aim)
    for o in (key, fill, rim):
        o.constraints[0].target = aim
    return [ground]


def frame_of(objects):
    xs, ys, zs = [], [], []
    for o in objects:
        for corner in o.bound_box:
            w = o.matrix_world @ mathutils.Vector(corner)
            xs.append(w.x)
            ys.append(w.y)
            zs.append(w.z)
    centre = mathutils.Vector(((min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2,
                               (min(zs) + max(zs)) / 2))
    radius = max(max(xs) - min(xs), max(ys) - min(ys), max(zs) - min(zs)) * 0.5
    return centre, max(radius, 0.02)


def render(name, subject, everything, keep, filename, lens=80, pad=3.0, azim=(0.70, -0.88, 0.38)):
    scene = bpy.context.scene
    for o in everything:
        o.hide_render = o not in subject and o not in keep

    centre, radius = frame_of(subject)
    dist = radius * pad + 0.25

    cam_data = bpy.data.cameras.new("cam_" + name)
    cam_data.lens = lens
    cam = bpy.data.objects.new("cam_" + name, cam_data)
    cam.location = (centre.x + dist * azim[0], centre.y + dist * azim[1], centre.z + dist * azim[2])
    bpy.context.collection.objects.link(cam)
    scene.camera = cam

    t = bpy.data.objects.new("t_" + name, None)
    t.location = centre
    bpy.context.collection.objects.link(t)
    cam.constraints.new('TRACK_TO').target = t

    scene.render.resolution_x = 900
    scene.render.resolution_y = 700
    scene.render.filepath = os.path.join(OUT_DIR, filename)
    scene.render.image_settings.file_format = 'PNG'
    try:
        scene.eevee.taa_render_samples = 48
    except Exception:
        pass
    bpy.ops.render.render(write_still=True)
    log("rendered " + filename)


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    os.makedirs(OUT_DIR, exist_ok=True)

    images = build_textures()
    mats = {
        "bronze": textured("M_Bronze", images["bronze"], 0.62, 0.40, rough_range=(0.62, 0.24)),
        "wood": textured("M_Wood", images["wood"], 0.0, 0.68),
        "crest": textured("M_Crest", images["crest"], 0.0, 0.78),
        "marble": textured("M_Marble", images["marble"], 0.0, 0.62),
        "leather": textured("M_Leather", images["leather"], 0.0, 0.72),
    }

    aspis = make_aspis(mats)
    dory = make_dory(mats)
    helmet_parts, cuts = make_helmet(mats)

    # Booleans applied and parts joined before baking: AO needs final geometry, and a
    # helmet is one mesh in the game regardless of how it was assembled here.
    apply_modifiers(helmet_parts[0])
    for c in cuts:
        bpy.data.objects.remove(c, do_unlink=True)
    cuts = []
    helmet = join(helmet_parts, "SM_Helmet")
    add_bake_uvs(helmet)

    # The spear ships as one mesh; three separate objects is an artefact of how it was
    # modelled, not something Unreal should have to know about.
    dory_mesh = join(dory, "SM_Dory")

    export_dir = os.path.join(HERE, "..", "export")
    os.makedirs(export_dir, exist_ok=True)
    export_fbx([aspis], "SM_Aspis", export_dir)
    export_fbx([dory_mesh], "SM_Dory", export_dir)
    export_fbx([helmet], "SM_Helmet", export_dir)

    dory = [dory_mesh]
    subjects = [aspis] + dory + [helmet]
    for o in subjects:
        d = o.dimensions
        log("  {:<14} {:>5} tris  {:.3f} x {:.3f} x {:.3f} m".format(
            o.name, len(o.data.polygons), d.x, d.y, d.z))

    pick_engine()
    tonemap()
    keep = studio()
    bake_ao(helmet, size=256, samples=64, hide=keep)
    everything = subjects + cuts

    render("helmet", [helmet], everything, keep, "vh_helmet.png", lens=85, pad=5.2)
    # Straight on. A Corinthian helm is a face; judging it from behind the ear
    # tells you nothing about whether the face reads.
    render("helmet_front", [helmet], everything, keep, "vh_helmet_front.png",
           lens=85, pad=5.0, azim=(1.15, -0.06, 0.12))
    render("aspis", [aspis], everything, keep, "vh_aspis.png", lens=70, pad=2.8)
    render("dory", dory, everything, keep, "vh_dory_head.png", lens=90, pad=2.2)
    # Laid out for the group shot. Everything is modelled at the origin, so without this
    # the shield, helmet and spear render stacked inside one another.
    aspis.location = (0.0, -0.62, 0.0)
    for o in dory:
        o.location = (0.0, 0.62, 0.0)
    render("kit", subjects, everything, keep, "vh_kit.png", lens=45, pad=1.35)

    bpy.ops.wm.save_as_mainfile(filepath=BLEND)
    log("saved " + BLEND)
    log("DONE")


main()
