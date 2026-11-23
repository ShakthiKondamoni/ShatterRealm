#include "Combat/SRHeroGameplayActors.h"
#include "Abilities/SRHeroGameplayLibrary.h"
#include "Character/PSBaseCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"

ASRHeroBarrier::ASRHeroBarrier()
{
    bReplicates = true; SetReplicateMovement(true);
    PrimaryActorTick.bCanEverTick = true;
    Collision = CreateDefaultSubobject<UBoxComponent>(TEXT("BarrierCollision"));
    SetRootComponent(Collision);
    Collision->SetMobility(EComponentMobility::Movable);
    Geometry = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BarrierGeometry"));
    Geometry->SetupAttachment(Collision);
    Geometry->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}
void ASRHeroBarrier::Initialize(APSBaseCharacter* InCaster, const FSRHeroActionConfig& InConfig)
{
    if (!HasAuthority()) return;
    Caster = InCaster; SetOwner(InCaster); Config = InConfig; Health = Config.StructureHealth;
    SetLifeSpan(FMath::Max(0.1f, Config.Duration));
    OnRep_Config(); ForceNetUpdate();
}
void ASRHeroBarrier::OnRep_Config()
{
    Collision->SetBoxExtent(FVector(25.f, Config.Width * 0.5f, Config.Height * 0.5f));
    Collision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Collision->SetCollisionObjectType(ECC_WorldStatic);
    Collision->SetCollisionResponseToAllChannels(ECR_Block);
    if (Config.Action == ESRHeroAction::GlassScreen || Config.Action == ESRHeroAction::MistVeil)
    {
        Collision->SetCollisionResponseToAllChannels(ECR_Ignore);
        Collision->SetCollisionResponseToChannel(Config.Action == ESRHeroAction::GlassScreen ? ECC_Pawn : ECC_WorldDynamic, ECR_Block);
    }
    const bool bGlass = Config.Action == ESRHeroAction::GlassScreen;
    Geometry->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, bGlass
        ? TEXT("/Engine/BasicShapes/Plane.Plane") : TEXT("/Engine/BasicShapes/Cube.Cube")));
    Geometry->SetRelativeRotation(bGlass ? FRotator(90,0,0) : FRotator::ZeroRotator);
    Geometry->SetRelativeScale3D(bGlass ? FVector(Config.Height / 100.f, Config.Width / 100.f, 1.f)
        : FVector(0.5f, Config.Width / 100.f, Config.Height / 100.f));
    if (bGlass)
        if (UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/ProjectS/Gameplay/M_OneWayScreen.M_OneWayScreen")))
            Geometry->SetMaterial(0, Material);
}
bool ASRHeroBarrier::CanPlace(UWorld* World, APSBaseCharacter* Caster, const FVector& Ground,
    const FRotator& Rotation, float Width, float Height)
{
    if (!World || !Caster || Width <= 0.f || Height <= 0.f) return false;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(SRBarrierPlacement), false);
    // Keep the bottom clear of its supporting floor; include other player capsules.
    return !World->OverlapBlockingTestByChannel(Ground + FVector(0,0,Height * 0.5f + 3.f),
        Rotation.Quaternion(), ECC_Pawn, FCollisionShape::MakeBox(FVector(24.f, Width * 0.5f - 1.f, Height * 0.5f - 1.f)), Params);
}
bool ASRHeroBarrier::Crosses(const FVector& Start, const FVector& End) const
{
    const FTransform Transform = Collision->GetComponentTransform();
    const FVector A = Transform.InverseTransformPosition(Start);
    const FVector B = Transform.InverseTransformPosition(End);
    const FVector Extent = Collision->GetUnscaledBoxExtent(), Delta = B - A;
    double Enter = 0., Exit = 1.;
    for (int32 Axis = 0; Axis < 3; ++Axis)
    {
        if (FMath::Abs(Delta[Axis]) < UE_SMALL_NUMBER)
        { if (A[Axis] < -Extent[Axis] || A[Axis] > Extent[Axis]) return false; continue; }
        double First = (-Extent[Axis] - A[Axis]) / Delta[Axis];
        double Last = (Extent[Axis] - A[Axis]) / Delta[Axis];
        if (First > Last) Swap(First, Last);
        Enter = FMath::Max(Enter, First); Exit = FMath::Min(Exit, Last);
        if (Enter > Exit) return false;
    }
    return true;
}
void ASRHeroBarrier::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    Elapsed += DeltaSeconds;
    if (Config.Action == ESRHeroAction::MistVeil)
    {
        const float CurrentHeight = Config.Height * FMath::Clamp(Elapsed / 0.75f, 0.02f, 1.f);
        const float OldHalf = Collision->GetUnscaledBoxExtent().Z;
        Collision->SetBoxExtent(FVector(25.f, Config.Width * 0.5f, CurrentHeight * 0.5f));
        Geometry->SetRelativeScale3D(FVector(0.5f, Config.Width / 100.f, CurrentHeight / 100.f));
        if (HasAuthority()) AddActorWorldOffset(FVector(0,0,CurrentHeight * 0.5f - OldHalf), false);
    }
    if (HasAuthority() && Config.Action == ESRHeroAction::IronVanguard && Traveled < Config.Range)
    {
        const float Distance = FMath::Min(Config.Speed * DeltaSeconds, Config.Range - Traveled);
        FHitResult Hit;
        const FVector Previous = GetActorLocation();
        SetActorLocation(Previous + GetActorForwardVector() * Distance, true, &Hit);
        Traveled += FVector::Dist(Previous, GetActorLocation());
        if (Hit.bBlockingHit) Config.Speed = 0.f; // Stop safely instead of crushing pawns against cover.
    }
}
float ASRHeroBarrier::TakeDamage(float Amount, const FDamageEvent&, AController*, AActor* Causer)
{
    if (!HasAuthority() || !FMath::IsFinite(Amount) || Amount <= 0.f || Config.StructureHealth <= 0.f
        || SRHeroGameplay::SameTeam(Caster, Causer)) return 0.f;
    const float Applied = FMath::Min(Health, Amount); Health -= Applied; ForceNetUpdate();
    if (Health <= 0.f) Destroy();
    return Applied;
}
void ASRHeroBarrier::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ASRHeroBarrier, Config); DOREPLIFETIME(ASRHeroBarrier, Health); DOREPLIFETIME(ASRHeroBarrier, Caster);
}
