# Creates the Stage 0 combat input actions and maps them into IMC_Default.
#
#   UnrealEditor-Cmd.exe Ichor.uproject -run=pythonscript -script="Scripts/make_input_assets.py"
#
# Notes on the UE 5.8 API, learned by probing rather than assuming:
#   - InputMappingContext.mappings is deprecated and empty; the data lives in
#     default_key_mappings. Use map_key()/unmap_all_keys_from_action() instead of
#     touching either array directly.
#   - unreal.Key takes no constructor arguments; set key_name as a property.
#   - save_asset() defaults to only_if_is_dirty=True, and map_key() does not mark the
#     package dirty. The first run of this script reported every mapping correctly and
#     wrote none of them: the read-back was showing memory, not disk. Save explicitly,
#     then reload from disk and verify there.
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


def entry_names(imc_asset):
    """['IA_Dodge -> SpaceBar', ...], or None if the mappings could not be read.

    default_key_mappings is not a plain array in 5.8 - it is an
    InputMappingContextMappingData wrapper, and iterating it raises. Rather than pin a
    property name that may move again, look for whichever one holds the array, and say so
    plainly when nothing matches instead of reporting a false failure.
    """
    data = imc_asset.get_editor_property("default_key_mappings")

    candidates = [data]
    for prop in ("mappings", "key_mappings"):
        try:
            candidates.append(data.get_editor_property(prop))
        except Exception:
            pass

    for c in candidates:
        try:
            entries = list(c)
        except TypeError:
            continue
        out = []
        for m in entries:
            try:
                act = m.get_editor_property("action")
                key = m.get_editor_property("key")
                out.append("{} -> {}".format(act.get_name() if act else "?",
                                             key.get_editor_property("key_name")))
            except Exception:
                out.append(str(m))
        return out

    return None


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

    imc.modify()
    unreal.EditorLoadingAndSavingUtils.save_packages([imc.get_outermost()], False)
    lib.save_asset(IMC_PATH, only_if_is_dirty=False)
    log("saved IMC_Default; {} combat action(s) mapped".format(created))

    # Verify against disk, not against memory. Reloading is the whole point - the
    # in-memory object will happily report mappings that were never written.
    fresh = lib.load_asset(IMC_PATH)
    names = entry_names(fresh)

    if names is None:
        log("could not introspect mappings - check IMC_Default in the editor by hand")
        return

    log("ON DISK: {}".format(names))
    missing = [n for n, _ in WANTED if not any(n in x for x in names)]
    if missing:
        log("FAILED - not persisted: {}".format(missing))
    else:
        log("VERIFIED: all {} combat actions persisted".format(len(WANTED)))


main()
