using UnrealBuildTool;
public class BasicFPS : ModuleRules
{
    public BasicFPS(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new [] { "Core", "CoreUObject", "Engine", "InputCore" });
        PrivateDependencyModuleNames.AddRange(new [] { "Slate", "SlateCore", "ApplicationCore", "WebSockets", "Json", "JsonUtilities", "RHI", "RenderCore" });
        if (Target.Platform == UnrealTargetPlatform.Win64) PublicSystemLibraries.Add("dxgi.lib");
    }
}
