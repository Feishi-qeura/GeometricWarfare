using System.IO;
using UnrealBuildTool;

public class GeometricWarfareEditor : ModuleRules
{
	public GeometricWarfareEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivateDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine", "UnrealEd", "AssetTools", "AssetRegistry", "RenderCore", "RHI",
			"Niagara", "NiagaraCore", "NiagaraShader", "MaterialEditor", "MeshDescription", "StaticMeshDescription"
		});
		// Lightweight (stateless) Niagara emitters are plain data; their editor-time types live in Niagara/Internal.
		string NiagaraSource = Path.Combine(EngineDirectory, "Plugins", "FX", "Niagara", "Source");
		PrivateIncludePaths.Add(Path.Combine(NiagaraSource, "Niagara", "Internal"));
		PrivateIncludePaths.Add(Path.Combine(NiagaraSource, "NiagaraShader", "Internal"));
	}
}
