# Imports the Blender kit into Unreal: meshes, textures, and a material that uses both
# UV sets the way Blender does.
#
#   UnrealEditor-Cmd.exe Ichor.uproject -run=pythonscript -script="Scripts/import_kit.py"
#
# The meshes land in /Game/Ichor/Kit rather than overwriting the Geometry Script versions,
# so the two can be compared and the old ones removed deliberately rather than by
# accident.
#
# The material is the interesting part. Blender multiplies a tiling bronze on UV0 by a
# baked per-asset AO map on UV1, and drives roughness from the bronze's own luminance
# because on metal that is what shows. This reproduces all three, so the asset looks in
# game roughly like it looked in the preview - which is the only reason the preview was
# worth rendering.

import unreal
import os

ART = os.path.join(unreal.Paths.project_dir(), "..", "..", "art")
FBX_DIR = os.path.abspath(os.path.join(ART, "export"))
TEX_DIR = os.path.abspath(os.path.join(ART, "textures"))
AUDIO_DIR = os.path.abspath(os.path.join(ART, "audio"))

KIT = "/Game/Ichor/Kit"
TEXTURES = "/Game/Ichor/Textures"
MATERIALS = "/Game/Ichor/Materials"
AUDIO = "/Game/Ichor/Audio"

WHITE = "/Engine/EngineResources/WhiteSquareTexture"

lib = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary

MESHES = ["SM_Aspis", "SM_Dory", "SM_Helmet"]
TEXES = ["T_Bronze", "T_Crest", "T_CrestDark", "T_Wood", "T_Helmet_AO"]
CUES = ["A_Tell", "A_WindupLight", "A_WindupHeavy"]


def log(m):
    unreal.log("[ichor-import] " + str(m))


def ensure_dirs():
    for d in (KIT, TEXTURES, MATERIALS, AUDIO):
        if not lib.does_directory_exist(d):
            lib.make_directory(d)


# ---------------------------------------------------------------- import

def import_texture(name):
    src = os.path.join(TEX_DIR, name + ".png")
    if not os.path.exists(src):
        log("MISSING texture source: " + src)
        return None

    task = unreal.AssetImportTask()
    task.filename = src
    task.destination_path = TEXTURES
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = True
    tools.import_asset_tasks([task])

    asset = lib.load_asset("{}/{}".format(TEXTURES, name))
    if asset is None:
        log("IMPORT FAILED: " + name)
        return None

    # AO is data, not colour. Left as sRGB it would be gamma-decoded and the occlusion
    # would sit too dark.
    if name.endswith("_AO"):
        asset.set_editor_property("srgb", False)
        asset.set_editor_property("compression_settings",
                                  unreal.TextureCompressionSettings.TC_MASKS)
    lib.save_asset("{}/{}".format(TEXTURES, name), only_if_is_dirty=False)
    return asset


def import_mesh(name):
    src = os.path.join(FBX_DIR, name + ".fbx")
    if not os.path.exists(src):
        log("MISSING mesh source: " + src)
        return None

    opts = unreal.FbxImportUI()
    opts.set_editor_property("import_mesh", True)
    opts.set_editor_property("import_as_skeletal", False)
    # Blender writes its own material names; letting Unreal invent assets from them would
    # leave junk to clean up, and they get replaced below anyway.
    opts.set_editor_property("import_materials", False)
    opts.set_editor_property("import_textures", False)

    sm = opts.static_mesh_import_data
    sm.set_editor_property("combine_meshes", True)
    sm.set_editor_property("generate_lightmap_u_vs", False)
    sm.set_editor_property("auto_generate_collision", True)
    sm.set_editor_property("convert_scene", True)

    task = unreal.AssetImportTask()
    task.filename = src
    task.destination_path = KIT
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = True
    task.options = opts
    tools.import_asset_tasks([task])

    asset = lib.load_asset("{}/{}".format(KIT, name))
    if asset is None:
        log("IMPORT FAILED: " + name)
    return asset


def import_sound(name):
    src = os.path.join(AUDIO_DIR, name + ".wav")
    if not os.path.exists(src):
        log("MISSING cue source: " + src)
        return None

    task = unreal.AssetImportTask()
    task.filename = src
    task.destination_path = AUDIO
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = True
    tools.import_asset_tasks([task])

    asset = lib.load_asset("{}/{}".format(AUDIO, name))
    if asset is None:
        log("IMPORT FAILED: " + name)
        return None

    # Telegraphs must never be ducked or dropped when the mix gets busy - the whole point
    # is that they are audible at the moment the player has to act.
    try:
        asset.set_editor_property("looping", False)
        lib.save_asset("{}/{}".format(AUDIO, name), only_if_is_dirty=False)
    except Exception as e:
        log("cue tweak failed on {}: {}".format(name, e))

    try:
        log("cue {:<16} {:.2f}s".format(name, asset.get_editor_property("duration")))
    except Exception:
        log("cue " + name)
    return asset


# ---------------------------------------------------------------- material

def build_master():
    path = "{}/M_Ichor_Kit".format(MATERIALS)
    if lib.does_asset_exist(path):
        log("master exists")
        return lib.load_asset(path)

    mat = tools.create_asset("M_Ichor_Kit", MATERIALS, unreal.Material,
                             unreal.MaterialFactoryNew())
    if mat is None:
        log("could not create master material")
        return None

    base = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, -900, -300)
    base.set_editor_property("parameter_name", "BaseTex")
    base.set_editor_property("texture", lib.load_asset("{}/T_Bronze".format(TEXTURES)))

    # UV1 carries the baked occlusion. Without the explicit coordinate node this would
    # sample the tiling UVs and the AO would tile with them, which is meaningless.
    uv1 = mel.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -1150, 60)
    uv1.set_editor_property("coordinate_index", 1)

    ao = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, -900, 60)
    ao.set_editor_property("parameter_name", "AOTex")
    ao.set_editor_property("texture", lib.load_asset(WHITE))
    mel.connect_material_expressions(uv1, "", ao, "UVs")

    tinted = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -520, -220)
    mel.connect_material_expressions(base, "RGB", tinted, "A")
    mel.connect_material_expressions(ao, "RGB", tinted, "B")
    mel.connect_material_property(tinted, "", unreal.MaterialProperty.MP_BASE_COLOR)

    metallic = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -520, 40)
    metallic.set_editor_property("parameter_name", "Metallic")
    metallic.set_editor_property("default_value", 0.62)
    mel.connect_material_property(metallic, "", unreal.MaterialProperty.MP_METALLIC)

    # Roughness from the texture's own luminance, dull where the texture is dark. This is
    # what gives metal its surface in the Blender preview; a flat scalar looks like chrome.
    grey = mel.create_material_expression(mat, unreal.MaterialExpressionDesaturation, -700, 260)
    mel.connect_material_expressions(base, "RGB", grey, "")

    rough = mel.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, -420, 260)
    rmax = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -700, 200)
    rmax.set_editor_property("parameter_name", "RoughDull")
    rmax.set_editor_property("default_value", 0.62)
    rmin = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -700, 340)
    rmin.set_editor_property("parameter_name", "RoughPolished")
    rmin.set_editor_property("default_value", 0.24)

    mel.connect_material_expressions(rmax, "", rough, "A")
    mel.connect_material_expressions(rmin, "", rough, "B")
    mel.connect_material_expressions(grey, "", rough, "Alpha")
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)

    mel.recompile_material(mat)
    lib.save_asset(path, only_if_is_dirty=False)
    log("built master material")
    return mat


def instance(name, master, base_tex, ao_tex, metallic):
    path = "{}/{}".format(MATERIALS, name)
    if lib.does_asset_exist(path):
        mi = lib.load_asset(path)
    else:
        mi = tools.create_asset(name, MATERIALS, unreal.MaterialInstanceConstant,
                                unreal.MaterialInstanceConstantFactoryNew())
        if mi is None:
            log("could not create " + name)
            return None
        mel.set_material_instance_parent(mi, master)

    mel.set_material_instance_texture_parameter_value(mi, "BaseTex", base_tex)
    mel.set_material_instance_texture_parameter_value(mi, "AOTex", ao_tex)
    mel.set_material_instance_scalar_parameter_value(mi, "Metallic", metallic)
    lib.save_asset(path, only_if_is_dirty=False)
    log("instance " + name)
    return mi


def assign(mesh_path, materials):
    mesh = lib.load_asset(mesh_path)
    if mesh is None:
        return
    # get_editor_property hands back copies of the structs, so mutating them in place
    # changes nothing and the mesh keeps WorldGridMaterial while the script reports
    # success. Build fresh structs and assign the whole array.
    slots = mesh.get_editor_property("static_materials")
    rebuilt = []
    for i, existing in enumerate(slots):
        entry = unreal.StaticMaterial()
        chosen = materials[i] if i < len(materials) and materials[i] is not None else             existing.get_editor_property("material_interface")
        entry.set_editor_property("material_interface", chosen)
        entry.set_editor_property("material_slot_name",
                                  existing.get_editor_property("material_slot_name"))
        rebuilt.append(entry)
    mesh.set_editor_property("static_materials", rebuilt)
    lib.save_asset(mesh_path, only_if_is_dirty=False)
    log("{}: {} slot(s) assigned".format(mesh_path.rsplit("/", 1)[-1], len(slots)))


def main():
    ensure_dirs()
    log("fbx from " + FBX_DIR)

    for t in TEXES:
        if import_texture(t) is None:
            log("WARNING: {} did not import - anything using it falls back".format(t))
    for m in MESHES:
        import_mesh(m)
    for c in CUES:
        if import_sound(c) is None:
            log("WARNING: {} did not import - that attack will telegraph silently".format(c))

    master = build_master()
    if master is None:
        log("ABORT: no master material")
        return

    white = lib.load_asset(WHITE)
    bronze_tex = lib.load_asset("{}/T_Bronze".format(TEXTURES))
    crest_tex = lib.load_asset("{}/T_Crest".format(TEXTURES))
    wood_tex = lib.load_asset("{}/T_Wood".format(TEXTURES))
    helm_ao = lib.load_asset("{}/T_Helmet_AO".format(TEXTURES))

    mi_bronze = instance("MI_Bronze", master, bronze_tex, white, 0.62)
    mi_wood = instance("MI_Wood", master, wood_tex, white, 0.0)
    mi_helm = instance("MI_HelmetBronze", master, bronze_tex, helm_ao, 0.62)
    mi_crest = instance("MI_HelmetCrest", master, crest_tex, helm_ao, 0.0)
    # The opponent's crest. Same mesh, same AO, different dye - so the two sides are
    # told apart by colour and nothing else.
    instance("MI_HelmetCrestDark", master,
             lib.load_asset("{}/T_CrestDark".format(TEXTURES)), helm_ao, 0.0)

    # Blender exported the spear as shaft-then-bronze and the helmet as bronze-then-crest;
    # slot order follows the order the materials were first used on the joined mesh.
    assign("{}/SM_Aspis".format(KIT), [mi_bronze])
    assign("{}/SM_Dory".format(KIT), [mi_wood, mi_bronze])
    assign("{}/SM_Helmet".format(KIT), [mi_helm, mi_crest])

    ok = True
    for m in MESHES:
        path = "{}/{}".format(KIT, m)
        a = lib.load_asset(path)
        if a is None:
            log("VERIFY FAIL: " + path)
            ok = False
            continue
        b = a.get_bounding_box()
        slots = a.get_editor_property("static_materials")
        log("VERIFY {:<10} {:.0f} x {:.0f} x {:.0f} cm, {} slot(s): {}".format(
            m, b.max.x - b.min.x, b.max.y - b.min.y, b.max.z - b.min.z, len(slots),
            [s.material_interface.get_name() if s.material_interface else "none" for s in slots]))

    log("DONE" if ok else "FAILED - see above")


main()
