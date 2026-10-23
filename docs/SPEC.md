# Comfy for Unreal: UI and API outline

This is the shape of the plugin as built in the proof of concept, plus what's planned next. Names match the code.

## 1. Concepts

| Concept | Type | Notes |
| --- | --- | --- |
| Backend profile | `FComfyBackendProfile` | Name, kind (Local, Cloud, Deployment, Router), base URL, local protocol (Native or ApiV2), key env var. Stored in `UComfySettings` (Project Settings > Plugins > Comfy). |
| API key | per user | Order: env var on the profile (default `COMFY_API_KEY`), session key from `SetSessionApiKey`, then Editor Preferences > Plugins > Comfy. Never in project config. |
| Workflow | `UComfyWorkflow` asset | API-format graph plus manifest: `Inputs` (`FComfyWorkflowInput`) and `Outputs` (`FComfyWorkflowOutput` with an import rule). Detected on import, editable in the asset's details. |
| Job | `UComfyJob` | One run. States: Pending, Uploading, Queued, Running, Downloading, Succeeded, Failed, Canceled. Outputs land in `Saved/Comfy/Jobs/<JobId>/`. |
| Provenance | `UComfyProvenanceUserData` | Asset user data on every imported asset: source workflow or model, graph hash, backend, job id, inputs, output name, time. |

### Backend protocols

| Kind | Protocol | Submit | Progress | Files in | Files out |
| --- | --- | --- | --- | --- | --- |
| Local (Native) | ComfyUI routes | `POST /prompt` | WebSocket `/ws` + `/history` poll | `POST /upload/image` | `GET /view` |
| Local (ApiV2), Cloud, Deployment | Comfy API v2 | `POST /api/v2/jobs` with Idempotency-Key | poll `urls.self` | `POST /api/v2/assets` (core/ASSET refs) | asset `url` |
| Router | Comfy Router | `POST /v2/models/{id}/requests` | poll `status_url` (no percent) | inline in the body | media URLs in the response |

Router is a direct model-call API; it doesn't run workflow graphs. Hosting your own workflow behind an API is a Comfy API **deployment** (`comfy build` / `comfy deploy`), which this plugin calls as a Deployment backend.

## 2. Editor UI

### Comfy Studio tab

Open from **Tools > Comfy Studio** or the **Comfy** toolbar button.

```
+------------------------------------------------------------------------------+
| Backend [Desktop      v]  ● ComfyUI 0.41, RTX 4080, 2.1 GB free [Check][Settings] |
+------------------------+-----------------------------------------------------+
| WORKFLOWS  [Import JSON...][Refresh] | Seamless Texture (Flux Schnell)        |
|                        | A flat, evenly lit surface texture...               |
| Seamless Texture (...) | ! Desktop doesn't have these nodes: ...             |
|   Textures · Built-in  | Surface   [mossy cobblestone path              ]    |
| Sound Effect (...)     | Seed      [random            ] [Random]            |
|   Audio · Built-in     | Width     [1024]   Height [1024]   Steps [4]        |
| Image to Mesh (...)    | --------------------------------------------------- |
|   Meshes · Built-in    | Save to [/Game/Comfy/Generated                 ]    |
| CW_MyWorkflow          | [x] Import results [x] Create material              |
|   Characters           | [x] Make tileable + normal map [ ] Assign to selected |
|                        |                                            [ Run ]  |
+------------------------+-----------------------------------------------------+
| JOBS                                                        [Clear finished] |
| Seamless Texture   Running   [=======      ]  KSampler           [Cancel]    |
| Sound Effect       Succeeded [=============]  1 output(s)     [Import][Folder] |
+------------------------------------------------------------------------------+
```

- The status dot is green when the last check passed, red when it failed.
- The form is generated from the manifest: multi-line text for prompts, a combo for enums, a checkbox for bools, a path box with a picker for file inputs, a seed box with Random.
- Warnings show missing node classes on the selected backend and when a run spends Comfy credits.
- On success, the job imports itself: images as textures (normal and mask rules set compression), audio as Sound Waves, glb/gltf/fbx/obj as Static Meshes. With "Create material", image jobs become a tileable texture, a derived normal map and a material instance of `/Game/Comfy/Materials/M_Comfy_PBR` (BaseColor, Normal, Roughness, Tiling).
- Toasts report success and failure; failures carry the backend's message (missing nodes, out of credits, bad key).

### Content Browser

- Drag in, or **Import**, an API-format `.json`: creates a **Comfy Workflow** asset (category Comfy). Canvas-format saves are refused with advice to use Export (API). **Reimport** keeps your manifest edits.
- The workflow asset's details panel shows the manifest (editable), required node classes, whether it uses partner nodes, and the graph hash.

### Settings

- **Project Settings > Plugins > Comfy**: backends, default backend, default import folder, poll interval, job timeout, "agents must confirm credit spend".
- **Editor Preferences > Plugins > Comfy**: active backend for you, your API key (password field).

### Planned next

- Asset context menu: "Run again" and "Run variations" from an asset's provenance.
- Live preview image in the job row.
- Backend node list feeding enum options (checkpoint names) from `/object_info`.
- A "Publish as deployment" button that shells out to `comfy deploy`.

## 3. Blueprint and C++ API

### `UComfySubsystem` (engine subsystem, editor and runtime)

| Function | Returns | Notes |
| --- | --- | --- |
| `RunWorkflow(Workflow, Inputs, Backend)` | `UComfyJob*` | Inputs keyed by manifest name; files as absolute paths. Backend None uses the active one. |
| `RunWorkflowJson(ApiGraphJson, Inputs, Backend)` | `UComfyJob*` | No asset needed. |
| `RunRouterModel(ModelId, RequestJson, Backend)` | `UComfyJob*` | Router call; media URLs in the result become outputs. |
| `GetRouterModelSchema(ModelId, OnResult)` / `ListRouterModels(Cursor, OnResult)` | async text | OpenAPI document; paged catalog. |
| `ProbeBackend(Backend, OnStatus)`, `GetLastStatus`, `GetBackendNames`, `GetBackendProfile` | | Health and configuration. |
| `GetActiveBackend` / `SetActiveBackend(Backend, bRemember)` | | |
| `SetSessionApiKey(Key)` | | Not saved. Shipped games should call their own server instead of holding a key. |
| `SpendsCredits(Backend, Workflow)`, `GetMissingNodeClasses(Backend, Workflow)` | | Pre-run checks. |
| `GetJobs`, `FindJob`, `ClearFinishedJobs`, `GetBuiltInWorkflows` | | |

C++ events: `OnJobAdded`, `OnBackendStatus`, `OnActiveBackendChanged`.

### `UComfyJob`

Properties: `JobId`, `RemoteId`, `Backend`, `Workflow`, `ModelId`, `Title`, `Inputs`, `State`, `Progress` (`Fraction`, `QueuePosition`, `CurrentNode`, `Message`), `Outputs` (`FComfyOutputFile`: name, node, file, kind, import rule, local path), `ErrorMessage`, `ResultJson`.

Events (Blueprint): `OnStateChanged`, `OnProgress`, `OnCompleted`, `OnFailed`. C++: `OnFinishedNative`.

Functions: `Cancel`, `IsFinished`, `Succeeded`, `GetOutput(Name)`, `GetOutputsOfKind(Kind)`, `GetOutputDirectory`.

### Blueprint async nodes (`UComfyRunWorkflowAsyncAction`)

- **Run Comfy Workflow** (Workflow, Inputs, Backend) with exec pins On Progress, On Completed, On Failed, each passing the job.
- **Run Comfy Router Model** (ModelId, RequestJson, Backend), same pins.

### `UComfyRuntimeLibrary` (runtime-safe)

| Function | Notes |
| --- | --- |
| `LoadTextureFromFile(Path, bSRGB)` / `LoadTextureFromJob(Job, OutputName, bSRGB)` | Transient `UTexture2D`. |
| `LoadSoundFromFile(Path)` / `LoadSoundFromJob(Job, OutputName)` | Transient `USoundWaveProcedural`; 16-bit PCM WAV. |
| `MakeTileable(In, Out, BlendWidth)` | Half offset plus smoothstep blend over the exposed seams. |
| `MakeNormalMap(In, Out, Strength)` | Wrap-aware Sobel on luminance, Unreal (DirectX) convention. |
| `NewSeed()` | |

### `UComfyWorkflow`

`SetGraphFromJson(Json, bDetectInterface)`, `DetectInterface()`, `FindInput`, `GetInputNames`, `GetInputSchemaJson()` (JSON Schema for agents), `CreateTransientFromJson(Json)`.

### `UComfyEditorLibrary` (editor only; Python `unreal.ComfyEditorLibrary`)

`ImportWorkflowFromFile`, `SaveWorkflowAsAsset`, `GetAllWorkflows`, `FindWorkflow`, `ImportJobOutputs(Job, Folder)`, `ImportFile(Path, Folder, Name, Rule)`, `CreateMaterialFromJob(Job, Folder, Name, FComfyMaterialOptions)`, `CreatePBRMaterialInstance`, `AssignMaterialToSelectedActors`, `GetProvenance(Asset)`.

## 4. Agent toolset (Unreal MCP)

Registered as `ComfyAgent.ComfyToolset` in Epic's ToolsetRegistry, so Unreal MCP's `list_toolsets`, `describe_toolset` and `call_tool` expose it with no extra server. Tool names are the C++ function names and parameters are camelCase, as the registry exposes them; every parameter is required (pass `""` for an empty string). Every tool returns `{"returnValue": "<JSON>"}`. Long-running tools return an async result and finish when the work does.

| Tool | Async | What it does |
| --- | --- | --- |
| `ListBackends()` | | Backends, active one, last health check. |
| `CheckBackend(backend)` | yes | Probe one backend. |
| `SetActiveBackend(backend)` | | |
| `ListWorkflows(categoryFilter)` | | Project and built-in workflows with input names. `""` lists all. |
| `DescribeWorkflow(workflow)` | | JSON Schema of inputs, outputs, missing nodes, credit use. |
| `RunWorkflow(workflow, inputs, backend, bAllowCreditSpend)` | | Starts a job; seeds default to random. |
| `WaitForJob(jobId, timeoutSeconds)` | yes | Up to 600 s; the job keeps running after a timeout. |
| `GetJob(jobId)`, `ListJobs()`, `CancelJob(jobId)` | | |
| `ImportJobOutputs(jobId, destinationPath)` | | Asset paths and classes. |
| `CreateMaterialFromJob(jobId, destinationPath, materialName, bAssignToSelection)` | | Tileable texture, normal map, material instance. |
| `GenerateMaterial(prompt, workflow, bAssignToSelection, bAllowCreditSpend)` | yes | Prompt to assigned material in one call. |
| `ImportWorkflow(filePath, destinationPath)` | | |
| `ListRouterModels(cursor)`, `DescribeRouterModel(modelId)` | yes | |
| `RunRouterModel(modelId, requestJson, bAllowCreditSpend)` | | |

Credit guard: when "agents must confirm credit spend" is on (default), any run that spends credits fails with a message telling the agent to ask the user and retry with `bAllowCreditSpend` set to true.

## 5. Bundled workflow format

Files in `Resources/Workflows/*.json` wrap the API graph with a manifest:

```json
{
  "comfy_unreal": {
    "display_name": "Seamless Texture (Flux Schnell)",
    "category": "Textures",
    "description": "...",
    "inputs": [ { "name": "prompt", "label": "Surface", "node_id": "10", "input": "string_b", "type": "Text" } ],
    "outputs": [ { "name": "albedo", "node_id": "9", "kind": "Image", "import": "Texture" } ]
  },
  "prompt": { "...": "API-format graph" }
}
```

`type` is an `EComfyInputType` name, `kind` an `EComfyOutputKind`, `import` an `EComfyImportRule`. An explicit `inputs` or `outputs` list replaces what detection finds. The same wrapper can be imported into a project as an asset.
