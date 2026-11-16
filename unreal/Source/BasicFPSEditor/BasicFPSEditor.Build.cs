using UnrealBuildTool;
public class BasicFPSEditor : ModuleRules
{
    public BasicFPSEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new [] { "Core", "CoreUObject", "Engine", "UnrealEd", "AssetRegistry", "BasicFPS" });
    }
}
