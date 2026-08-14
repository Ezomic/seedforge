import unreal
lib = unreal.EditorAssetLibrary

def log(m):
    unreal.log("[ichor-floor] " + str(m))

for name in ("SM_FloorSlab", "SM_WallBlock", "SM_Column_Doric"):
    a = lib.load_asset("/Game/Ichor/Arena/" + name)
    if a is None:
        log("MISSING " + name)
        continue
    body = a.get_editor_property("body_setup")
    prims = 0
    trace = "?"
    if body:
        try:
            agg = body.get_editor_property("agg_geom")
            prims = (len(agg.get_editor_property("box_elems"))
                     + len(agg.get_editor_property("sphere_elems"))
                     + len(agg.get_editor_property("convex_elems"))
                     + len(agg.get_editor_property("sphyl_elems")))
        except Exception as e:
            log("  agg_geom read failed: {}".format(e))
        try:
            trace = str(body.get_editor_property("collision_trace_flag"))
        except Exception:
            pass
    log("{:<18} simple prims={}  trace={}".format(name, prims, trace))

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
les.load_level("/Game/Ichor/Arena/Lvl_Arena")
floors = [a for a in eas.get_all_level_actors() if "Floor" in a.get_actor_label()]
log("floor actors in level: {}".format(len(floors)))
if floors:
    f = floors[0]
    c = f.static_mesh_component
    log("first floor at {}  collision={}".format(
        f.get_actor_location(), c.get_collision_enabled()))
