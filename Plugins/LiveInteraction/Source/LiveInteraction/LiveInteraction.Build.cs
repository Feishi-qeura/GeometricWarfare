using UnrealBuildTool;
public class LiveInteraction : ModuleRules
{
    public LiveInteraction(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "HTTP", "Json" });
        PrivateDependencyModuleNames.AddRange(new[] { "ImageWrapper", "WebSockets" });
    }
}
