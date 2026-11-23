#include "Combat/SRVoidAreaEffects.h"
#include "Debug/SRDebug.h"
#include "Combat/SRCombatComponent.h"
#include "Character/PSBaseCharacter.h"
#include "Character/SRCombatDummy.h"
#include "GAS/PSPlayerState.h"
#include "GAS/PSAttributeSet.h"
#include "GAS/PSAttributeLibrary.h"
#include "Abilities/SRGameplayTags.h"
#include "Abilities/SRGameplayEffects.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "TimerManager.h"

namespace
{
// Enemies are pawns and combat dummies; world geometry never roots.
bool IsRootableEnemy(const AActor* Candidate, const AActor* Caster)
{
    if (!Candidate || Candidate == Caster) return false;
    const bool bCharacter = Cast<APSBaseCharacter>(Candidate) != nullptr;
    const bool bDummy = Cast<APSCombatDummy>(Candidate) != nullptr;
    return bCharacter || bDummy;
}

// Team filter mirrors SRCombatComponent::ApplyCombatDamage: same-team
// PlayerStates never affect each other (dummies carry no PlayerState and are
// treated as enemies).
bool IsEnemyOf(const AActor* Candidate, const AActor* Caster)
{
    const APawn* CandidatePawn = Cast<APawn>(Candidate);
    const APawn* CasterPawn = Cast<APawn>(Caster);
    const APSPlayerState* CandidateState = CandidatePawn ? CandidatePawn->GetPlayerState<APSPlayerState>() : nullptr;
    const APSPlayerState* CasterState = CasterPawn ? CasterPawn->GetPlayerState<APSPlayerState>() : nullptr;
    if (CandidateState && CasterState && CandidateState != CasterState)
    {
        return CandidateState->TeamId != CasterState->TeamId;
    }
    return CandidateState == nullptr && CasterState == nullptr; // unaffiliated actors (dummies) are enemies
}

void ApplyRootGE(AActor* Target, float RootDuration)
{
    UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target);
    if (!TargetASC || RootDuration <= 0.f) return;

    FGameplayEffectSpecHandle Spec = TargetASC->MakeOutgoingSpec(UGE_SRRoot::StaticClass(), 1.f, TargetASC->MakeEffectContext());
    if (!Spec.IsValid()) return;
    Spec.Data->SetSetByCallerMagnitude(SRGameplayTags::Tag_Data_RootDuration, RootDuration);
    TargetASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
}
}

ASRVoidSnareField::ASRVoidSnareField()
{
    PrimaryActorTick.bCanEverTick = false;
    bReplicates = true;
    SetReplicateMovement(false);

    FieldSphere = CreateDefaultSubobject<USphereComponent>(TEXT("SnareSphere"));
    FieldSphere->InitSphereRadius(400.f);
    FieldSphere->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
    SetRootComponent(FieldSphere);

    FieldVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SnareVisual"));
    FieldVisual->SetupAttachment(FieldSphere);
    FieldVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    FieldVisual->SetIsReplicated(false);
}

void ASRVoidSnareField::InitializeField(AActor* InCaster, float InRadius, float InLifetime, float InRootDuration)
{
    CasterActor = InCaster;
    RootDuration = InRootDuration;
    bInitialized = true;
    FieldSphere->SetSphereRadius(InRadius);

    // Replicated proxy spheres size from the replicated sphere radius.
    FieldSphere->SetGenerateOverlapEvents(true);
    FieldSphere->OnComponentBeginOverlap.AddDynamic(this, &ASRVoidSnareField::OnOverlapBegin);

    // Current occupants root immediately, then later entrants through overlap.
    TArray<FOverlapResult> Overlaps;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(SRSnareInitial), false);
    if (InCaster) Params.AddIgnoredActor(InCaster);
    GetWorld()->OverlapMultiByObjectType(Overlaps, GetActorLocation(), FQuat::Identity,
        FCollisionObjectQueryParams::InitType::AllDynamicObjects, FCollisionShape::MakeSphere(InRadius), Params);

    for (const FOverlapResult& Overlap : Overlaps)
    {
        AActor* Candidate = Overlap.GetActor();
        if (Candidate && IsRootableEnemy(Candidate, InCaster) && IsEnemyOf(Candidate, InCaster))
        {
            RootEnemy(Candidate);
        }
    }

    SetLifeSpan(InLifetime);
}

void ASRVoidSnareField::BeginPlay()
{
    Super::BeginPlay();
    // Replicated proxies re-register overlap so later entrants root on clients
    // too (cosmetic), while the authoritative root stays server-side.
    FieldSphere->SetGenerateOverlapEvents(true);
    if (!HasAuthority() && bInitialized)
    {
        FieldSphere->OnComponentBeginOverlap.AddDynamic(this, &ASRVoidSnareField::OnOverlapBegin);
    }
}

void ASRVoidSnareField::OnOverlapBegin(UPrimitiveComponent*, AActor* OtherActor,
    UPrimitiveComponent*, int32, bool, const FHitResult&)
{
    if (!HasAuthority() || !bInitialized || !OtherActor) return;
    if (RootedActors.Contains(OtherActor)) return;
    if (!IsRootableEnemy(OtherActor, CasterActor.Get()) || !IsEnemyOf(OtherActor, CasterActor.Get())) return;
    RootEnemy(OtherActor);
}

void ASRVoidSnareField::RootEnemy(AActor* Enemy)
{
    // One root per enemy per field lifetime.
    RootedActors.Add(Enemy);
    ApplyRootGE(Enemy, RootDuration);
}

ASRVoidReaperWave::ASRVoidReaperWave()
{
    PrimaryActorTick.bCanEverTick = true;
    bReplicates = true;
    SetReplicateMovement(false);

    WaveSphere = CreateDefaultSubobject<USphereComponent>(TEXT("WaveSphere"));
    WaveSphere->InitSphereRadius(800.f);
    WaveSphere->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
    SetRootComponent(WaveSphere);

    WaveVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WaveVisual"));
    WaveVisual->SetupAttachment(WaveSphere);
    WaveVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    WaveVisual->SetIsReplicated(false);
}

void ASRVoidReaperWave::InitializeWave(AActor* InCaster, const FVector& InDirection, float InSpeed,
    float InMaxDistance, float InRadius, float InDamage, float InRootDuration)
{
    CasterActor = InCaster;
    WaveDirection = InDirection.GetSafeNormal2D();
    WaveSpeed = InSpeed;
    MaxTravelDistance = InMaxDistance;
    WaveRadius = InRadius;
    WaveDamage = InDamage;
    RootDuration = InRootDuration;
    TraveledDistance = 0.f;
    bInitialized = true;
    WaveSphere->SetSphereRadius(InRadius);
    SetActorRotation(WaveDirection.Rotation());

    SetActorTickEnabled(true);
}

void ASRVoidReaperWave::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!HasAuthority() || !bInitialized) return;
    AdvanceWave(DeltaSeconds);
}

void ASRVoidReaperWave::AdvanceWave(float DeltaSeconds)
{
    const float StepDistance = WaveSpeed * DeltaSeconds;
    const FVector OldLocation = GetActorLocation();
    const FVector NewLocation = OldLocation + WaveDirection * StepDistance;

    // World obstruction: a forward trace stops the wave at solid cover.
    FHitResult Obstruction;
    FCollisionQueryParams WallParams(SCENE_QUERY_STAT(SRReaperWave), false);
    if (CasterActor.Get()) WallParams.AddIgnoredActor(CasterActor.Get());
    const bool bBlocked = GetWorld()->LineTraceSingleByChannel(Obstruction, OldLocation,
        NewLocation + WaveDirection * WaveRadius, ECC_WorldStatic, WallParams);

    const float Step = bBlocked ? FVector::Dist(OldLocation, Obstruction.ImpactPoint) : StepDistance;
    SetActorLocation(OldLocation + WaveDirection * Step);
    TraveledDistance += Step;

    // Swept sphere query between the old and new center so fast steps cannot
    // skip over enemies standing between updates.
    TArray<FOverlapResult> Overlaps;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(SRReaperSweep), false);
    if (CasterActor.Get()) Params.AddIgnoredActor(CasterActor.Get());
    const FVector SweepStart = OldLocation;
    const FVector SweepEnd = NewLocation + WaveDirection * WaveRadius;
    GetWorld()->OverlapMultiByObjectType(Overlaps, (SweepStart + SweepEnd) * 0.5f, FQuat::Identity,
        FCollisionObjectQueryParams::InitType::AllDynamicObjects,
        FCollisionShape::MakeSphere(FMath::Max(WaveRadius, FVector::Dist(SweepStart, SweepEnd) * 0.5f)), Params);

    const FVector SweepDir = (SweepEnd - SweepStart).GetSafeNormal();
    for (const FOverlapResult& Overlap : Overlaps)
    {
        AActor* Candidate = Overlap.GetActor();
        if (!Candidate || AffectedActors.Contains(Candidate)) continue;
        if (!IsRootableEnemy(Candidate, CasterActor.Get()) || !IsEnemyOf(Candidate, CasterActor.Get())) continue;

        // Point-to-segment distance keeps the 16m-wide region honest for big actors.
        const FVector Closest = FMath::ClosestPointOnSegment(Candidate->GetActorLocation(), SweepStart, SweepEnd);
        if (FVector::Dist(Candidate->GetActorLocation(), Closest) > WaveRadius + 100.f) continue;

        ApplyWaveToEnemy(Candidate, Candidate->GetActorLocation());
    }

    if (bBlocked || TraveledDistance >= MaxTravelDistance)
    {
        Destroy();
        return;
    }
}

void ASRVoidReaperWave::ApplyWaveToEnemy(AActor* Enemy, const FVector& HitLocation)
{
    // Strictly one damage + one root per enemy per wave.
    if (AffectedActors.Contains(Enemy)) return;
    AffectedActors.Add(Enemy);

    bool bEnemy = false;
    float Applied = 0.f;
    if (APSBaseCharacter* Hero = Cast<APSBaseCharacter>(CasterActor.Get()))
    {
        if (UPSCombatComponent* Combat = Hero->GetCombatComponent())
        {
            Applied = Combat->ApplyCombatDamage(Enemy, WaveDamage, CasterActor.Get(), bEnemy);
        }
    }
    ApplyRootGE(Enemy, RootDuration);

    SRDebug::Message(SRDebug::ECategory::Damage, TEXT("Server"),
        FString::Printf(TEXT("[VEYRA][REAPER] %s -> %s for %.0f (root %.1fs)"), *GetNameSafe(this), *GetNameSafe(Enemy), Applied, RootDuration), 0.5f, FColor::Magenta);
}
