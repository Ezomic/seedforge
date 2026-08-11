# Enforces doc 17's content rule: organise by feature, not by asset type.
#
#   UnrealEditor-Cmd.exe Ichor.uproject -run=pythonscript -script="Scripts/content_convention.py"
#       reports only - safe to run any time, changes nothing
#   ... -script="Scripts/content_convention.py -apply"
#       actually moves assets into their feature folders
#
# Reporting is the default deliberately. A commandlet does not pass its trailing switches
# through to sys.argv reliably, so a flag that turns moving ON is a flag that can fail
# closed; a flag that turns moving OFF fails open and reorganises somebody's content
# because an argument went missing.
#
# "/Content/Dungeons/CryptKit/ beats /Content/Meshes/. Type-based folders stop scaling
# around the thousandth asset." - doc 17
#
# /Game/Ichor grew the wrong way during Stage 0: Anims, Audio, Kit, Maps, Materials,
# Props, Textures, Weapons. Every one of those is a type. Moving 25 assets costs nothing;
# the ticket exists because moving the thousandth costs a week.
#
# Renaming rather than copying matters - EditorAssetLibrary.rename_asset leaves a redirector
# and fixes referencing assets, so the arena keeps its floor.

import sys
import unreal

ROOT = "/Game/Ichor"
CHECK_ONLY = "-apply" not in " ".join(sys.argv)

# Folders whose names describe what an asset IS rather than what it is FOR.
TYPE_NAMES = {
    "anims", "animations", "audio", "sounds", "materials", "textures", "meshes",
    "props", "weapons", "maps", "levels", "blueprints", "fx", "ui", "kit",
}

# feature -> the assets that make it up, wherever they currently live.
FEATURES = {
    "Hoplite": [
        "Kit/SM_Dory", "Kit/SM_Aspis", "Kit/SM_Helmet",
        "Textures/T_Bronze", "Textures/T_Crest", "Textures/T_CrestDark",
        "Textures/T_Wood", "Textures/T_Helmet_AO",
        "Materials/M_Ichor_Kit", "Materials/MI_Bronze", "Materials/MI_Wood",
        "Materials/MI_HelmetBronze", "Materials/MI_HelmetCrest", "Materials/MI_HelmetCrestDark",
        "Audio/A_Tell", "Audio/A_WindupLight", "Audio/A_WindupHeavy",
        "Anims/Spear/AM_Light_Spear", "Anims/Spear/AM_Heavy_Spear",
        "Anims/Spear/AM_Stagger_Spear", "Anims/Spear/AM_Death_Spear",
        "Anims/Unarmed/AM_Light_Unarmed", "Anims/Unarmed/AM_Heavy_Unarmed",
        "Anims/Unarmed/AM_Stagger_Unarmed", "Anims/Unarmed/AM_Death_Unarmed",
    ],
    "Arena": [
        "Maps/Lvl_Arena",
        "Props/SM_Column_Doric", "Props/SM_WallBlock", "Props/SM_FloorSlab",
        "Materials/M_Ichor_Marble", "Materials/M_Ichor_Terracotta", "Materials/M_Ichor_Sand",
    ],
}

lib = unreal.EditorAssetLibrary


def log(m):
    unreal.log("[ichor-convention] " + str(m))


def move_batch(pairs):
    """AssetTools.rename_assets, not EditorAssetLibrary.rename_asset.

    The simple call silently refuses any package that is currently in use, and every asset
    named in a C++ ConstructorHelpers is loaded into a CDO at module startup - so meshes,
    montages, cues and the startup map all failed while loose textures moved fine. This is
    the API the editor's own rename uses; it fixes referencers and leaves redirectors."""
    renames = []
    for src, dst in pairs:
        asset = lib.load_asset(src)
        if asset is None:
            log("  could not load " + src)
            continue
        folder, name = dst.rsplit("/", 1)
        try:
            renames.append(unreal.AssetRenameData(asset, folder, name))
        except Exception:
            data = unreal.AssetRenameData()
            data.set_editor_property("asset", asset)
            data.set_editor_property("new_package_path", folder)
            data.set_editor_property("new_name", name)
            renames.append(data)

    if not renames:
        return 0
    if CHECK_ONLY:
        for src, dst in pairs:
            log("  WOULD MOVE {} -> {}".format(src, dst))
        return len(renames)

    tools = unreal.AssetToolsHelpers.get_asset_tools()
    tools.rename_assets(renames)

    done = sum(1 for _, dst in pairs if lib.does_asset_exist(dst))
    for _, dst in pairs:
        if not lib.does_asset_exist(dst):
            log("  MOVE FAILED: " + dst)
    return done


def reorganise():
    moved = 0
    for feature, assets in FEATURES.items():
        pairs = []
        for rel in assets:
            name = rel.rsplit("/", 1)[-1]
            src = "{}/{}".format(ROOT, rel)
            dst = "{}/{}/{}".format(ROOT, feature, name)
            if lib.does_asset_exist(dst):
                continue
            if not lib.does_asset_exist(src):
                log("  MISSING: " + rel)
                continue
            pairs.append((src, dst))

        log("{}: {} to move".format(feature, len(pairs)))
        moved += move_batch(pairs)

    log("moved {} assets".format(moved))
    return moved


def leftovers():
    """Anything still sitting in a type-named folder, and anything nothing references.

    Reported rather than deleted: an unreferenced asset might be unreferenced because
    something is broken, and quietly removing it would hide that."""
    stragglers = []
    orphans = []

    for path in lib.list_assets(ROOT, recursive=True, include_folder=False):
        clean = path.split(".")[0]
        parts = clean[len(ROOT) + 1:].split("/")
        if len(parts) < 2:
            continue
        if parts[0].lower() in TYPE_NAMES:
            stragglers.append(clean)
            deps = lib.find_package_referencers_for_asset(clean, False)
            if not deps:
                orphans.append(clean)

    if stragglers:
        log("STILL IN TYPE FOLDERS ({}):".format(len(stragglers)))
        for s in stragglers:
            log("  {}{}".format(s, "   [nothing references it]" if s in orphans else ""))
    else:
        log("no assets left in type-named folders")

    return stragglers, orphans


def check_only():
    """The enforcement half: fails loudly if the top level is organised by type."""
    bad = []
    for folder in lib.list_assets(ROOT, recursive=False, include_folder=True):
        name = folder.rstrip("/").rsplit("/", 1)[-1]
        if name.lower() in TYPE_NAMES:
            bad.append(name)
    if bad:
        log("CONVENTION VIOLATION: top-level folders named by type: {}".format(sorted(bad)))
        log("doc 17: organise Content by feature, not asset type")
    else:
        log("CONVENTION OK: every top-level folder under {} is a feature".format(ROOT))
    return bad


def main():
    if not CHECK_ONLY:
        reorganise()
    leftovers()
    check_only()
    log("done")


main()
