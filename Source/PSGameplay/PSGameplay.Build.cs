using UnrealBuildTool;

public class PSGameplay : ModuleRules
{
    public PSGameplay(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] {
            "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput",
            "GameplayAbilities", "GameplayTags", "GameplayTasks", "Niagara", "UMG", "Slate", "SlateCore"
        });
    }
}

