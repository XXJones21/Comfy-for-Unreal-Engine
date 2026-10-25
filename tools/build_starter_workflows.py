"""Writes the plugin's built-in starter workflows to Resources/Workflows and checks them.

Each workflow is a plugin bundle, {"comfy_unreal": <manifest>, "prompt": <API graph>} (docs/MANIFEST.md).
The graphs use ComfyUI core nodes only, so they run unchanged on Comfy Cloud and on a local ComfyUI
that has the models. Node settings follow the official Comfy-Org templates named in each docstring.

The check runs offline against NODE_SPECS, which is copied from Comfy Cloud's /object_info (October
2026): every node class exists, every required input is set, links point at real outputs of the right
type, combo values are allowed, and every manifest input and output addresses a real node.

    python tools/build_starter_workflows.py           # write and check
    python tools/build_starter_workflows.py --check   # check only, write nothing
"""

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT_DIR = ROOT / "Resources" / "Workflows"

# Bundles this script owns. Anything else in Resources/Workflows is left alone, except RETIRED.
RETIRED = ["texture_cloud_basic.json", "texture_flux_schnell.json", "texture_flux_dev_gguf.json",
           "audio_stable_audio_open.json", "mesh_hunyuan3d_v2.json"]

# ---------------------------------------------------------------------------------------------
# Node specs: input name -> type (COMBO lists only where a template sets a fixed value), outputs.

A = "*"  # any value the check doesn't constrain

NODE_SPECS = {
    "UNETLoader": ({"unet_name": "MODELFILE", "weight_dtype": ["default", "fp8_e4m3fn", "fp8_e4m3fn_fast", "fp8_e5m2"]}, ["MODEL"]),
    "CLIPLoader": ({"clip_name": "MODELFILE", "type": ["stable_audio", "flux2", "ace"], "device?": ["default", "cpu"]}, ["CLIP"]),
    "VAELoader": ({"vae_name": "MODELFILE"}, ["VAE"]),
    "CheckpointLoaderSimple": ({"ckpt_name": "MODELFILE"}, ["MODEL", "CLIP", "VAE"]),
    "ImageOnlyCheckpointLoader": ({"ckpt_name": "MODELFILE"}, ["MODEL", "CLIP_VISION", "VAE"]),
    "UpscaleModelLoader": ({"model_name": "MODELFILE"}, ["UPSCALE_MODEL"]),
    "CLIPTextEncode": ({"text": "STRING", "clip": "CLIP"}, ["CONDITIONING"]),
    "StringConcatenate": ({"string_a": "STRING", "string_b": "STRING", "delimiter": "STRING"}, ["STRING"]),
    "ConditioningZeroOut": ({"conditioning": "CONDITIONING"}, ["CONDITIONING"]),
    "ReferenceLatent": ({"conditioning": "CONDITIONING", "latent?": "LATENT"}, ["CONDITIONING"]),
    "CFGGuider": ({"model": "MODEL", "positive": "CONDITIONING", "negative": "CONDITIONING", "cfg": "FLOAT"}, ["GUIDER"]),
    "Flux2Scheduler": ({"steps": "INT", "width": "INT", "height": "INT"}, ["SIGMAS"]),
    "KSamplerSelect": ({"sampler_name": ["euler", "lcm", "dpmpp_3m_sde_gpu"]}, ["SAMPLER"]),
    "RandomNoise": ({"noise_seed": "INT"}, ["NOISE"]),
    "EmptyFlux2LatentImage": ({"width": "INT", "height": "INT", "batch_size": "INT"}, ["LATENT"]),
    "SamplerCustomAdvanced": ({"noise": "NOISE", "guider": "GUIDER", "sampler": "SAMPLER", "sigmas": "SIGMAS", "latent_image": "LATENT"}, ["LATENT", "LATENT"]),
    "KSampler": ({"model": "MODEL", "seed": "INT", "steps": "INT", "cfg": "FLOAT",
                  "sampler_name": ["euler", "lcm", "dpmpp_3m_sde_gpu"], "scheduler": ["simple", "normal", "exponential"],
                  "positive": "CONDITIONING", "negative": "CONDITIONING", "latent_image": "LATENT", "denoise": "FLOAT"}, ["LATENT"]),
    "VAEDecode": ({"samples": "LATENT", "vae": "VAE"}, ["IMAGE"]),
    "VAEEncode": ({"pixels": "IMAGE", "vae": "VAE"}, ["LATENT"]),
    "SaveImage": ({"images": "IMAGE", "filename_prefix": "STRING"}, []),
    "LoadImage": ({"image": "STRING"}, ["IMAGE", "MASK"]),
    "ImageScaleToTotalPixels": ({"image": "IMAGE", "upscale_method": ["nearest-exact", "bilinear", "area", "bicubic", "lanczos"],
                                 "megapixels": "FLOAT", "resolution_steps": "INT"}, ["IMAGE"]),
    "GetImageSize": ({"image": "IMAGE"}, ["INT", "INT", "INT"]),
    "ImageUpscaleWithModel": ({"upscale_model": "UPSCALE_MODEL", "image": "IMAGE"}, ["IMAGE"]),
    "ImageScaleBy": ({"image": "IMAGE", "upscale_method": ["nearest-exact", "bilinear", "area", "bicubic", "lanczos"], "scale_by": "FLOAT"}, ["IMAGE"]),
    "PrimitiveInt": ({"value": "INT"}, ["INT"]),
    "PrimitiveFloat": ({"value": "FLOAT"}, ["FLOAT"]),
    "EmptyLatentAudio": ({"seconds": "FLOAT", "batch_size": "INT"}, ["LATENT"]),
    "ConditioningStableAudio": ({"positive": "CONDITIONING", "negative": "CONDITIONING", "seconds_start": "FLOAT", "seconds_total": "FLOAT"},
                                ["CONDITIONING", "CONDITIONING"]),
    "VAEDecodeAudio": ({"samples": "LATENT", "vae": "VAE"}, ["AUDIO"]),
    "SaveAudio": ({"audio": "AUDIO", "filename_prefix": "STRING"}, []),
    "TextEncodeAceStepAudio1.5": ({"clip": "CLIP", "tags": "STRING", "lyrics": "STRING", "seed": "INT", "bpm": "INT", "duration": "FLOAT",
                                   "timesignature": ["2", "3", "4", "6"], "language": ["en"], "keyscale": "STRING",
                                   "generate_audio_codes": "BOOLEAN", "cfg_scale": "FLOAT", "temperature": "FLOAT", "top_p": "FLOAT",
                                   "top_k": "INT", "min_p": "FLOAT"}, ["CONDITIONING"]),
    "EmptyAceStep1.5LatentAudio": ({"seconds": "FLOAT", "batch_size": "INT"}, ["LATENT"]),
    "ModelSamplingAuraFlow": ({"model": "MODEL", "shift": "FLOAT", "sampling?": ["flow", "img_to_img_velocity"]}, ["MODEL"]),
    "CLIPVisionEncode": ({"clip_vision": "CLIP_VISION", "image": "IMAGE", "crop": ["center", "none"]}, ["CLIP_VISION_OUTPUT"]),
    "Hunyuan3Dv2Conditioning": ({"clip_vision_output": "CLIP_VISION_OUTPUT"}, ["CONDITIONING", "CONDITIONING"]),
    "EmptyLatentHunyuan3Dv2": ({"resolution": "INT", "batch_size": "INT"}, ["LATENT"]),
    "VAEDecodeHunyuan3D": ({"samples": "LATENT", "vae": "VAE", "num_chunks": "INT", "octree_resolution": "INT"}, ["VOXEL"]),
    "VoxelToMesh": ({"voxel": "VOXEL", "algorithm": ["surface net", "basic"], "threshold": "FLOAT"}, ["MESH"]),
    "SaveGLB": ({"mesh": "MESH", "filename_prefix": "STRING"}, []),
}

KEYSCALES = [f"{k} {m}" for m in ("major", "minor") for k in
             ("C", "C#", "Db", "D", "D#", "Eb", "E", "F", "F#", "Gb", "G", "G#", "Ab", "A", "A#", "Bb", "B")]

# Model files each starter needs: name -> (ComfyUI models folder, size, download URL). Every name is in
# Comfy Cloud's loader lists; URLs and sizes are the ones the official templates give.
MODELS = {
    "flux-2-klein-4b-fp8.safetensors": ("diffusion_models", "3.8 GB", "https://huggingface.co/black-forest-labs/FLUX.2-klein-4b-fp8/resolve/main/flux-2-klein-4b-fp8.safetensors"),
    "flux-2-klein-4b.safetensors": ("diffusion_models", "7.2 GB", "https://huggingface.co/Comfy-Org/flux2-klein/resolve/main/split_files/diffusion_models/flux-2-klein-4b.safetensors"),
    "qwen_3_4b.safetensors": ("text_encoders", "7.5 GB", "https://huggingface.co/Comfy-Org/flux2-klein/resolve/main/split_files/text_encoders/qwen_3_4b.safetensors"),
    "flux2-vae.safetensors": ("vae", "321 MB", "https://huggingface.co/Comfy-Org/flux2-dev/resolve/main/split_files/vae/flux2-vae.safetensors"),
    "RealESRGAN_x4plus.safetensors": ("upscale_models", "67 MB", "https://huggingface.co/Comfy-Org/Real-ESRGAN_repackaged/resolve/main/RealESRGAN_x4plus.safetensors"),
    "stable-audio-open-1.0.safetensors": ("checkpoints", "4.9 GB", "https://huggingface.co/Comfy-Org/stable-audio-open-1.0_repackaged/resolve/main/stable-audio-open-1.0.safetensors"),
    "t5-base.safetensors": ("text_encoders", "0.9 GB", "https://huggingface.co/ComfyUI-Wiki/t5-base/resolve/main/t5-base.safetensors"),
    "ace_step_1.5_turbo_aio.safetensors": ("checkpoints", "10 GB", "https://huggingface.co/Comfy-Org/ace_step_1.5_ComfyUI_files/resolve/main/checkpoints/ace_step_1.5_turbo_aio.safetensors"),
    "hunyuan3d-dit-v2_fp16.safetensors": ("checkpoints", "4.9 GB", "https://huggingface.co/Comfy-Org/hunyuan3D_2.0_repackaged/resolve/main/split_files/hunyuan3d-dit-v2_fp16.safetensors"),
}


def node(class_type, title, **inputs):
    return {"class_type": class_type, "_meta": {"title": title}, "inputs": inputs}


def model_input(name, label, node_id, input_name, prefer, family, tooltip):
    """A Model input. The first preferred file is the one a missing-model message offers to download."""
    folder, size, url = MODELS[prefer[0]]
    return {"name": name, "label": label, "node_id": node_id, "input": input_name, "type": "Model",
            "prefer": prefer, "family": family, "tooltip": tooltip,
            "download": {"file": prefer[0], "folder": folder, "size": size, "url": url}}


# ---------------------------------------------------------------------------------------------
# FLUX.2 Klein 4B (distilled): text to image and image edit share the loaders and the sampler.
# Settings from the official templates image_flux2_klein_text_to_image (Distilled subgraph) and
# image_flux2_klein_image_edit_4b_distilled: euler, 4 steps, cfg 1, zeroed negative.

KLEIN_MODEL_INPUTS = [
    model_input("unet_name", "Diffusion model", "1", "unet_name",
                ["flux-2-klein-4b-fp8.safetensors", "flux-2-klein-4b.safetensors"], "flux2-klein-4b",
                "FLUX.2 Klein 4B distilled. The fp8 file is smaller and works the same."),
    model_input("clip_name", "Text encoder", "2", "clip_name", ["qwen_3_4b.safetensors"], "qwen-3-4b",
                "Qwen 3 4B, the text encoder FLUX.2 Klein 4B uses."),
    model_input("vae_name", "VAE", "3", "vae_name", ["flux2-vae.safetensors"], "flux2-vae", "The FLUX.2 VAE."),
]


def klein_loaders():
    return {
        "1": node("UNETLoader", "Load Diffusion Model", unet_name="flux-2-klein-4b-fp8.safetensors", weight_dtype="default"),
        "2": node("CLIPLoader", "Load CLIP", clip_name="qwen_3_4b.safetensors", type="flux2", device="default"),
        "3": node("VAELoader", "Load VAE", vae_name="flux2-vae.safetensors"),
    }


def klein_sampler(positive, negative, width, height, prefix):
    return {
        "7": node("CFGGuider", "CFG Guider", model=["1", 0], positive=positive, negative=negative, cfg=1.0),
        "8": node("KSamplerSelect", "Sampler", sampler_name="euler"),
        "9": node("Flux2Scheduler", "Scheduler", steps=4, width=width, height=height),
        "10": node("RandomNoise", "Seed", noise_seed=0),
        "11": node("EmptyFlux2LatentImage", "Size", width=width, height=height, batch_size=1),
        "12": node("SamplerCustomAdvanced", "Sample", noise=["10", 0], guider=["7", 0], sampler=["8", 0], sigmas=["9", 0], latent_image=["11", 0]),
        "13": node("VAEDecode", "VAE Decode", samples=["12", 0], vae=["3", 0]),
        "14": node("SaveImage", "Save Image", images=["13", 0], filename_prefix=prefix),
    }


TEXTURE_PREFIX = ("seamless tileable texture, flat top-down orthographic view, even diffuse lighting, "
                  "no shadows, no perspective, highly detailed material surface:")


def seamless_texture():
    prompt = {**klein_loaders(),
              "4": node("StringConcatenate", "Texture prompt", string_a=TEXTURE_PREFIX, string_b="mossy cobblestone path", delimiter=" "),
              "5": node("CLIPTextEncode", "Positive", text=["4", 0], clip=["2", 0]),
              "6": node("ConditioningZeroOut", "Negative (zero)", conditioning=["5", 0]),
              "20": node("PrimitiveInt", "Width", value=1024),
              "21": node("PrimitiveInt", "Height", value=1024),
              **klein_sampler(["5", 0], ["6", 0], ["20", 0], ["21", 0], "unreal/texture")}
    manifest = {
        "version": 1,
        "display_name": "Seamless Texture",
        "category": "Texture",
        "group": "Starter",
        "description": ("A flat, evenly lit surface texture from a text prompt, with FLUX.2 Klein 4B. "
                        "Use Create material to make it tile and get a PBR material."),
        "inputs": [
            {"name": "prompt", "label": "Surface", "node_id": "4", "input": "string_b", "type": "Text", "source": "Prompt",
             "tooltip": "What the surface looks like, for example: mossy cobblestone path"},
            {"name": "seed", "label": "Seed", "node_id": "10", "input": "noise_seed", "type": "Seed"},
            {"name": "width", "label": "Width", "node_id": "20", "input": "value", "type": "Int", "tooltip": "512 to 2048. 1024 is the model's native size."},
            {"name": "height", "label": "Height", "node_id": "21", "input": "value", "type": "Int", "tooltip": "512 to 2048. 1024 is the model's native size."},
            *KLEIN_MODEL_INPUTS,
        ],
        "outputs": [{"name": "albedo", "node_id": "14", "kind": "Texture", "slot": "BaseColor"}],
        "presets": [
            {"name": "Cobblestone", "values": {"prompt": "mossy cobblestone path"}},
            {"name": "Brick", "values": {"prompt": "weathered red brick wall"}},
            {"name": "Forest floor", "values": {"prompt": "forest floor with pine needles, twigs and small stones"}},
            {"name": "Sci-fi panel", "values": {"prompt": "worn sci-fi metal floor panels with bolts and scratches"}},
        ],
    }
    return "texture_seamless.json", manifest, prompt


def texture_restyle():
    prompt = {**klein_loaders(),
              "30": node("LoadImage", "Texture to restyle", image="example.png"),
              "31": node("ImageScaleToTotalPixels", "Scale to model size", image=["30", 0], upscale_method="lanczos", megapixels=1.0, resolution_steps=1),
              "32": node("GetImageSize", "Size", image=["31", 0]),
              "33": node("VAEEncode", "Encode reference", pixels=["31", 0], vae=["3", 0]),
              "5": node("CLIPTextEncode", "Edit instruction", text="make it wet and covered in moss", clip=["2", 0]),
              "6": node("ConditioningZeroOut", "Negative (zero)", conditioning=["5", 0]),
              "34": node("ReferenceLatent", "Reference (positive)", conditioning=["5", 0], latent=["33", 0]),
              "35": node("ReferenceLatent", "Reference (negative)", conditioning=["6", 0], latent=["33", 0]),
              **klein_sampler(["34", 0], ["35", 0], ["32", 0], ["32", 1], "unreal/restyle")}
    manifest = {
        "version": 1,
        "display_name": "Texture Restyle",
        "category": "Texture",
        "group": "Starter",
        "description": ("Changes an existing texture from an instruction, with FLUX.2 Klein 4B: weathering, a new material, "
                        "a new color. Select a texture in the Content Browser, or drop an image file."),
        "inputs": [
            {"name": "image", "label": "Texture", "node_id": "30", "input": "image", "type": "File", "source": "SelectedTextures",
             "tooltip": "The texture to change"},
            {"name": "prompt", "label": "Change", "node_id": "5", "input": "text", "type": "Text", "source": "Prompt",
             "tooltip": "What to change, for example: make it wet and covered in moss"},
            {"name": "megapixels", "label": "Size (megapixels)", "node_id": "31", "input": "megapixels", "type": "Float",
             "tooltip": "Output size. 1 is about 1024 x 1024. The aspect ratio of the input is kept."},
            {"name": "seed", "label": "Seed", "node_id": "10", "input": "noise_seed", "type": "Seed"},
            *KLEIN_MODEL_INPUTS,
        ],
        "outputs": [{"name": "albedo", "node_id": "14", "kind": "Texture", "slot": "BaseColor"}],
        "presets": [
            {"name": "Wet and mossy", "values": {"prompt": "make it wet and covered in moss"}},
            {"name": "Snow", "values": {"prompt": "cover it with a thin layer of snow and frost"}},
            {"name": "Rust", "values": {"prompt": "make it old, rusty and stained"}},
        ],
    }
    return "texture_restyle.json", manifest, prompt


def texture_upscale():
    """Template: utility-gan_upscaler (UpscaleModelLoader + ImageUpscaleWithModel), for images."""
    prompt = {
        "30": node("LoadImage", "Texture to upscale", image="example.png"),
        "40": node("UpscaleModelLoader", "Load Upscale Model", model_name="RealESRGAN_x4plus.safetensors"),
        "41": node("ImageUpscaleWithModel", "Upscale 4x", upscale_model=["40", 0], image=["30", 0]),
        "42": node("ImageScaleBy", "Final size", image=["41", 0], upscale_method="lanczos", scale_by=0.5),
        "43": node("SaveImage", "Save Image", images=["42", 0], filename_prefix="unreal/upscale"),
    }
    manifest = {
        "version": 1,
        "display_name": "Texture Upscale",
        "category": "Upscale",
        "group": "Starter",
        "description": ("Makes a texture larger and sharper with Real-ESRGAN x4. Select a texture in the Content Browser, "
                        "or drop an image file. The default doubles the size."),
        "inputs": [
            {"name": "image", "label": "Texture", "node_id": "30", "input": "image", "type": "File", "source": "SelectedTextures",
             "tooltip": "The texture to upscale"},
            {"name": "scale", "label": "Scale of the 4x result", "node_id": "42", "input": "scale_by", "type": "Float",
             "tooltip": "The model always makes the image 4 times larger. 0.5 gives 2x, 1 keeps 4x."},
            model_input("model_name", "Upscale model", "40", "model_name",
                        ["RealESRGAN_x4plus.safetensors", "RealESRGAN_x4plus.pth"], "esrgan-4x",
                        "A 4x upscale model in models/upscale_models."),
        ],
        "outputs": [{"name": "upscaled", "node_id": "43", "kind": "Texture", "slot": "BaseColor"}],
        "presets": [
            {"name": "2x", "values": {"scale": "0.5"}},
            {"name": "4x", "values": {"scale": "1.0"}},
        ],
    }
    return "texture_upscale.json", manifest, prompt


def sound_effect():
    """Template: audio_stable_audio_example (Stable Audio Open 1.0)."""
    prompt = {
        "4": node("CheckpointLoaderSimple", "Load Checkpoint", ckpt_name="stable-audio-open-1.0.safetensors"),
        "10": node("CLIPLoader", "Text encoder", clip_name="t5-base.safetensors", type="stable_audio"),
        "6": node("CLIPTextEncode", "Positive", text="heavy wooden door creaking open in a stone hall, reverb", clip=["10", 0]),
        "7": node("CLIPTextEncode", "Negative", text="low quality, noise, music", clip=["10", 0]),
        "20": node("PrimitiveFloat", "Seconds", value=6.0),
        "11": node("EmptyLatentAudio", "Length", seconds=["20", 0], batch_size=1),
        "12": node("ConditioningStableAudio", "Timing", positive=["6", 0], negative=["7", 0], seconds_start=0.0, seconds_total=["20", 0]),
        "3": node("KSampler", "KSampler", model=["4", 0], seed=0, steps=50, cfg=4.98, sampler_name="dpmpp_3m_sde_gpu",
                  scheduler="exponential", positive=["12", 0], negative=["12", 1], latent_image=["11", 0], denoise=1.0),
        "18": node("VAEDecodeAudio", "VAE Decode Audio", samples=["3", 0], vae=["4", 2]),
        "19": node("SaveAudio", "Save Audio", audio=["18", 0], filename_prefix="unreal/audio"),
    }
    manifest = {
        "version": 1,
        "display_name": "Sound Effect",
        "category": "Sound",
        "group": "Starter",
        "description": "A sound effect or ambience from a text prompt, up to 47 seconds, with Stable Audio Open 1.0. Imported as a Sound Wave.",
        "inputs": [
            {"name": "prompt", "label": "Sound", "node_id": "6", "input": "text", "type": "Text", "source": "Prompt",
             "tooltip": "For example: heavy wooden door creaking open in a stone hall, reverb"},
            {"name": "negative_prompt", "label": "Avoid", "node_id": "7", "input": "text", "type": "Text"},
            {"name": "seconds", "label": "Seconds", "node_id": "20", "input": "value", "type": "Float", "tooltip": "Length, 1 to 47"},
            {"name": "seed", "label": "Seed", "node_id": "3", "input": "seed", "type": "Seed"},
            model_input("ckpt_name", "Checkpoint", "4", "ckpt_name", ["stable-audio-open-1.0.safetensors"], "stable-audio-open",
                        "Stable Audio Open 1.0 in models/checkpoints."),
            model_input("text_encoder", "Text encoder", "10", "clip_name", ["t5-base.safetensors", "t5_base.safetensors"], "t5-base",
                        "T5 base in models/text_encoders."),
        ],
        "outputs": [{"name": "sound", "node_id": "19", "kind": "Sound"}],
        "presets": [
            {"name": "Door", "values": {"prompt": "heavy wooden door creaking open in a stone hall, reverb", "seconds": "6"}},
            {"name": "Footsteps", "values": {"prompt": "footsteps on gravel, slow walk, close microphone", "seconds": "8"}},
            {"name": "Forest ambience", "values": {"prompt": "forest ambience, birds, light wind in the leaves", "seconds": "20"}},
            {"name": "Sword hit", "values": {"prompt": "metal sword hitting a shield, single impact", "seconds": "2"}},
        ],
    }
    return "audio_sound_effect.json", manifest, prompt


def music():
    """Template: audio_ace_step_1_5_checkpoint (ACE-Step 1.5 turbo, all-in-one checkpoint)."""
    prompt = {
        "50": node("CheckpointLoaderSimple", "Load Checkpoint", ckpt_name="ace_step_1.5_turbo_aio.safetensors"),
        "60": node("PrimitiveInt", "Seed", value=0),
        "61": node("PrimitiveFloat", "Seconds", value=60.0),
        "51": node("TextEncodeAceStepAudio1.5", "Music prompt", clip=["50", 1],
                   tags="orchestral fantasy exploration theme, strings, french horn, harp, calm, cinematic",
                   lyrics="[Instrumental]", seed=["60", 0], bpm=90, duration=["61", 0], timesignature="4", language="en",
                   keyscale="D minor", generate_audio_codes=True, cfg_scale=2.0, temperature=0.85, top_p=0.9, top_k=0, min_p=0.0),
        "52": node("ModelSamplingAuraFlow", "Model sampling", model=["50", 0], shift=3.0),
        "53": node("ConditioningZeroOut", "Negative (zero)", conditioning=["51", 0]),
        "54": node("EmptyAceStep1.5LatentAudio", "Length", seconds=["61", 0], batch_size=1),
        "55": node("KSampler", "KSampler", model=["52", 0], seed=["60", 0], steps=8, cfg=1.0, sampler_name="euler", scheduler="simple",
                   positive=["51", 0], negative=["53", 0], latent_image=["54", 0], denoise=1.0),
        "56": node("VAEDecodeAudio", "VAE Decode Audio", samples=["55", 0], vae=["50", 2]),
        "57": node("SaveAudio", "Save Audio", audio=["56", 0], filename_prefix="unreal/music"),
    }
    manifest = {
        "version": 1,
        "display_name": "Music Track",
        "category": "Music",
        "group": "Starter",
        "description": "A music track from a style description, with ACE-Step 1.5. Instrumental by default; add lyrics to get vocals. Imported as a Sound Wave.",
        "inputs": [
            {"name": "tags", "label": "Style", "node_id": "51", "input": "tags", "type": "Text", "source": "Prompt",
             "tooltip": "Genre, mood and instruments, for example: tense stealth music, low strings, soft percussion"},
            {"name": "lyrics", "label": "Lyrics", "node_id": "51", "input": "lyrics", "type": "Text",
             "tooltip": "Keep [Instrumental] for no vocals. Use [Verse] and [Chorus] tags for sung lyrics."},
            {"name": "seconds", "label": "Seconds", "node_id": "61", "input": "value", "type": "Float", "tooltip": "Length, 10 to 240"},
            {"name": "bpm", "label": "BPM", "node_id": "51", "input": "bpm", "type": "Int"},
            {"name": "keyscale", "label": "Key", "node_id": "51", "input": "keyscale", "type": "Enum", "options": KEYSCALES},
            {"name": "seed", "label": "Seed", "node_id": "60", "input": "value", "type": "Seed"},
            model_input("ckpt_name", "Checkpoint", "50", "ckpt_name", ["ace_step_1.5_turbo_aio.safetensors"], "ace-step-1.5",
                        "ACE-Step 1.5 turbo all-in-one checkpoint in models/checkpoints."),
        ],
        "outputs": [{"name": "music", "node_id": "57", "kind": "Music"}],
        "presets": [
            {"name": "Exploration", "values": {"tags": "orchestral fantasy exploration theme, strings, french horn, harp, calm, cinematic", "bpm": "90", "keyscale": "D minor"}},
            {"name": "Combat", "values": {"tags": "intense battle music, taiko drums, brass stabs, fast strings, epic", "bpm": "140", "keyscale": "C minor"}},
            {"name": "Menu", "values": {"tags": "ambient synth menu music, soft pads, slow arpeggio, relaxed", "bpm": "80", "keyscale": "A minor"}},
        ],
    }
    return "audio_music.json", manifest, prompt


def image_to_mesh():
    """Template: 3d_hunyuan3d_image_to_model (Hunyuan3D 2.0): 20 steps, cfg 8, surface net."""
    prompt = {
        "54": node("ImageOnlyCheckpointLoader", "Load Checkpoint", ckpt_name="hunyuan3d-dit-v2_fp16.safetensors"),
        "56": node("LoadImage", "Reference image", image="example.png"),
        "51": node("CLIPVisionEncode", "Encode image", clip_vision=["54", 1], image=["56", 0], crop="center"),
        "50": node("Hunyuan3Dv2Conditioning", "Conditioning", clip_vision_output=["51", 0]),
        "52": node("EmptyLatentHunyuan3Dv2", "Latent", resolution=3072, batch_size=1),
        "70": node("ModelSamplingAuraFlow", "Model sampling", model=["54", 0], shift=1.0),
        "3": node("KSampler", "KSampler", model=["70", 0], seed=0, steps=20, cfg=8.0, sampler_name="euler", scheduler="normal",
                  positive=["50", 0], negative=["50", 1], latent_image=["52", 0], denoise=1.0),
        "61": node("VAEDecodeHunyuan3D", "Decode", samples=["3", 0], vae=["54", 2], num_chunks=8000, octree_resolution=256),
        "59": node("VoxelToMesh", "Voxel to Mesh", voxel=["61", 0], algorithm="surface net", threshold=0.6),
        "67": node("SaveGLB", "Save 3D Model", mesh=["59", 0], filename_prefix="unreal/mesh"),
    }
    manifest = {
        "version": 1,
        "display_name": "Image to Mesh",
        "category": "Mesh",
        "group": "Starter",
        "description": ("An untextured 3D mesh from one reference image, with Hunyuan3D 2.0. Imported as a Static Mesh. "
                        "Use one centered object on a plain background."),
        "inputs": [
            {"name": "image", "label": "Reference image", "node_id": "56", "input": "image", "type": "File", "source": "SelectedTextures",
             "tooltip": "A texture or image file showing one object on a plain background"},
            {"name": "octree_resolution", "label": "Detail", "node_id": "61", "input": "octree_resolution", "type": "Int",
             "tooltip": "256 is fast. 384 or 512 is finer and slower."},
            {"name": "seed", "label": "Seed", "node_id": "3", "input": "seed", "type": "Seed"},
            model_input("ckpt_name", "Checkpoint", "54", "ckpt_name",
                        ["hunyuan3d-dit-v2_fp16.safetensors", "hunyuan3d-dit-v2.safetensors"], "hunyuan3d-2",
                        "Hunyuan3D 2.0 in models/checkpoints."),
        ],
        "outputs": [{"name": "mesh", "node_id": "67", "kind": "Mesh"}],
    }
    return "mesh_image_to_mesh.json", manifest, prompt


STARTERS = [seamless_texture, texture_restyle, texture_upscale, sound_effect, music, image_to_mesh]

# ---------------------------------------------------------------------------------------------
# Check


def type_of(value):
    if isinstance(value, bool):
        return "BOOLEAN"
    if isinstance(value, int):
        return "INT"
    if isinstance(value, float):
        return "FLOAT"
    return "STRING"


def check(file_name, manifest, prompt):
    errors = []
    for node_id, entry in prompt.items():
        spec = NODE_SPECS.get(entry["class_type"])
        if not spec:
            errors.append(f"node {node_id}: unknown class {entry['class_type']}")
            continue
        inputs, _ = spec
        for raw_name, wanted in inputs.items():
            name = raw_name.rstrip("?")
            if name not in entry["inputs"]:
                if not raw_name.endswith("?"):
                    errors.append(f"node {node_id} {entry['class_type']}: missing input {name}")
                continue
            value = entry["inputs"][name]
            if isinstance(value, list):
                source = prompt.get(value[0])
                if not source:
                    errors.append(f"node {node_id}.{name}: link to missing node {value[0]}")
                    continue
                outputs = NODE_SPECS.get(source["class_type"], ({}, []))[1]
                if value[1] >= len(outputs):
                    errors.append(f"node {node_id}.{name}: {source['class_type']} has no output {value[1]}")
                    continue
                got = outputs[value[1]]
                expected = wanted if isinstance(wanted, str) else "STRING"
                if expected == "MODELFILE":
                    expected = "STRING"
                if expected not in (A, got) and not (expected == "FLOAT" and got == "INT"):
                    errors.append(f"node {node_id}.{name}: wants {expected}, linked to {got}")
            elif isinstance(wanted, list):
                if value not in wanted:
                    errors.append(f"node {node_id}.{name}: {value!r} not in {wanted}")
            elif wanted == "MODELFILE":
                if value not in MODELS:
                    errors.append(f"node {node_id}.{name}: model {value!r} not in MODELS")
            elif wanted not in (A,) and wanted in ("INT", "FLOAT", "STRING", "BOOLEAN"):
                got = type_of(value)
                if got != wanted and not (wanted == "FLOAT" and got == "INT"):
                    errors.append(f"node {node_id}.{name}: wants {wanted}, has {got} {value!r}")
        for name in entry["inputs"]:
            if name not in [n.rstrip("?") for n in inputs]:
                errors.append(f"node {node_id} {entry['class_type']}: unexpected input {name}")

    for item in manifest["inputs"]:
        target = prompt.get(item["node_id"])
        if not target or item["input"] not in target["inputs"]:
            errors.append(f"manifest input {item['name']}: no {item['node_id']}.{item['input']}")
        elif isinstance(target["inputs"][item["input"]], list):
            errors.append(f"manifest input {item['name']}: {item['node_id']}.{item['input']} is a link, not a widget")
        if item["type"] == "Model" and prompt[item["node_id"]]["inputs"][item["input"]] != item["prefer"][0]:
            errors.append(f"manifest input {item['name']}: graph default isn't the first preferred file")
        if item["type"] == "Enum" and str(prompt[item["node_id"]]["inputs"][item["input"]]) not in item["options"]:
            errors.append(f"manifest input {item['name']}: graph value not in options")
    names = [i["name"] for i in manifest["inputs"]]
    if len(names) != len(set(names)):
        errors.append("manifest input names repeat")
    for item in manifest["outputs"]:
        target = prompt.get(item["node_id"])
        if not target or target["class_type"] not in ("SaveImage", "SaveAudio", "SaveGLB"):
            errors.append(f"manifest output {item['name']}: {item['node_id']} is not a save node")
    for preset in manifest.get("presets", []):
        for key in preset["values"]:
            if key not in names:
                errors.append(f"preset {preset['name']}: no input {key}")
    return [f"{file_name}: {e}" for e in errors]


def main():
    write = "--check" not in sys.argv
    errors = []
    for make in STARTERS:
        file_name, manifest, prompt = make()
        errors += check(file_name, manifest, prompt)
        if write:
            text = json.dumps({"comfy_unreal": manifest, "prompt": prompt}, indent=1, ensure_ascii=False) + "\n"
            (OUT_DIR / file_name).write_text(text, encoding="utf-8", newline="\n")
    if write:
        for name in RETIRED:
            (OUT_DIR / name).unlink(missing_ok=True)
    for e in errors:
        print(e)
    print(f"{len(STARTERS)} starters, {len(errors)} problems")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
