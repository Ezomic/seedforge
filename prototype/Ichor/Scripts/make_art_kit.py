# Generates the materials and props for the player, the opponent and the arena.
#
#   UnrealEditor-Cmd.exe Ichor.uproject -run=pythonscript -script="Scripts/make_art_kit.py"
#
# All of it is Geometry Script primitives and flat-colour materials. Doc 16 says a
# restricted palette is the biggest perceptual win per unit of effort, and doc 18 says
# grey-box the art - so this is silhouette and colour only, no textures, no detail.
#
# Props are sized to doc 18's 2m/4m snap grid so an arena can be laid out on it.
#
# The helmet is the character work. Doc 16 puts covered faces on day one, which removes
# facial rigging entirely and happens to be exactly right for hoplites: a Corinthian helm
# reads as a person from any angle without a face existing. Its crest is a separate
# material slot so the player and the opponent can be told apart at a glance.
#
# Idempotent: existing assets are left alone.

import unreal

MAT_DIR = "/Game/Ichor/Arena"
PROP_DIR = "/Game/Ichor/Arena"

prims = unreal.GeometryScript_Primitives
tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary

# name -> (r, g, b, roughness, metallic)
PALETTE = [
    ("M_Ichor_Marble",     (0.86, 0.84, 0.78), 0.55, 0.0),
    ("M_Ichor_Bronze",     (0.72, 0.45, 0.20), 0.35, 1.0),
    ("M_Ichor_Terracotta", (0.62, 0.30, 0.20), 0.75, 0.0),
    ("M_Ichor_Sand",       (0.76, 0.68, 0.52), 0.90, 0.0),
    ("M_Ichor_CrestRed",   (0.55, 0.09, 0.09), 0.80, 0.0),
    ("M_Ichor_CrestDark",  (0.13, 0.13, 0.16), 0.85, 0.0),
]


def log(m):
    unreal.log("[ichor-art] " + str(m))


def opts(material_id=0):
    o = unreal.GeometryScriptPrimitiveOptions()
    o.set_editor_property("material_id", material_id)
    return o


def T(x=0.0, y=0.0, z=0.0, pitch=0.0, yaw=0.0, roll=0.0, scale=None):
    return unreal.Transform([x, y, z], [roll, pitch, yaw], scale or [1.0, 1.0, 1.0])


def guarded(what, fn, *args):
    try:
        fn(*args)
        return True
    except Exception as e:
        log("  piece '{}' failed: {}".format(what, e))
        return False


# ---------------------------------------------------------------- materials

def make_material(name, rgb, roughness, metallic):
    path = "{}/{}".format(MAT_DIR, name)
    if lib.does_asset_exist(path):
        log("exists: " + name)
        return lib.load_asset(path)

    mat = tools.create_asset(name, MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
    if mat is None:
        log("FAILED to create " + name)
        return None

    try:
        col = mel.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -400, 0)
        col.set_editor_property("constant", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
        mel.connect_material_property(col, "", unreal.MaterialProperty.MP_BASE_COLOR)

        r = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -400, 200)
        r.set_editor_property("r", roughness)
        mel.connect_material_property(r, "", unreal.MaterialProperty.MP_ROUGHNESS)

        if metallic > 0.0:
            mt = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -400, 320)
            mt.set_editor_property("r", metallic)
            mel.connect_material_property(mt, "", unreal.MaterialProperty.MP_METALLIC)

        mel.recompile_material(mat)
    except Exception as e:
        log("wiring failed for {}: {}".format(name, e))

    lib.save_asset(path, only_if_is_dirty=False)
    log("created material " + name)
    return mat


# ---------------------------------------------------------------- props

def build_helmet():
    """Corinthian helm. Slot 0 is the bronze, slot 1 is the crest."""
    m = unreal.DynamicMesh()

    # Skull. A sphere squashed slightly and dropped over the head bone.
    guarded("skull", prims.append_sphere_lat_long, m, opts(0),
            T(z=2.0, scale=[1.0, 1.1, 1.15]), 12.5, 20, 12)

    # The cheek pieces and nasal are what make it Corinthian rather than a bowl. One box
    # down the front, two down the sides - crude, but the silhouette is unmistakable.
    guarded("nasal", prims.append_box, m, opts(0), T(x=11.0, z=-6.0), 3.0, 2.2, 14.0, 1, 1, 1)
    guarded("cheek_l", prims.append_box, m, opts(0), T(x=6.0, y=-8.5, z=-8.0), 10.0, 3.0, 12.0, 1, 1, 1)
    guarded("cheek_r", prims.append_box, m, opts(0), T(x=6.0, y=8.5, z=-8.0), 10.0, 3.0, 12.0, 1, 1, 1)

    # Neck guard, flaring back.
    guarded("neck", prims.append_box, m, opts(0), T(x=-9.0, z=-7.0, pitch=20.0), 8.0, 20.0, 10.0, 1, 1, 1)

    # Transverse crest. Material slot 1 so team colour lives here.
    guarded("crest", prims.append_box, m, opts(1), T(z=14.0), 26.0, 3.0, 9.0, 1, 1, 1)

    return m


def build_column():
    """Doric column, 4m - two cells of the 2m grid."""
    m = unreal.DynamicMesh()
    # Slight taper; a Doric shaft is not a tube.
    guarded("shaft", prims.append_cone, m, opts(0), T(), 40.0, 33.0, 380.0, 16, 1, True)
    guarded("echinus", prims.append_cone, m, opts(0), T(z=380.0), 36.0, 46.0, 14.0, 16, 1, True)
    guarded("abacus", prims.append_box, m, opts(0), T(z=401.0), 100.0, 100.0, 22.0, 1, 1, 1)
    return m


def build_wall():
    """Ashlar block, 4m x 2m. Walls are what make 'cornered' possible."""
    m = unreal.DynamicMesh()
    guarded("block", prims.append_box, m, opts(0), T(), 400.0, 60.0, 200.0, 1, 1, 1)
    return m


def build_slab():
    """Floor slab on the 4m grid."""
    m = unreal.DynamicMesh()
    guarded("slab", prims.append_box, m, opts(0), T(), 400.0, 400.0, 20.0, 1, 1, 1)
    return m


def save_mesh(mesh, name, writer):
    path = "{}/{}".format(PROP_DIR, name)
    if lib.does_asset_exist(path):
        log("exists: " + name)
        return True

    o = unreal.GeometryScriptCreateNewStaticMeshAssetOptions()
    o.set_editor_property("enable_recompute_normals", True)
    o.set_editor_property("enable_recompute_tangents", True)
    o.set_editor_property("enable_collision", True)
    o.set_editor_property("enable_nanite", False)

    try:
        writer(mesh, path, o)
    except Exception as e:
        log("FAILED writing {}: {}".format(name, e))
        return False

    lib.save_asset(path, only_if_is_dirty=False)
    log("created prop " + name)
    return True


def find_writer():
    for lib_name in ("GeometryScript_NewAssetUtils", "GeometryScript_AssetUtils"):
        l = getattr(unreal, lib_name, None)
        if l and hasattr(l, "create_new_static_mesh_asset_from_mesh"):
            return getattr(l, "create_new_static_mesh_asset_from_mesh")
    return None


def main():
    for d in (MAT_DIR, PROP_DIR):
        if not lib.does_directory_exist(d):
            lib.make_directory(d)

    for name, rgb, rough, metal in PALETTE:
        make_material(name, rgb, rough, metal)

    writer = find_writer()
    if writer is None:
        log("ABORT: no asset writer - no props built")
        return

    save_mesh(build_helmet(), "SM_Helmet_Corinthian", writer)
    save_mesh(build_column(), "SM_Column_Doric", writer)
    save_mesh(build_wall(), "SM_WallBlock", writer)
    save_mesh(build_slab(), "SM_FloorSlab", writer)

    ok = True
    for name in ("SM_Helmet_Corinthian", "SM_Column_Doric", "SM_WallBlock", "SM_FloorSlab"):
        a = lib.load_asset("{}/{}".format(PROP_DIR, name))
        if a is None:
            log("VERIFY FAIL: " + name)
            ok = False
            continue
        b = a.get_bounding_box()
        slots = len(a.get_editor_property("static_materials") or [])
        log("VERIFY OK: {:<22} {:.0f} x {:.0f} x {:.0f} cm, {} material slot(s)".format(
            name, b.max.x - b.min.x, b.max.y - b.min.y, b.max.z - b.min.z, slots))

    for name, _, _, _ in PALETTE:
        if not lib.does_asset_exist("{}/{}".format(MAT_DIR, name)):
            log("VERIFY FAIL: material " + name)
            ok = False

    log("VERIFIED" if ok else "FAILED - see above")


main()
