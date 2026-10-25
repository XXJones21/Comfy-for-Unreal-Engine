# Cloud-first starter templates: investigation

Date: 2026-10-24. Scope: replace the 5 built-in workflows in `Resources/Workflows/` with a small set of generic starter templates. Each one must run unchanged on Comfy Cloud and on a local Comfy Desktop, built from ComfyUI core nodes, plus partner (API) nodes where they clearly earn their place. Nothing was run or queued. No credits were spent.

## How to read the evidence marks

| Mark | Meaning |
| --- | --- |
| **[cloud]** | Confirmed with the Comfy Cloud MCP discovery tools (`get_node`, `search_models`, `search_templates`, `get_template_schema`, `estimate_credits`) against the live catalog (3,835 nodes, 2,347 models, read 2026-10-25 UTC). |
| **[src]** | Confirmed in the local ComfyUI source, `D:\Tools\ComfyUI_windows_portable_nvidia\ComfyUI_windows_portable\ComfyUI` (version 0.39.0, commit 7d9e5a04). |
| **[tpl]** | Confirmed by reading the official template JSON from `https://cloud.comfy.org/templates/<id>.json`. Copies are in the session scratchpad. |
| **[hf]** | File size or license read from the Hugging Face API, or from HEAD requests on the download URLs. |
| **[inf]** | Inferred. Not verified. Treat it as a hypothesis to test. |

## TL;DR

1. **Seven templates replace the five built-ins.** Five of them are core-only and run on both backends: Seamless Texture, Texture Restyle, Texture Upscale, Sound Effect and Music. Mesh comes in two flavors: one core image-to-mesh template, and one partner template that also holds the optional rig and animate chain. An optional eighth template, Sprite/Icon/Decal with a cut-out background, is cheap to add.
2. **Use one model family for all texture work: FLUX.2 [Klein] 4B.** It is Apache-2.0, does both text-to-image and image edit with the same three loaders, and is on Cloud in fp8 **[cloud][hf]**. A Desktop user downloads about 10 GB. That removes the three Seamless Texture variants and the GGUF custom-node dependency.
3. **Tiling can be done with core nodes.** The half-offset and seam-inpaint trick needs only `ImageCropV2` + `ImageStitch` + a masked img2img pass, or the core `GLSLShader` node. ComfyUI has no core "circular padding" node **[src]**. Keep the plugin's `MakeTileable` as a fallback toggle.
4. **Keep PBR derivation (normal, roughness, height) on the plugin side by default.** It is deterministic, free, and works the same on every backend. A core `GLSLShader` version is possible and appears to be Cloud-safe, but it is a nice-to-have.
5. **Choose models with the plugin's per-backend `prefer` lists, not with switches inside the graph.** ComfyUI validates every linked input, lazy switch branches included. A graph that references a model file the backend lacks fails validation, even if that branch never runs **[src]**.
6. **Two gaps block "mesh plus rig":** the manifest has no Skeletal Mesh or Animation output, and the plugin imports every mesh file as a Static Mesh **[src]**. The Hunyuan3D 2.x license also excludes the EU, UK and South Korea **[hf]**. That is a reason to make TRELLIS.2 (MIT) the core mesh template.

---

## 1. What we have today

| Built-in | Category | Model / nodes | Cloud status |
| --- | --- | --- | --- |
| `texture_cloud_basic.json` "Seamless Texture (Cloud)" | Texture | `CheckpointLoaderSimple` with `flux1-schnell-fp8` | The checkpoint is on Cloud **[cloud]**. The file is 17.2 GB for Desktop **[hf]**. |
| `texture_flux_schnell.json` "Seamless Texture (Flux Schnell)" | Texture | Same graph as above | A duplicate of the Cloud one. |
| `texture_flux_dev_gguf.json` "Seamless Texture (Flux Dev GGUF)" | Texture | `UnetLoaderGGUF` (a **custom node**) | Cannot run on Cloud. Flux Dev is also non-commercial. |
| `audio_stable_audio_open.json` | Sound | Stable Audio Open 1.0 + `t5-base` | Both files are on Cloud with the exact names **[cloud]**. It should run as-is. |
| `mesh_hunyuan3d_v2.json` | Mesh | Hunyuan3D 2.0, untextured | `hunyuan3d-dit-v2.safetensors` is on Cloud **[cloud]**. |

Notes:
- None of the three "Seamless" graphs actually tiles. They only put "seamless tileable" in the prompt. Tiling happens later, in the plugin's `MakeTileable` (half offset plus smoothstep blend) when the user picks Create material **[src: ComfyEditorLibrary.cpp, SPEC.md]**.
- The plugin already derives a normal map with a wrap-aware Sobel filter (`MakeNormalMap`, DirectX convention) **[src]**.

## 2. Recommended starter set

| # | Template | Type | Core / partner | Replaces |
| --- | --- | --- | --- | --- |
| T1 | Seamless Texture | Texture | Core | All three Seamless Texture built-ins |
| T2 | Texture Restyle (image to image) | Texture | Core | New |
| T3 | Texture Upscale | Upscale | Core | New |
| T4 | Sound Effect | Sound | Core (partner alternative noted) | Sound Effect (Stable Audio Open) |
| T5 | Music Loop / Track | Music | Core | New |
| T6 | Image to Mesh | Mesh | Core | Image to Mesh (Hunyuan3D 2) |
| T7 | Image to Game Asset (Partner), with optional rig and animate | Mesh | Partner | New (covers "Meshy rigging") |
| T8 (optional) | Sprite / UI Icon / Decal (cut-out) | Texture | Core | New |

PBR maps (the owner's item 4) are **not** a separate template by default. See section 4.

### Per-template detail

#### T1. Seamless Texture

- **Type:** Texture.
- **Start from:** `image_flux2_klein_text_to_image` (its "Klein 4B Distilled" subgraph) for generation, and `image_flux2_klein_image_edit_4b_distilled` for the seam pass **[cloud][tpl]**.
- **Node outline (all core):**
  1. `UNETLoader` → `CLIPLoader` (type `flux2`) → `VAELoader`.
  2. `StringConcatenate` adds the texture prefix (keep the existing prefix text).
  3. `CLIPTextEncode` → `ConditioningZeroOut`, then `CFGGuider` (cfg 1) → `Flux2Scheduler` (4 steps) → `KSamplerSelect` (euler) → `RandomNoise` → `EmptyFlux2LatentImage` → `SamplerCustomAdvanced` → `VAEDecode`. The node list is from the official subgraph **[tpl]**.
  4. **Seam pass:** half-offset the image in both axes. Use 4× `ImageCropV2` + 3× `ImageStitch` to swap the quadrants, or one `GLSLShader` with `fract(uv+0.5)`. Then build a cross-shaped seam mask with `SolidMask` + `MaskComposite` + `FeatherMask`. Run `VAEEncode` → `SetLatentNoiseMask` → the same sampler at partial denoise, with `ReferenceLatent` of the offset image. Then `VAEDecode` → `SaveImage`. Every node named here exists on Cloud **[cloud: ImageCropV2, ImageStitch, GLSLShader; the others are long-standing core]**. Seam quality at a given denoise is **not tested [inf]**.
- **Models:**

  | File | On Cloud | Desktop download |
  | --- | --- | --- |
  | `flux-2-klein-4b-fp8.safetensors` (BFL repo, not gated) | yes **[cloud]** | 4.07 GB **[hf]** |
  | `qwen_3_4b_fp8_mixed.safetensors` | yes **[cloud]** | 5.63 GB **[hf]** |
  | `flux2-vae.safetensors` | yes **[cloud]** | 0.34 GB **[hf]** |

  - Total about **10 GB**. The bf16 alternatives are `flux-2-klein-4b` (7.75 GB) and `qwen_3_4b` (8.04 GB), about 16 GB in total **[hf]**.
  - License: Apache-2.0 **[hf]**.
  - VRAM: probably 8-12 GB with fp8 **[inf]**.
  - Sizes: 1024² native. Offer 512/1024/2048 as presets. 2048² direct generation is **untested [inf]**.
- **`prefer` lists:**
  - `unet_name`: `["flux-2-klein-4b-fp8", "flux-2-klein-4b"]`
  - `clip_name`: `["qwen_3_4b_fp8_mixed", "qwen_3_4b"]`
  - `vae_name`: `["flux2-vae"]`
  - The fp8_mixed text encoder works with the `flux2` CLIP type: **[inf]**. The official Klein templates use `qwen_3_4b`.
- **Manifest:**
  - Inputs: `prompt` (Text, source Prompt), `seed`, `width`, `height`, `seam_denoise` (Float), `make_tileable` (Bool; bypasses the seam pass if the plugin supports bypass, otherwise use two output nodes), and the three Model inputs.
  - Outputs: `albedo` → Texture/BaseColor.
- **Credits:** no partner nodes, so 0 partner credits. Cloud GPU time is not priced by `estimate_credits`.
- **Optional partner variant:** none recommended. Partner image models (Nano Banana, GPT Image, Seedream) can be prompted for "seamless", but they tile no better, and they add cost.

#### T2. Texture Restyle (update an existing texture)

- **Type:** Texture.
- **Start from:** `image_flux2_klein_image_edit_4b_distilled` **[cloud][tpl]**. A core blueprint, "Image Edit (Flux.2 Klein 4B)", is also on Cloud **[cloud]**.
- **Node outline (core):** `LoadImage` → `ImageScaleToTotalPixels` → `GetImageSize` → `VAEEncode` → `ReferenceLatent` (×2, positive and zeroed negative) → the same Klein sampler chain as T1 → `VAEDecode` → `SaveImage` **[tpl]**. Optionally reuse T1's seam pass so a tileable input stays tileable.
- **Models:** same as T1, so users download them once.
- **Manifest:**
  - Inputs: `image` (File, source **SelectedTextures**), `prompt` (Text, source Prompt, e.g. "make it wet and mossy"), `seed`, and the Model inputs.
  - Outputs: `albedo` → Texture/BaseColor.
  - Suggestion: the plugin could carry the input asset's slot through to the output (restyle a Normal → Normal). That needs a manifest field such as `"slot": "FromInput:image"`. Not in v1 of MANIFEST.md.
- **Credits:** 0 partner credits.
- **Partner alternative:** `api_nano_banana_2_1_image_edit` or `api_bytedance_seedream_5_0_pro_image_edit` exist on Cloud **[cloud]**. Not priced here. Leave them as user-made variants.

#### T3. Texture Upscale

- **Type:** Upscale.
- **Start from:** `utility-gan_upscaler` (video, but the same `UpscaleModelLoader` → `ImageUpscaleWithModel` core) and `utility_interpolation_image_upscale` (`ImageScaleBy`) **[tpl]**. No official image-ESRGAN template turned up.
- **Node outline (core):**
  1. `LoadImage`, then a **wrap pad**: stitch a 32-64 px border taken from the opposite edges, with `ImageCropV2` + `ImageStitch`.
  2. `UpscaleModelLoader` → `ImageUpscaleWithModel`, then `ImageScaleBy` down to the requested factor.
  3. Crop the pad off with `ImageCropV2` → `SaveImage`.
  - Without the wrap pad, a tileable texture gets visible seams after upscaling. The core upscaler is not wrap-aware **[inf, standard behavior]**.
- **Models on Cloud** (all present in the `UpscaleModelLoader` combo) **[cloud]**: `RealESRGAN_x4plus(.safetensors/.pth)`, `RealESRGAN_x2plus.pth`, `4x-UltraSharp`, `4x-ClearRealityV1`, `4x_NMKD-Siax_2d00k`, `4xNomosUniDAT_otf`, `DF2K_JPEG`, `RealESRGAN_x4plus_anime_6B`, `8xNMKDFaces160000G_v10`, `1x-ITF-SkinDiffDetail-Lite-v1`.
  - Default: `RealESRGAN_x4plus.safetensors`. It is 67 MB **[hf]** and BSD-3-Clause.
  - `4x-UltraSharp` is widely reported to be CC BY-NC-SA. Do **not** make it the default without a license check **[inf]**.
  - VRAM: 2-4 GB **[inf]**.
- **Manifest:**
  - Inputs: `image` (File, SelectedTextures), `scale` (Float: 2 or 4), `model_name` (Model, prefer `["RealESRGAN_x4plus", "RealESRGAN_x2plus"]`), `tile_safe` (Bool).
  - Outputs: `upscaled` → Texture (slot carried from input, see the T2 note; BaseColor by default).
- **Credits:** 0. Partner upscalers (Topaz, Magnific, Recraft Crisp, Bria) exist **[cloud]**. They are not wrap-aware either, so there is no clear win for tiling textures.
- **Diffusion upscaler:** a Z-Image-Turbo 2K upscaler blueprint and template exist (`utility_z_image_turbo_2k_upscaler.app`) **[cloud]**. Too heavy for a starter.

#### T4. Sound Effect

- **Type:** Sound.
- **Start from:** `audio_stable_audio_3_medium` (core blueprint "Audio Generation (Stable Audio 3 Medium)", which exposes `ckpt_name`, `category` (SFX/One-shot/…) and `duration`) **[cloud][tpl]**. The minimal starting point is `audio_stable_audio_example` (Stable Audio Open 1.0) **[tpl]**.
- **Node outline (core):** `CheckpointLoaderSimple` → `CLIPLoader` (type `stable_audio`) → `CLIPTextEncode` ×2 → `EmptyLatentAudio` → `KSampler` → `VAEDecodeAudio` → `SaveAudioAdvanced` (flac) **[tpl]**. Leave out the blueprint's optional Qwen 3.5 `TextGenerate` prompt expansion. It adds another text encoder, `qwen3.5_2b_bf16`.
- **Models (recommended):**

  | File | On Cloud | Desktop download |
  | --- | --- | --- |
  | `stable_audio_3_small_sfx.safetensors` | yes **[cloud]** | 2.27 GB **[hf]** |
  | `t5gemma_b_b_ul2.safetensors` | yes **[cloud]** | 1.19 GB **[hf]** |

  - Total about 3.5 GB. License: Stability AI Community License (free below $1M annual revenue) **[hf]**.
  - VRAM: under 8 GB **[inf]**.
  - Sampler settings for the *small SFX* checkpoint are **not verified**. The SA3 Medium blueprint uses 8 steps, `lcm`, cfg 1 **[tpl]**. The `_base` variants are probably undistilled **[inf]**.
- **Fallback:** Stable Audio Open 1.0 (4.85 GB) + `t5-base` (0.89 GB), both on Cloud **[cloud][hf]**. It uses different steps/cfg (50, `dpmpp_3m_sde_gpu`, 4.98), so it is a second preset rather than a `prefer` swap.
- **Manifest:**
  - Inputs: `prompt` (Text, Prompt), `negative_prompt`, `seconds` (Float), `seed`, `ckpt_name` (Model, prefer `["stable_audio_3_small_sfx", "stable_audio_3_medium", "stable-audio-open-1.0"]`), `text_encoder` (Model, prefer `["t5gemma_b_b_ul2"]`).
  - Outputs: `sound` → Sound.
- **Credits:** 0 partner credits.
- **Partner alternative:** `ElevenLabsTextToSoundEffects` has a **loop** toggle and goes up to 30 s **[cloud]**. Template `api_elevenlabs_text_to_sound_effects`, about **30 credits/minute** **[cloud estimate]**. A good optional "Loopable SFX (ElevenLabs)" variant for Cloud users.

#### T5. Music

- **Type:** Music.
- **Start from:** `audio_ace_step_1_5_checkpoint` (all-in-one checkpoint, the simplest graph) **[cloud][tpl]**. The higher-quality alternative is `audio_ace_step1_5_xl_turbo` (split files) **[cloud][tpl]**.
- **Node outline (core):** `CheckpointLoaderSimple` → `TextEncodeAceStepAudio1.5` → `ConditioningZeroOut` → `ModelSamplingAuraFlow` (shift 3) → `EmptyAceStep1.5LatentAudio` → `KSampler` (8 steps, euler/simple, cfg 1) → `VAEDecodeAudio` → `SaveAudioAdvanced` **[tpl][src: nodes_ace.py]**.
- **Models:**
  - AIO: `ace_step_1.5_turbo_aio.safetensors`, on Cloud **[cloud]**, 10.0 GB **[hf]**.
  - XL turbo split files, all on Cloud **[cloud]**:

    | File | Size **[hf]** |
    | --- | --- |
    | `acestep_v1.5_xl_turbo_bf16` | 9.97 GB |
    | `qwen_4b_ace15` | 8.38 GB |
    | `qwen_0.6b_ace15` | 1.19 GB |
    | `ace_1.5_vae` | 0.34 GB |

    Total about 19.9 GB.
  - License: MIT **[hf]**. VRAM for the AIO: about 8-12 GB **[inf]**.
- **Manifest:**
  - Inputs: `tags` (Text, Prompt, style/BPM/instruments), `lyrics` (Text; empty or `[instrumental]`), `seconds`, `seed`, `ckpt_name` (Model, prefer `["ace_step_1.5_turbo_aio"]`).
  - Outputs: `music` → Music.
  - Note: the output does not loop seamlessly. A loop-point trim would be plugin-side.
- **Credits:** 0. Partner alternatives (MiniMax Music 3 on Cloud as a core OSS template `audio_minimax_music_3`, Sonilo, ElevenLabs) exist **[cloud]**. Not needed for a starter.

#### T6. Image to Mesh (core)

- **Type:** Mesh.
- **Recommended:** TRELLIS.2, textured with PBR, MIT license.
  - **Start from:** `3d_pixal3d_trellis2_image_to_model`, with the Pixal3D branch and the MoGe FOV estimation stripped out **[cloud][tpl]**.
  - **Node outline (core):**
    1. Input: `LoadImage` → `LoadBackgroundRemovalModel` + `RemoveBackground` (birefnet) → `ImageCropToMask`.
    2. Shape: `CLIPVisionLoader` (DINOv3) → `Trellis2Conditioning` → `EmptyTrellis2LatentStructure` → `KSampler` → `VaeDecodeStructureTrellis2` → `Trellis2ShapeStage` → `KSampler` → `Trellis2UpsampleStage` → `VaeDecodeShapeTrellis`.
    3. Texture: `Trellis2TextureStage` → `KSampler` → `VaeDecodeTextureTrellis`.
    4. Mesh clean-up: `RemeshMesh` → `DecimateMesh` → `UnwrapMesh`.
    5. Bake: `BakeTextureFromVoxel` (base color + metallic + roughness) → `BakeNormalMapFromMesh` → `ApplyTextureToMesh`.
    6. Save: `MeshToFile3D` → `SaveGLB`.
    - All of these are on Cloud **[cloud]** and defined in `comfy_extras/nodes_trellis2.py`, `nodes_mesh_postprocess.py` and `nodes_save_3d.py` **[src]**.
  - **Models:**

    | File | On Cloud | Size **[hf]** |
    | --- | --- | --- |
    | `trellis_2_int8_convrot` | yes **[cloud]** | 5.25 GB |
    | `trellis_2_shape_vae_bf16` | yes **[cloud]** | 1.10 GB |
    | `trellis_2_texture_vae_bf16` | yes **[cloud]** | 0.95 GB |
    | `dino_v3_L_naf_fp32` | yes **[cloud]** | 1.22 GB |
    | `birefnet` | yes **[cloud]** | 0.44 GB |

    - Total about 9 GB. MIT **[hf]**.
    - VRAM is **unknown**. Upstream TRELLIS.2 targets 24 GB-class GPUs. The int8 build is probably 12-16 GB **[inf]**.
    - Decimation target: drop the template's 700k faces to about 50-100k for a game asset.
- **Light / shape-only alternative:** Hunyuan3D 2.0/2.1.
  - **Start from:** `3d_hunyuan3d_image_to_model` **[cloud][tpl]**, or the Cloud blueprint "Image to 3D Model (Hunyuan3d 2.1)" **[cloud]**.
  - **Nodes:** `ImageOnlyCheckpointLoader` → `CLIPVisionEncode` → `Hunyuan3Dv2Conditioning` → `EmptyLatentHunyuan3Dv2` → `ModelSamplingAuraFlow` → `KSampler` → `VAEDecodeHunyuan3D` → `VoxelToMesh` (surface net) → `SaveGLB` **[tpl]**. The built-in should switch from `VoxelToMeshBasic` to `VoxelToMesh` with "surface net", as the official template does.
  - **Models:** `hunyuan3d-dit-v2_fp16` 4.93 GB, or `hunyuan_3d_v2.1` 7.37 GB. Both on Cloud **[cloud][hf]**.
  - **License risk:** the Tencent Hunyuan Community License **[hf]**. Its territory clause excludes the EU, UK and South Korea (known from the license text, not re-read here) **[inf]**. Shipping it as the default template in a plugin is a legal question.
- **Manifest:**
  - Inputs: `image` (File, source SelectedTextures **or ViewportCapture**), `seed`, `target_faces` (Int), `texture_size` (Int), the Model inputs.
  - Outputs: `mesh` → Mesh (GLB). Interchange brings the embedded PBR material along **[inf]**.
- **Credits:** 0 partner credits.

#### T7. Image to Game Asset (Partner), with optional rig and animate

- **Type:** Mesh.
- **Recommended default:** Tripo P2. **Start from:** `api_tripo_p2_image_to_model` **[cloud][tpl]**. It already contains `TripoRigNode` + `TripoRetargetNode` + `TripoTextureNodeV2` + `TripoConversionNode`, **bypassed** (mode 4) **[tpl]**.
- **Node outline (partner):** `LoadImage` → `TripoPSeriesImageToModelNode` → `Save3DAdvanced`. Optional chain: `TripoRigNode` (`rig_type` auto/biped/quadruped/…, spec tripo or mixamo, GLB/FBX) → `TripoRetargetNode` (preset animations such as idle/walk/run, `animate_in_place`) **[cloud]**.
  - Tripo P2 output: quad-dominant (FBX) or triangle (GLB) meshes, 48-50k faces, 2K/4K/8K texture, PBR on/off **[cloud]**.
- **Cost (catalog estimates) [cloud estimate_credits]:**

  | Pipeline | Credits |
  | --- | --- |
  | Tripo P2 image to model | ~232 |
  | + rig | ~53 |
  | + retarget | ~21 |
  | **Tripo, full chain** | **~306** |
  | Meshy 7.1 image to model, textured, remesh | 362 (241 untextured) |
  | + Meshy rig | 60 |
  | + Meshy animate | 36 |
  | **Meshy, full chain** | **459** |
  | Hunyuan3D 3.1 partner (`TencentImageToModelNode`, PBR, also returns texture/metallic/normal/roughness IMAGEs) | 190 |
  | Rodin Gen2.5 | 158 |

  - In US dollars: Comfy has quoted 211 credits = $1 **[inf; confirm]**. On that rate the Tripo full chain is about $1.45 and Meshy textured about $1.70.
- **Rig node constraints:**
  - Meshy rig: "not suitable for untextured meshes, non-humanoid assets…" Outputs GLB + FBX **[cloud]**.
  - Tripo rig: v1.0 is biped only. v2.5 handles quadruped, hexapod, octopod, avian, serpentine and aquatic **[cloud]**.
- **Manifest:**
  - Inputs: `image` (File, SelectedTextures/ViewportCapture), `face_limit`, `quad` (Bool), `texture` (Enum), `pbr` (Bool), `rig` (Bool, enables the bypassed chain), `animation` (Enum).
  - Outputs: `mesh` → Mesh.
  - **Blocker:** a rigged/animated FBX or GLB needs a Skeletal Mesh plus Animation Sequence import. The manifest has no such kind, and `ComfyTypes.cpp` maps glb/gltf/fbx/obj/usd to Static Mesh **[src]**. See Open question 1.
- **Desktop:** partner nodes run on Desktop too. The plugin already sends `extra_data.api_key_comfy_org` on both the native and the API v2 backends **[src: ComfyNativeBackend.cpp:215, ComfyApiV2Backend.cpp:103]**.

#### T8 (optional). Sprite / UI Icon / Decal

- **Type:** Texture.
- **Start from:** `image_flux2_klein_text_to_image` + the Cloud blueprint "Remove Background (BiRefNet)" **[cloud]**.
- **Nodes (core):** the T1 sampler chain (no seam pass) → `LoadBackgroundRemovalModel` + `RemoveBackground` → `InvertMask` → `JoinImageWithAlpha` → `SaveImage` (RGBA) **[cloud: blueprint internals]**.
- **Models:** T1's models + `birefnet.safetensors` (0.44 GB, MIT) **[cloud][hf]**.
- **Manifest:** inputs `prompt`, `seed`, size. Outputs `sprite` → Texture/BaseColor (with alpha) and optionally `mask` → Texture/Opacity.
  - A decal material and the UI texture group (no mips, `TEXTUREGROUP_UI`) would be plugin-side import options **[inf]**.
- **Credits:** 0.

## 3. The table

| Template | Type | Nodes | Models (Cloud = on Cloud) | Desktop download / VRAM | Manifest inputs → outputs | Credits | Replaces | Closest official template |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| T1 Seamless Texture | Texture | Core (Flux2 sampler chain, `ImageCropV2`/`ImageStitch` or `GLSLShader`, `SetLatentNoiseMask`) | `flux-2-klein-4b-fp8`, `qwen_3_4b_fp8_mixed`, `flux2-vae`: all Cloud | ≈10.0 GB / ≈8-12 GB [inf] | Prompt, seed, size, seam_denoise, make_tileable, 3× Model → BaseColor | 0 partner | 3× Seamless Texture | `image_flux2_klein_text_to_image` |
| T2 Texture Restyle | Texture | Core (`VAEEncode` + `ReferenceLatent`) | Same as T1 | Shared with T1 | image (SelectedTextures), prompt, seed, Models → BaseColor (or carried slot) | 0 | n/a (new) | `image_flux2_klein_image_edit_4b_distilled` |
| T3 Texture Upscale | Upscale | Core (`UpscaleModelLoader`, `ImageUpscaleWithModel`, `ImageScaleBy`, wrap pad via crop/stitch) | `RealESRGAN_x4plus`: Cloud | 67 MB / 2-4 GB [inf] | image (SelectedTextures), scale, model, tile_safe → Texture | 0 | n/a (new) | `utility-gan_upscaler` (video), `utility_interpolation_image_upscale` |
| T4 Sound Effect | Sound | Core (Stable Audio chain) | `stable_audio_3_small_sfx` + `t5gemma_b_b_ul2` (fallback `stable-audio-open-1.0` + `t5-base`): all Cloud | ≈3.5 GB (fallback 5.7 GB) / <8 GB [inf] | prompt, negative, seconds, seed, 2× Model → Sound | 0 (ElevenLabs alternative ≈30 cr/min) | Sound Effect (SAO) | `audio_stable_audio_3_medium`, `audio_stable_audio_example`, `api_elevenlabs_text_to_sound_effects` |
| T5 Music | Music | Core (ACE-Step 1.5 chain) | `ace_step_1.5_turbo_aio`: Cloud | 10.0 GB / ≈8-12 GB [inf] | tags, lyrics, seconds, seed, Model → Music | 0 | n/a (new) | `audio_ace_step_1_5_checkpoint`, `audio_ace_step1_5_xl_turbo` |
| T6 Image to Mesh | Mesh | Core (TRELLIS.2 + mesh post-process + bake) | `trellis_2_int8_convrot`, 2 VAEs, `dino_v3_L_naf_fp32`, `birefnet`: all Cloud. Light alternative `hunyuan3d-dit-v2(_fp16)` | ≈9 GB / unknown, probably 12-16 GB+ [inf] | image (SelectedTextures/ViewportCapture), seed, faces, texture size, Models → Mesh | 0 | Image to Mesh (Hunyuan3D 2) | `3d_pixal3d_trellis2_image_to_model`, `3d_hunyuan3d_image_to_model` |
| T7 Game Asset (Partner) + Rig | Mesh | Partner (Tripo P2 → Rig → Retarget, bypassed by default) | n/a (API) | none / none | image, face_limit, quad, texture, pbr, rig, animation → Mesh (+ Skeletal/Anim needed) | ~232; ~306 with rig + anim (Meshy: 362 / 459) | n/a (new) | `api_tripo_p2_image_to_model`, `api_meshy7_1_image_to_model` |
| T8 Sprite/Icon/Decal | Texture | Core (T1 chain + `RemoveBackground` + `JoinImageWithAlpha`) | T1 + `birefnet`: Cloud | +0.44 GB | prompt, seed, size → BaseColor (RGBA), Opacity | 0 | n/a (new) | Remove Background (BiRefNet) blueprint + `image_flux2_klein_text_to_image` |

## 4. PBR maps: in-graph or plugin-side?

**Recommendation: plugin-side by default, as today.** Templates output BaseColor. The plugin's Create material derives Normal (wrap-aware Sobel), plus Roughness and Height if added later. Reasons:
- It is deterministic, instant, free, and the same on every backend. It needs no model download.
- The plugin already does it, and the wrap-aware filter is the hard part for tiling.

What core ComfyUI offers, for reference:
- **`GLSLShader`** (core, experimental): up to 5 image inputs and 4 outputs, float/int/bool uniforms, GLSL ES 3.0, rendered headless through bundled ANGLE (`comfy-angle`, PyOpenGL) **[src: nodes_glsl.py, requirements.txt]**. It is on Cloud **[cloud]**. Comfy's own Cloud blueprints (Sharpen, Image Blur, Color Curves, Image Channels, …) are built on it **[cloud]**, so it very likely executes on Cloud workers **[inf; no run]**.
  - One shader could output Normal (Sobel with `fract()` wrapping; textures are uploaded as CLAMP_TO_EDGE, so the wrap must be done in the shader **[src]**), Height (luminance), Roughness (inverted, contrast-curved luminance) and AO (height cavity).
  - That makes an in-graph "Material" template possible, with the outputs tagged Normal/Height/Roughness/AO.
  - Worth doing later as an optional "PBR from Texture (GLSL)" template, so users can see and tweak the maps in ComfyUI.
- **Learned estimators:** core has Marigold V2 (depth/normals/albedo), Lotus, MoGe and Depth Anything 3 **[src]**.
  - These are trained for photos and scenes, not for top-down tileable materials, and their output does not wrap. Not recommended for textures.
  - Marigold *albedo* could be useful for de-lighting a photo-sourced texture **[inf]**.
  - The Marigold blueprints exist locally but were **not found** in Cloud's blueprint list **[cloud]**.
- **Partner 3D nodes** (Hunyuan3D partner, Meshy, Tripo with `pbr`) return real PBR maps for meshes. Those come inside the GLB, or, for `TencentImageToModelNode`, as separate IMAGE outputs that could be tagged Metallic/Normal/Roughness **[cloud]**.

## 5. Model choice: in the graph or in `prefer` lists?

**Use the plugin's per-backend `prefer` lists. Do not use conditional logic inside the workflow.**

- ComfyUI validates **every linked input recursively**, including lazy inputs of `ComfySwitchNode` **[src: execution.py `validate_inputs`, recursion at line 967; nodes_logic.py lazy inputs]**. A graph whose unused branch names a model file the backend doesn't have fails validation with `value_not_in_list`. Switches cannot express "use whichever model is installed".
- `prefer` already solves this for one loader. `PickModel`: exact name, then substring, per entry **[src: ComfyTypes.cpp:83]**.
- What `prefer` cannot do is swap graph *shapes*: Flux.1 checkpoint vs Flux.2 split loaders, or Stable Audio Open vs SA3 settings. So pick one model family per template (Klein 4B for all image work). Use `presets` for sampler settings tied to an alternative checkpoint.
- **Risk in the current code:** `PickModel` falls back to the default, then the **first option**, when nothing matches **[src: ComfyTypes.h:363]**. On a Desktop without Klein, a Model input could silently pick an unrelated `diffusion_models` file and fail at sampling time with a confusing error. Recommend failing loudly with "Missing model X (4.07 GB): Download / Open folder". The download URL comes from `properties.models` (section 6).

## 6. How to ship them to both Cloud and Desktop users

| Channel | Reaches | Verdict |
| --- | --- | --- |
| Plugin `Resources/Workflows/*.json` (`{comfy_unreal, prompt}`) | Every backend the plugin talks to (Desktop, Cloud, API deployment) | **Canonical.** The plugin submits the API `prompt` itself, so Cloud needs no extension. |
| ComfyUI extension `example_workflows/` | Desktop / local ComfyUI only. ComfyUI serves these at `/workflow_templates` and in the Templates browser **[src: app/custom_node_manager.py:94-127]** | **Yes, for Desktop discovery.** `comfy-for-unreal/example_workflows/` already holds `seamless_texture_cloud.json`. Cloud loads no custom nodes, so Cloud users never see these. |
| The plugin's Graph view (embedded Cloud page) | Cloud users editing in Studio | **Yes.** Load the canvas JSON into the signed-in Cloud page with `app.loadGraphData`. This already exists through the bridge and the library's Open. This is the practical way to "give Cloud users the template" without the extension. |
| Cloud saved/shared workflows (`save_workflow`, `share_workflow`, `import_shared_workflow`) | One Cloud account, or anyone with a share link | Optional. Saved workflows are per-account. A public share link per template could go in the docs, but links can drift from the plugin's copy. |
| Official templates (Comfy-Org `workflow_templates`) | Cloud **and** Desktop Templates browser, for everyone | **Best long-term.** Needs a PR and review. Candidate category: "Game assets / Unreal". |

**Format recommendation:**
- Author each template **once**, in canvas (save) format, the way official templates are:
  - subgraphs for the model-specific block;
  - a `MarkdownNote` with the input assets;
  - per-node `properties.models` = `{name, url, directory}` on every loader **[tpl]**. Desktop uses this to offer missing-model downloads, and the plugin can reuse it for its "missing model" message.
  - `extra.comfy_unreal` carries the manifest.
- Generate the plugin bundle (`{comfy_unreal, prompt}`) from that file with a build script, through the same `graphToPrompt` flattening the extension's Send uses.
- Put the identical canvas file in the extension's `example_workflows/`.
- One source, three outputs.

## 7. Open questions

1. **Skeletal Mesh and Animation import.** Add a manifest output kind (`SkeletalMesh`, `Animation`) or an import rule. Teach the importer to send rigged FBX/GLB to a Skeletal Mesh plus Anim Sequence through Interchange (UE 5.8). Without this, T7's rig chain has no Unreal destination.
2. **Hunyuan3D licensing.** Can a plugin ship a default template that downloads a Tencent-community-licensed model that excludes some territories? If not, T6 = TRELLIS.2 only.
3. **Output slot carry-through.** Should Restyle and Upscale write back to the same slot as the selected input (BaseColor/Normal/…)? That needs a manifest addition.
4. **Bypass and toggles in the manifest.** T1's `make_tileable` and T7's `rig` are graph-structure toggles (node bypass). The manifest has no `Bool → bypass node(s)` input type. Options: add one, or ship two output nodes and let the plugin pick, or ship variants.
5. **Missing-model UX.** Replace the first-option fallback in `PickModel` with a hard error plus a download prompt that uses `properties.models` URLs. Add an optional `requires` block to the manifest (file, directory, url, size).
6. **Upstreaming.** Does Comfy-Org want "Game assets" official templates? That would give Cloud users these templates natively.
7. **Cloud GPU-time cost.** `estimate_credits` excludes GPU time. What does a T6 TRELLIS.2 run cost a Cloud subscriber, compared with a ~232-credit Tripo call?

## 8. What I could not verify

- **No workflow was run.** Seam-pass quality (T1), wrap-pad upscaling (T3), SA3 small SFX settings (T4) and every VRAM figure are unverified.
- **GLSLShader execution on Cloud workers.** The node and the GLSL blueprints are listed on Cloud. Actual execution (headless GL on Cloud GPUs) was not tested.
- **`qwen_3_4b_fp8_mixed` with Klein.** Its compatibility with Klein (`CLIPLoader` type `flux2`) is inferred. The official Klein templates use the bf16 `qwen_3_4b`.
- **TRELLIS.2 int8.** VRAM needs and GPU support are unknown.
- **Credit prices.** They are from the MCP server's bundled pricing table, which may lag live prices. The credit-to-USD rate (211/$1) is from memory.
- **Upscaler licenses** (4x-UltraSharp, ClearReality, NMKD-Siax, Nomos) were not checked.
- **Hunyuan3D territory clause.** The license *name* was read from Hugging Face metadata. The territory clause was not re-read.
- **Stable Audio 3 small SFX quality** compared with SA Open 1.0 was not compared.
- **Whether the current Desktop stable release includes the newest nodes.** The local source (0.39.0) has them all. The official templates' notes warn that "Desktop/Cloud updates follow stable releases, so some nightly-supported models may not be available yet" **[tpl]**.
