# Gives the arena props actual collision geometry.
#
#   UnrealEditor-Cmd.exe Ichor.uproject -run=pythonscript -script="Scripts/fix_prop_collision.py"
#
# GeometryScriptCreateNewStaticMeshAssetOptions.enable_collision only turns the collision
# SETTING on - it does not generate any collision primitives. The floor slabs came out with
# zero of them, so the arena's 49-slab floor was purely visual and the player fell through
# it. The mesh reported CollisionEnabled.QUERY_AND_PHYSICS the whole time, which is exactly
# why it was not obvious.
#
# Complex-as-simple rather than fitted boxes: these are slabs, walls and columns that never
# move, the render mesh is already the right shape, and an approximation would only be
# wrong in ways nobody wants to debug.

import unreal

PROPS = ["SM_FloorSlab", "SM_WallBlock", "SM_Column_Doric"]
lib = unreal.EditorAssetLibrary


def log(m):
    unreal.log("[ichor-collision] " + str(m))


fixed = 0
for name in PROPS:
    path = "/Game/Ichor/Arena/" + name
    mesh = lib.load_asset(path)
    if mesh is None:
        log("MISSING " + name)
        continue

    body = mesh.get_editor_property("body_setup")
    if body is None:
        log("no body setup on " + name)
        continue

    body.set_editor_property("collision_trace_flag",
                             unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    lib.save_asset(path, only_if_is_dirty=False)
    fixed += 1

# Read back from a reloaded asset - the setting is only real once it is on disk.
ok = True
for name in PROPS:
    path = "/Game/Ichor/Arena/" + name
    mesh = lib.load_asset(path)
    flag = mesh.get_editor_property("body_setup").get_editor_property("collision_trace_flag")
    good = flag == unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE
    ok = ok and good
    log("{:<18} {}".format(name, "complex-as-simple" if good else "STILL WRONG: {}".format(flag)))

log("VERIFIED: {} props collide".format(fixed) if ok else "FAILED - see above")
