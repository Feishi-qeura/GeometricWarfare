using UnrealBuildTool;
using System.IO;
public class DouyinLiveProvider : ModuleRules
{
    public DouyinLiveProvider(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "Projects", "Json", "LiveInteraction" });
        string HostRoot = Path.Combine(PluginDirectory, "Binaries", "Win64", "SdkHost");
        if (Directory.Exists(HostRoot))
            foreach (string File in Directory.GetFiles(HostRoot, "*", SearchOption.AllDirectories))
                RuntimeDependencies.Add(File, StagedFileType.NonUFS);
    }
}
