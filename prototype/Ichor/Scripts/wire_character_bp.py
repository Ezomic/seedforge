# Assigns the combat input actions onto BP_ThirdPersonCharacter's defaults, so the
# bindings in AIchorCharacter::SetupPlayerInputComponent actually resolve.
#
#   UnrealEditor-Cmd.exe Ichor.uproject -run=pythonscript -script="Scripts/wire_character_bp.py"

import unreal

BP_PATH = "/Game/ThirdPerson/Blueprints/BP_ThirdPersonCharacter"
ACTIONS = "/Game/Input/Actions"

# C++ name -> Python property name
WIRING = [
    ("IA_LightAttack", "light_attack_action"),
    ("IA_HeavyAttack", "heavy_attack_action"),
    ("IA_Block",       "block_action"),
    ("IA_Dodge",       "dodge_action"),
]

lib = unreal.EditorAssetLibrary


def log(m):
    unreal.log("[ichor-wire] " + str(m))


def main():
    bp = lib.load_asset(BP_PATH)
    if bp is None:
        log("ABORT: no " + BP_PATH)
        return

    gen = bp.generated_class()
    log("generated class: {}".format(gen.get_name() if gen else "?"))

    cdo = unreal.get_default_object(gen)
    if cdo is None:
        log("ABORT: no CDO")
        return

    ok = 0
    for asset_name, prop in WIRING:
        path = "{}/{}".format(ACTIONS, asset_name)
        action = lib.load_asset(path)
        if action is None:
            log("missing action asset: " + path)
            continue
        try:
            cdo.set_editor_property(prop, action)
            log("set {} = {}".format(prop, asset_name))
            ok += 1
        except Exception as e:
            log("FAILED {}: {}".format(prop, e))

    # read back rather than assume
    for asset_name, prop in WIRING:
        try:
            v = cdo.get_editor_property(prop)
            log("verify {} -> {}".format(prop, v.get_name() if v else None))
        except Exception as e:
            log("verify failed {}: {}".format(prop, e))

    lib.save_asset(BP_PATH)
    log("saved; {} of {} wired".format(ok, len(WIRING)))


main()
