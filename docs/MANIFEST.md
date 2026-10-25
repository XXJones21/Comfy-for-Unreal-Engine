# The `comfy_unreal` manifest (version 1)

The contract between the **Comfy for Unreal** ComfyUI extension and the Unreal plugin. It says which inputs of a ComfyUI workflow Unreal may set, and what each output becomes in the project. The graph itself is untouched, so a tagged workflow runs unchanged on any ComfyUI, Comfy Desktop, Comfy Cloud or an API deployment.

## Where it lives

- **In a canvas save** (what ComfyUI's Save writes): `workflow.extra.comfy_unreal`. The extension writes it there. Per-node tags also sit in `node.properties.comfy_unreal`, but the manifest is the source of truth.
- **In a plugin bundle file** (`Resources/Workflows/*.json`): `{"comfy_unreal": <manifest>, "prompt": <API graph>}`. The plugin's built-in workflows use this.
- **In the extension's server library**: `user/default/comfy-unreal/workflows/<id>.json` holding `{"manifest", "workflow", "prompt", "thumbnail"}`. `thumbnail` is a data URL or empty.

## Fields

```json
{
  "version": 1,
  "display_name": "Seamless Texture (Cloud)",
  "category": "Texture",
  "description": "What it makes, what it needs.",
  "group": "Environment",
  "inputs": [
    { "name": "prompt", "label": "Surface", "node_id": "10", "input": "string_b",
      "type": "Text", "source": "Prompt", "tooltip": "..." },
    { "name": "ckpt_name", "label": "Checkpoint", "node_id": "4", "input": "ckpt_name",
      "type": "Model", "prefer": ["flux1-schnell-fp8"], "family": "flux-checkpoint" },
    { "name": "image", "label": "Reference", "node_id": "56", "input": "image",
      "type": "File", "source": "SelectedTextures" }
  ],
  "outputs": [
    { "name": "albedo", "node_id": "9", "kind": "Texture", "slot": "BaseColor" },
    { "name": "normal", "node_id": "12", "kind": "Texture", "slot": "Normal" }
  ],
  "presets": [ { "name": "Brick", "values": { "prompt": "weathered red brick" } } ]
}
```

- **`version`**: 1. A reader ignores fields it doesn't know.
- **`category`**: the workflow type, one of `Material`, `Texture`, `Sound`, `Music`, `Mesh`, `Upscale`, `Other`. Older plugin files use `Textures`, `Audio`, `Meshes`; readers treat those as `Texture`, `Sound`, `Mesh`.
- **`group`**: free text for grouping in a library. Optional.
- **`inputs[]`**: `node_id` and `input` address a widget in the API graph. `name` is the stable key Blueprints, Python and agents use. `type` is one of `String`, `Text`, `Int`, `Float`, `Bool`, `Seed`, `File`, `Enum`, `Model`. Optional: `label`, `tooltip`, `default` (as text), `options` (for `Enum`), `prefer` and `family` (for `Model`), `source`.
- **`inputs[].download`** (Model inputs, optional): `{"file", "folder", "size", "url"}`, the file to get when the backend has none of `prefer`. `folder` is the folder under `ComfyUI/models`. When a Model input has a `prefer` list and the backend has none of those files, the plugin doesn't fall back to another file: the form says which file is missing and offers this download, and a run stops with the same message.
- **`inputs[].source`**: where Unreal fills it from. `Constant` (the form value, the default), `Prompt` (the main text box), `SelectedTextures` (Content Browser selection), `SelectedMeshes`, `ViewportCapture`.
- **`outputs[]`**: `node_id` is a save or preview node. `kind` is one of `Texture`, `Image`, `Sound`, `Music`, `Mesh`, `Video`, `File`. `slot` applies to `Texture`: `BaseColor`, `Normal`, `Roughness`, `Metallic`, `AO`, `Height`, `ORM`, `Emissive`, `Opacity`.
- **`presets[]`**: named sets of input values, as text. Optional. Comfy Studio shows them as buttons above the form.

## How Unreal imports outputs

| kind / slot | Unreal asset | sRGB | Compression |
| --- | --- | --- | --- |
| Texture BaseColor, Emissive; Image | Texture2D | on | Default |
| Texture Normal | Texture2D | off | Normal map |
| Texture Roughness, Metallic, AO, Height, ORM, Opacity | Texture2D | off | Masks |
| Sound, Music | SoundWave | | |
| Mesh | StaticMesh | | |

The plugin's older `kind` values (`Image`, `Audio`) and `import` rule (`Texture`, `NormalMap`, `Masks`, `Sound`, `StaticMesh`, `None`) are still read.

## Precedence when importing a graph

1. `extra.comfy_unreal` (this manifest).
2. ComfyUI App mode's `extra.linearData`, when present: its chosen inputs become exposed inputs.
3. The plugin's detection: prompts, seeds, sizes, steps, model names, load and save nodes.
