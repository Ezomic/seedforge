# Creates the combat montages from animations the project already ships.
#
#   UnrealEditor-Cmd.exe Ichor.uproject -run=pythonscript -script="Scripts/make_combat_montages.py"
#
# One clip covers a whole attack - wind-up, active and recovery are phases of the same
# swing, not separate assets. The combat component scales the play rate so the montage
# lasts exactly as long as the C++ frame data says the attack lasts, which keeps every
# timing tunable from a slider. See docs/18-vertical-slice.md on why that matters more
# than play-rate fidelity at this stage.
#
# Idempotent: existing montages are left alone.

import unreal

OUT_DIR = "/Game/Ichor/Anims/Unarmed"
ANIMS = "/Game/Characters/Mannequins/Anims"

# (montage, source clip, what the combat component drives it with)
WANTED = [
    ("AM_Light_Unarmed",   ANIMS + "/Unarmed/Attack/MM_Attack_01",          "Light: 1.000s -> 0.85s"),
    ("AM_Heavy_Unarmed",   ANIMS + "/Unarmed/Attack/MM_Attack_03",          "Heavy: 1.667s -> 1.64s"),
    ("AM_Stagger_Unarmed", ANIMS + "/Rifle/HitReact/MM_HitReact_Front_Med_01", "Stagger: 0.767s -> 0.70s"),
    ("AM_Death_Unarmed",   ANIMS + "/Death/MM_Death_Front_01",              "Death: 1.100s, unscaled"),
]

lib = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()


def log(m):
    unreal.log("[ichor-montage] " + str(m))


def play_length(asset):
    for prop in ("sequence_length", "play_length"):
        try:
            return float(asset.get_editor_property(prop))
        except Exception:
            pass
    try:
        return float(asset.get_play_length())
    except Exception:
        return -1.0


def build(name, source_path, note):
    out = "{}/{}".format(OUT_DIR, name)
    if lib.does_asset_exist(out):
        log("exists, left alone: " + name)
        return True

    seq = lib.load_asset(source_path)
    if seq is None:
        log("MISSING SOURCE: " + source_path)
        return False

    try:
        skel = seq.get_editor_property("skeleton")
    except Exception as e:
        log("no skeleton on {}: {}".format(source_path, e))
        return False

    factory = unreal.AnimMontageFactory()
    factory.set_editor_property("target_skeleton", skel)

    # The content browser's "Create Montage" sets this; without it the montage is created
    # empty and plays nothing, which looks identical to success from the outside.
    try:
        factory.set_editor_property("source_animation", seq)
    except Exception as e:
        log("FACTORY HAS NO source_animation ({}). Properties: {}".format(
            e, [p for p in dir(factory) if not p.startswith("_")]))
        return False

    montage = tools.create_asset(name, OUT_DIR, unreal.AnimMontage, factory)
    if montage is None:
        log("create_asset returned None for " + name)
        return False

    lib.save_asset(out, only_if_is_dirty=False)
    log("created {}  ({})".format(name, note))
    return True


def verify():
    """Reload from disk and confirm the montage actually contains the clip."""
    ok = True
    for name, source_path, _ in WANTED:
        out = "{}/{}".format(OUT_DIR, name)
        m = lib.load_asset(out)
        if m is None:
            log("VERIFY FAIL: {} does not exist".format(name))
            ok = False
            continue

        length = play_length(m)
        expected = play_length(lib.load_asset(source_path))

        slot = "?"
        try:
            tracks = m.get_editor_property("slot_anim_tracks")
            slot = str(tracks[0].get_editor_property("slot_name")) if tracks else "none"
        except Exception:
            pass

        if length <= 0.0:
            log("VERIFY FAIL: {} is empty ({:.3f}s) - montage exists but plays nothing".format(name, length))
            ok = False
        else:
            log("VERIFY OK:   {:<20} {:.3f}s (source {:.3f}s) slot={}".format(name, length, expected, slot))
    return ok


def main():
    if not lib.does_directory_exist(OUT_DIR):
        lib.make_directory(OUT_DIR)
        log("created " + OUT_DIR)

    for name, source_path, note in WANTED:
        build(name, source_path, note)

    log("VERIFIED: all montages contain their clip" if verify()
        else "FAILED: see the verify lines above - do not wire C++ to these yet")


main()
