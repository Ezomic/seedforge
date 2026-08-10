# Builds Lvl_Arena: the Stage 0 fighting space, laid out on doc 18's 4m grid.
#
#   UnrealEditor-Cmd.exe Ichor.uproject -run=pythonscript -script="Scripts/build_arena.py"
#
# Shape is chosen from the death causes the fight instrumentation records, not from taste:
#
#   Cornered      -> two wall runs meeting in a corner, so being trapped is possible
#   OffScreen     -> a colonnade the enemy can be lost behind
#   spacing play  -> a defile narrow enough that the 2.5m engagement band does not fit
#
# The rest is open court, because doc 18 wants the fight readable before terrain matters.
#
# Idempotent: rebuilds the level from scratch each run, so layout tweaks are one edit away.

import unreal

LEVEL = "/Game/Ichor/Maps/Lvl_Arena"
PROPS = "/Game/Ichor/Props"
MATS = "/Game/Ichor/Materials"
SKIRMISHER = "/Game/ThirdPerson/Blueprints/BP_Skirmisher"

CELL = 400.0          # 4m grid
COURT = 7             # 7x7 cells = 28m across

lib = unreal.EditorAssetLibrary
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def log(m):
    unreal.log("[ichor-arena] " + str(m))


def mat(name):
    return lib.load_asset("{}/{}".format(MATS, name))


def mesh(name):
    return lib.load_asset("{}/{}".format(PROPS, name))


def place(static_mesh, loc, yaw=0.0, material=None, label=None):
    """Yaw only, deliberately. Passing a 3-tuple to unreal.Rotator means (roll, pitch,
    yaw), and an earlier version of this script passed 90 as the second element intending
    a turn - which pitched the walls onto their sides instead. Nothing here ever needs to
    tip, so the parameter that could do it is gone."""
    actor = eas.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*loc),
                                       unreal.Rotator(0.0, 0.0, yaw))
    if actor is None:
        return None
    comp = actor.static_mesh_component
    comp.set_static_mesh(static_mesh)
    if material:
        comp.set_material(0, material)
    # Props are scenery; the fight traces on ECC_Pawn and walls must block movement.
    comp.set_mobility(unreal.ComponentMobility.STATIC)
    if label:
        actor.set_actor_label(label)
    return actor


def build_floor(slab, marble):
    half = (COURT - 1) / 2.0
    n = 0
    for ix in range(COURT):
        for iy in range(COURT):
            x = (ix - half) * CELL
            y = (iy - half) * CELL
            if place(slab, (x, y, -10.0), material=marble, label="Floor_{}_{}".format(ix, iy)):
                n += 1
    log("floor: {} slabs, {:.0f}m across".format(n, COURT * CELL / 100.0))


def build_walls(block, terracotta):
    """Two runs meeting at a corner. 'Cornered' is a death cause; it needs a corner."""
    half = COURT * CELL / 2.0
    n = 0
    # Run along -X, facing in
    for i in range(COURT):
        y = (i - (COURT - 1) / 2.0) * CELL
        if place(block, (-half, y, 100.0), material=terracotta, label="Wall_X_{}".format(i)):
            n += 1
    # Run along -Y, rotated to meet it
    for i in range(COURT):
        x = (i - (COURT - 1) / 2.0) * CELL
        if place(block, (x, -half, 100.0), (0.0, 90.0, 0.0), terracotta, "Wall_Y_{}".format(i)):
            n += 1
    log("walls: {} blocks forming one corner".format(n))


def build_colonnade(column, marble):
    """A defile on the +X side: 3m clear, narrower than the 2.5m band plus two bodies."""
    n = 0
    gap = 150.0
    for i in range(4):
        y = -600.0 + i * 400.0
        if place(column, (900.0, y - gap, 0.0), material=marble, label="Col_A_{}".format(i)):
            n += 1
        if place(column, (1700.0, y + gap, 0.0), material=marble, label="Col_B_{}".format(i)):
            n += 1
    log("colonnade: {} columns, two staggered rows".format(n))


def build_lighting():
    """Lumen is off for the prototype, so this has to light the scene on its own."""
    made = []

    def light(cls, comp_cls, loc, rot, intensity, label):
        """Actors do not expose their light component under a predictable name in 5.8;
        asking for it by class is version-proof where a property name is not."""
        a = eas.spawn_actor_from_class(cls, unreal.Vector(*loc), unreal.Rotator(*rot))
        if not a:
            return None
        a.set_actor_label(label)
        c = a.get_component_by_class(comp_cls)
        if c is None:
            log("  {}: no {} found - left at defaults".format(label, comp_cls.get_name()))
            return a
        # Movable throughout: nothing here is baked, and stationary lights without a
        # lighting build fall back to something dimmer than they look in the editor.
        c.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
        c.set_editor_property("intensity", intensity)
        made.append(label)
        return a

    light(unreal.DirectionalLight, unreal.DirectionalLightComponent,
          (0, 0, 1200), (0.0, -48.0, -35.0), 6.0, "Sun")
    light(unreal.SkyLight, unreal.SkyLightComponent,
          (0, 0, 600), (0.0, 0.0, 0.0), 1.5, "SkyLight")

    for cls, name in ((unreal.SkyAtmosphere, "SkyAtmosphere"),
                      (unreal.ExponentialHeightFog, "Fog")):
        a = eas.spawn_actor_from_class(cls, unreal.Vector(0, 0, 0))
        if a:
            a.set_actor_label(name)
            made.append(name)

    log("lighting: {}".format(made))


def build_fighters():
    start = eas.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(-400.0, 0.0, 120.0),
                                       unreal.Rotator(0.0, 0.0, 0.0))
    if start:
        start.set_actor_label("PlayerStart")
        log("PlayerStart placed")

    bp = lib.load_asset(SKIRMISHER)
    if bp is None:
        log("WARNING: no BP_Skirmisher - arena has no opponent")
        return
    foe = eas.spawn_actor_from_class(bp.generated_class(), unreal.Vector(500.0, 0.0, 120.0),
                                     unreal.Rotator(0.0, 180.0, 0.0))
    if foe:
        foe.set_actor_label("Skirmisher_01")
        log("skirmisher placed 9m out - beyond aggro, so the approach is visible")


def main():
    les.new_level(LEVEL)
    log("created level " + LEVEL)

    slab, block, column = mesh("SM_FloorSlab"), mesh("SM_WallBlock"), mesh("SM_Column_Doric")
    marble, terracotta = mat("M_Ichor_Marble"), mat("M_Ichor_Terracotta")
    sand = mat("M_Ichor_Sand")

    missing = [n for n, a in (("SM_FloorSlab", slab), ("SM_WallBlock", block),
                              ("SM_Column_Doric", column)) if a is None]
    if missing:
        log("ABORT: missing props {} - run make_art_kit.py first".format(missing))
        return

    build_floor(slab, sand or marble)
    build_walls(block, terracotta)
    build_colonnade(column, marble)
    build_lighting()
    build_fighters()

    les.save_current_level()
    log("saved")

    actors = eas.get_all_level_actors()
    log("VERIFIED: {} actors in {}".format(len(actors), LEVEL))


main()
