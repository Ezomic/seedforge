# Creates BP_Skirmisher from AIchorSkirmisher, gives it a body, and places one in the
# test level a few metres from the PlayerStart.
#
#   UnrealEditor-Cmd.exe Ichor.uproject -run=pythonscript -script="Scripts/make_skirmisher_bp.py"
#
# Idempotent: reuses the Blueprint if present and refuses to place a second instance.

import unreal

BP_DIR = "/Game/ThirdPerson/Blueprints"
BP_NAME = "BP_Skirmisher"
BP_PATH = "{}/{}".format(BP_DIR, BP_NAME)
LEVEL = "/Game/ThirdPerson/Lvl_ThirdPerson"

# Manny for the enemy; the player is Quinn. Telling them apart matters in a grey box.
MESH = "/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple"
ANIM = "/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed"

lib = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()


def log(m):
    unreal.log("[ichor-skirm] " + str(m))


def get_or_create_bp():
    if lib.does_asset_exist(BP_PATH):
        log("blueprint exists")
        return lib.load_asset(BP_PATH)
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", unreal.IchorSkirmisher)
    bp = tools.create_asset(BP_NAME, BP_DIR, unreal.Blueprint, factory)
    log("created blueprint: {}".format(bp))
    return bp


def dress(bp):
    """Give it a body. Character's Mesh component sits at -90 Z, yawed -90, by convention."""
    gen = bp.generated_class()
    cdo = unreal.get_default_object(gen)
    if cdo is None:
        log("no CDO; skipping mesh setup")
        return

    mesh_comp = cdo.get_editor_property("mesh")
    if mesh_comp is None:
        log("no mesh component found")
        return

    skm = lib.load_asset(MESH)
    if skm:
        try:
            mesh_comp.set_editor_property("skeletal_mesh_asset", skm)
            log("mesh set: SKM_Manny_Simple")
        except Exception as e:
            log("mesh set failed: {}".format(e))

    abp = lib.load_asset(ANIM)
    if abp:
        try:
            mesh_comp.set_editor_property("anim_class", abp.generated_class())
            log("anim class set: ABP_Unarmed")
        except Exception as e:
            log("anim set failed: {}".format(e))

    try:
        mesh_comp.set_editor_property("relative_location", unreal.Vector(0.0, 0.0, -90.0))
        mesh_comp.set_editor_property("relative_rotation", unreal.Rotator(0.0, 0.0, -90.0))
        log("mesh transform set")
    except Exception as e:
        log("transform failed: {}".format(e))

    lib.save_asset(BP_PATH)


def place(bp):
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    les.load_level(LEVEL)
    log("level loaded: " + LEVEL)

    actors = eas.get_all_level_actors()

    # already placed?
    for a in actors:
        if BP_NAME in a.get_class().get_name():
            log("a skirmisher is already in the level at {}".format(a.get_actor_location()))
            return

    # spawn relative to the PlayerStart so it lands somewhere sensible
    origin = unreal.Vector(0.0, 0.0, 200.0)
    for a in actors:
        if isinstance(a, unreal.PlayerStart):
            origin = a.get_actor_location()
            log("PlayerStart at {}".format(origin))
            break

    loc = unreal.Vector(origin.x + 600.0, origin.y, origin.z)
    # spawn_actor_from_object wants an asset it can instance; for a Blueprint the
    # generated class is what actually spawns.
    spawned = eas.spawn_actor_from_class(bp.generated_class(), loc, unreal.Rotator(0.0, 180.0, 0.0))
    if spawned:
        spawned.set_actor_label("Skirmisher_01")
        log("placed at {}".format(loc))
    else:
        log("SPAWN FAILED")

    les.save_current_level()
    log("level saved")


def main():
    bp = get_or_create_bp()
    if bp is None:
        log("ABORT: no blueprint")
        return
    dress(bp)
    place(bp)
    log("done")


main()
