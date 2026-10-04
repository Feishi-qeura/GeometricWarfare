using UnrealBuildTool;
public class DouyinLiveBridge : ModuleRules
{
    public DouyinLiveBridge(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "HTTP" });
        PrivateDependencyModuleNames.AddRange(new[] { "Json", "ImageWrapper", "WebSockets" });
    }
}
