#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"

class UWorld;
class AActor;

namespace SRGameplayQueries
{
    // Candidate collection only. Callers retain authority/team/status/damage policy.
    PSGAMEPLAY_API void SweepBoxTargets(UWorld* World, AActor* Owner, const FVector& Start,
        const FVector& End, const FVector& HalfExtent, const FQuat& Rotation,
        TArray<AActor*>& OutTargets, bool bDrawDebug = false);
    PSGAMEPLAY_API bool HasCoverFreePath(UWorld* World, AActor* Owner, AActor* Target,
        const FVector& Start, ECollisionChannel Channel = ECC_Visibility);
}
