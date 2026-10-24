# Comfy for Unreal

Run ComfyUI workflows from the Unreal Editor, Blueprints, Python and AI agents, and get the results back as Unreal assets: textures and PBR materials, sound waves, and static meshes.

Proof of concept for the 2026 Epic AI MegaJam. Targets Unreal Engine 5.8.

## What it does

- **Four kinds of backend, one API.** Comfy Desktop or any local ComfyUI (native `/prompt` API), Comfy Cloud and Comfy API deployments (Comfy API v2), and Comfy Router (direct partner-model calls). Switch with one dropdown; Blueprints and agents don't change.
- **Workflows are assets.** Import a ComfyUI `Export (API)` JSON and it becomes a Comfy Workflow asset with a manifest of inputs (prompt, seed, size, files...) and outputs (image, audio, mesh). The manifest drives the editor form, the Blueprint node, and the agent tool schema.
- **Built-in game workflows** in `Resources/Workflows`: seamless textures (Flux Schnell, Flux Dev GGUF), sound effects (Stable Audio Open) and image-to-mesh (Hunyuan3D 2).
- **Comfy Studio tab** (Tools menu or the Comfy toolbar button): pick a backend and workflow, fill the form, run, and watch jobs import themselves.
- **Agent toolset** for Epic's Unreal MCP server: `ComfyAgent.ComfyToolset` with tools such as `ListWorkflows`, `RunWorkflow`, `WaitForJob`, `CreateMaterialFromJob` and `GenerateMaterial` (prompt to assigned material in one call).
- **Provenance.** Every imported asset carries the workflow, inputs, seed, backend and job that made it.

## Install

1. Copy this folder to `YourProject/Plugins/ComfyUnreal`.
2. Regenerate project files and build the editor target.
3. Enable **Comfy for Unreal** (it pulls in ToolsetRegistry and Python Editor Script Plugin).
4. Open **Project Settings > Plugins > Comfy** to check the backends. The defaults are:
   - `Desktop`: `http://127.0.0.1:8188`, native protocol (Comfy Desktop's default port)
   - `Comfy Cloud`: `https://cloud.comfy.org`
   - `Comfy Router`: `https://api.comfy.org`
5. For Cloud, deployments, Router or partner nodes, set an API key from [platform.comfy.org](https://platform.comfy.org) in the `COMFY_API_KEY` environment variable or in **Editor Preferences > Plugins > Comfy**. Keys are never written to project config.

## Use it

**Editor:** open Comfy Studio, pick `Seamless Texture (Flux Schnell)`, type a surface, press Run. With "Create material" on, you get `T_..._D`, `T_..._N` and `MI_...` in `/Game/Comfy/Generated`, optionally assigned to the selected actors.

**Blueprint:** `Run Comfy Workflow` (async node with On Progress, On Completed, On Failed), then `Load Texture From Job` or `Load Sound From Job` for runtime use.

**C++:**

```cpp
UComfyJob* Job = UComfySubsystem::Get()->RunWorkflow(Workflow, { { TEXT("prompt"), TEXT("mossy cobblestone") } });
Job->OnFinishedNative.AddLambda([](UComfyJob* Done)
{
	if (Done->Succeeded())
	{
		UTexture2D* Texture = UComfyRuntimeLibrary::LoadTextureFromJob(Done, TEXT("albedo"));
	}
});
```

**Python:**

```python
comfy = unreal.get_engine_subsystem(unreal.ComfySubsystem)
job = comfy.run_workflow(workflow, {"prompt": "weathered red brick"}, "Desktop")
```

**Agents (Unreal MCP):** `call_tool` with `{"toolset_name": "ComfyAgent.ComfyToolset", "tool_name": "GenerateMaterial", "arguments": {"prompt": "weathered red brick", "workflow": "", "bAssignToSelection": true, "bAllowCreditSpend": false}}`.

See [docs/SPEC.md](docs/SPEC.md) for the full UI and API outline.

## Layout

| Module | Type | What's in it |
| --- | --- | --- |
| ComfyCore | Runtime | Settings, workflow asset, jobs, backends (native, API v2, Router), subsystem, Blueprint async node, runtime loaders, image helpers, provenance |
| ComfyEditor | Editor | Comfy Studio tab, workflow importer and reimport, asset definition, output import, PBR material builder |
| ComfyAgent | Editor | `UComfyToolset` registered with the ToolsetRegistry for Unreal MCP |

## Troubleshooting

- **Desktop says "couldn't connect".** The `Desktop` profile defaults to `http://127.0.0.1:8188`. Comfy Desktop, a portable install or a WSL bridge may use another port (a portable install started with `--port 8189`, or a Windows `netsh interface portproxy` rule that holds 8188 and forwards it elsewhere). Check with `netsh interface portproxy show all` and `netstat -ano | findstr LISTENING`, then set the profile's URL in **Project Settings > Plugins > Comfy**. A project can override the whole list in `Config/DefaultGame.ini` under `[/Script/ComfyCore.ComfySettings]`.
- **Cloud or Router says the key was refused (401).** The key is read from the profile's environment variable (`COMFY_API_KEY`), then `COMFY_CLOUD_API_KEY` (comfy-cli's workspace key), then Editor Preferences. Environment variables reach the editor only when whatever launched it started after they were set, so restart the launcher, or start the editor from a new terminal.

## Credits

The toolbar, menu, tab and asset icon is the Comfy logo, resized from `comfy-icon-512.png` in the ComfyUI frontend (`comfyui_frontend_package` 1.53.10, `static/assets/images/`). Its package metadata names no license; the frontend source is [Comfy-Org/ComfyUI_frontend](https://github.com/Comfy-Org/ComfyUI_frontend), and the logo is Comfy Org's mark. Confirm terms with Comfy Org before a public release.

## Status

Proof of concept. Known gaps: runtime sound loading needs 16-bit PCM WAV (FLAC imports fine in the editor); Comfy Cloud's job listing isn't used; no workflow editing inside Unreal yet (edit in ComfyUI and reimport).
