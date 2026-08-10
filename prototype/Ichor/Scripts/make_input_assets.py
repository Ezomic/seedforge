# Creates the Stage 0 combat input actions and maps them into IMC_Default.
#
#   UnrealEditor-Cmd.exe Ichor.uproject -run=pythonscript -script="Scripts/make_input_assets.py"
#
# Notes on the UE 5.8 API, learned by probing rather than assuming:
#   - InputMappingContext.mappings is deprecated and empty; the data lives in
#     default_key_mappings. Use map_key()/unmap_all_keys_from_action() instead of
#     touching either array directly.
#   - unreal.Key takes no constructor arguments; set key_name as a property.
#
# Idempotent: each action is unmapped before being mapped, so re-runs do not duplicate.

import unreal

ACTIONS_PATH = "/Game/Input/Actions"
IMC_PATH = "/Game/Input/IMC_Default"

# Space is Jump in the template. Stage 0 is grounded combat - doc 18 puts no jump in
# scope - so Space is reclaimed for Dodge and Jump is unmapped.
WANTED = [
    ("IA_LightAttack", "LeftMouseButton"),
    ("IA_HeavyAttack", "RightMouseButton"),
    ("IA_Block",       "LeftShift"),
    ("IA_Dodge",       "SpaceBar"),
]

tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.EditorAssetLibrary


def log(msg):
    unreal.log("[ichor-input] " + str(msg))


def make_key(name):
    k = unreal.Key()
    k.set_editor_property("key_name", name)
    return k


def get_or_create_action(name):
    full = "{}/{}".format(ACTIONS_PATH, name)
    if lib.does_asset_exist(full):
        log("exists: " + name)
        return lib.load_asset(full)
    asset = tools.create_asset(name, ACTIONS_PATH, unreal.InputAction, None)
    if asset is None:
        log("FAILED to create " + full)
        return None
    try:
        # Digital: these are press actions, not axes.
        asset.set_editor_property("value_type", unreal.InputActionValueType.BOOLEAN)
    except Exception as e:
        log("could not set value_type on {}: {}".format(name, e))
    lib.save_asset(full)
    log("created: " + name)
    return asset


def main():
    imc = lib.load_asset(IMC_PATH)
    if imc is None:
        log("ABORT: could not load " + IMC_PATH)
        return

    created = 0
    for name, key_name in WANTED:
        action = get_or_create_action(name)
        if action is None:
            continue
        # unmap first so repeated runs stay clean
        try:
            imc.unmap_all_keys_from_action(action)
        except Exception as e:
            log("unmap warning for {}: {}".format(name, e))
        imc.map_key(action, make_key(key_name))
        log("mapped {} -> {}".format(name, key_name))
        created += 1

    # Reclaim Space: Dodge owns it now, and Stage 0 has no jump.
    jump = "{}/IA_Jump".format(ACTIONS_PATH)
    if lib.does_asset_exist(jump):
        try:
            imc.unmap_all_keys_from_action(lib.load_asset(jump))
            log("unmapped IA_Jump (Space reclaimed for Dodge)")
        except Exception as e:
            log("could not unmap IA_Jump: {}".format(e))

    lib.save_asset(IMC_PATH)
    log("saved IMC_Default; {} combat action(s) mapped".format(created))

    # read back so the result is verified, not assumed
    data = imc.get_editor_property("default_key_mappings")
    log("default_key_mappings now: {}".format(data))
    log("done")


main()
