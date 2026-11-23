#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Character/PSBaseCharacter.h"
#include "Components/ActorComponent.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSTraversalSampleContractTest, "ProjectS.Traversal.SampleIntegration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSTraversalSampleContractTest::RunTest(const FString& Parameters)
{
    UClass* PawnClass = LoadClass<APSBaseCharacter>(nullptr,
        TEXT("/Game/ProjectS/Characters/BP_PSBaseCharacter.BP_PSBaseCharacter_C"));
    UClass* TraversalClass = LoadClass<UActorComponent>(nullptr,
        TEXT("/Game/Blueprints/AC_TraversalLogic.AC_TraversalLogic_C"));
    if (!TestNotNull(TEXT("Playable sample character"), PawnClass)
        || !TestNotNull(TEXT("Sample traversal component class"), TraversalClass)) return false;

    TestTrue(TEXT("Traversal enabled on playable character"), PawnClass->GetDefaultObject<APSBaseCharacter>()->bPSEnableTraversal);
    TestNotNull(TEXT("Sample traversal state"), FindFProperty<FBoolProperty>(TraversalClass, TEXT("DoingTraversalAction")));
    TestNotNull(TEXT("Character supplies sample traversal properties"), PawnClass->FindFunctionByName(TEXT("Get_PropertiesForTraversal")));
    UFunction* GetInputs = PawnClass->FindFunctionByName(TEXT("GetTraversalCheckInputs"));
    UFunction* TryAction = TraversalClass->FindFunctionByName(TEXT("TryTraversalAction"));
    if (!TestNotNull(TEXT("Sample traversal input getter"), GetInputs)
        || !TestNotNull(TEXT("Sample traversal entry point"), TryAction)) return false;

    FStructProperty* Result = FindFProperty<FStructProperty>(GetInputs, TEXT("ReturnValue"));
    FStructProperty* Inputs = FindFProperty<FStructProperty>(TryAction, TEXT("Inputs"));
    if (!TestNotNull(TEXT("Traversal getter output struct"), Result)
        || !TestNotNull(TEXT("Traversal action input struct"), Inputs)) return false;
    TestTrue(TEXT("Getter and action use the same Blueprint struct"), Result->Struct == Inputs->Struct);
    TestTrue(TEXT("Getter output is a reflected result"), Result->HasAnyPropertyFlags(CPF_ReturnParm | CPF_OutParm));
    TestNotNull(TEXT("Obstacle failure result used by jump fallback"), FindFProperty<FBoolProperty>(TryAction, TEXT("TraversalCheckFailed")));
    TestNotNull(TEXT("Animation failure result used by jump fallback"), FindFProperty<FBoolProperty>(TryAction, TEXT("MontageSelectionFailed")));

    bool bHasDirection = false;
    for (TFieldIterator<FStructProperty> It(Inputs->Struct); It; ++It)
        bHasDirection |= It->GetName().StartsWith(TEXT("TraceForwardDirection")) && It->Struct == TBaseStructure<FVector>::Get();
    TestTrue(TEXT("Native movement can supply the traversal trace direction"), bHasDirection);
    TestNotNull(TEXT("Sample traversal montage chooser"), LoadObject<UObject>(nullptr,
        TEXT("/Game/Characters/UEFN_Mannequin/Animations/Traversal/CHT_TraversalMontages_CMC.CHT_TraversalMontages_CMC")));
    TestNotNull(TEXT("Supported traversal obstacle class"), LoadClass<AActor>(nullptr,
        TEXT("/Game/Levels/LevelPrototyping/LevelBlock_Traversable.LevelBlock_Traversable_C")));
    return true;
}

#endif
