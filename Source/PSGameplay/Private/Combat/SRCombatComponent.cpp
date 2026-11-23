#include "Combat/SRCombatComponent.h"
#include "Combat/SRHeroGameplayActors.h"
#include "Abilities/SRHeroGameplayLibrary.h"
#include "Character/SRHeroGameplayComponent.h"
#include "Engine/DamageEvents.h"
#include "GameFramework/GameStateBase.h"
#include "Character/SRCombatDummy.h"
#include "Debug/SRDebug.h"
#include "Combat/SRElementalProjectile.h"
#include "Combat/SREquipmentComponent.h"
#include "Combat/SRCombatPresentationComponent.h"
#include "Abilities/SRGameplayTags.h"
#include "Combat/SRWeaponDataAsset.h"
#include "GAS/PSAttributeLibrary.h"
#include "GAS/PSAttributeSet.h"
#include "Character/PSBaseCharacter.h"
#include "GAS/PSPlayerState.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraFunctionLibrary.h"
#include "Kismet/GameplayStatics.h"

namespace
{
// Combat traces ignore the shooter and everything attached to it (equipment included).
void AddOwnerToIgnore(APSBaseCharacter* Character, FCollisionQueryParams& Params)
{
    if (Character)
    {
        Params.AddIgnoredActor(Character);
        TArray<UPrimitiveComponent*> PrimComponents;
        Character->GetComponents<UPrimitiveComponent>(PrimComponents);
        for (UPrimitiveComponent* Primitive : PrimComponents)
        {
            if (Primitive) Params.AddIgnoredComponent(Primitive);
        }
    }
}
}

UPSCombatComponent::UPSCombatComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(true);
}

void UPSCombatComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    // Charge details are owner-private; remote clients get presentation, not numbers.
    DOREPLIFETIME_CONDITION(UPSCombatComponent, ReplicatedState, COND_OwnerOnly);
}

void UPSCombatComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(FireTimerHandle);
        World->GetTimerManager().ClearTimer(RechargeTimerHandle);
    }
    StopFiring(TEXT("EndPlay"));
    Super::EndPlay(EndPlayReason);
}

APSBaseCharacter* UPSCombatComponent::GetOwnerCharacter() const { return Cast<APSBaseCharacter>(GetOwner()); }

UAbilitySystemComponent* UPSCombatComponent::GetOwnerASC() const
{
    APSBaseCharacter* Character = GetOwnerCharacter();
    return Character ? Character->GetAbilitySystemComponent() : nullptr;
}

APlayerController* UPSCombatComponent::GetOwnerController() const
{
    APSBaseCharacter* Character = GetOwnerCharacter();
    return Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
}

void UPSCombatComponent::SetWeaponData(USRWeaponDataAsset* NewWeaponData)
{
    // Never initialize authoritative ammo from an owning client.
    // Called at character BeginPlay and whenever a hero equips a different primary.
    if (GetOwnerRole() != ROLE_Authority)
    {
        WeaponData = NewWeaponData;
        return;
    }
    const bool bChanged = WeaponData != NewWeaponData;
    WeaponData = NewWeaponData;
    if (!WeaponData)
    {
        StopFiring(TEXT("Weapon unequipped"));
        CancelPrimaryRecharge(TEXT("Weapon unequipped"));
        ReplicatedState.PrimaryCharges = 0;
    }
    else if (bChanged || !bPrimaryChargesInitialized)
    {
        ResetFirePrediction(TEXT("weapon changed"));
        StopFiring(TEXT("Weapon configured"));
        CancelPrimaryRecharge(TEXT("Weapon configured"));
        ReplicatedState.PrimaryCharges = FMath::Max(1, WeaponData->MaxCharges);
        bPrimaryChargesInitialized = true;
        LastShotRealTime = 0.0;
        UE_LOG(LogTemp, Display, TEXT("[COMBAT] Initial charges %d/%d for %s"),
            ReplicatedState.PrimaryCharges, GetMaxCharges(), *GetNameSafe(GetOwner()));
    }
    SRDebug::NetLog(GetOwner(), FString::Printf(TEXT("Weapon init (server-authoritative): weapon=%s charges=%d/%d"),
        *GetNameSafe(WeaponData), ReplicatedState.PrimaryCharges, GetMaxCharges()));
    OnRep_ReplicatedState();
    if (AActor* Owner = GetOwner()) Owner->ForceNetUpdate();
}

int32 UPSCombatComponent::GetMaxCharges() const { return WeaponData ? WeaponData->MaxCharges : 0; }

ECollisionChannel UPSCombatComponent::GetTraceChannel() const
{
    return WeaponData ? static_cast<ECollisionChannel>(WeaponData->TraceChannel) : ECC_Visibility;
}

float UPSCombatComponent::GetEffectiveFireInterval() const
{
    const float BaseInterval = (WeaponData && WeaponData->FireRate > 0.f) ? (1.f / WeaponData->FireRate) : 0.2f;
    const bool bOvercharged = GetOwnerASC() ? GetOwnerASC()->GetTagCount(SRGameplayTags::Tag_Status_Buff_Overcharge) > 0 : false;
    const UPSAttributeSet* Attributes = GetOwnerASC() ? GetOwnerASC()->GetSet<UPSAttributeSet>() : nullptr;
    return BaseInterval / ((bOvercharged ? OverchargeFireRateMultiplier : 1.f) * (Attributes ? FMath::Max(0.05f, Attributes->GetFireRateMultiplier()) : 1.f));
}

float UPSCombatComponent::GetEffectiveRechargeDuration() const
{
    const float BaseDuration = WeaponData ? WeaponData->RechargeDuration : 1.8f;
    const bool bOvercharged = GetOwnerASC() ? GetOwnerASC()->GetTagCount(SRGameplayTags::Tag_Status_Buff_Overcharge) > 0 : false;
    const UPSAttributeSet* Attributes = GetOwnerASC() ? GetOwnerASC()->GetSet<UPSAttributeSet>() : nullptr;
    return BaseDuration / ((bOvercharged ? OverchargeRechargeMultiplier : 1.f) * (Attributes ? FMath::Max(0.05f, Attributes->GetRechargeRateMultiplier()) : 1.f));
}

float UPSCombatComponent::GetRechargeProgress01() const
{
    if (!ReplicatedState.bRecharging || ReplicatedState.EffectiveRechargeDuration <= 0.f) return 0.f;
    const UWorld* World = GetWorld();
    const AGameStateBase* GameState = World ? World->GetGameState<AGameStateBase>() : nullptr;
    if (!GameState) return 0.f;
    const float Elapsed = GameState->GetServerWorldTimeSeconds() - ReplicatedState.RechargeStartTime;
    return FMath::Clamp(Elapsed / ReplicatedState.EffectiveRechargeDuration, 0.f, 1.f);
}

bool UPSCombatComponent::CanAttack() const
{
    const APSBaseCharacter* Character = GetOwnerCharacter();
    const UAbilitySystemComponent* ASC = GetOwnerASC();
    return Character && !Character->IsPSDead() && WeaponData != nullptr && ASC
        && !ASC->HasMatchingGameplayTag(SRGameplayTags::Tag_State_Stunned)
        && !ASC->HasMatchingGameplayTag(SRGameplayTags::Tag_State_Downed);
}

void UPSCombatComponent::RequestStartPrimaryFire()
{
    SRDebug::Message(SRDebug::ECategory::Input, UEnum::GetDisplayValueAsText(GetOwnerRole()).ToString(), TEXT("LMB Pressed"));
    SRDebug::Log(SRDebug::ECategory::Input, TEXT("Client"), TEXT("LMB Pressed"));
    if (GetOwnerRole() == ROLE_Authority) { StartFiring(); return; }

    // Owning client: predict the first shot immediately (instant cosmetics, recoil,
    // ammo display), then submit aim + shot id. The server validates independently
    // and every hit/damage/region decision remains server-side.
    bLocalFireHeld = true;
    PredictShot();
}

void UPSCombatComponent::PredictShot()
{
    const APSBaseCharacter* Character = GetOwnerCharacter();
    if (!Character || !WeaponData || GetOwnerRole() == ROLE_Authority) return;
    if (Character->IsPSDead()) { StopPredictedFireTimer(); return; }
    if (ReplicatedState.bRecharging) { StopPredictedFireTimer(); return; }
    if (bLocalFireHeld == false && PendingPredictedShots.Num() == 0) return;

    // Predicted fire-rate gate: mirrors the server gate with the same weapon-
    // driven interval (Overcharge's replicated tag changes both in lockstep).
    const double Now = FPlatformTime::Seconds();
    const double Interval = GetEffectiveFireInterval();
    if (Now - LastPredictedShotRealTime < Interval - KINDA_SMALL_NUMBER)
    {
        // Called early: schedule the remaining wait instead of dropping the intent.
        if (bLocalFireHeld && GetDisplayCharges() > 0)
        {
            GetWorld()->GetTimerManager().SetTimer(PredictedFireTimerHandle, this,
                &UPSCombatComponent::ExecutePredictedShot,
                static_cast<float>(Interval - (Now - LastPredictedShotRealTime)), false);
        }
        return;
    }

    // Ammunition gate on the predicted view (authoritative minus in-flight).
    if (GetDisplayCharges() <= 0)
    {
        SRDebug::PredictionLog(Character, FString::Printf(TEXT("Predicted shot withheld: display charges exhausted (pending=%d)"), PendingPredictedShots.Num()));
        StopPredictedFireTimer();
        return;
    }

    FVector CameraOrigin;
    FVector AimDirection;
    if (!GetLocalAimView(CameraOrigin, AimDirection)) return;

    const int32 ClientShotId = ++NextPredictedShotId;
    LastPredictedShotRealTime = Now;

    // Bounded pending record: the confirmation pops it; a stale entry is trimmed
    // after 2s (server rejection or packet loss reconciliation).
    PendingPredictedShots.Add({ClientShotId, Now});
    if (PendingPredictedShots.Num() > 16)
    {
        PendingPredictedShots.RemoveAt(0);
        SRDebug::PredictionLog(Character, TEXT("Pending queue overflow: oldest predicted shot dropped (reconciled by authoritative charges)"));
    }

    // Instant local feedback: visual-only aim trace for the cosmetic impact.
    FHitResult VisualHit;
    FCollisionQueryParams VisualParams(SCENE_QUERY_STAT(SRPredictedShotVisual), false);
    {
        const AActor* Owner = GetOwner();
        if (Owner) VisualParams.AddIgnoredActor(Owner);
    }
    const FVector VisualEnd = CameraOrigin + AimDirection * (WeaponData ? WeaponData->Range : 12000.f);
    const bool bVisualHit = GetWorld()->LineTraceSingleByChannel(VisualHit, CameraOrigin, VisualEnd,
        GetTraceChannel(), VisualParams);
    const FVector VisualImpact = bVisualHit ? VisualHit.ImpactPoint : VisualEnd;

    // Cosmetics + recoil immediately (deduped by shot id when confirmations arrive).
    ApplyCosmeticShot(ClientShotId, CameraOrigin, VisualImpact, bVisualHit);

    SRDebug::PredictionLog(Character, FString::Printf(TEXT("Predicted shot %d | display charges=%d | pending=%d | weapon=%s"),
        ClientShotId, GetDisplayCharges(), PendingPredictedShots.Num(), *GetNameSafe(WeaponData)));

    // Submit to the server: aim + shot id. First submission is reliable; burst
    // continuation rides the unreliable per-shot refresh (bounded by fire rate).
    if (ReplicatedState.bFiring)
    {
        ServerUpdateFireAim(CameraOrigin, AimDirection, ClientShotId);
    }
    else if (PendingPredictedShots.Num() == 1)
    {
        ServerStartFiringWithAim(CameraOrigin, AimDirection, ClientShotId);
    }
    else
    {
        ServerUpdateFireAim(CameraOrigin, AimDirection, ClientShotId);
    }

    // Automatic weapons keep the predicted cadence while the trigger is held.
    if (bLocalFireHeld && WeaponData->FireMode == ESRFireMode::Automatic && GetDisplayCharges() > 0)
    {
        ScheduleNextPredictedShot();
    }
}

void UPSCombatComponent::ExecutePredictedShot()
{
    if (bLocalFireHeld) PredictShot();
}

void UPSCombatComponent::StopPredictedFireTimer()
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(PredictedFireTimerHandle);
    }
}

void UPSCombatComponent::ScheduleNextPredictedShot()
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().SetTimer(PredictedFireTimerHandle, this,
            &UPSCombatComponent::ExecutePredictedShot, static_cast<float>(GetEffectiveFireInterval()), false);
    }
}

int32 UPSCombatComponent::GetDisplayCharges() const
{
    // Authoritative minus in-flight predictions: responsive without hiding the truth.
    return FMath::Max(0, ReplicatedState.PrimaryCharges - PendingPredictedShots.Num());
}

void UPSCombatComponent::ResetFirePrediction(const FString& Reason)
{
    PendingPredictedShots.Reset();
    ProcessedConfirmedShotIds.Reset();
    NextPredictedShotId = 0;
    bLocalFireHeld = false;
    LastPredictedShotRealTime = 0.0;
    StopPredictedFireTimer();
    SRDebug::PredictionLog(GetOwner(), FString::Printf(TEXT("Prediction state reset: %s"), *Reason));
}

bool UPSCombatComponent::GetLocalAimView(FVector& OutOrigin, FVector& OutDirection) const
{
    const APSBaseCharacter* Character = GetOwnerCharacter();
    if (!Character) return false;

    // Same camera-selection rule as the server: the active camera wins (first-
    // or third-person), then the controller view.
    TArray<UCameraComponent*> Cameras;
    Character->GetComponents<UCameraComponent>(Cameras);
    const UCameraComponent* ActiveCamera = nullptr;
    for (const UCameraComponent* Candidate : Cameras)
    {
        if (Candidate && Candidate->IsActive()) { ActiveCamera = Candidate; break; }
    }
    if (ActiveCamera)
    {
        OutOrigin = ActiveCamera->GetComponentLocation();
        OutDirection = ActiveCamera->GetComponentRotation().Vector();
        return true;
    }
    if (const APlayerController* PC = Cast<APlayerController>(Character->GetController()))
    {
        FVector ViewLocation; FRotator ViewRotation;
        PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
        OutOrigin = ViewLocation;
        OutDirection = ViewRotation.Vector();
        return true;
    }
    return false;
}

bool UPSCombatComponent::ServerStartFiringWithAim_Validate(FVector_NetQuantize10 CameraOrigin, FVector_NetQuantizeNormal AimDirection, int32 /*ClientShotId*/)
{
    // Structural check only; full plausibility runs in _Implementation so
    // rejections are logged instead of tearing down the channel.
    return !AimDirection.IsZero();
}

void UPSCombatComponent::ServerStartFiringWithAim_Implementation(FVector_NetQuantize10 CameraOrigin, FVector_NetQuantizeNormal AimDirection, int32 ClientShotId)
{
    SubmittedClientShotIds.Add(ClientShotId);
    if (SubmittedClientShotIds.Num() > 32) SubmittedClientShotIds.RemoveAt(0); // bounded FIFO
    SRDebug::PredictionLog(GetOwner(), FString::Printf(TEXT("Server received start submission: clientShot=%d queue=%d"),
        ClientShotId, SubmittedClientShotIds.Num()));

    FString RejectReason;
    if (ValidateClientAim(CameraOrigin, AimDirection, RejectReason))
    {
        ApplyValidatedAim(CameraOrigin, AimDirection, true);
    }
    else
    {
        bHasValidatedClientAim = false;
        SRDebug::CombatLog(GetOwner(), FString::Printf(TEXT("Client aim REJECTED (%s) - shots use the server view"), *RejectReason));
    }
    StartFiring();
}

void UPSCombatComponent::ServerUpdateFireAim_Implementation(FVector_NetQuantize10 CameraOrigin, FVector_NetQuantizeNormal AimDirection, int32 ClientShotId)
{
    SubmittedClientShotIds.Add(ClientShotId);
    if (SubmittedClientShotIds.Num() > 32) SubmittedClientShotIds.RemoveAt(0); // bounded FIFO
    if (!ReplicatedState.bFiring) return; // aim refresh outside a burst is meaningless
    FString RejectReason;
    if (ValidateClientAim(CameraOrigin, AimDirection, RejectReason))
    {
        ApplyValidatedAim(CameraOrigin, AimDirection, false);
    }
    else
    {
        bHasValidatedClientAim = false;
        SRDebug::CombatLog(GetOwner(), FString::Printf(TEXT("Aim refresh REJECTED (%s) - burst falls back to the server view"), *RejectReason));
    }
}

bool UPSCombatComponent::ValidateClientAim(const FVector& CameraOrigin, const FVector& AimDirection, FString& OutReason) const
{
    const APSBaseCharacter* Character = GetOwnerCharacter();
    if (!Character || !WeaponData) { OutReason = TEXT("no character/weapon"); return false; }
    if (Character->IsPSDead()) { OutReason = TEXT("shooter dead"); return false; }
    if (!AimDirection.IsNormalized()) { OutReason = TEXT("aim direction not normalized"); return false; }

    // Camera origin plausibility: the submitted origin must sit near the pawn's
    // replicated view position (covers the first-person head and the third-person
    // spring arm; rejects far-origin exploits).
    const FVector ServerView = Character->GetPawnViewLocation();
    const float OriginDistance = FVector::Dist(CameraOrigin, ServerView);
    if (OriginDistance > WeaponData->CameraOriginTolerance)
    {
        OutReason = FString::Printf(TEXT("camera origin implausible (dist=%.0fcm > %.0fcm)"), OriginDistance, WeaponData->CameraOriginTolerance);
        return false;
    }

    // Maximum acceptable aim deviation vs the server-side view direction.
    FVector ServerOrigin;
    FVector ServerDirection;
    GetServerAimBasis(ServerOrigin, ServerDirection);
    const float DeviationDeg = FMath::RadiansToDegrees(
        FMath::Acos(FMath::Clamp(FVector::DotProduct(AimDirection, ServerDirection), -1.f, 1.f)));
    if (DeviationDeg > WeaponData->MaxAimDeviationDegrees)
    {
        OutReason = FString::Printf(TEXT("aim deviation %.1f deg > %.1f deg"), DeviationDeg, WeaponData->MaxAimDeviationDegrees);
        return false;
    }
    return true;
}

void UPSCombatComponent::ApplyValidatedAim(const FVector& CameraOrigin, const FVector& AimDirection, bool bLogAccept)
{
    ValidatedShotOrigin = FVector(CameraOrigin);
    ValidatedAimDirection = FVector(AimDirection).GetSafeNormal();
    bHasValidatedClientAim = true;
    if (bLogAccept)
    {
        SRDebug::CombatLog(GetOwner(), FString::Printf(TEXT("Client aim accepted: origin=%s dir=%s"),
            *ValidatedShotOrigin.ToString(), *ValidatedAimDirection.ToString()));
    }
}

void UPSCombatComponent::StopAimUpdateTimer()
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(AimUpdateTimerHandle);
    }
}

void UPSCombatComponent::SendAimUpdate()
{
    if (GetOwnerRole() == ROLE_Authority || !ReplicatedState.bFiring) { StopAimUpdateTimer(); return; }
    const double Now = FPlatformTime::Seconds();
    if (Now - LastAimUpdateRealTime < 0.05) return; // hard 20Hz ceiling
    FVector CameraOrigin;
    FVector AimDirection;
    if (!GetLocalAimView(CameraOrigin, AimDirection)) return;
    LastAimUpdateRealTime = Now;
    ServerUpdateFireAim(CameraOrigin, AimDirection, 0); // pure aim refresh, no shot id
}

void UPSCombatComponent::TrimStalePendingShots()
{
    const double Now = FPlatformTime::Seconds();
    const int32 Before = PendingPredictedShots.Num();
    PendingPredictedShots.RemoveAll([Now](const FPendingPredictedShot& P) { return Now - P.PredictedRealTime > 2.0; });
    if (PendingPredictedShots.Num() != Before)
    {
        SRDebug::PredictionLog(GetOwner(), FString::Printf(TEXT("Stale pending predictions trimmed: %d -> %d (authoritative=%d, display=%d)"),
            Before, PendingPredictedShots.Num(), ReplicatedState.PrimaryCharges, GetDisplayCharges()));
        OnChargesChanged.Broadcast(GetDisplayCharges(), GetMaxCharges());
    }
}

void UPSCombatComponent::RequestStopPrimaryFire()
{
    SRDebug::Log(SRDebug::ECategory::Input, TEXT("Client"), TEXT("LMB Released"));
    if (GetOwnerRole() == ROLE_Authority) { StopFiring(TEXT("Input released")); }
    else { ServerStopFiring(); }
}

void UPSCombatComponent::RequestPrimaryRecharge()
{
    if (GetOwnerRole() == ROLE_Authority) { StartRecharge(TEXT("R pressed")); }
    else { ServerRequestRecharge(); }
}

bool UPSCombatComponent::ServerStartFiring_Validate() { return true; }

void UPSCombatComponent::ServerStartFiring_Implementation() { StartFiring(); }

void UPSCombatComponent::ServerStopFiring_Implementation() { StopFiring(TEXT("Input released")); }

bool UPSCombatComponent::ServerRequestRecharge_Validate() { return true; }

void UPSCombatComponent::ServerRequestRecharge_Implementation() { StartRecharge(TEXT("R pressed (rpc)")); }

void UPSCombatComponent::StartFiring()
{
    if (GetOwnerRole() != ROLE_Authority) return;
    APSBaseCharacter* Character = GetOwnerCharacter();
    if (!Character || !WeaponData)
    {
        SRDebug::Message(SRDebug::ECategory::Combat, TEXT("Server"), TEXT("Fire Rejected: no weapon configuration"), 0.5f, FColor::Orange);
        return;
    }
    if (!CanAttack())
    {
        SRDebug::Message(SRDebug::ECategory::Combat, TEXT("Server"), TEXT("Fire Rejected: dead"), 0.5f, FColor::Orange);
        return;
    }
    if (ReplicatedState.bRecharging)
    {
        SRDebug::Message(SRDebug::ECategory::Combat, TEXT("Server"), TEXT("Fire Rejected: recharging"), 0.5f, FColor::Orange);
        return;
    }
    // Ignore repeated start RPCs while an automatic burst is already active.
    if (ReplicatedState.bFiring) return;
    if (ReplicatedState.PrimaryCharges <= 0)
    {
        SRDebug::Message(SRDebug::ECategory::Combat, TEXT("Server"), TEXT("Magazine empty: auto-recharging"), 0.5f, FColor::Orange);
        StartRecharge(TEXT("Empty magazine at trigger press"));
        return;
    }

    ReplicatedState.bFiring = true;
    OnCombatStateChanged.Broadcast();
    SRDebug::Message(SRDebug::ECategory::Combat, TEXT("Server"), TEXT("Fire Accepted"), 0.5f, FColor::Green);

    // Single-shot weapons fire exactly once; automatic weapons run on a server timer.
    ExecutePrimaryShot();
    if (WeaponData->FireMode != ESRFireMode::Automatic && ReplicatedState.bFiring)
    {
        StopFiring(TEXT("Single shot or no remaining charges"));
    }
}

void UPSCombatComponent::StopFiring(const FString& Reason)
{
    if (GetOwnerRole() != ROLE_Authority) return;
    APSBaseCharacter* Character = GetOwnerCharacter();
    if (Character) Character->GetWorldTimerManager().ClearTimer(FireTimerHandle);
    bHasValidatedClientAim = false; // a new burst requires a fresh validated submission
    StopAimUpdateTimer();
    if (ReplicatedState.bFiring)
    {
        ReplicatedState.bFiring = false;
        OnCombatStateChanged.Broadcast();
        SRDebug::Log(SRDebug::ECategory::Combat, TEXT("Server"), FString::Printf(TEXT("Firing stopped: %s"), *Reason));
    }
}

float UPSCombatComponent::ComputeRangeMultiplier(const USRWeaponDataAsset* Weapon, float Distance)
{
    if (!Weapon) return 1.f;
    const float FullRange = FMath::Max(0.f, Weapon->FullDamageRange);
    const float FalloffEnd = FMath::Max(FullRange + 1.f, Weapon->MinDamageRange);
    // Linear falloff between the full-damage distance and the falloff end.
    const float Alpha = FMath::Clamp((Distance - FullRange) / (FalloffEnd - FullRange), 0.f, 1.f);
    return FMath::Lerp(1.f, FMath::Clamp(Weapon->MinDamageMultiplier, 0.f, 1.f), Alpha);
}

float UPSCombatComponent::GetRegionMultiplier(EPSDamageRegion Region) const
{
    if (!WeaponData) return 1.f;
    switch (Region)
    {
    case EPSDamageRegion::Head: return WeaponData->HeadshotMultiplier;
    case EPSDamageRegion::Pelvis: return WeaponData->PelvisDamageMultiplier;
    case EPSDamageRegion::Arm: return WeaponData->ArmDamageMultiplier;
    case EPSDamageRegion::Leg: return WeaponData->LegDamageMultiplier;
    default: return WeaponData->TorsoDamageMultiplier;
    }
}

const TCHAR* UPSCombatComponent::GetRegionName(EPSDamageRegion Region) const
{
    switch (Region)
    {
    case EPSDamageRegion::Head: return TEXT("head");
    case EPSDamageRegion::Pelvis: return TEXT("pelvis");
    case EPSDamageRegion::Arm: return TEXT("arm");
    case EPSDamageRegion::Leg: return TEXT("leg");
    default: return TEXT("torso");
    }
}

float UPSCombatComponent::ComputeShotDamage(const USRWeaponDataAsset* Weapon, float Distance, EPSDamageRegion Region)
{
    if (!Weapon) return 0.f;
    float BodyMultiplier = 1.f;
    switch (Region)
    {
    case EPSDamageRegion::Head: BodyMultiplier = Weapon->HeadshotMultiplier; break;
    case EPSDamageRegion::Pelvis: BodyMultiplier = Weapon->PelvisDamageMultiplier; break;
    case EPSDamageRegion::Arm: BodyMultiplier = Weapon->ArmDamageMultiplier; break;
    case EPSDamageRegion::Leg: BodyMultiplier = Weapon->LegDamageMultiplier; break;
    default: BodyMultiplier = Weapon->TorsoDamageMultiplier; break;
    }
    return Weapon->Damage * FMath::Max(0.f, BodyMultiplier) * ComputeRangeMultiplier(Weapon, Distance);
}

void UPSCombatComponent::ExecutePrimaryShot()
{
    if (GetOwnerRole() != ROLE_Authority) return;
    APSBaseCharacter* Character = GetOwnerCharacter();
    if (!Character || !WeaponData) { StopFiring(TEXT("No weapon")); return; }
    if (!ReplicatedState.bFiring) return;
    if (Character->IsPSDead()) { StopFiring(TEXT("Dead")); return; }
    if (ReplicatedState.bRecharging) { StopFiring(TEXT("Recharge started")); return; }
    if (ReplicatedState.PrimaryCharges <= 0)
    {
        StopFiring(TEXT("Out of charges"));
        StartRecharge(TEXT("Empty magazine during firing"));
        return;
    }
    TryExecutePrimaryShot();
    // Schedule from the latest shot so temporary fire-rate buffs take effect and expire
    // without keeping a stale repeating interval.
    if (WeaponData && WeaponData->FireMode == ESRFireMode::Automatic && ReplicatedState.bFiring)
    {
        RefreshAutomaticFireInterval();
    }
}

bool UPSCombatComponent::TryExecutePrimaryShot()
{
    APSBaseCharacter* Character = GetOwnerCharacter();
    UWorld* World = GetWorld();
    if (!Character || !World || !WeaponData || !Character->HasAuthority()) return false;
    if (ReplicatedState.bRecharging) return false;
    if (Character->IsPSDead())
    {
        SRDebug::CombatLog(Character, TEXT("Shot rejected: shooter dead"));
        return false;
    }
    if (ReplicatedState.PrimaryCharges <= 0)
    {
        StopFiring(TEXT("Out of charges"));
        return false;
    }

    // Authoritative fire-rate gate: this single executor is the only rate source;
    // a burst of start RPCs can only re-assert intent, never shorten the interval.
    const double Now = FPlatformTime::Seconds();
    const double Interval = GetEffectiveFireInterval();
    if (Now - LastShotRealTime < Interval - KINDA_SMALL_NUMBER)
    {
        SRDebug::CombatLog(Character, FString::Printf(TEXT("Shot rejected: fire-rate gate (%.3fs < %.3fs interval)"), Now - LastShotRealTime, Interval));
        return false;
    }

    FTransform FireOrigin;
    if (!GetFireOriginTransform(FireOrigin)) return false;

    const int32 ShotId = NextShotId++;
    if (Character->HeroGameplayComponent) Character->HeroGameplayComponent->BreakConcealment();

    // 1. Aim basis: the validated client aim (remote owning clients) or the
    //    server-side active camera (listen host / fallback). The client never
    //    submits hits, damage or body parts - only an origin and a direction.
    FVector ShotCameraOrigin;
    FVector ShotCameraDirection;
    if (bHasValidatedClientAim)
    {
        // Re-validate against the live server view; a stale/despynchronized
        // submission falls back to the server view for this shot.
        FString RejectReason;
        if (ValidateClientAim(ValidatedShotOrigin, ValidatedAimDirection, RejectReason))
        {
            ShotCameraOrigin = ValidatedShotOrigin;
            ShotCameraDirection = ValidatedAimDirection;
        }
        else
        {
            bHasValidatedClientAim = false;
            GetServerAimBasis(ShotCameraOrigin, ShotCameraDirection);
            SRDebug::CombatLog(Character, FString::Printf(TEXT("Validated aim went stale (%s) - server view used for shot %d"), *RejectReason, ShotId));
        }
    }
    else
    {
        GetServerAimBasis(ShotCameraOrigin, ShotCameraDirection);
    }

    // 2. Camera aim: where does the player intend to shoot?
    FHitResult CameraHit;
    const FVector AimTarget = ResolveAimTargetFrom(ShotCameraOrigin, ShotCameraDirection, WeaponData->Range, &CameraHit);
    const bool bFirstPersonShot = Character->IsFirstPersonView();

    // 2. Validate the physical muzzle-to-crosshair line. Camera aim is authoritative
    // for targets beyond 3m, but muzzle obstructions are never ignored.
    const FVector MuzzleStart = FireOrigin.GetLocation();
    const FVector AimVector = AimTarget - MuzzleStart;
    const FVector AimDirection = AimVector.GetSafeNormal();
    if (AimDirection.IsNearlyZero()) return false;

    const FVector ShotEnd = AimTarget;
    FCollisionQueryParams ShotParams(SCENE_QUERY_STAT(SRPrimaryShot), false);
    AddOwnerToIgnore(Character, ShotParams);
    FHitResult MuzzleHit;
    // First-person aim uses ONLY the active camera ray; do not let the third-person
    // hand socket / 3m muzzle-convergence algorithm change its hit result.
    const bool bMuzzleHit = !bFirstPersonShot && World->LineTraceSingleByChannel(
        MuzzleHit, MuzzleStart, ShotEnd, GetTraceChannel(), ShotParams);

    // If the muzzle intersects different cover before reaching the camera hit,
    // that obstruction wins. Use a small tolerance for the same target surface:
    // the camera and muzzle see its surface at slightly different positions.
    constexpr float AimAlignmentDistance = 300.f; // 3m, Unreal units are cm
    constexpr float ObstructionTolerance = 12.f;
    const bool bCameraTarget = CameraHit.bBlockingHit &&
        FVector::DistSquared(CameraHit.TraceStart, CameraHit.ImpactPoint) >=
        FMath::Square(AimAlignmentDistance);
    const bool bMuzzleObstructed = bMuzzleHit &&
        (!bCameraTarget || MuzzleHit.GetActor() != CameraHit.GetActor()) &&
        FVector::Dist(MuzzleStart, MuzzleHit.ImpactPoint) + ObstructionTolerance < AimVector.Size();

    FHitResult ShotHit;
    bool bTraceHit = false;
    if (bFirstPersonShot)
    {
        if (CameraHit.bBlockingHit) { ShotHit = CameraHit; bTraceHit = true; }
    }
    else if (bMuzzleObstructed)
    {
        ShotHit = MuzzleHit;
        bTraceHit = true;
    }
    else if (bCameraTarget)
    {
        ShotHit = CameraHit;
        bTraceHit = true;
    }
    else if (bMuzzleHit)
    {
        ShotHit = MuzzleHit;
        bTraceHit = true;
    }

    // 4. Charge consumption happens exactly once per confirmed shot.
    ReplicatedState.PrimaryCharges = FMath::Max(0, ReplicatedState.PrimaryCharges - 1);
    OnRep_ReplicatedState();

    // 5. Damage through GAS (server-authoritative; region and range multipliers
    //    come from the weapon asset, computed once, never client-supplied).
    bool bHitActor = false;
    float Distance = 0.f;
    float DamageApplied = 0.f;
    bool bEnemy = false;
    EPSDamageRegion HitRegion = EPSDamageRegion::Torso;
    float BodyMultiplier = 1.f;
    float RangeMultiplier = 1.f;
    FVector ShotRegionImpact = FVector::ZeroVector;
    bool ShotRegionValid = false;
    if (bTraceHit && ShotHit.GetActor())
    {
        bHitActor = true;
        Distance = FVector::Dist(bFirstPersonShot ? CameraHit.TraceStart : FireOrigin.GetLocation(), ShotHit.ImpactPoint);
        const FString Bone = ShotHit.BoneName.ToString().ToLower();
        const bool bHead = Bone.Contains(TEXT("head")) || Bone.Contains(TEXT("neck"));
        const bool bLimb = Bone.Contains(TEXT("arm")) || Bone.Contains(TEXT("hand")) ||
                           Bone.Contains(TEXT("leg")) || Bone.Contains(TEXT("foot")) ||
                           Bone.Contains(TEXT("thigh")) || Bone.Contains(TEXT("calf")) ||
                           Bone.Contains(TEXT("upperarm")) || Bone.Contains(TEXT("lowerarm"));
        // The capsule is usually the initial hit (BoneName=None). On the server,
        // refine a CHARACTER hit against animated bone-attached boxes.
        // Other damaged actors keep the previous skeletal-bone fallback.
        HitRegion = bHead ? EPSDamageRegion::Head : (bLimb ? EPSDamageRegion::Arm : EPSDamageRegion::Torso);
        if (APSBaseCharacter* Victim = Cast<APSBaseCharacter>(ShotHit.GetActor()))
        {
            FVector RegionImpact = ShotHit.ImpactPoint;
            const FVector RayStart = bFirstPersonShot ? CameraHit.TraceStart : MuzzleStart;
            // Extend beyond the capsule contact to reach the boxes inside.
            // Segment remains aimed at the hit actor; no client hit-region claims.
            const FVector RayDir = (ShotHit.ImpactPoint - RayStart).GetSafeNormal();
            if (Victim->ResolveDamageRegion(RayStart,
                ShotHit.ImpactPoint + RayDir * 200.f, HitRegion, RegionImpact))
            {
                ShotRegionImpact = RegionImpact;
                ShotRegionValid = true;
            }
        }
        BodyMultiplier = GetRegionMultiplier(HitRegion);
        RangeMultiplier = ComputeRangeMultiplier(WeaponData, Distance);
        DamageApplied = ApplyCombatDamage(ShotHit.GetActor(),
            ComputeShotDamage(WeaponData, Distance, HitRegion) * SRHeroGameplay::HitscanTransmission(World, FireOrigin.GetLocation(), ShotHit.ImpactPoint), Character, bEnemy);
    }

    // 6. Replicate the confirmed cosmetic event + acknowledgment (the echoed
    //    client shot id drives prediction reconciliation on the owning client).
    int32 ConfirmedClientShotId = 0;
    if (SubmittedClientShotIds.Num() > 0)
    {
        ConfirmedClientShotId = SubmittedClientShotIds[0];
        SubmittedClientShotIds.RemoveAt(0);
    }
    MulticastShotCosmetic(ShotId, FireOrigin.GetLocation(), bTraceHit ? ShotHit.ImpactPoint : ShotEnd, bHitActor, ConfirmedClientShotId);
    ApplyCosmeticShot(ShotId, FireOrigin.GetLocation(), bTraceHit ? ShotHit.ImpactPoint : ShotEnd, bHitActor);

    // 7. Feedback events.
    OnConfirmedShot.Broadcast(ShotId);
    if (bHitActor)
    {
        OnHitConfirmed.Broadcast(ShotId, ShotHit.GetActor(), Distance, DamageApplied);
        SRDebug::CombatLog(Character, FString::Printf(TEXT("Shot %d HIT %s | region=%s | dist=%.1fm | body=x%.2f range=x%.2f | base=%.1f -> applied=%.1f (enemy=%d)"),
            ShotId, *GetNameSafe(ShotHit.GetActor()), GetRegionName(HitRegion), Distance / 100.f,
            BodyMultiplier, RangeMultiplier, WeaponData->Damage, DamageApplied, bEnemy ? 1 : 0));
        SRDebug::Message(SRDebug::ECategory::Hitscan, TEXT("Server"),
            FString::Printf(TEXT("Target=%s Dist=%.1fm"), *GetNameSafe(ShotHit.GetActor()), Distance / 100.f), 0.3f, FColor::Green);
        SRDebug::Message(SRDebug::ECategory::Damage, TEXT("Server"),
            FString::Printf(TEXT("Applied=%.1f"), DamageApplied), 0.3f, FColor::Green);
    }
    else
    {
        SRDebug::CombatLog(Character, FString::Printf(TEXT("Shot %d MISS (impact=%s)"), ShotId,
            *(bTraceHit ? ShotHit.ImpactPoint : ShotEnd).ToString()));
        SRDebug::Message(SRDebug::ECategory::Hitscan, TEXT("Server"), TEXT("MISS"), 0.3f, FColor::Silver);
    }

    SRDebug::Message(SRDebug::ECategory::Combat, TEXT("Server"),
        FString::Printf(TEXT("Shot %d fired, charges=%d/%d"), ShotId, ReplicatedState.PrimaryCharges, GetMaxCharges()), 0.3f, FColor::Cyan);

#if !UE_BUILD_SHIPPING
    if (SRDebug::IsEnabled(SRDebug::ECategory::Hitscan))
    {
        const FVector TraceEnd = bTraceHit ? ShotHit.ImpactPoint : ShotEnd;
        DrawDebugLine(World, FireOrigin.GetLocation(), TraceEnd, bHitActor ? FColor::Green : FColor::Red, false, 1.5f, 0, 1.f);
        DrawDebugPoint(World, TraceEnd, 12.f, FColor::Yellow, false, 1.5f);
        // Camera aiming ray (blue) + third-person muzzle trace (muzzle -> impact).
        DrawDebugLine(World, CameraHit.TraceStart, AimTarget, FColor::Blue, false, 1.5f, 0, 0.5f);
        if (bHitActor && ShotRegionValid)
        {
            // Selected hit region: the winning server-side hitbox point (orange).
            DrawDebugPoint(World, ShotRegionImpact, 16.f, FColor::Orange, false, 1.5f);
            DrawDebugString(World, ShotRegionImpact,
                FString::Printf(TEXT("%s x%.2f range x%.2f = %.1f"), GetRegionName(HitRegion), BodyMultiplier, RangeMultiplier, DamageApplied),
                nullptr, FColor::Orange, 1.5f, true);
        }
        if (GEngine && SRDebug::IsEnabled(SRDebug::ECategory::Damage))
        {
            GEngine->AddOnScreenDebugMessage(8101, 1.5f, bHitActor ? FColor::Green : FColor::Silver,
                FString::Printf(TEXT("[ProjectS][Combat] Shot %d %s dist=%.1fm dmg=%.1f (body x%.2f, range x%.2f)"),
                    ShotId, bHitActor ? TEXT("HIT") : TEXT("MISS"), Distance / 100.f, DamageApplied, BodyMultiplier, RangeMultiplier));
        }
    }
#endif

    LastShotRealTime = Now;

    if (ReplicatedState.PrimaryCharges <= 0)
    {
        StopFiring(TEXT("Charges exhausted"));
        StartRecharge(TEXT("Last magazine charge consumed"));
    }
    return true;
}

void UPSCombatComponent::GetServerAimBasis(FVector& OutOrigin, FVector& OutDirection) const
{
    const APSBaseCharacter* OwnerCharacter = GetOwnerCharacter();
    OutOrigin = OutDirection = FVector::ZeroVector;
    if (!OwnerCharacter) return;

    // Use the actual active camera (first- or third-person, including spring-arm
    // collision output). The pawn's GetPawnViewLocation() is a mesh offset.
    TArray<UCameraComponent*> Cameras;
    OwnerCharacter->GetComponents<UCameraComponent>(Cameras);
    const UCameraComponent* ActiveCamera = nullptr;
    for (const UCameraComponent* Candidate : Cameras)
    {
        if (Candidate && Candidate->IsActive())
        {
            ActiveCamera = Candidate;
            break;
        }
    }
    if (ActiveCamera)
    {
        OutOrigin = ActiveCamera->GetComponentLocation();
        OutDirection = ActiveCamera->GetComponentRotation().Vector();
    }
    else if (const APlayerController* PC = Cast<APlayerController>(OwnerCharacter->GetController()))
    {
        FVector ViewLocation; FRotator ViewRotation;
        PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
        OutOrigin = ViewLocation;
        OutDirection = ViewRotation.Vector();
    }
    else
    {
        OutOrigin = OwnerCharacter->GetPawnViewLocation();
        OutDirection = OwnerCharacter->GetBaseAimRotation().Vector();
    }
}

FVector UPSCombatComponent::ResolveAimTarget(const FVector& FireOrigin, float Range, FHitResult* OutCameraHit) const
{
    FVector CameraLoc;
    FVector CameraDir;
    GetServerAimBasis(CameraLoc, CameraDir);
    return ResolveAimTargetFrom(CameraLoc, CameraDir, Range, OutCameraHit);
}

FVector UPSCombatComponent::ResolveAimTargetFrom(const FVector& CameraLoc, const FVector& CameraDir, float Range, FHitResult* OutCameraHit) const
{
    const APSBaseCharacter* OwnerCharacter = GetOwnerCharacter();
    const FVector AimEnd = CameraLoc + CameraDir * Range;
    SRDebug::Log(SRDebug::ECategory::Hitscan, TEXT("Aim"), FString::Printf(TEXT("cam=%s dir=%s aimTarget=%s"), *CameraLoc.ToString(), *CameraDir.ToString(), *AimEnd.ToString()));
    FCollisionQueryParams Params(SCENE_QUERY_STAT(SRAimTrace), false);
    AddOwnerToIgnore(GetOwnerCharacter(), Params);

    FHitResult Hit;
    const UWorld* World = GetWorld();
    const ECollisionChannel TraceChannel = WeaponData ? static_cast<ECollisionChannel>(WeaponData->TraceChannel) : ECC_Visibility;
    World->LineTraceSingleByChannel(Hit, CameraLoc, AimEnd, TraceChannel, Params);
    if (Hit.bBlockingHit && (!OwnerCharacter || !OwnerCharacter->IsFirstPersonView()) &&
        (Hit.Distance < 1.f || Hit.bStartPenetrating))
    {
        // The view point starts inside/ON geometry (e.g. standing exactly on the floor):
        // zero-distance hits carry no aim information. Retry from slightly above and
        // only accept a hit that starts in open space; otherwise treat as unblocked.
        FHitResult Raised;
        World->LineTraceSingleByChannel(Raised, CameraLoc + FVector(0, 0, 60.f), AimEnd, TraceChannel, Params);
        Hit = (Raised.bBlockingHit && !Raised.bStartPenetrating && Raised.Distance >= 1.f) ? Raised : FHitResult();
    }
    if (OutCameraHit) *OutCameraHit = Hit;

#if !UE_BUILD_SHIPPING
    if (SRDebug::IsEnabled(SRDebug::ECategory::Hitscan) && World)
    {
        DrawDebugLine(World, CameraLoc, AimEnd, FColor::Blue, false, 1.5f, 0, 0.5f);
    }
#endif
    return Hit.bBlockingHit ? Hit.ImpactPoint : AimEnd;
}

bool UPSCombatComponent::GetFireOriginTransform(FTransform& OutTransform) const
{
    const APSBaseCharacter* Character = GetOwnerCharacter();
    if (!Character || !WeaponData) return false;
    USkeletalMeshComponent* Mesh = Character->GetMesh();
    if (!Mesh) return false;
    if (Mesh->DoesSocketExist(WeaponData->FireSocketName) || Mesh->GetBoneIndex(WeaponData->FireSocketName) != INDEX_NONE)
    {
        OutTransform = Mesh->GetSocketTransform(WeaponData->FireSocketName, RTS_World);
        return true;
    }
    OutTransform = Mesh->GetComponentTransform();
    SRDebug::Log(SRDebug::ECategory::Combat, TEXT("Server"), FString::Printf(TEXT("Socket %s missing on mesh; falling back to mesh transform"), *WeaponData->FireSocketName.ToString()));
    return true;
}

float UPSCombatComponent::ApplyCombatDamage(AActor* Target, float BaseDamage, AActor* DamageInstigator, bool& bOutTargetWasEnemy)
{
    bOutTargetWasEnemy = false;
    if (GetOwnerRole() != ROLE_Authority || !FMath::IsFinite(BaseDamage) || BaseDamage <= 0.f) return 0.f;
    if (!Target || Target == GetOwner()) return 0.f;
    if (!SRHeroGameplay::IsDamageable(Target)) return 0.f;
    if (SRHeroGameplay::SameTeam(GetOwner(), Target)) return 0.f;
    const UPSAttributeSet* SourceAttributes = GetOwnerASC() ? GetOwnerASC()->GetSet<UPSAttributeSet>() : nullptr;
    BaseDamage *= SourceAttributes ? SourceAttributes->GetDamageDealtMultiplier() : 1.f;
    if (Target->IsA<ASRHeroBarrier>() || Target->IsA<ASRHeroField>() || Target->IsA<ASRHeroDecoy>())
    {
        FDamageEvent DamageEvent;
        const float Applied = Target->TakeDamage(BaseDamage, DamageEvent, GetOwnerCharacter() ? GetOwnerCharacter()->GetController() : nullptr, GetOwner());
        bOutTargetWasEnemy = Applied > 0.f;
        return Applied;
    }

    // Team gate: heroes on the same team never damage each other (configurable later).
    APSBaseCharacter* Character = GetOwnerCharacter();
    const APSPlayerState* InstigatorState = Character ? Character->GetPlayerState<APSPlayerState>() : nullptr;
    APSPlayerState* TargetState = Cast<APawn>(Target) ? Cast<APawn>(Target)->GetPlayerState<APSPlayerState>() : nullptr;
    if (TargetState && InstigatorState && TargetState->TeamId == InstigatorState->TeamId && TargetState != InstigatorState)
    {
        SRDebug::Message(SRDebug::ECategory::Damage, TEXT("Server"), TEXT("Rejected: friendly target"), 0.5f, FColor::Orange);
        return 0.f;
    }

    UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target);
    if (!TargetASC)
    {
        SRDebug::Message(SRDebug::ECategory::Damage, TEXT("Server"), TEXT("Rejected: target has no ASC"), 0.5f, FColor::Orange);
        return 0.f;
    }

    const UPSAttributeSet* TargetAttributes = TargetASC->GetSet<UPSAttributeSet>();
    if (!TargetAttributes || TargetAttributes->GetHealth() <= 0.f
        || TargetASC->HasMatchingGameplayTag(SRGameplayTags::Tag_State_DamageImmune)) return 0.f;

    const float ShieldBefore = TargetAttributes->GetShield();
    const float HealthBefore = TargetAttributes->GetHealth();
    // Damage never self-applies (Spark Shot has no self-damage) and dead targets are skipped above.
    const float Applied = UPSAttributeLibrary::ApplyDamage(Target, BaseDamage, DamageInstigator) ? BaseDamage : 0.f;
    if (Applied <= 0.f)
    {
        SRDebug::Message(SRDebug::ECategory::Damage, TEXT("Server"), TEXT("Rejected: effect not accepted"), 0.5f, FColor::Orange);
        return 0.f;
    }

    const float ShieldAbsorbed = FMath::Max(0.f, ShieldBefore - TargetAttributes->GetShield());
    const float HealthDamage = FMath::Max(0.f, HealthBefore - TargetAttributes->GetHealth());
    SRDebug::Message(SRDebug::ECategory::Damage, TEXT("Server"),
        FString::Printf(TEXT("Applied=%.1f (shield -%.1f, health -%.1f)"), Applied, ShieldAbsorbed, HealthDamage), 0.4f, FColor::Green);
    SRDebug::Log(SRDebug::ECategory::Damage, TEXT("Server"),
        FString::Printf(TEXT("Target=%s shield=%.1f health=%.1f"), *GetNameSafe(Target), TargetAttributes->GetShield(), TargetAttributes->GetHealth()));

    bOutTargetWasEnemy = true;
    GainUltimateCharge(Target);

    // Presentation: floating damage number + impact burst + knockback impulse,
    // multicast from the attacker's presentation component to every client.
    // Shared by Spark Shot, the landing burst, Stormpiercer and projectile splash.
    if (Character && Character->GetPresentationComponent())
    {
        const FVector HitLocation = Target->GetActorLocation() + FVector(0.f, 0.f, 80.f);
        const FVector HitDirection = Target->GetActorLocation() - Character->GetActorLocation();
        const bool bShieldHit = ShieldAbsorbed > 0.f && HealthDamage <= 0.f;
        Character->GetPresentationComponent()->PlayHitFeedback(Target, HitLocation, Applied, bShieldHit, false, HitDirection);
    }
    return Applied;
}

void UPSCombatComponent::GainUltimateCharge(AActor* DamageTarget)
{
    if (UltimateChargePerConfirmedHit <= 0.f) return;
    UAbilitySystemComponent* ASC = GetOwnerASC();
    const UPSAttributeSet* Attributes = ASC ? ASC->GetSet<UPSAttributeSet>() : nullptr;
    if (!ASC || !Attributes) return;
    const float NewCharge = FMath::Min(Attributes->GetUltimateCharge() + UltimateChargePerConfirmedHit, Attributes->GetMaxUltimateCharge());
    ASC->SetNumericAttributeBase(UPSAttributeSet::GetUltimateChargeAttribute(), NewCharge);
}

void UPSCombatComponent::ApplyCosmeticShot(int32 ShotId, const FVector& Origin, const FVector& ImpactPoint, bool bHit)
{
    // Owning-client dedupe: the local prediction already showed this shot.
    PredictedShotIds.Remove(ShotId);
    if (WeaponData && GetOwnerCharacter())
    {
        USkeletalMeshComponent* Mesh = GetOwnerCharacter()->GetMesh();
        if (WeaponData->MuzzleVFX && Mesh && WeaponData->FiringMontage == nullptr)
        {
            UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), WeaponData->MuzzleVFX, Origin);
        }
        if (WeaponData->ImpactVFX && bHit)
        {
            UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), WeaponData->ImpactVFX, ImpactPoint);
        }
        if (WeaponData->FiringSound)
        {
            UGameplayStatics::SpawnSoundAtLocation(GetWorld(), WeaponData->FiringSound, Origin);
        }
    }
    // First-person viewmodel montage is cosmetic and only runs for the owning player.
    if (APSBaseCharacter* Character = GetOwnerCharacter())
        Character->PlayPSFirstPersonFireAnimation();

    // Provisional presentation layer (engine-shape tracer/flash/impact, recoil).
    if (APSBaseCharacter* Character = GetOwnerCharacter())
    {
        if (USRCombatPresentationComponent* Presentation = Character->GetPresentationComponent())
        {
            Presentation->PlayShotCosmetic(Origin, ImpactPoint, bHit, ShotId);
        }
    }
}

void UPSCombatComponent::HandleImpactCosmetic(int32 ShotId, const FVector& ImpactPoint)
{
    if (WeaponData && WeaponData->ImpactVFX)
    {
        UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), WeaponData->ImpactVFX, ImpactPoint);
    }
}

void UPSCombatComponent::MulticastShotCosmetic_Implementation(int32 ShotId, FVector_NetQuantize100 Origin, FVector_NetQuantize100 ImpactPoint, bool bHit, int32 ClientShotId)
{
    if (GetOwnerRole() == ROLE_Authority) return;

    // Owning client: this is the shot acknowledgment. Duplicate acks are ignored
    // (bounded ring); a matched pending prediction suppresses the cosmetic replay
    // (flash/sound/recoil already played at predict time) and drains the queue.
    if (ClientShotId != 0)
    {
        if (ResolveShotAcknowledgment(ShotId, ClientShotId))
        {
            OnChargesChanged.Broadcast(GetDisplayCharges(), GetMaxCharges()); // HUD refresh, no replay
            return;
        }
        // No matching prediction (the server fired a shot the client did not
        // predict, e.g. rejected/lost submission): play authoritative visuals.
        SRDebug::PredictionLog(GetOwner(), FString::Printf(TEXT("ACK without prediction: serverShot=%d clientShot=%d - playing confirmed effects"), ShotId, ClientShotId));
    }

    ApplyCosmeticShot(ShotId, Origin, ImpactPoint, bHit);
    SRDebug::Log(SRDebug::ECategory::Networking, TEXT("Multicast"), FString::Printf(TEXT("Shot %d confirmed cosmetic"), ShotId));
}

void UPSCombatComponent::OnRep_ReplicatedState()
{
    SRDebug::NetLog(GetOwner(), FString::Printf(TEXT("Owner combat state replicated: charges=%d/%d recharging=%d firing=%d"),
        ReplicatedState.PrimaryCharges, GetMaxCharges(), ReplicatedState.bRecharging ? 1 : 0, ReplicatedState.bFiring ? 1 : 0));
    OnChargesChanged.Broadcast(ReplicatedState.PrimaryCharges, GetMaxCharges());
    OnRechargeChanged.Broadcast(ReplicatedState.bRecharging, ReplicatedState.RechargeStartTime);
    SRDebug::Message(SRDebug::ECategory::HUD, TEXT("Client"),
        FString::Printf(TEXT("Charges=%d/%d recharge=%d"), ReplicatedState.PrimaryCharges, GetMaxCharges(), ReplicatedState.bRecharging ? 1 : 0), 0.4f, FColor::Cyan);

    // Reconciliation: stale pending predictions (rejections, lost submissions)
    // drain after 2s; the authoritative charge base is always ReplicatedState.
    TrimStalePendingShots();

    // Recharge presentation is driven by the replicated state — no extra RPCs.
    if (ReplicatedState.bRecharging && !bPresentedRecharge)
    {
        bPresentedRecharge = true;
        APSBaseCharacter* Character = GetOwnerCharacter();
        if (Character && Character->GetPresentationComponent())
        {
            FTransform FireOrigin;
            const FVector Origin = GetFireOriginTransform(FireOrigin) ? FireOrigin.GetLocation() : Character->GetActorLocation();
            Character->GetPresentationComponent()->PlayRechargeCosmetic(Origin);
        }
    }
    else if (!ReplicatedState.bRecharging)
    {
        bPresentedRecharge = false;
    }
}

void UPSCombatComponent::StartRecharge(const FString& RequestReason)
{
    if (GetOwnerRole() != ROLE_Authority) return;
    FString RejectReason;
    if (!CanStartRecharge(RejectReason))
    {
        LastRechargeRejectReason = RejectReason;
        SRDebug::Message(SRDebug::ECategory::Combat, TEXT("Server"), FString::Printf(TEXT("Recharge rejected: %s"), *RejectReason), 0.6f, FColor::Orange);
        SRDebug::Log(SRDebug::ECategory::Combat, TEXT("Server"), FString::Printf(TEXT("Recharge rejected (%s): %s"), *RequestReason, *RejectReason));
        return;
    }

    StopFiring(TEXT("Recharge started"));
    ReplicatedState.bRecharging = true;
    const UWorld* World = GetWorld();
    const AGameStateBase* GameState = World ? World->GetGameState<AGameStateBase>() : nullptr;
    ReplicatedState.RechargeStartTime = GameState ? GameState->GetServerWorldTimeSeconds() : (World ? World->GetTimeSeconds() : 0.f);
    ReplicatedState.EffectiveRechargeDuration = GetEffectiveRechargeDuration();
    OnRep_ReplicatedState();

    APSBaseCharacter* Character = GetOwnerCharacter();
    if (Character)
    {
        if (WeaponData && WeaponData->RechargeMontage && Character->GetMesh() && Character->GetMesh()->GetAnimInstance())
        {
            Character->GetMesh()->GetAnimInstance()->Montage_Play(WeaponData->RechargeMontage);
        }
        Character->GetWorldTimerManager().SetTimer(RechargeTimerHandle, this, &UPSCombatComponent::FinishPrimaryRecharge, ReplicatedState.EffectiveRechargeDuration, false);
    }
    SRDebug::Message(SRDebug::ECategory::Combat, TEXT("Server"), FString::Printf(TEXT("Recharge started (%.2fs)"), ReplicatedState.EffectiveRechargeDuration), 0.6f, FColor::Green);
    SRDebug::Log(SRDebug::ECategory::Combat, TEXT("Server"), FString::Printf(TEXT("Recharge started (%s): %.2fs"), *RequestReason, ReplicatedState.EffectiveRechargeDuration));
}

bool UPSCombatComponent::CanStartRecharge(FString& OutRejectReason) const
{
    const APSBaseCharacter* Character = GetOwnerCharacter();
    if (!Character || !WeaponData) { OutRejectReason = TEXT("no weapon"); return false; }
    if (Character->IsPSDead()) { OutRejectReason = TEXT("dead"); return false; }
    if (ReplicatedState.bRecharging) { OutRejectReason = TEXT("already recharging"); return false; }
    if (ReplicatedState.PrimaryCharges >= GetMaxCharges()) { OutRejectReason = TEXT("charges already full"); return false; }
    return true;
}

void UPSCombatComponent::FinishPrimaryRecharge()
{
    if (GetOwnerRole() != ROLE_Authority || !ReplicatedState.bRecharging) return;
    ReplicatedState.bRecharging = false;
    ReplicatedState.PrimaryCharges = GetMaxCharges();
    OnRep_ReplicatedState();
    APSBaseCharacter* Character = GetOwnerCharacter();
    if (Character) Character->GetWorldTimerManager().ClearTimer(RechargeTimerHandle);
    SRDebug::Message(SRDebug::ECategory::Combat, TEXT("Server"), FString::Printf(TEXT("Recharge complete: %d/%d"), ReplicatedState.PrimaryCharges, GetMaxCharges()), 0.6f, FColor::Green);
    SRDebug::Log(SRDebug::ECategory::Combat, TEXT("Server"), TEXT("Recharge complete"));
    OnCombatStateChanged.Broadcast();
}

void UPSCombatComponent::CancelPrimaryRecharge(const FString& Reason)
{
    if (GetOwnerRole() != ROLE_Authority || !ReplicatedState.bRecharging) return;
    ReplicatedState.bRecharging = false;
    OnRep_ReplicatedState();
    APSBaseCharacter* Character = GetOwnerCharacter();
    if (Character) Character->GetWorldTimerManager().ClearTimer(RechargeTimerHandle);
    SRDebug::Message(SRDebug::ECategory::Combat, TEXT("Server"), FString::Printf(TEXT("Recharge cancelled: %s"), *Reason), 0.6f, FColor::Orange);
    SRDebug::Log(SRDebug::ECategory::Combat, TEXT("Server"), FString::Printf(TEXT("Recharge cancelled: %s"), *Reason));
}

void UPSCombatComponent::RefreshAutomaticFireInterval()
{
    if (GetOwnerRole() != ROLE_Authority || !WeaponData ||
        WeaponData->FireMode != ESRFireMode::Automatic || !ReplicatedState.bFiring) return;
    APSBaseCharacter* Character = GetOwnerCharacter();
    if (!Character) return;
    const double Remaining = FMath::Max(0.01, GetEffectiveFireInterval() - (FPlatformTime::Seconds() - LastShotRealTime));
    Character->GetWorldTimerManager().SetTimer(FireTimerHandle, this,
        &UPSCombatComponent::ExecutePrimaryShot, static_cast<float>(Remaining), false);
}

void UPSCombatComponent::RestartRechargeIfActive()
{
    if (GetOwnerRole() != ROLE_Authority || !ReplicatedState.bRecharging) return;
    const float OldDuration = ReplicatedState.EffectiveRechargeDuration;
    ReplicatedState.EffectiveRechargeDuration = GetEffectiveRechargeDuration();
    const UWorld* World = GetWorld();
    const AGameStateBase* GameState = World ? World->GetGameState<AGameStateBase>() : nullptr;
    const double Elapsed = (GameState ? GameState->GetServerWorldTimeSeconds() : (World ? World->GetTimeSeconds() : 0.f)) - ReplicatedState.RechargeStartTime;
    const float Remaining = FMath::Max(0.05f, ReplicatedState.EffectiveRechargeDuration - static_cast<float>(Elapsed));
    APSBaseCharacter* Character = GetOwnerCharacter();
    if (Character) Character->GetWorldTimerManager().SetTimer(RechargeTimerHandle, this, &UPSCombatComponent::FinishPrimaryRecharge, Remaining, false);
    SRDebug::Log(SRDebug::ECategory::Combat, TEXT("Server"), FString::Printf(TEXT("Recharge restarted: %.2fs -> %.2fs"), OldDuration, ReplicatedState.EffectiveRechargeDuration));
}

ASRElementalProjectile* UPSCombatComponent::SpawnAbilityProjectile(TSubclassOf<ASRElementalProjectile> ProjectileClass, const FVector& Location,
    const FVector& Direction, AActor* AbilityInstigator, float SpeedOverride, float RangeOverride)
{
    if (GetOwnerRole() != ROLE_Authority)
    {
        SRDebug::Message(SRDebug::ECategory::Projectile, TEXT("Server"), TEXT("Projectile spawn rejected: client"), 0.5f, FColor::Orange);
        return nullptr;
    }
    UWorld* World = GetWorld();
    if (!World || !ProjectileClass)
    {
        SRDebug::Message(SRDebug::ECategory::Projectile, TEXT("Server"), TEXT("Projectile spawn rejected: no class"), 0.5f, FColor::Orange);
        return nullptr;
    }
    FActorSpawnParameters Params;
    Params.Owner = GetOwner();
    Params.Instigator = Cast<APawn>(AbilityInstigator);
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ASRElementalProjectile* Projectile = World->SpawnActor<ASRElementalProjectile>(ProjectileClass, Location, Direction.Rotation(), Params);
    if (Projectile)
    {
        Projectile->InitializeProjectile(Direction, SpeedOverride > 0.f ? SpeedOverride : 4000.f,
            RangeOverride > 0.f ? RangeOverride : 8000.f, GetOwnerCharacter());
        SRDebug::Message(SRDebug::ECategory::Projectile, TEXT("Server"), FString::Printf(TEXT("Spawned %s at %s"), *GetNameSafe(Projectile), *Location.ToString()), 0.4f, FColor::Green);
        SRDebug::Log(SRDebug::ECategory::Projectile, TEXT("Server"), FString::Printf(TEXT("Spawned at %s dir=%s"), *Location.ToString(), *Direction.ToString()));
    }
    return Projectile;
}

bool UPSCombatComponent::ResolveShotAcknowledgment(int32 ServerShotId, int32 ClientShotId)
{
    // Duplicate acknowledgment guard (bounded ring of processed server shot ids).
    if (ProcessedConfirmedShotIds.Contains(ServerShotId))
    {
        SRDebug::PredictionLog(GetOwner(), FString::Printf(TEXT("Duplicate acknowledgment ignored: serverShot=%d clientShot=%d"), ServerShotId, ClientShotId));
        return false;
    }
    ProcessedConfirmedShotIds.Add(ServerShotId);
    if (ProcessedConfirmedShotIds.Num() > 64) ProcessedConfirmedShotIds.RemoveAt(0);

    const int32 PendingIndex = PendingPredictedShots.IndexOfByPredicate(
        [ClientShotId](const FPendingPredictedShot& P) { return P.ClientShotId == ClientShotId; });
    if (PendingIndex == INDEX_NONE)
    {
        SRDebug::PredictionLog(GetOwner(), FString::Printf(TEXT("ACK without prediction: serverShot=%d clientShot=%d - playing confirmed effects"), ServerShotId, ClientShotId));
        return false;
    }
    const double ConfirmDelay = FPlatformTime::Seconds() - PendingPredictedShots[PendingIndex].PredictedRealTime;
    PendingPredictedShots.RemoveAt(PendingIndex);
    SRDebug::PredictionLog(GetOwner(), FString::Printf(TEXT("ACK serverShot=%d clientShot=%d | pending=%d | display charges=%d | confirm delay=%.0fms | authoritative=%d"),
        ServerShotId, ClientShotId, PendingPredictedShots.Num(), GetDisplayCharges(), ConfirmDelay * 1000.f, ReplicatedState.PrimaryCharges));
    return true;
}

int32 UPSCombatComponent::PushPredictedShotForTest(double PredictedRealTime)
{
    const int32 ClientShotId = ++NextPredictedShotId;
    PendingPredictedShots.Add({ClientShotId, PredictedRealTime});
    if (PendingPredictedShots.Num() > 16)
    {
        PendingPredictedShots.RemoveAt(0);
        SRDebug::PredictionLog(GetOwner(), TEXT("Pending queue overflow: oldest predicted shot dropped (reconciled by authoritative charges)"));
    }
    return ClientShotId;
}

bool UPSCombatComponent::AcknowledgePredictedShotForTest(int32 ServerShotId, int32 ClientShotId)
{
    return ResolveShotAcknowledgment(ServerShotId, ClientShotId);
}
