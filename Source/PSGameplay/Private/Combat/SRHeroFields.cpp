#include "Combat/SRHeroGameplayActors.h"
#include "Abilities/SRHeroGameplayLibrary.h"
#include "Abilities/SRGameplayTags.h"
#include "Character/PSBaseCharacter.h"
#include "Character/SRHeroGameplayComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/OverlapResult.h"
#include "Net/UnrealNetwork.h"
#include "DrawDebugHelpers.h"
#include "Debug/SRDebug.h"

ASRHeroField::ASRHeroField()
{
    bReplicates = true;
    PrimaryActorTick.bCanEverTick = true; PrimaryActorTick.TickInterval = 0.05f;
    Collision = CreateDefaultSubobject<USphereComponent>(TEXT("FieldBody"));
    Collision->InitSphereRadius(50.f); Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SetRootComponent(Collision);
    FieldGeometry = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GameplayMarker"));
    FieldGeometry->SetupAttachment(Collision); FieldGeometry->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    FieldGeometry->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")));
    FieldGeometry->SetHiddenInGame(true);
}
ASRHeroField* ASRHeroField::Spawn(UWorld* World, APSBaseCharacter* InCaster, const FVector& Ground, const FSRHeroActionConfig& InConfig)
{
    if (!World || !InCaster || !InCaster->HasAuthority()) return nullptr;
    FActorSpawnParameters Params; Params.Owner = InCaster; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ASRHeroField* Field = World->SpawnActor<ASRHeroField>(Ground + FVector(0,0,3), FRotator::ZeroRotator, Params);
    if (Field) Field->Initialize(InCaster, InConfig);
    return Field;
}
void ASRHeroField::Initialize(APSBaseCharacter* InCaster, const FSRHeroActionConfig& InConfig)
{
    if (!HasAuthority()) return;
    Caster = InCaster; SetOwner(InCaster); Config = InConfig; Health = Config.StructureHealth;
    const bool bAlreadyInitialized = bInitialized;
    Elapsed = Accumulator = 0.f; bInitialized = true; bImpactResolved = false;
    const bool bWarning = Config.Action == ESRHeroAction::PhoenixMeteorfall || Config.Action == ESRHeroAction::PrismSkyfall;
    SetLifeSpan(bWarning ? Config.WarningDuration + 0.5f : FMath::Max(0.1f, Config.Duration));
    if (Config.Action == ESRHeroAction::HealingSeedling || Config.Action == ESRHeroAction::LastSeed)
    {
        if (!bAlreadyInitialized) AddActorWorldOffset(FVector(0,0,50), false);
        Collision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        Collision->SetCollisionObjectType(ECC_WorldDynamic);
        Collision->SetCollisionResponseToAllChannels(ECR_Block);
        Collision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
    }
    if (IsTimeBubble()) GetWorld()->GetSubsystem<USRProjectileTimeSubsystem>()->RegisterBubble(this);
    if (Config.Action == ESRHeroAction::VoidSnare || Config.Action == ESRHeroAction::CrystalBind || Config.Action == ESRHeroAction::TimeFracture)
    {
        Collision->SetSphereRadius(Config.Radius);
        Collision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        Collision->SetCollisionObjectType(ECC_WorldDynamic);
        Collision->SetCollisionResponseToAllChannels(ECR_Ignore);
        Collision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
        Collision->SetGenerateOverlapEvents(true);
        Collision->OnComponentBeginOverlap.AddUniqueDynamic(this, &ThisClass::OnFieldEntry);
    }
    BuildStructures();
    if (!bWarning) Evaluate(0.f);
    ForceNetUpdate();
}
void ASRHeroField::BuildStructures()
{
    if (Config.Action == ESRHeroAction::RootCage)
    {
        TArray<FTransform> Transforms;
        if (!SRHeroGameplay::BarrierFormation(Caster, GetActorLocation(), Config, Transforms)) { Destroy(); return; }
        FSRHeroActionConfig Section = Config;
        Section.Width = 2.f * Config.Radius * FMath::Tan(PI / Transforms.Num()) + 4.f;
        for (const FTransform& Transform : Transforms)
        {
            FActorSpawnParameters Params; Params.Owner = Caster;
            ASRHeroBarrier* Wall = GetWorld()->SpawnActor<ASRHeroBarrier>(ASRHeroBarrier::StaticClass(), Transform, Params);
            if (Wall) { Wall->Initialize(Caster, Section); Children.Add(Wall); }
        }
    }
    if (Config.Action == ESRHeroAction::WorldseedDominion)
    {
        const int32 Count = FMath::Clamp(Config.Segments, 3, 8);
        for (int32 Index = 0; Index < Count; ++Index)
        {
            const float Angle = Index * 2.f * PI / Count;
            FVector Ground;
            if (!SRHeroGameplay::Ground(GetWorld(), Caster, GetActorLocation()
                + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * Config.Radius * 0.45f, Ground)) continue;
            FSRHeroActionConfig Plant = Config; Plant.Action = ESRHeroAction::HealingSeedling; Plant.Healing = 0.f; Plant.Radius = 0.f;
            if (ASRHeroField* Seed = Spawn(GetWorld(), Caster, Ground, Plant)) Children.Add(Seed);
            FSRHeroActionConfig Cover = Config; Cover.Action = ESRHeroAction::BulwarkPlate; Cover.Width = 300.f; Cover.Height = 320.f;
            TArray<FTransform> Placement;
            if (SRHeroGameplay::BarrierFormation(Caster, Ground + FVector(0,0,0), Cover, Placement))
            {
                FActorSpawnParameters Params; Params.Owner = Caster;
                ASRHeroBarrier* Wall = GetWorld()->SpawnActor<ASRHeroBarrier>(ASRHeroBarrier::StaticClass(), Placement[0], Params);
                if (Wall) { Wall->Initialize(Caster, Cover); Children.Add(Wall); }
            }
        }
    }
}
void ASRHeroField::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds); Elapsed += DeltaSeconds;
    const bool bGameplayWarning = Config.Action == ESRHeroAction::PhoenixMeteorfall || Config.Action == ESRHeroAction::PrismSkyfall;
    const bool bPlant = Config.Action == ESRHeroAction::HealingSeedling || Config.Action == ESRHeroAction::LastSeed;
    FieldGeometry->SetHiddenInGame(!Caster || (!bGameplayWarning && !bPlant));
    if (bGameplayWarning) FieldGeometry->SetRelativeScale3D(FVector(Config.Radius / 50.f, Config.Radius / 50.f, 0.02f));
    else if (bPlant) FieldGeometry->SetRelativeScale3D(Config.Action == ESRHeroAction::LastSeed ? FVector(1.5f,1.5f,3.f) : FVector(0.65f,0.65f,1.f));
#if !UE_BUILD_SHIPPING
    const bool bWarning = Config.Action == ESRHeroAction::PhoenixMeteorfall || Config.Action == ESRHeroAction::PrismSkyfall;
    if (Caster && (bWarning || Config.bDrawDebug || SRDebug::IsEnabled(SRDebug::ECategory::Ability)))
    {
        DrawDebugCircle(GetWorld(), GetActorLocation(), Config.Radius, 40, bWarning ? FColor::Orange : FColor::Green,
            false, 0.07f, 0, 3.f, FVector::ForwardVector, FVector::RightVector, false);
        if (Config.Action == ESRHeroAction::ShardSpike)
            DrawDebugBox(GetWorld(), GetActorLocation() + FVector(0,0,Config.Height * 0.5f),
                FVector(Config.Radius, Config.Radius, Config.Height * 0.5f), FColor::Red, false, 0.07f);
    }
#endif
    if (!HasAuthority() || !bInitialized || IsTimeBubble()) return;
    if (Config.Action == ESRHeroAction::Undertow) { Evaluate(DeltaSeconds); return; }
    Accumulator += DeltaSeconds;
    if (Accumulator >= FMath::Max(0.05f, Config.Interval))
    { const float Step = Accumulator; Accumulator = 0.f; Evaluate(Step); }
}
void ASRHeroField::OnFieldEntry(UPrimitiveComponent*, AActor*, UPrimitiveComponent*, int32, bool, const FHitResult&)
{ if (HasAuthority() && bInitialized) Evaluate(0.f); }
void ASRHeroField::Evaluate(float Step)
{
    if (!Caster || !HasAuthority()) return;
    if (Config.Action == ESRHeroAction::RootCage && Elapsed > 0.1f) return;
    const bool bStrike = Config.Action == ESRHeroAction::PhoenixMeteorfall || Config.Action == ESRHeroAction::PrismSkyfall;
    if (bStrike && (Elapsed < Config.WarningDuration || bImpactResolved)) return;
    if (bStrike) bImpactResolved = true;
    TArray<AActor*> Targets;
    if (Config.Action == ESRHeroAction::ShardSpike)
    {
        TArray<FOverlapResult> Hits;
        GetWorld()->OverlapMultiByObjectType(Hits, GetActorLocation() + FVector(0,0,Config.Height * 0.5f), FQuat::Identity,
            FCollisionObjectQueryParams::AllDynamicObjects, FCollisionShape::MakeBox(FVector(Config.Radius, Config.Radius, Config.Height * 0.5f)));
        for (const FOverlapResult& Hit : Hits) if (AActor* Target = Hit.GetActor()) Targets.AddUnique(Target);
    }
    else Targets = SRHeroGameplay::InRadius(Caster, GetActorLocation(), Config.Radius);
    for (AActor* Target : Targets)
    {
        const bool bAlly = SRHeroGameplay::SameTeam(Caster, Target);
        const bool bEnemy = SRHeroGameplay::IsEnemy(Caster, Target);
        if (bStrike)
        {
            if (!bEnemy) continue;
            FCollisionQueryParams Params(SCENE_QUERY_STAT(SRAerialCover), false, Caster); Params.AddIgnoredActor(Target); Params.AddIgnoredActor(this);
            FHitResult Roof;
            if (GetWorld()->LineTraceSingleByChannel(Roof, Target->GetActorLocation() + FVector(0,0,20000),
                Target->GetActorLocation(), ECC_Visibility, Params)) continue;
            const float Alpha = FMath::Clamp(FVector::Dist2D(GetActorLocation(), Target->GetActorLocation()) / Config.Radius, 0.f, 1.f);
            SRHeroGameplay::Damage(Caster, Target, FMath::Lerp(Config.Damage, Config.EdgeDamage, Alpha));
            continue;
        }
        if (Config.Action == ESRHeroAction::HealingSeedling || Config.Action == ESRHeroAction::LastSeed
            || Config.Action == ESRHeroAction::DawnIntervention || Config.Action == ESRHeroAction::WorldseedDominion)
        {
            if (bAlly && SRHeroGameplay::IsAlive(Target)
                && (Config.Action == ESRHeroAction::WorldseedDominion || SRHeroGameplay::Cover(this, Target, GetActorLocation())))
            {
                bool bInHealingRegion = Config.Action != ESRHeroAction::WorldseedDominion;
                if (!bInHealingRegion)
                    for (const auto& Child : Children)
                        if (Child.IsValid() && Child->IsA<ASRHeroField>()
                            && FVector::Dist2D(Child->GetActorLocation(), Target->GetActorLocation()) < Config.Radius * 0.4f
                            && SRHeroGameplay::Cover(Child.Get(), Target, Child->GetActorLocation()))
                        { bInHealingRegion = true; break; }
                if (bInHealingRegion)
                {
                    if (Config.Action != ESRHeroAction::DawnIntervention && Step > 0.f) SRHeroGameplay::Heal(Caster, Target, Config.Healing * Step);
                    if (Config.DamageTakenScale < 1.f)
                        if (auto* State = Target->FindComponentByClass<USRHeroGameplayComponent>()) State->RefreshAura(this, Config);
                }
            }
            if (Config.Action == ESRHeroAction::WorldseedDominion && bEnemy && !Affected.Contains(Target))
            {
                for (const auto& Child : Children)
                    if (Child.IsValid() && Child->IsA<ASRHeroField>())
                    {
                        const FVector Closest = FMath::ClosestPointOnSegment(Target->GetActorLocation(), GetActorLocation(), Child->GetActorLocation());
                        const float Feet = Target->GetActorLocation().Z - Target->GetSimpleCollisionHalfHeight();
                        if (FVector::Dist2D(Closest, Target->GetActorLocation()) < 100.f
                            && FMath::Abs(Feet - Closest.Z) < 100.f && SRHeroGameplay::Cover(this, Target, Closest + FVector(0,0,30)))
                        {
                            SRHeroGameplay::Status(Target, Caster, SRGameplayTags::Tag_State_Rooted, Config.EffectDuration);
                            Affected.Add(Target); break;
                        }
                    }
            }
            continue;
        }
        if (!bEnemy || !SRHeroGameplay::Cover(this, Target, GetActorLocation() + FVector(0,0,40))) continue;
        if (Config.Action == ESRHeroAction::Undertow)
        {
            const FVector Delta = (GetActorLocation() - Target->GetActorLocation()).GetSafeNormal2D() * Config.Speed * Step;
            SRHeroGameplay::MoveSafely(Target, Delta); continue;
        }
        if (Config.Action == ESRHeroAction::ShardSpike)
        {
            const ACharacter* Character = Cast<ACharacter>(Target);
            const float Feet = Target->GetActorLocation().Z - Target->GetSimpleCollisionHalfHeight();
            if (Character && !Character->GetCharacterMovement()->IsMovingOnGround()) continue;
            if (Feet > GetActorLocation().Z + Config.Height || FVector::Dist2D(GetActorLocation(), Target->GetActorLocation()) > Config.Radius || Step <= 0.f) continue;
            SRHeroGameplay::Damage(Caster, Target, Config.Damage); continue;
        }
        if (Affected.Contains(Target)) continue;
        if (Config.Action == ESRHeroAction::TimeFracture)
        {
            SRHeroGameplay::Buff(Target, Caster, Config, Config.EffectDuration); Affected.Add(Target);
        }
        else if ((Config.Action == ESRHeroAction::VoidSnare || Config.Action == ESRHeroAction::RootCage
            || Config.Action == ESRHeroAction::CrystalBind) && Config.EffectDuration > 0.f)
        {
            SRHeroGameplay::Status(Target, Caster, SRGameplayTags::Tag_State_Rooted, Config.EffectDuration); Affected.Add(Target);
        }
    }
}
float ASRHeroField::TakeDamage(float Amount, const FDamageEvent&, AController*, AActor* Causer)
{
    if (!HasAuthority() || (Config.Action != ESRHeroAction::HealingSeedling && Config.Action != ESRHeroAction::LastSeed)
        || Amount <= 0.f || !FMath::IsFinite(Amount) || SRHeroGameplay::SameTeam(Caster, Causer)) return 0.f;
    const float Applied = FMath::Min(Amount, Health); Health -= Applied; ForceNetUpdate();
    if (Health <= 0.f) Destroy();
    return Applied;
}
void ASRHeroField::EndPlay(const EEndPlayReason::Type Reason)
{
    if (HasAuthority())
    {
        if (IsTimeBubble()) GetWorld()->GetSubsystem<USRProjectileTimeSubsystem>()->UnregisterBubble(this);
        for (const auto& Child : Children) if (Child.IsValid()) Child->Destroy();
    }
    Super::EndPlay(Reason);
}
void ASRHeroField::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ASRHeroField, Config); DOREPLIFETIME(ASRHeroField, Caster); DOREPLIFETIME(ASRHeroField, Health);
}
