# Generates the spear and shield as real StaticMesh assets, procedurally.
#
#   UnrealEditor-Cmd.exe Ichor.uproject -run=pythonscript -script="Scripts/make_weapon_meshes.py"
#
# Nothing is bought and nothing is modelled by hand: these are primitives welded together
# by Geometry Script. A dory is a shaft, a leaf head and a sauroter; an aspis is a shallow
# bowl with the rolled rim that makes it read as Greek rather than as a dustbin lid.
# Crude, but real geometry with real silhouettes rather than a scaled cylinder.
#
# Both are built along +Z with the origin at the grip, so attaching them to a hand needs
# rotation only, not a fudged offset.
#
# Idempotent: existing meshes are left alone.

import unreal

OUT_DIR = "/Game/Ichor/Weapons"

prims = unreal.GeometryScript_Primitives
xf = unreal.GeometryScript_MeshTransforms


def log(m):
    unreal.log("[ichor-weapon] " + str(m))


def opts():
    return unreal.GeometryScriptPrimitiveOptions()


def T(x=0.0, y=0.0, z=0.0, pitch=0.0, yaw=0.0, roll=0.0):
    return unreal.Transform([x, y, z], [roll, pitch, yaw], [1.0, 1.0, 1.0])


def guarded(what, fn, *args):
    """One bad signature should cost the piece, not the whole weapon."""
    try:
        fn(*args)
        return True
    except Exception as e:
        log("  piece '{}' failed: {}".format(what, e))
        return False


def find_asset_writer():
    """The library that turns a DynamicMesh into a saved asset moved between versions."""
    for lib_name in ("GeometryScript_NewAssetUtils", "GeometryScript_AssetUtils",
                     "GeometryScript_CreateNewAssets"):
        lib = getattr(unreal, lib_name, None)
        if not lib:
            continue
        for fn_name in ("create_new_static_mesh_asset_from_mesh",
                        "create_new_static_mesh_asset_from_mesh_lo_ds"):
            if hasattr(lib, fn_name):
                log("asset writer: {}.{}".format(lib_name, fn_name))
                return getattr(lib, fn_name)
        log("{} has no obvious writer; candidates: {}".format(
            lib_name, [n for n in dir(lib) if "static_mesh" in n.lower()]))
    return None


def build_dory():
    """Hoplite spear. ~2.4m, gripped behind centre so it reaches forward, not evenly."""
    m = unreal.DynamicMesh()

    # Shaft: cornel wood was about 2.5cm thick. Grip sits at the origin, 70cm from the butt.
    prims.append_cylinder(m, opts(), T(z=-70.0), 1.4, 220.0, 12, 1, True)

    # Leaf-shaped head. A cone is not a leaf, but at grey-box scale the silhouette that
    # matters is "narrow point extending the reach".
    prims.append_cone(m, opts(), T(z=150.0), 4.0, 0.4, 26.0, 12, 1, True)

    # Sauroter - the bronze butt-spike. Points the other way, hence the pitch flip.
    prims.append_cone(m, opts(), T(z=-70.0, pitch=180.0), 3.0, 0.3, 14.0, 10, 1, True)

    return m


def build_aspis():
    """Round shield, ~90cm across, shallow bowl with a rolled rim."""
    m = unreal.DynamicMesh()

    # The face. Thin, because the bowl depth comes from the dome rather than the slab.
    guarded("face", prims.append_cylinder, m, opts(), T(z=-2.0), 45.0, 4.0, 32, 1, True)

    # The dome. A sphere squashed by the transform's own scale - appending a second mesh
    # needs a compositing call that is not on the booleans library, and this avoids it.
    dome_xf = unreal.Transform([0.0, 0.0, 2.0], [0.0, 0.0, 0.0], [1.0, 1.0, 0.28])
    guarded("dome", prims.append_sphere_lat_long, m, opts(), dome_xf, 45.0, 24, 12)

    # The rolled rim, which is most of what makes the silhouette read as Greek.
    # The fourth parameter is revolve options, not an axis.
    guarded("rim", prims.append_torus, m, opts(), T(),
            unreal.GeometryScriptRevolveOptions(), 45.0, 3.0, 32, 10)

    return m


def save(mesh, name, writer):
    path = "{}/{}".format(OUT_DIR, name)
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        log("exists, left alone: " + name)
        return True

    o = unreal.GeometryScriptCreateNewStaticMeshAssetOptions()
    o.set_editor_property("enable_recompute_normals", True)
    o.set_editor_property("enable_recompute_tangents", True)
    o.set_editor_property("enable_collision", False)
    o.set_editor_property("enable_nanite", False)

    try:
        result = writer(mesh, path, o)
    except Exception as e:
        log("FAILED writing {}: {}".format(name, e))
        return False

    log("created {} ({})".format(name, result))
    unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
    return True


def verify():
    ok = True
    for name in ("SM_Dory", "SM_Aspis"):
        path = "{}/{}".format(OUT_DIR, name)
        a = unreal.EditorAssetLibrary.load_asset(path)
        if a is None:
            log("VERIFY FAIL: {} missing".format(name))
            ok = False
            continue
        try:
            bounds = a.get_bounding_box()
            log("VERIFY OK: {:<10} bounds min={} max={}".format(
                name, bounds.min, bounds.max))
        except Exception:
            log("VERIFY OK: {} exists (bounds unavailable)".format(name))
    return ok


def main():
    writer = find_asset_writer()
    if writer is None:
        log("ABORT: no asset-writing function found - nothing built")
        return

    if not unreal.EditorAssetLibrary.does_directory_exist(OUT_DIR):
        unreal.EditorAssetLibrary.make_directory(OUT_DIR)

    save(build_dory(), "SM_Dory", writer)
    save(build_aspis(), "SM_Aspis", writer)

    log("VERIFIED" if verify() else "FAILED - see above")


main()
