using UnrealBuildTool;

public class ProjectSEditor : ModuleRules
{
    public ProjectSEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "UnrealEd" });
        PrivateDependencyModuleNames.AddRange(new[] {
            "PSGameplay", "AssetRegistry", "Kismet", "KismetCompiler", "BlueprintEditorLibrary",
            "EnhancedInput", "InputCore", "GameplayAbilities", "GameplayTags", "GameplayTasks", "Json"
        });
    }
}

