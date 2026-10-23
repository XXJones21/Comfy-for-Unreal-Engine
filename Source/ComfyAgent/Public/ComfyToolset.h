// Copyright Comfy Org. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "ToolsetRegistry/ToolCallAsyncResultString.h"
#include "ComfyToolset.generated.h"

/**
 * Generate game assets with ComfyUI from inside the Unreal Editor: textures and PBR materials,
 * sound effects and music, 3D meshes, or any custom ComfyUI workflow imported into the project.
 * Runs on Comfy Desktop (local GPU), Comfy Cloud, Comfy API deployments, or Comfy Router models.
 * Typical flow: ListWorkflows, DescribeWorkflow, RunWorkflow, WaitForJob, then
 * ImportJobOutputs or CreateMaterialFromJob. GenerateMaterial does all of that in one call.
 * Every result is JSON. Jobs keep running in the background; poll GetJob or call WaitForJob.
 */
UCLASS(BlueprintType, MinimalAPI)
class UComfyToolset : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	/**
	 * List the configured Comfy backends, which one is active, and the last health check of each.
	 * @return JSON: {active, backends:[{name, kind, url, reachable, summary, spends_credits}]}
	 */
	UFUNCTION(meta = (AICallable), Category = "Comfy")
	static FString ListBackends();

	/**
	 * Check that a backend answers and accepts the API key. Waits for the answer.
	 * @param Backend Backend name from ListBackends. Empty checks the active backend.
	 * @return JSON: {name, reachable, summary}
	 */
	UFUNCTION(meta = (AICallable), Category = "Comfy")
	static UToolCallAsyncResultString* CheckBackend(const FString& Backend);

	/**
	 * Make a backend the active one for later runs. Saved to the user's editor settings.
	 * @param Backend Backend name from ListBackends.
	 */
	UFUNCTION(meta = (AICallable), Category = "Comfy")
	static FString SetActiveBackend(const FString& Backend);

	/**
	 * List the workflows available to run: workflow assets in the project and the plugin's built-in ones.
	 * @param CategoryFilter Only list this category (for example Textures, Audio, Meshes). Empty lists all.
	 * @return JSON array: [{name, path, display_name, category, description, uses_credits, inputs, outputs}]
	 */
	UFUNCTION(meta = (AICallable), Category = "Comfy")
	static FString ListWorkflows(const FString& CategoryFilter);

	/**
	 * Describe one workflow: its inputs as JSON Schema, its outputs, and anything the active backend is missing.
	 * @param Workflow Workflow name, display name or asset path from ListWorkflows.
	 * @return JSON: {name, description, input_schema, outputs, missing_nodes, uses_credits}
	 */
	UFUNCTION(meta = (AICallable), Category = "Comfy")
	static FString DescribeWorkflow(const FString& Workflow);

	/**
	 * Start a workflow and return at once with the job. Use WaitForJob to wait for it.
	 * @param Workflow Workflow name, display name or asset path from ListWorkflows.
	 * @param Inputs Input values by name, as strings, for example {"prompt": "mossy cobblestone", "seed": "random"}. Inputs left out keep their defaults.
	 * @param Backend Backend name. Empty uses the active backend.
	 * @param bAllowCreditSpend Set true only after the user agreed to spend Comfy credits on this run.
	 * @return JSON of the job: {job_id, state, progress, outputs, error}
	 */
	UFUNCTION(meta = (AICallable), Category = "Comfy")
	static FString RunWorkflow(const FString& Workflow, const TMap<FString, FString>& Inputs, const FString& Backend, bool bAllowCreditSpend);

	/**
	 * Wait until a job finishes, or until the timeout passes, and return its state.
	 * @param JobId Job id from RunWorkflow or RunRouterModel.
	 * @param TimeoutSeconds How long to wait, 1 to 600. The job keeps running after a timeout.
	 * @return JSON of the job.
	 */
	UFUNCTION(meta = (AICallable), Category = "Comfy")
	static UToolCallAsyncResultString* WaitForJob(const FString& JobId, int32 TimeoutSeconds);

	/**
	 * Get a job's current state, progress, outputs and error.
	 * @param JobId Job id.
	 * @return JSON of the job.
	 */
	UFUNCTION(meta = (AICallable), Category = "Comfy")
	static FString GetJob(const FString& JobId);

	/**
	 * List this session's jobs, newest first.
	 * @return JSON array of jobs.
	 */
	UFUNCTION(meta = (AICallable), Category = "Comfy")
	static FString ListJobs();

	/**
	 * Ask the backend to stop a job.
	 * @param JobId Job id.
	 * @return JSON of the job.
	 */
	UFUNCTION(meta = (AICallable), Category = "Comfy")
	static FString CancelJob(const FString& JobId);

	/**
	 * Import a finished job's outputs as project assets: images become textures, audio becomes
	 * sound waves, glb/gltf/fbx/obj become static meshes. Each asset records the job that made it.
	 * @param JobId Job id of a succeeded job.
	 * @param DestinationPath Content folder such as /Game/Comfy/Generated. Empty uses the project setting.
	 * @return JSON: {assets:[{path, class}]}
	 */
	UFUNCTION(meta = (AICallable), Category = "Comfy")
	static FString ImportJobOutputs(const FString& JobId, const FString& DestinationPath);

	/**
	 * Turn a finished image job into a PBR material instance: make the image tileable, derive a
	 * normal map, import both, and create a material instance. Optionally assign it to the selected actors.
	 * @param JobId Job id of a succeeded job with an image output.
	 * @param DestinationPath Content folder. Empty uses the project setting.
	 * @param MaterialName Name for the material, without prefix. Empty derives one from the job.
	 * @param bAssignToSelection Put the material on every mesh slot of the actors selected in the level.
	 * @return JSON: {material, base_color, normal, assigned_components}
	 */
	UFUNCTION(meta = (AICallable), Category = "Comfy")
	static FString CreateMaterialFromJob(const FString& JobId, const FString& DestinationPath, const FString& MaterialName, bool bAssignToSelection);

	/**
	 * One call from a text prompt to a tileable PBR material in the project: runs a texture
	 * workflow, waits, builds the material and optionally assigns it to the selected actors.
	 * @param Prompt What the surface looks like, for example "weathered red brick wall".
	 * @param Workflow Texture workflow to use. Empty picks the first workflow in the Textures category.
	 * @param bAssignToSelection Put the material on the selected actors.
	 * @param bAllowCreditSpend Set true only after the user agreed to spend Comfy credits.
	 * @return JSON: {job, material, base_color, normal, assigned_components}
	 */
	UFUNCTION(meta = (AICallable), Category = "Comfy")
	static UToolCallAsyncResultString* GenerateMaterial(const FString& Prompt, const FString& Workflow, bool bAssignToSelection, bool bAllowCreditSpend);

	/**
	 * Import a ComfyUI workflow exported with Export (API) as a workflow asset, so it can be run.
	 * @param FilePath Absolute path of the .json file on this machine.
	 * @param DestinationPath Content folder. Empty uses /Game/Comfy/Workflows.
	 * @return JSON of the workflow, as DescribeWorkflow returns it.
	 */
	UFUNCTION(meta = (AICallable), Category = "Comfy")
	static FString ImportWorkflow(const FString& FilePath, const FString& DestinationPath);

	/**
	 * List Comfy Router models (partner models such as Flux, Kling, Meshy, ElevenLabs), one page at a time.
	 * @param Cursor next_cursor from the previous page, or empty.
	 * @return The Router's JSON listing.
	 */
	UFUNCTION(meta = (AICallable), Category = "Comfy")
	static UToolCallAsyncResultString* ListRouterModels(const FString& Cursor);

	/**
	 * Get a Comfy Router model's OpenAPI document, which defines its request body.
	 * @param ModelId Model id such as bfl/flux-2-pro.
	 * @return The model's OpenAPI JSON.
	 */
	UFUNCTION(meta = (AICallable), Category = "Comfy")
	static UToolCallAsyncResultString* DescribeRouterModel(const FString& ModelId);

	/**
	 * Call a Comfy Router model with its native request body. Media in the result are downloaded
	 * as the job's outputs. Always spends Comfy credits.
	 * @param ModelId Model id such as bfl/flux-2-pro.
	 * @param RequestJson Request body as JSON text, shaped by DescribeRouterModel.
	 * @param bAllowCreditSpend Set true only after the user agreed to spend Comfy credits.
	 * @return JSON of the job.
	 */
	UFUNCTION(meta = (AICallable), Category = "Comfy")
	static FString RunRouterModel(const FString& ModelId, const FString& RequestJson, bool bAllowCreditSpend);
};
