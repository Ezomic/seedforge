# Read-only. Can animation be authored from Python in 5.8 at all?
#
# Everything downstream depends on the answer, so it is checked before anything is
# promised on top of it. Reports what exists, not what ought to.

import unreal


def log(m):
    unreal.log("[ichor-authoring] " + str(m))


def members(obj, needles):
    out = []
    for n in dir(obj):
        if n.startswith("_"):
            continue
        low = n.lower()
        if any(k in low for k in needles):
            out.append(n)
    return sorted(out)


# 1. Is there a library that writes bone keys?
for lib_name in ("AnimationLibrary", "AnimSequencerHelpers", "AnimationBlueprintLibrary"):
    lib = getattr(unreal, lib_name, None)
    log("{}: {}".format(lib_name, "PRESENT" if lib else "absent"))
    if lib:
        log("  bone/track/key fns: {}".format(
            members(lib, ["bone", "track", "key", "frame", "length", "raw"])))

# 2. Can an empty AnimSequence be created?
fac = getattr(unreal, "AnimSequenceFactory", None)
log("AnimSequenceFactory: {}".format("PRESENT" if fac else "absent"))
if fac:
    log("  props: {}".format(members(fac(), ["skeleton", "anim", "source"])))

# 3. What does the skeleton call the bones we would need to pose?
skel = unreal.EditorAssetLibrary.load_asset("/Game/Characters/Mannequins/Meshes/SK_Mannequin")
if skel is None:
    for alt in ("/Game/Characters/Mannequins/Meshes/SK_Mannequin_Skeleton",
                "/Game/Characters/Mannequins/Rigs/SK_Mannequin"):
        skel = unreal.EditorAssetLibrary.load_asset(alt)
        if skel:
            log("skeleton found at " + alt)
            break
log("skeleton asset: {}".format(skel.get_name() if skel else "NOT FOUND"))

seq = unreal.EditorAssetLibrary.load_asset(
    "/Game/Characters/Mannequins/Anims/Unarmed/Attack/MM_Attack_01")
if seq:
    lib = getattr(unreal, "AnimationLibrary", None)
    if lib and hasattr(lib, "get_animation_track_names"):
        try:
            names = lib.get_animation_track_names(seq)
            arms = [str(n) for n in names
                    if any(k in str(n).lower() for k in
                           ("hand", "lowerarm", "upperarm", "spine", "clavicle", "root", "pelvis"))]
            log("track count: {}".format(len(names)))
            log("poseable arm/spine tracks: {}".format(arms))
        except Exception as e:
            log("get_animation_track_names failed: {}".format(e))
    else:
        log("no get_animation_track_names - cannot read existing tracks")

log("done")
