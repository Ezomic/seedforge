"""blender -b --python fbx_to_glb.py -- <in.fbx> <out.glb>: the rigged FBX as a GLB with its skin, nothing else changed."""
import sys

import bpy

source, target = sys.argv[sys.argv.index("--") + 1:]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=source)
for obj in bpy.data.objects:
    print("[rig]", obj.type, obj.name, tuple(round(v, 4) for v in obj.scale), tuple(round(v, 4) for v in obj.location))
    if obj.type == "ARMATURE":
        print("[rig] bones", len(obj.data.bones), [b.name for b in obj.data.bones])
    if obj.type == "MESH":
        print("[rig] verts", len(obj.data.vertices), "groups", len(obj.vertex_groups))
bpy.ops.export_scene.gltf(filepath=target, export_format="GLB", export_animations=False, export_skins=True, export_all_influences=False,
                          export_image_format="NONE", export_materials="NONE")
