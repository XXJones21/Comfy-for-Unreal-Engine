# Comfy for Unreal

Run ComfyUI workflows from the Unreal Editor, Blueprints, Python and AI agents, and get the results back as Unreal assets: textures and PBR materials, sound waves, and static meshes.

Alpha (0.1.0) for Comfy Org testers, built for the 2026 Epic AI MegaJam. Targets Unreal Engine 5.8 on Windows.

> **Testers:** start with [docs/user/get-started.mdx](docs/user/get-started.mdx). It covers install, connecting, running and troubleshooting. This README is for people building the plugin from source.

<!-- SCREENSHOT: docs/screenshots/studio-run.png
     Comfy Studio's Run view on Comfy Cloud: a workflow chosen in the left column, a texture result in the gallery, the status line showing "Connected", the account and credits. -->
> **Screenshot needed:** `docs/screenshots/studio-run.png`. Comfy Studio's Run view on Comfy Cloud, with a texture result in the gallery.

## What it does

- **Comfy Studio**, an editor tab (Tools menu or the Comfy toolbar button) with two views:
  - **Run:** pick a workflow, fill its form, run it, and get the results imported into the project.
  - **Graph:** the ComfyUI of the active backend (Comfy Desktop, or Comfy Cloud with its sign-in), embedded in the editor, with **Send to Unreal**.
- **One API, four kinds of backend.** Comfy Desktop or any local ComfyUI (native `/prompt` API), Comfy Cloud and Comfy API deployments (Comfy API v2), and Comfy Router (direct partner-model calls). Switch with one dropdown; Blueprints and agents don't change.
- **Comfy Cloud without a key.** Sign in on the Comfy Cloud page in the Graph view and Studio runs with that sign-in. The status line shows the account, free runs and Comfy Credits. A Comfy API Key works too.
- **Workflows are assets.** Import a ComfyUI `Export (API)` JSON and it becomes a Comfy Workflow asset with a manifest of inputs and outputs ([docs/MANIFEST.md](docs/MANIFEST.md)). The manifest drives the Studio form, the Blueprint node's pins and the agent tool schema.
- **Starter workflows** in `Resources/Workflows`, core ComfyUI nodes only, so they run on Comfy Cloud and on a local ComfyUI. See [Starter workflows](#starter-workflows).
- **Project runs.** Every run is kept in `Saved/Comfy/Jobs` and listed at the bottom of the Run view. Opening one shows the result and puts its inputs back in the form.
- **Agent toolset** for Epic's Unreal MCP server: `ComfyAgent.ComfyToolset` with tools such as `ListWorkflows`, `RunWorkflow`, `WaitForJob`, `CreateMaterialFromJob` and `GenerateMaterial` (prompt to assigned material in one call).
- **Provenance.** Every imported asset carries the workflow, inputs, seed, backend and job that made it.

## Starter workflows

| Workflow | Makes | Model | Download for a local ComfyUI |
| --- | --- | --- | --- |
| Seamless Texture | A surface texture from a prompt | FLUX.2 Klein 4B | about 11.6 GB (3 files) |
| Texture Restyle | A changed copy of a selected texture | FLUX.2 Klein 4B | same files |
| Texture Upscale | A larger copy of a selected texture | Real-ESRGAN x4 | 67 MB |
| Sound Effect | A sound effect or ambience, up to 47 s | Stable Audio Open 1.0 | 5.8 GB (2 files) |
| Music Track | A music track, instrumental by default | ACE-Step 1.5 turbo | 10 GB |
| Image to Mesh | An untextured static mesh from one image | Hunyuan3D 2.0 | 4.9 GB |

Comfy Cloud has every model. On a local ComfyUI without a file, the model card names it and offers a **Download** button, and Run stops before sending anything.

The files are generated, not edited by hand: change `tools/build_starter_workflows.py` and run

```text
python tools/build_starter_workflows.py
```

It writes `Resources/Workflows/*.json` and checks each graph offline against Comfy Cloud's node specs (node classes, required inputs, link types, model names).

<!-- SCREENSHOT: docs/screenshots/studio-library.png
     The Run view with no workflow selected: the "What do you want to make?" library with the six starter cards grouped by type. -->
> **Screenshot needed:** `docs/screenshots/studio-library.png`. The workflow library with the six starter cards.

## Build from source

1. Copy this folder to `YourProject/Plugins/ComfyUnreal`.
2. Regenerate project files and build the editor target.
3. Enable **Comfy for Unreal** (it pulls in ToolsetRegistry and Python Editor Script Plugin).
4. Open **Project Settings > Plugins > Comfy** to check the backends. The defaults are:
   - `Desktop`: `http://127.0.0.1:8188`, native protocol. When that doesn't answer, Studio also tries port 8000 (Comfy Desktop's default), 8188 (ComfyUI's default) and the ports set on Comfy Desktop's installations.
   - `Comfy Cloud`: `https://cloud.comfy.org`
   - `Comfy Router`: `https://api.comfy.org`
5. For Comfy Cloud, either sign in from the Graph view or set a Comfy API Key from [platform.comfy.org](https://platform.comfy.org/profile/api-keys). Deployments, Router and partner nodes need the key. Put it in the `COMFY_API_KEY` environment variable or in **Editor Preferences > Plugins > Comfy**. Keys are never written to project config.

To make the prebuilt zip that testers install, package the plugin with `RunUAT BuildPlugin -Plugin=<path>/ComfyUnreal.uplugin -Package=<out> -TargetPlatforms=Win64` and zip the output folder.

## Use it

**Editor:** open Comfy Studio, pick **Seamless Texture**, click a preset such as **Brick** or type a surface, and press **Run**. With **Make a material from images** on, you get `T_..._D`, `T_..._N` and `MI_...` in `/Game/Comfy/Generated`. **Replace on selection** puts the material on the selected actors, and **Revert** undoes it.

<!-- SCREENSHOT: docs/screenshots/replace-on-selection.png
     A level with a mesh using the generated material after Replace on selection, with Comfy Studio docked beside the viewport. -->
> **Screenshot needed:** `docs/screenshots/replace-on-selection.png`. A level object wearing a generated material after **Replace on selection**.

**Blueprint:** `Run Comfy Workflow` is an async node with On Progress, On Completed and On Failed. Set its **Workflow** pin to an asset and it grows one typed pin per workflow input, the same inputs the Run view shows: text, whole numbers, numbers, on/off, a seed (-1 for a new one every run), a dropdown for choices and model files, and a texture pin for image inputs. Pins left at their defaults keep the workflow's own values, and an empty model pin lets each backend pick its preferred file. The right-click menu also has one **Run <workflow>** entry per workflow asset, starters included (`/ComfyUnreal/Workflows`, written by the editor from `Resources/Workflows`). Then use `Load Texture From Job` or `Load Sound From Job` for runtime use.

To build inputs yourself, chain the **Set (Comfy Inputs)** nodes (Text, Int, Float, Bool, Seed, File, Texture, Asset) into the node's **Inputs** pin. A name the workflow doesn't have fails the run with the list of its inputs. The older `Run Comfy Workflow (String Map)` node still takes a string map.

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

<!-- SCREENSHOT: docs/screenshots/graph-cloud.png
     The Graph view showing Comfy Cloud signed in, with a workflow open and the Send to Unreal button visible. -->
> **Screenshot needed:** `docs/screenshots/graph-cloud.png`. The Graph view on Comfy Cloud with **Send to Unreal**.

See [docs/SPEC.md](docs/SPEC.md) for the API outline and [docs/design/comfy-for-unreal.html](docs/design/comfy-for-unreal.html) for the design doc, journeys and roadmap.

## Layout

| Module | Type | What's in it |
| --- | --- | --- |
| ComfyCore | Runtime | Settings, workflow asset, jobs and run history, backends (native, API v2, Router, Cloud page bridge), subsystem, Comfy Inputs and the async actions, runtime loaders, image helpers, provenance |
| ComfyBlueprint | UncookedOnly | The Run Comfy Workflow node: one pin per workflow input, compiled to Comfy Inputs calls |
| ComfyEditor | Editor | Comfy Studio tab (Run and Graph views), workflow importer and reimport, asset definition, output import, PBR material builder |
| ComfyAgent | Editor | `UComfyToolset` registered with the ToolsetRegistry for Unreal MCP |

| Folder | What's in it |
| --- | --- |
| `Resources/Workflows` | Starter workflow bundles, generated by `tools/build_starter_workflows.py` |
| `Resources/Bridge` | The script Studio injects into the Comfy Cloud page to run with its sign-in |
| `docs/user` | Tester-facing docs (Mintlify MDX) |
| `docs/design` | Design doc (`build.py` turns the template into the self-contained `comfy-for-unreal.html`) |

## Troubleshooting

- **Desktop says it can't find ComfyUI.** Start ComfyUI first. Studio tries the `Desktop` profile's URL, then ports 8000 and 8188, then the ports set on Comfy Desktop's installations. A portable install started with `--port 8189`, or a Windows `netsh interface portproxy` rule that holds a port and forwards it elsewhere, needs the profile's URL set in **Project Settings > Plugins > Comfy**. Check with `netsh interface portproxy show all` and `netstat -ano | findstr LISTENING`. A project can override the whole list in `Config/DefaultGame.ini` under `[/Script/ComfyCore.ComfySettings]`.
- **Comfy Cloud refused the API key.** The key is read from the profile's environment variable (`COMFY_API_KEY`), then `COMFY_CLOUD_API_KEY` (comfy-cli's workspace key), then a session key, then Editor Preferences. Studio remembers a refused key and runs with the Comfy Cloud sign-in instead when there is one. Environment variables reach the editor only when whatever launched it started after they were set, so restart the launcher, or start the editor from a new terminal.
- **A model card says "Not found on this backend".** The backend lacks a file the workflow names. Download it into the folder the card shows, under `ComfyUI/models`, then click **Retry**.

<!-- SCREENSHOT: docs/screenshots/model-missing.png
     A Model input card showing "Not found on this backend: <file> (<size>). It goes in ComfyUI/models/<folder>." with the Download button. -->
> **Screenshot needed:** `docs/screenshots/model-missing.png`. A model card with the "Not found on this backend" message and **Download**.

## Credits

The toolbar, menu, tab and asset icon is the Comfy logo, resized from `comfy-icon-512.png` in the ComfyUI frontend (`comfyui_frontend_package` 1.53.10, `static/assets/images/`). Its package metadata names no license; the frontend source is [Comfy-Org/ComfyUI_frontend](https://github.com/Comfy-Org/ComfyUI_frontend), and the logo is Comfy Org's mark. Confirm terms with Comfy Org before a public release.

## Status

Alpha. Known gaps:

- The starter workflows haven't all been run end to end on both Comfy Cloud and a local ComfyUI.
- Runtime sound loading needs 16-bit PCM WAV (FLAC imports fine in the editor).
- Rigged or animated meshes import as Static Meshes.
- **Send to Unreal** from the Comfy Cloud page hasn't been tested, and the Comfy for Unreal ComfyUI extension (exposing inputs, tagging outputs) doesn't load on Comfy Cloud.

The roadmap is in the design doc.
