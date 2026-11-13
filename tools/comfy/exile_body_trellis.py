"""Image to textured GLB with TRELLIS.2 through a local ComfyUI (SEED-793).

    python exile_body_trellis.py <image name in ComfyUI's input folder> <out.glb> [faces] [upsample resolution] [seed]

art/exile/SK_ExileBody.glb is 50000 faces, resolution 1536, seed 42: at 1024 the face smeared and red blotches appeared.

The graph follows ComfyUI's bundled 3d_pixal3d_trellis2_image_to_model template on its TRELLIS.2 branch, with the game
mesh decimated to `faces` and the normal map and occlusion baked from the dense remesh.
"""
import sys
import time

import comfy


def sampler(model, source, seed, steps, cfg, scheduler):
    return {"class_type": "KSampler", "inputs": {"model": [model, 0], "seed": seed, "steps": steps, "cfg": cfg, "sampler_name": "euler",
                                                 "scheduler": scheduler, "denoise": 1, "positive": [source, 0], "negative": [source, 1],
                                                 "latent_image": [source, 2]}}


def graph(image, faces, resolution, seed, prefix, texture_size=4096):
    return {
        "image": {"class_type": "LoadImage", "inputs": {"image": image}},
        "bg": {"class_type": "LoadBackgroundRemovalModel", "inputs": {"bg_removal_name": "birefnet.safetensors"}},
        "mask": {"class_type": "RemoveBackground", "inputs": {"bg_removal_model": ["bg", 0], "image": ["image", 0]}},
        "crop": {"class_type": "ImageCropToMask", "inputs": {"images": ["image", 0], "masks": ["mask", 0], "width": 1024, "height": 1024,
                                                             "pad_factor": 1.1, "grow_mask": 0, "background": "#000000"}},
        "crop_save": {"class_type": "SaveImage", "inputs": {"filename_prefix": prefix + "_input", "images": ["crop", 0]}},
        "vision": {"class_type": "CLIPVisionLoader", "inputs": {"clip_name": "dino_v3_L_naf_fp32.safetensors"}},
        "cond": {"class_type": "Trellis2Conditioning", "inputs": {"clip_vision_model": ["vision", 0], "image": ["crop", 0]}},
        "unet": {"class_type": "UNETLoader", "inputs": {"unet_name": "trellis_2_int8_convrot.safetensors", "weight_dtype": "default"}},
        "shape_vae": {"class_type": "VAELoader", "inputs": {"vae_name": "trellis_2_shape_vae_bf16.safetensors"}},
        "texture_vae": {"class_type": "VAELoader", "inputs": {"vae_name": "trellis_2_texture_vae_bf16.safetensors"}},

        "struct_cfg": {"class_type": "CFGOverride", "inputs": {"model": ["unet", 0], "cfg": 1, "start_percent": 0.667, "end_percent": 1}},
        "struct_rescale": {"class_type": "RescaleCFG", "inputs": {"model": ["struct_cfg", 0], "multiplier": 0.7}},
        "struct_model": {"class_type": "ModelSamplingSD3", "inputs": {"model": ["struct_rescale", 0], "shift": 5}},
        "struct_latent": {"class_type": "EmptyTrellis2LatentStructure", "inputs": {"batch_size": 1}},
        "struct_sample": {"class_type": "KSampler", "inputs": {"model": ["struct_model", 0], "seed": seed, "steps": 12, "cfg": 7.5, "sampler_name": "euler",
                                                               "scheduler": "normal", "denoise": 1, "positive": ["cond", 0], "negative": ["cond", 1],
                                                               "latent_image": ["struct_latent", 0]}},
        "voxel": {"class_type": "VaeDecodeStructureTrellis2", "inputs": {"samples": ["struct_sample", 0], "vae": ["shape_vae", 0], "resolution": "32"}},

        "shape_cfg": {"class_type": "CFGOverride", "inputs": {"model": ["unet", 0], "cfg": 1, "start_percent": 0.769, "end_percent": 1}},
        "shape_model": {"class_type": "RescaleCFG", "inputs": {"model": ["shape_cfg", 0], "multiplier": 0.5}},
        "shape_stage": {"class_type": "Trellis2ShapeStage", "inputs": {"positive": ["cond", 0], "negative": ["cond", 1], "voxel": ["voxel", 0]}},
        "shape_sample": sampler("shape_model", "shape_stage", seed, 20, 7.5, "normal"),
        "upsample": {"class_type": "Trellis2UpsampleStage", "inputs": {"positive": ["shape_stage", 0], "negative": ["shape_stage", 1],
                                                                       "shape_latent": ["shape_sample", 0], "vae": ["shape_vae", 0],
                                                                       "target_resolution": resolution}},
        "fine_sample": sampler("shape_model", "upsample", seed, 12, 7.5, "simple"),
        "shape": {"class_type": "VaeDecodeShapeTrellis", "inputs": {"samples": ["fine_sample", 0], "vae": ["shape_vae", 0]}},

        "texture_stage": {"class_type": "Trellis2TextureStage", "inputs": {"positive": ["upsample", 0], "negative": ["upsample", 1],
                                                                           "shape_latent": ["fine_sample", 0]}},
        "texture_sample": sampler("unet", "texture_stage", seed + 1, 12, 1, "normal"),
        "colors": {"class_type": "VaeDecodeTextureTrellis", "inputs": {"samples": ["texture_sample", 0], "vae": ["texture_vae", 0],
                                                                       "shape_subdivides": ["shape", 1]}},

        "dense": {"class_type": "RemeshMesh", "inputs": {"mesh": ["shape", 0], "resolution": 768, "sign_mode": "solid", "sign_mode.fill": 20,
                                                         "sign_mode.qef": False, "band": 1, "project_back": 0, "fix_poles": False, "smooth_iters": 20,
                                                         "drop_small_components": 0.01, "precluster_max_verts": 20000000}},
        "low": {"class_type": "DecimateMesh", "inputs": {"mesh": ["dense", 0], "target_face_count": faces, "placement_mode": "midpoint"}},
        "low_smooth": {"class_type": "MeshSmoothNormals", "inputs": {"mesh": ["low", 0], "crease_angle": 180}},
        "unwrap": {"class_type": "UnwrapMesh", "inputs": {"mesh": ["low_smooth", 0], "segmenter": "pec", "resolution": texture_size, "padding": 1,
                                                          "weld_distance": 0.0002}},
        "bake": {"class_type": "BakeTextureFromVoxel", "inputs": {"mesh": ["unwrap", 0], "voxel_colors": ["colors", 0], "reference_mesh": ["shape", 0],
                                                                  "texture_size": texture_size}},
        "normal": {"class_type": "BakeNormalMapFromMesh", "inputs": {"low_poly": ["unwrap", 0], "high_poly": ["dense", 0], "resolution": 2048,
                                                                     "cage_distance": 0.05, "ignore_backfaces": True}},
        "occlusion": {"class_type": "BakeAmbientOcclusion", "inputs": {"low_poly": ["unwrap", 0], "high_poly": ["dense", 0], "resolution": 1024,
                                                                       "samples": 64, "max_distance": 0.71, "strength": 1, "bias": 0.01}},
        "textured": {"class_type": "ApplyTextureToMesh", "inputs": {"mesh": ["unwrap", 0], "base_color": ["bake", 0], "metallic": ["bake", 1],
                                                                    "roughness": ["bake", 2], "occlusion": ["occlusion", 0], "normal_map": ["normal", 0]}},
        "final": {"class_type": "MeshSmoothNormals", "inputs": {"mesh": ["textured", 0], "crease_angle": 180}},
        "save": {"class_type": "SaveGLB", "inputs": {"mesh": ["final", 0], "filename_prefix": prefix}},
    }


if __name__ == "__main__":
    image, out = sys.argv[1], sys.argv[2]
    faces = int(sys.argv[3]) if len(sys.argv) > 3 else 50000
    resolution = int(sys.argv[4]) if len(sys.argv) > 4 else 1024
    seed = int(sys.argv[5]) if len(sys.argv) > 5 else 42
    start = time.time()
    entry = comfy.run(graph(image, faces, resolution, seed, "exile_body/body"), timeout=3000)
    print({k: list(v) for k, v in entry["outputs"].items()})
    for key, value in entry["outputs"]["save"].items():
        for item in value:
            if isinstance(item, dict) and item.get("filename", "").endswith(".glb"):
                comfy.fetch(item, out)
    crop = entry["outputs"].get("crop_save", {}).get("images")
    if crop:
        comfy.fetch(crop[0], out.replace(".glb", "_input.png"))
    print(f"{out}: {time.time() - start:.0f} s")
