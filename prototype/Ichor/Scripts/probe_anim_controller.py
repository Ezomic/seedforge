# Read-only. UE5 moved bone-track authoring out of AnimationLibrary and into a controller
# on the sequence itself. If that is exposed to Python, animation can be authored here.
# If it is not, it cannot, and the armed set has to come from somewhere else.

import unreal


def log(m):
    unreal.log("[ichor-ctrl] " + str(m))


def names(obj, needles=None):
    out = [n for n in dir(obj) if not n.startswith("_")]
    if needles:
        out = [n for n in out if any(k in n.lower() for k in needles)]
    return sorted(out)


for cls in ("AnimDataController", "AnimationDataController", "AnimDataModel",
            "AnimationDataModel", "AnimSequenceBase"):
    obj = getattr(unreal, cls, None)
    log("unreal.{}: {}".format(cls, "PRESENT" if obj else "absent"))
    if obj:
        log("  bone/track/key fns: {}".format(
            names(obj, ["bone", "track", "key", "transform", "curve"])))

seq = unreal.EditorAssetLibrary.load_asset(
    "/Game/Characters/Mannequins/Anims/Unarmed/Attack/MM_Attack_01")
if seq:
    log("AnimSequence controller/model accessors: {}".format(
        names(seq, ["controller", "model", "data"])))

    for getter in ("get_controller", "get_data_model", "get_data_model_interface"):
        if hasattr(seq, getter):
            try:
                got = getattr(seq, getter)()
                log("{}() -> {}".format(getter, got))
                if got:
                    log("  its fns: {}".format(names(got, ["bone", "key", "track", "transform"])))
            except Exception as e:
                log("{}() failed: {}".format(getter, e))

log("done")
