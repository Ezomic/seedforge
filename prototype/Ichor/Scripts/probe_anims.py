# Read-only. Dumps length, frame rate and existing notifies for the combat-relevant
# animations, plus the montage slots ABP_Unarmed exposes.
#
#   UnrealEditor-Cmd.exe Ichor.uproject -run=pythonscript -script="Scripts/probe_anims.py"
#
# The lengths decide the architecture: whether the C++ frame data can keep its current
# numbers, or whether the animations have to be play-rate scaled to fit them.

import unreal

TARGETS = [
    "/Game/Characters/Mannequins/Anims/Unarmed/Attack/MM_Attack_01",
    "/Game/Characters/Mannequins/Anims/Unarmed/Attack/MM_Attack_02",
    "/Game/Characters/Mannequins/Anims/Unarmed/Attack/MM_Attack_03",
    "/Game/Characters/Mannequins/Anims/Unarmed/Attack/MM_ChargedAttack",
    "/Game/Characters/Mannequins/Anims/Rifle/HitReact/MM_HitReact_Front_Lgt_01",
    "/Game/Characters/Mannequins/Anims/Rifle/HitReact/MM_HitReact_Front_Med_01",
    "/Game/Characters/Mannequins/Anims/Rifle/HitReact/MM_HitReact_Front_Hvy_01",
    "/Game/Characters/Mannequins/Anims/Death/MM_Death_Front_01",
    "/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle",
]

lib = unreal.EditorAssetLibrary


def log(m):
    unreal.log("[ichor-anim] " + str(m))


def describe(path):
    a = lib.load_asset(path)
    if a is None:
        log("MISSING  {}".format(path))
        return

    name = path.rsplit("/", 1)[-1]

    length = None
    for prop in ("sequence_length", "play_length"):
        try:
            length = a.get_editor_property(prop)
            break
        except Exception:
            pass
    if length is None:
        try:
            length = a.get_play_length()
        except Exception:
            length = -1.0

    try:
        rate = a.get_editor_property("target_frame_rate")
        rate = "{}fps".format(rate.numerator)
    except Exception:
        rate = "?"

    notifies = []
    try:
        for ev in a.get_editor_property("notifies"):
            try:
                t = ev.get_editor_property("trigger_time_offset")
            except Exception:
                t = "?"
            notifies.append(str(t))
    except Exception:
        pass

    log("{:<32} {:>6.3f}s  {:<7} notifies={}".format(name, float(length), rate, len(notifies)))


def slots():
    abp = lib.load_asset("/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed")
    log("ABP_Unarmed loaded: {}".format(abp is not None))
    try:
        sk = abp.get_editor_property("target_skeleton")
        log("skeleton: {}".format(sk.get_name() if sk else "?"))
        groups = sk.get_editor_property("slot_group_names")
        log("slot groups on skeleton: {}".format([str(g) for g in groups]))
    except Exception as e:
        log("slot lookup failed: {}".format(e))


for t in TARGETS:
    describe(t)
slots()
log("done")
