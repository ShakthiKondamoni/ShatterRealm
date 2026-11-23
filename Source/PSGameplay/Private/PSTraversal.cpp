#include "Character/PSBaseCharacter.h"
#include "Components/ActorComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

static TAutoConsoleVariable<int32> CVarPSTraversalDebug(TEXT("ps.TraversalDebug"), 1,
    TEXT("Log traversal attempts, failed obstacle checks and montage selection. 0: off, 1: on."));

namespace
{
FStructProperty* FindTraversalInputResult(UFunction* Function)
{
    if (!Function) return nullptr;
    for (TFieldIterator<FStructProperty> It(Function); It; ++It)
    {
        if (It->HasAnyPropertyFlags(CPF_Parm) && It->HasAnyPropertyFlags(CPF_ReturnParm | CPF_OutParm))
            return *It;
    }
    return nullptr;
}
}

UActorComponent* APSBaseCharacter::FindPSTraversalComponent() const
{
    if (UActorComponent* Cached = PSTraversalComponent.Get()) return Cached;
    TInlineComponentArray<UActorComponent*> Components;
    GetComponents(Components);
    for (UActorComponent* Component : Components)
    {
        if (Component && Component->FindFunction(TEXT("TryTraversalAction"))
            && FindFProperty<FBoolProperty>(Component->GetClass(), TEXT("DoingTraversalAction")))
        {
            PSTraversalComponent = Component;
            return Component;
        }
    }
    return nullptr;
}

bool APSBaseCharacter::IsPSTraversing() const
{
    const UActorComponent* Traversal = FindPSTraversalComponent();
    const FBoolProperty* Active = Traversal
        ? FindFProperty<FBoolProperty>(Traversal->GetClass(), TEXT("DoingTraversalAction")) : nullptr;
    return Active && Active->GetPropertyValue_InContainer(Traversal);
}

bool APSBaseCharacter::TryPSTraversal()
{
    if (!bPSEnableTraversal || IsPSDead() || IsPSRooted() || IsPSSliding() || IsPSTraversing() || bIsCrouched
        || GetLocalRole() == ROLE_SimulatedProxy || GetMesh()->IsSimulatingPhysics()) return false;
    if (!GetCharacterMovement()->IsMovingOnGround() && !GetCharacterMovement()->IsFalling()) return false;

    UActorComponent* Traversal = FindPSTraversalComponent();
    UFunction* GetInputs = FindFunction(TEXT("GetTraversalCheckInputs"));
    UFunction* TryAction = Traversal ? Traversal->FindFunction(TEXT("TryTraversalAction")) : nullptr;
    FStructProperty* InputResult = FindTraversalInputResult(GetInputs);
    FStructProperty* ActionInputs = TryAction ? FindFProperty<FStructProperty>(TryAction, TEXT("Inputs")) : nullptr;
    FBoolProperty* CheckFailed = TryAction ? FindFProperty<FBoolProperty>(TryAction, TEXT("TraversalCheckFailed")) : nullptr;
    FBoolProperty* MontageFailed = TryAction ? FindFProperty<FBoolProperty>(TryAction, TEXT("MontageSelectionFailed")) : nullptr;
    if (!InputResult || !ActionInputs || InputResult->Struct != ActionInputs->Struct || !CheckFailed || !MontageFailed)
    {
        if (!bPSTraversalWarningLogged)
        {
            bPSTraversalWarningLogged = true;
            UE_LOG(LogProjectSCharacter, Warning, TEXT("Traversal bridge unavailable on %s. Use BP_PSBaseCharacter with the sample AC_TraversalLogic and GetTraversalCheckInputs. Space will use normal jump."), *GetName());
        }
        return false;
    }

    // These sample functions use Blueprint-defined structs. Build the complete
    // reflected frames instead of hard-coding struct layout or GUID member names.
    FStructOnScope InputFrame(GetInputs);
    ProcessEvent(GetInputs, InputFrame.GetStructMemory());
    FStructOnScope ActionFrame(TryAction);
    void* Destination = ActionInputs->ContainerPtrToValuePtr<void>(ActionFrame.GetStructMemory());
    const void* Source = InputResult->ContainerPtrToValuePtr<void>(InputFrame.GetStructMemory());
    ActionInputs->Struct->CopyScriptStruct(Destination, Source);

    // The old sample input action is no longer mapped. Use our real movement
    // direction while retaining the sample's trace dimensions and offsets.
    FVector Direction = GetLastMovementInputVector().GetSafeNormal2D();
    if (Direction.IsNearlyZero()) Direction = GetVelocity().GetSafeNormal2D();
    if (Direction.IsNearlyZero()) Direction = GetActorForwardVector().GetSafeNormal2D();
    for (TFieldIterator<FStructProperty> It(ActionInputs->Struct); It; ++It)
    {
        if (It->GetName().StartsWith(TEXT("TraceForwardDirection")) && It->Struct == TBaseStructure<FVector>::Get())
        {
            *It->ContainerPtrToValuePtr<FVector>(Destination) = Direction;
            break;
        }
    }

    // AC_TraversalLogic performs the ledge/clearance checks, selects a suitable
    // hurdle/vault/mantle montage and uses its existing server/client playback.
    Traversal->ProcessEvent(TryAction, ActionFrame.GetStructMemory());
    const bool bCheckFailed = CheckFailed->GetPropertyValue_InContainer(ActionFrame.GetStructMemory());
    const bool bMontageFailed = MontageFailed->GetPropertyValue_InContainer(ActionFrame.GetStructMemory());
    const bool bStarted = IsPSTraversing();
    if (CVarPSTraversalDebug.GetValueOnGameThread() > 0)
    {
        UE_LOG(LogProjectSCharacter, Log, TEXT("Space traversal: active=%d obstacleCheckFailed=%d montageSelectionFailed=%d"),
            bStarted, bCheckFailed, bMontageFailed);
    }
    // Match the sample's fallback logic. A valid selection may be waiting for
    // replicated playback, so don't start a jump while that traversal is accepted.
    return bStarted || (!bCheckFailed && !bMontageFailed);
}
