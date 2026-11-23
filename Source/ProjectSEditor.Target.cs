using UnrealBuildTool;

public class ProjectSEditorTarget : TargetRules
{
    public ProjectSEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
        ExtraModuleNames.AddRange(new[] { "PSGameplay", "ProjectSEditor" });
    }
}

