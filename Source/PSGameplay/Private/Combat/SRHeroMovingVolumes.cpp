#include "Combat/SRHeroGameplayActors.h"
#include "Abilities/SRHeroGameplayLibrary.h"
#include "Abilities/SRGameplayTags.h"
#include "Character/PSBaseCharacter.h"
#include "Combat/SRGameplayQueries.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "DrawDebugHelpers.h"
#include "Debug/SRDebug.h"

ASRHeroMovingVolume::ASRHeroMovingVolume()
{
    bReplicates = true; SetReplicateMovement(true);
    PrimaryActorTick.bCanEverTick = true;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("VolumeRoot")));
    Geometry = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VisionGeometry"));
    Geometry->SetupAttachment(RootComponent);
    Geometry->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Geometry->SetHiddenInGame(true);
}
void ASRHeroMovingVolume::Initialize(APSBaseCharacter* InCaster, const FSRHeroActionConfig& InConfig, const FVector& InDirection)
{
    if (!HasAuthority()) return;
    Caster = InCaster; SetOwner(InCaster); Config = InConfig; Direction = InDirection.GetSafeNormal();
    SetActorRotation(Direction.Rotation());
    bInitialized = true;
    SetLifeSpan(Config.Range / FMath::Max(1.f, Config.Speed) + 0.5f);
}
void ASRHeroMovingVolume::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!Caster) return;
    const bool bCapsule = Config.Action == ESRHeroAction::RiftDagger;
    const FVector Extent(30.f, Config.Width * 0.5f, Config.Height * 0.5f);
    const FQuat Rotation = bCapsule ? FQuat::FindBetweenNormals(FVector::UpVector, Direction) : Direction.ToOrientationQuat();
    const FCollisionShape Shape = bCapsule ? FCollisionShape::MakeCapsule(Config.Radius, FMath::Max(Config.Radius, Config.Height * 0.5f))
        : FCollisionShape::MakeBox(Extent);
    if (Config.Action == ESRHeroAction::TidalCurtain)
    {
        // Opaque placeholder geometry is intentional vision gameplay; it has no attack collision.
        if (!Geometry->GetStaticMesh()) Geometry->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
        Geometry->SetHiddenInGame(false);
        Geometry->SetRelativeScale3D(FVector(0.5f, Config.Width / 100.f, Config.Height / 100.f));
    }
#if !UE_BUILD_SHIPPING
    if (Config.bDrawDebug || SRDebug::IsEnabled(SRDebug::ECategory::Ability))
    {
        if (bCapsule) DrawDebugCapsule(GetWorld(), GetActorLocation(), FMath::Max(Config.Radius, Config.Height * 0.5f), Config.Radius, Rotation, FColor::Magenta, false, 0.05f);
        else DrawDebugBox(GetWorld(), GetActorLocation(), Extent, Rotation, FColor::Cyan, false, 0.05f);
    }
#endif
    if (!HasAuthority() || !bInitialized) return;
    const float Distance = FMath::Min(Config.Speed * DeltaSeconds, Config.Range - Traveled);
    const FVector Start = GetActorLocation();
    FVector End = Start + Direction * Distance;
    if (Config.Action == ESRHeroAction::FaultlineTitan)
    {
        FVector Ground;
        if (!SRHeroGameplay::Ground(GetWorld(), Caster, End, Ground) || FMath::Abs(Ground.Z + Extent.Z + 3.f - Start.Z) > 180.f)
        { Destroy(); return; }
        End.Z = Ground.Z + Extent.Z + 3.f;
    }
    bool bBlocked = false;
    if (!bCapsule)
    {
        FCollisionQueryParams Params(SCENE_QUERY_STAT(SRMovingAttackCover), false, Caster);
        Params.AddIgnoredActor(this);
        for (TActorIterator<AActor> It(GetWorld()); It; ++It)
            if ((SRHeroGameplay::IsDamageable(*It) && !It->IsA<ASRHeroBarrier>())
                || (Config.Action == ESRHeroAction::FaultlineTitan && It->IsA<ASRHeroBarrier>() && SRHeroGameplay::SameTeam(Caster, *It)))
                Params.AddIgnoredActor(*It);
        FHitResult Wall;
        bBlocked = GetWorld()->SweepSingleByChannel(Wall, Start, End, Rotation, ECC_Visibility, Shape, Params);
        if (bBlocked) End = FMath::Lerp(Start, End, FMath::Max(0.f, Wall.Time - 0.01f));
    }
    TArray<AActor*> Targets;
    if (!bCapsule) SRGameplayQueries::SweepBoxTargets(GetWorld(), Caster, Start, End, Extent, Rotation, Targets);
    else
    {
        FCollisionObjectQueryParams Objects; Objects.AddObjectTypesToQuery(ECC_Pawn); Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
        FCollisionQueryParams Params(SCENE_QUERY_STAT(SRPiercingCapsule), false, Caster);
        Params.bFindInitialOverlaps = true;
        TArray<FHitResult> Hits;
        GetWorld()->SweepMultiByObjectType(Hits, Start, End, Rotation, Objects, Shape, Params);
        for (const FHitResult& Hit : Hits) if (AActor* Target = Hit.GetActor()) Targets.AddUnique(Target);
    }
    const FVector Travel = End - Start;
    for (AActor* Target : Targets)
    {
        if (Config.Action == ESRHeroAction::TidalMend)
        {
            if (SRHeroGameplay::SameTeam(Caster, Target) && !HitActors.Contains(Target))
            { if (SRHeroGameplay::Heal(Caster, Target, Config.Healing)) HitActors.Add(Target); }
            else if (SRHeroGameplay::IsEnemy(Caster, Target) && SRHeroGameplay::Cover(Caster, Target, Start))
                SRHeroGameplay::MoveSafely(Target, Travel);
            continue;
        }
        if (HitActors.Contains(Target) || !SRHeroGameplay::IsEnemy(Caster, Target)) continue;
        if (!bCapsule && !SRHeroGameplay::Cover(Caster, Target, Start)) continue;
        HitActors.Add(Target);
        SRHeroGameplay::Damage(Caster, Target, Config.Damage);
        if (Config.Action == ESRHeroAction::RiftReaper)
            SRHeroGameplay::Status(Target, Caster, SRGameplayTags::Tag_State_Rooted, Config.EffectDuration);
        if (Config.Action == ESRHeroAction::TidalCurtain)
        {
            SRHeroGameplay::Status(Target, Caster, SRGameplayTags::Tag_State_Stunned, Config.StunDuration);
            SRHeroGameplay::Status(Target, Caster, SRGameplayTags::Tag_State_Nearsighted, Config.VisionDuration);
        }
        if (Config.Action == ESRHeroAction::FaultlineTitan)
        {
            SRHeroGameplay::Push(Target, Direction, Config.Knockback, Config.LaunchHeight);
            FVector Ground;
            if (SRHeroGameplay::Ground(GetWorld(), Caster, Target->GetActorLocation(), Ground))
            {
                FSRHeroActionConfig Cage = Config; Cage.Action = ESRHeroAction::RootCage;
                Cage.Radius = 250.f; Cage.Height = 320.f; Cage.EffectDuration = 0.f;
                ASRHeroField::Spawn(GetWorld(), Caster, Ground, Cage);
            }
        }
    }
    SetActorLocation(End); Traveled += Travel.Size();
    if (bBlocked || Traveled >= Config.Range - 1.f || Distance <= 0.f) Destroy();
}
void ASRHeroMovingVolume::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ASRHeroMovingVolume, Config); DOREPLIFETIME(ASRHeroMovingVolume, Caster); DOREPLIFETIME(ASRHeroMovingVolume, Direction);
}
