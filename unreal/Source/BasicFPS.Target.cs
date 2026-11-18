using UnrealBuildTool;
using System.Collections.Generic;
public class BasicFPSTarget : TargetRules
{
    public BasicFPSTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V5;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
        ExtraModuleNames.Add("BasicFPS");
    }
}
