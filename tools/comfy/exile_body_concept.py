"""The bare Exile body's concept image, with Z-Image Turbo through a local ComfyUI (SEED-793).

    python exile_body_concept.py <out.png> [seed]

Flat light and a plain background, because the image is the only input TRELLIS.2 gets (exile_body_trellis.py): any
highlight or shadow in it ends up baked into the model's colour. art/concept/characters/T_ExileBodyFront.png is seed 303.
"""
import sys

import comfy

PROMPT = ("Full body character concept art of a muscular ancient Greek warrior without any armor, standing upright in a symmetrical A-pose, "
          "both arms held straight out from the sides at 35 degrees, hands open and relaxed with fingers slightly curled, bare chest, "
          "plain off-white linen loincloth wrapped around the hips, barefoot, bare head, a man in his thirties with stubble, "
          "shoulder length dark hair tied back, weathered tanned skin, realistic adult proportions, "
          "front view facing the camera, feet shoulder width apart, flat even diffuse studio lighting, no cast shadows, no rim light, "
          "no specular highlights, matte skin, plain light grey background, the whole figure from head to toes in frame with margin, "
          "realistic digital painting, game character model sheet, not cartoon")


def graph(prompt, seed, width, height, prefix):
    return {
        "clip": {"class_type": "CLIPLoader", "inputs": {"clip_name": "qwen_3_4b.safetensors", "type": "lumina2", "device": "default"}},
        "vae": {"class_type": "VAELoader", "inputs": {"vae_name": "ae.safetensors"}},
        "unet": {"class_type": "UNETLoader", "inputs": {"unet_name": "z_image_turbo_bf16.safetensors", "weight_dtype": "default"}},
        "shift": {"class_type": "ModelSamplingAuraFlow", "inputs": {"shift": 3, "model": ["unet", 0]}},
        "pos": {"class_type": "CLIPTextEncode", "inputs": {"text": prompt, "clip": ["clip", 0]}},
        "neg": {"class_type": "ConditioningZeroOut", "inputs": {"conditioning": ["pos", 0]}},
        "latent": {"class_type": "EmptySD3LatentImage", "inputs": {"width": width, "height": height, "batch_size": 1}},
        "sample": {"class_type": "KSampler", "inputs": {"seed": seed, "steps": 8, "cfg": 1, "sampler_name": "res_multistep", "scheduler": "simple",
                                                        "denoise": 1, "model": ["shift", 0], "positive": ["pos", 0], "negative": ["neg", 0],
                                                        "latent_image": ["latent", 0]}},
        "decode": {"class_type": "VAEDecode", "inputs": {"samples": ["sample", 0], "vae": ["vae", 0]}},
        "save": {"class_type": "SaveImage", "inputs": {"filename_prefix": prefix, "images": ["decode", 0]}},
    }


if __name__ == "__main__":
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 303
    entry = comfy.run(graph(PROMPT, seed, 768, 1152, "exile_body/concept"))
    comfy.fetch(entry["outputs"]["save"]["images"][0], sys.argv[1])
    print(f"wrote {sys.argv[1]}")
