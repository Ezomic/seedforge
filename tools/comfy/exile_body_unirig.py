"""Auto-rigs a mesh from ComfyUI's input folder with UniRig (SEED-793).

    python exile_body_unirig.py <path under ComfyUI's input folder, e.g. 3d/exile_body.glb> <fbx name> [template]

The FBX lands in ComfyUI's output folder. The template defaults to articulationxl, UniRig's own skeleton: its mixamo
template hands the Mixamo names to the wrong bones and moves the joints to a T-pose the mesh is not in.
"""
import sys
import time

import comfy

graph = {
    "mesh": {"class_type": "UniRigLoadMesh", "inputs": {"source_folder": "input", "file_path": sys.argv[1]}},
    "model": {"class_type": "UniRigLoadModel", "inputs": {"precision": "auto", "attn_backend": "auto"}},
    "rig": {"class_type": "UniRigAutoRig", "inputs": {"trimesh": ["mesh", 0], "model": ["model", 0], "skeleton_template": sys.argv[3] if len(sys.argv) > 3 else "articulationxl",
                                                      "fbx_name": sys.argv[2], "target_face_count": 50000, "debug": False}},
    "show": {"class_type": "PreviewAny", "inputs": {"source": ["rig", 0]}},
}
start = time.time()
entry = comfy.run(graph, timeout=1800)
print(entry["outputs"], f"{time.time() - start:.0f} s")
