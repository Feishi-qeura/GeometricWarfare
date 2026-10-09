using System.IO;
using UnrealBuildTool;

public class LiveSpoutOutput : ModuleRules
{
    public LiveSpoutOutput(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "RHI", "RenderCore" });
        PrivateDependencyModuleNames.AddRange(new[] { "Projects", "Slate", "SlateCore" });
        if (Target.bBuildEditor) PrivateDependencyModuleNames.Add("UnrealEd");
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PrivateDependencyModuleNames.Add("D3D12RHI");
            string Native = Path.Combine(PluginDirectory, "Source", "ThirdParty");
            PrivateIncludePaths.Add(Path.Combine(Native, "include"));
            foreach (string Name in new[] { "Spout", "SpoutDX12" })
            {
                PublicAdditionalLibraries.Add(Path.Combine(Native, "lib", "Win64", Name + ".lib"));
                PublicDelayLoadDLLs.Add(Name + ".dll");
                RuntimeDependencies.Add("$(BinaryOutputDir)/" + Name + ".dll", Path.Combine(Native, "bin", "Win64", Name + ".dll"));
            }
            RuntimeDependencies.Add(Path.Combine(PluginDirectory, "Licenses", "GPUbrainStorm-MIT.txt"), StagedFileType.NonUFS);
            RuntimeDependencies.Add(Path.Combine(PluginDirectory, "Licenses", "Spout-BSD-2-Clause.txt"), StagedFileType.NonUFS);
        }
    }
}
