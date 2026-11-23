#include "Combat/SRGameplayQueries.h"
#include "CollisionQueryParams.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

void SRGameplayQueries::SweepBoxTargets(UWorld* World, AActor* Owner, const FVector& Start,
    const FVector& End, const FVector& HalfExtent, const FQuat& Rotation,
    TArray<AActor*>& OutTargets, bool bDrawDebug)
{
    OutTargets.Reset();
    if (!World || !Owner || !Owner->HasAuthority() || Start.ContainsNaN() || End.ContainsNaN()
        || HalfExtent.ContainsNaN() || Rotation.ContainsNaN() || HalfExtent.GetMin() <= 0.f) return;
    FCollisionObjectQueryParams Objects;
    Objects.AddObjectTypesToQuery(ECC_Pawn);
    Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
    FCollisionQueryParams Params(SCENE_QUERY_STAT(SRWideAttack), false, Owner);
    Params.bFindInitialOverlaps = true;
    TArray<FHitResult> Hits;
    World->SweepMultiByObjectType(Hits, Start, End, Rotation, Objects,
        FCollisionShape::MakeBox(HalfExtent), Params);
    for (const FHitResult& Hit : Hits)
        if (AActor* Actor = Hit.GetActor(); Actor && Actor != Owner) OutTargets.AddUnique(Actor);
#if !UE_BUILD_SHIPPING
    if (bDrawDebug)
    {
        DrawDebugBox(World, Start, HalfExtent, Rotation, FColor::Cyan, false, 1.f);
        DrawDebugBox(World, End, HalfExtent, Rotation, FColor::Yellow, false, 1.f);
        for (int32 Y : {-1, 1})
            for (int32 Z : {-1, 1})
            {
                const FVector Offset = Rotation.RotateVector(FVector(0.f, HalfExtent.Y * Y, HalfExtent.Z * Z));
                DrawDebugLine(World, Start + Offset, End + Offset, FColor::Cyan, false, 1.f, 0, 1.f);
            }
    }
#endif
}

bool SRGameplayQueries::HasCoverFreePath(UWorld* World, AActor* Owner, AActor* Target,
    const FVector& Start, ECollisionChannel Channel)
{
    if (!World || !Owner || !Target) return false;
    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(SRAbilityCover), false, Owner);
    if (Owner->GetOwner()) Params.AddIgnoredActor(Owner->GetOwner());
    TArray<AActor*> Attached;
    Owner->GetAttachedActors(Attached);
    for (AActor* Actor : Attached) Params.AddIgnoredActor(Actor);
    return !World->LineTraceSingleByChannel(Hit, Start, Target->GetActorLocation(), Channel, Params)
        || Hit.GetActor() == Target;
}
