#include "Abilities/SRAbilities.h"
#include "Combat/SRCombatComponent.h"
#include "Combat/SRGameplayQueries.h"
#include "GAS/PSAttributeLibrary.h"
#include "Combat/SRCombatPresentationComponent.h"
#include "Character/SRCombatDummy.h"
#include "Debug/SRDebug.h"
#include "Combat/SRElementalProjectile.h"
#include "Abilities/SRGameplayEffects.h"
#include "Abilities/SRGameplayTags.h"
#include "GAS/PSAttributeSet.h"
#include "Character/PSBaseCharacter.h"
#include "Character/PSCharacterMovementComponent.h"
#include "GAS/PSPlayerState.h"
#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "Abilities/Tasks/AbilityTask_WaitMovementModeChange.h"
#include "Camera/CameraComponent.h"
#include "Components/SphereComponent.h"
#include "EngineUtils.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace
{
UPSCombatComponent* GetHeroCombatComponent(const FGameplayAbilityActorInfo* ActorInfo)
{
    const AActor* Avatar = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
    return Avatar ? Avatar->FindComponentByClass<UPSCombatComponent>() : nullptr;
}
}

// ---------------------------------------------------------------------------
// Q — Arc Bolt
// ---------------------------------------------------------------------------

USRGameplayAbility_ArcBolt::USRGameplayAbility_ArcBolt()
{
    NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
    CooldownTag = SRGameplayTags::Tag_SR_Cooldown_Q;
    AbilityDisplayName = NSLOCTEXT("SR", "ArcBoltName", "Arc Bolt");
    CooldownDuration = 8.f;
    ProjectileClass = ASRElementalProjectile::StaticClass();
}

void USRGameplayAbility_ArcBolt::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
    // Q projectile and damage are created by the authority only.
    if (!ActorInfo || !ActorInfo->IsNetAuthority())
    {
        EndAbility(Handle, ActorInfo, ActivationInfo, false, false);
        return;
    }
    APSBaseCharacter* Hero = Cast<APSBaseCharacter>(GetAvatarActorFromActorInfo());
    UPSCombatComponent* Combat = GetHeroCombatComponent(ActorInfo);
    if (!Hero || !Combat || !ProjectileClass)
    {
        EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
        return;
    }

    FTransform FireOrigin;
    if (!Combat->GetFireOriginTransform(FireOrigin))
    {
        EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
        return;
    }

    // Shared aiming: camera intent, then authoritative spawn from the gauntlet.
    const FVector AimTarget = Combat->ResolveAimTarget(FireOrigin.GetLocation(), MaxProjectileRange);
    const FVector Direction = (AimTarget - FireOrigin.GetLocation()).GetSafeNormal();
    if (Direction.IsNearlyZero() || !CommitAbility(Handle, ActorInfo, ActivationInfo))
    {
        EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
        return;
    }
    ASRElementalProjectile* Projectile = Combat->SpawnAbilityProjectile(ProjectileClass, FireOrigin.GetLocation(),
        Direction, Hero, ProjectileSpeed, MaxProjectileRange);

    // Presentation: gauntlet discharge (muzzle flash, tracer toward the aim
    // point, audio hook, recoil) matching the primary-fire look.
    if (Projectile && Hero->GetPresentationComponent())
    {
        Hero->GetPresentationComponent()->PlayShotCosmetic(FireOrigin.GetLocation(), AimTarget, false);
    }
    PlayAbilityMontage();

    SRDebug::Message(SRDebug::ECategory::Ability, TEXT("Server"),
        Projectile ? TEXT("Arc Bolt launched: first-impact direct damage, splash and slow") : TEXT("Arc Bolt activated: projectile spawn FAILED"),
        0.6f, Projectile ? FColor::Green : FColor::Orange);

    // CommitAbility already started the cooldown (CommitExecute -> ApplyCooldown).
    EndAbility(Handle, ActorInfo, ActivationInfo, false, false);
}

// ---------------------------------------------------------------------------
// E — Static Leap
// ---------------------------------------------------------------------------

USRGameplayAbility_StaticLeap::USRGameplayAbility_StaticLeap()
{
    CooldownTag = SRGameplayTags::Tag_SR_Cooldown_E;
    AbilityDisplayName = NSLOCTEXT("SR", "StaticLeapName", "Static Leap");
    CooldownDuration = 12.f;
}

void USRGameplayAbility_StaticLeap::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
    APSBaseCharacter* Hero = Cast<APSBaseCharacter>(GetAvatarActorFromActorInfo());
    UPSCharacterMovementComponent* CMC = Hero ? Cast<UPSCharacterMovementComponent>(Hero->GetCharacterMovement()) : nullptr;
    if (!Hero || !CMC)
    {
        EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
        return;
    }

    // Movement-state gate BEFORE commitment: a rejected activation never spends the cooldown.
    if (Hero->IsPSSliding() || Hero->IsPSTraversing() || Hero->IsPSDead() || Hero->IsPSRooted())
    {
        SRDebug::Message(SRDebug::ECategory::MovementAbility, TEXT("Server"), TEXT("Leap rejected: sliding/vaulting/dead/rooted"), 0.6f, FColor::Orange);
        EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
        return;
    }
    if (!bAllowLeapWhileFalling && !CMC->IsMovingOnGround())
    {
        SRDebug::Message(SRDebug::ECategory::MovementAbility, TEXT("Server"), TEXT("Leap rejected: airborne"), 0.6f, FColor::Orange);
        EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
        return;
    }

    // Direction: current movement input, else control-rotation facing (pawn-view
    // basis — GetPlayerViewPoint degenerates to straight-down without a camera,
    // collapsing the horizontal direction to zero and killing the dash).
    FVector Direction = Hero->GetLastMovementInputVector().GetSafeNormal2D();
    if (Direction.IsNearlyZero())
    {
        Direction = Hero->GetControlRotation().Vector().GetSafeNormal2D();
    }
    if (Direction.IsNearlyZero())
    {
        Direction = Hero->GetActorForwardVector().GetSafeNormal2D();
    }
    Direction = Direction.GetSafeNormal2D();
    if (Direction.IsNearlyZero())
    {
        SRDebug::Message(SRDebug::ECategory::MovementAbility, TEXT("Server"), TEXT("Leap rejected: no direction"), 0.6f, FColor::Orange);
        EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
        return;
    }

    // Path validation BEFORE commitment: a rejected leap must not consume the
    // cooldown. The query capsule is shorter than the character capsule so it
    // stays clear of the ground — otherwise the floor underfoot registers as a
    // capsule-sized obstruction at distance ~0 and every leap is rejected.
    LeapDistance = MaxLeapDistance;
    const FVector Start = Hero->GetActorLocation();
    const FVector SweepEnd = Start + Direction * MaxLeapDistance;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(SRLeapPath), false, Hero);
    FHitResult PathHit;
    const bool bPathBlocked = GetWorld()->SweepSingleByChannel(PathHit, Start, SweepEnd, FQuat::Identity, ECC_Pawn,
        FCollisionShape::MakeCapsule(40.f, 80.f), Params)
        && !PathHit.bStartPenetrating && PathHit.Distance > 10.f;
    if (bPathBlocked)
    {
        const float ClearDistance = FVector::Dist(Start, PathHit.ImpactPoint);
        LeapDistance = FMath::Max(0.f, ClearDistance - 50.f);
        if (LeapDistance < 100.f)
        {
            SRDebug::Message(SRDebug::ECategory::MovementAbility, TEXT("Server"), TEXT("Leap rejected: obstruction too close"), 0.6f, FColor::Orange);
            SRDebug::Log(SRDebug::ECategory::MovementAbility, TEXT("Server"), TEXT("Leap rejected: obstruction too close"));
            EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
            return;
        }
    }

    if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
    {
        EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
        return;
    }
    LeapDirection = Direction;
    bLandingBurstApplied = false;

    // Normal falling physics owns vertical motion and collision. The horizontal
    // drive lasts for the nominal flight time; an obstacle can end it early.
    const float FlightTime = FMath::Max(0.1f, LeapDuration);
    const float DashSpeed = LeapDistance / FlightTime;
    if (!CMC->StartPSLeapDash(Direction, DashSpeed, FlightTime))
    {
        EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
        return;
    }
    CMC->OnPSLeapDashFinished.AddDynamic(this, &USRGameplayAbility_StaticLeap::OnLeapDashFinished);
    UAbilityTask_WaitMovementModeChange* LandTask = UAbilityTask_WaitMovementModeChange::CreateWaitMovementModeChange(this, MOVE_Walking);
    LandTask->OnChange.AddDynamic(this, &USRGameplayAbility_StaticLeap::OnLanded);
    LandTask->ReadyForActivation();
    CMC->SetMovementMode(MOVE_Falling);
    const float VerticalSpeed = FMath::Max(150.f, FMath::Abs(CMC->GetGravityZ()) * FlightTime * 0.5f);
    Hero->LaunchCharacter(Direction * DashSpeed + FVector::UpVector * VerticalSpeed, true, true);

    // Presentation: takeoff sparks on everyone, camera punch instantly on the leaping player.
    if (USRCombatPresentationComponent* Presentation = Hero->GetPresentationComponent())
    {
        Presentation->PlayLeapTakeoff(Start, Direction);
    }
    if (Hero->IsLocallyControlled())
    {
        if (USRCombatPresentationComponent* Presentation = Hero->GetPresentationComponent())
        {
            Presentation->PlayLeapCameraImpulse();
        }
    }
    PlayAbilityMontage();

    SRDebug::Message(SRDebug::ECategory::MovementAbility, TEXT("Server"),
        FString::Printf(TEXT("Leap dash started dir=%s dist=%.0f speed=%.0f"), *Direction.ToString(), LeapDistance, DashSpeed), 0.6f, FColor::Cyan);
    SRDebug::Log(SRDebug::ECategory::MovementAbility, TEXT("Server"),
        FString::Printf(TEXT("Leap dash started dir=%s dist=%.0f speed=%.0f"), *Direction.ToString(), LeapDistance, DashSpeed));
}

void USRGameplayAbility_StaticLeap::OnLeapDashFinished(bool bWasObstructed)
{
    // Detach now so EndAbility's forced dash teardown cannot re-broadcast.
    if (const APSBaseCharacter* Hero = Cast<APSBaseCharacter>(GetAvatarActorFromActorInfo()))
    {
        if (auto* CMC = Cast<UPSCharacterMovementComponent>(Hero->GetCharacterMovement()))
        {
            CMC->OnPSLeapDashFinished.RemoveDynamic(this, &USRGameplayAbility_StaticLeap::OnLeapDashFinished);
        }
    }
    SRDebug::Message(SRDebug::ECategory::MovementAbility, TEXT("Server"),
        bWasObstructed ? TEXT("Leap dash ended early: obstruction") : TEXT("Leap dash finished"), 0.5f, FColor::Cyan);
    SRDebug::Log(SRDebug::ECategory::MovementAbility, TEXT("Server"),
        bWasObstructed ? TEXT("Leap dash ended early: obstruction") : TEXT("Leap dash finished"));

    APSBaseCharacter* Hero = Cast<APSBaseCharacter>(GetAvatarActorFromActorInfo());
    if (Hero && Hero->GetCharacterMovement() && !Hero->GetCharacterMovement()->IsMovingOnGround())
    {
        // Landing task was armed at takeoff. A timeout/wall hit is not a landing.
        return;
    }
    ExecuteLandingBurstInternal();
}

void USRGameplayAbility_StaticLeap::OnLanded(EMovementMode NewMovementMode)
{
    if (IsActive() && NewMovementMode == MOVE_Walking) ExecuteLandingBurstInternal();
}

void USRGameplayAbility_StaticLeap::ExecuteLandingBurstAt(APSBaseCharacter* Hero)
{
    UPSCombatComponent* Combat = Hero ? Hero->GetCombatComponent() : nullptr;
    if (!Hero || !Hero->HasAuthority() || !Combat) return;

    const FVector Center = Hero->GetActorLocation();
    SRDebug::Message(SRDebug::ECategory::MovementAbility, TEXT("Server"),
        FString::Printf(TEXT("Landing burst at %s radius=%.0f"), *Center.ToString(), LandingBurstRadius), 0.6f, FColor::Cyan);

    // Presentation: expanding electric shockwave ring at the landing point.
    if (USRCombatPresentationComponent* Presentation = Hero->GetPresentationComponent())
    {
        Presentation->PlayLeapLand(Center, LandingBurstRadius);
    }

#if !UE_BUILD_SHIPPING
    if (Hero->GetWorld() && SRDebug::IsEnabled(SRDebug::ECategory::MovementAbility))
    {
        DrawDebugSphere(Hero->GetWorld(), Center, LandingBurstRadius, 12, FColor::Cyan, false, 2.f);
    }
#endif

    TArray<FOverlapResult> Overlaps;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(SRLeapBurst), false, Hero);
    Hero->GetWorld()->OverlapMultiByObjectType(Overlaps, Center, FQuat::Identity,
        FCollisionObjectQueryParams::InitType::AllDynamicObjects, FCollisionShape::MakeSphere(LandingBurstRadius), Params);

    TSet<TObjectKey<AActor>> Damaged;
    for (const FOverlapResult& Overlap : Overlaps)
    {
        AActor* Candidate = Overlap.GetActor();
        if (!Candidate || Candidate == Hero || Damaged.Contains(Candidate)) continue;
        if (Cast<APawn>(Candidate) == nullptr && Cast<APSCombatDummy>(Candidate) == nullptr) continue;
        if (!SRGameplayQueries::HasCoverFreePath(Hero->GetWorld(), Hero, Candidate, Center)) continue;

        bool bEnemy = false;
        const float Applied = Combat->ApplyCombatDamage(Candidate, LandingDamage, Hero, bEnemy);
        if (Applied > 0.f) Damaged.Add(Candidate);
    }
}

void USRGameplayAbility_StaticLeap::ExecuteLandingBurstInternal()
{
    if (bLandingBurstApplied) return;
    bLandingBurstApplied = true;
    APSBaseCharacter* Hero = Cast<APSBaseCharacter>(GetAvatarActorFromActorInfo());
    ExecuteLandingBurstAt(Hero);
    EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, false, false);
}

void USRGameplayAbility_StaticLeap::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
    // Unbind first, then force the dash closed so its broadcast finds no listeners
    // (a re-entrant EndAbility from the delegate would trip the GAS end guard).
    if (APSBaseCharacter* Hero = Cast<APSBaseCharacter>(GetAvatarActorFromActorInfo()))
    {
        if (auto* CMC = Cast<UPSCharacterMovementComponent>(Hero->GetCharacterMovement()))
        {
            CMC->OnPSLeapDashFinished.RemoveDynamic(this, &USRGameplayAbility_StaticLeap::OnLeapDashFinished);
            CMC->EndPSLeapDash(bWasCancelled);
        }
    }
    SRDebug::Log(SRDebug::ECategory::MovementAbility, TEXT("Server"), bWasCancelled ? TEXT("Leap cancelled") : TEXT("Leap ended"));
    Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

// ---------------------------------------------------------------------------
// C — Overcharge
// ---------------------------------------------------------------------------

USRGameplayAbility_Overcharge::USRGameplayAbility_Overcharge()
{
    NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
    CooldownTag = SRGameplayTags::Tag_SR_Cooldown_C;
    AbilityDisplayName = NSLOCTEXT("SR", "OverchargeName", "Overcharge");
    CooldownDuration = 18.f;
}

void USRGameplayAbility_Overcharge::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
    UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
    if (!ActorInfo || !ActorInfo->IsNetAuthority() || !ASC ||
        ASC->HasMatchingGameplayTag(SRGameplayTags::Tag_Status_Buff_Overcharge))
    {
        EndAbility(Handle, ActorInfo, ActivationInfo, false, false);
        return;
    }
    if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
    {
        EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
        return;
    }

    // Timed buff: the effect grants Status.Buff.Overcharge for its duration and
    // expires automatically. The cooldown (> duration) makes self-stacking impossible.
    if (ASC)
    {
        const FGameplayEffectSpecHandle Spec = MakeOutgoingGameplayEffectSpec(UGE_SROvercharge::StaticClass(), GetAbilityLevel(Handle, ActorInfo));
        if (Spec.IsValid())
        {
            BuffHandle = ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
            if (!BuffHandle.IsValid())
            {
                EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
                return;
            }
            const UPSCombatComponent* Combat = GetHeroCombatComponent(ActorInfo);
            const float EffectiveInterval = Combat ? Combat->GetEffectiveFireInterval() : 0.f;
            const float EffectiveRecharge = Combat ? Combat->GetEffectiveRechargeDuration() : 0.f;
            SRDebug::Message(SRDebug::ECategory::Ability, TEXT("Server"), TEXT("Overcharge activated: fire/recharge multipliers active"), 0.8f, FColor::Yellow);
            SRDebug::Log(SRDebug::ECategory::Ability, TEXT("Server"),
                FString::Printf(TEXT("Effective fire interval=%.3fs effective recharge=%.2fs"), EffectiveInterval, EffectiveRecharge));
        }
    }

    UE_LOG(LogProjectSAbilities, Display, TEXT("Overcharge activated: buff applied for 6s"));
    // An active recharge restarts at the improved rate (multiplier change mid-recharge).
    if (APSBaseCharacter* Hero = Cast<APSBaseCharacter>(GetAvatarActorFromActorInfo()))
    {
        if (UPSCombatComponent* Combat = Hero->GetCombatComponent())
        {
            Combat->RestartRechargeIfActive();
            Combat->RefreshAutomaticFireInterval();
        }
    }
    if (!BuffHandle.IsValid())
    {
        EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
        return;
    }
    const float Duration = ASC->GetGameplayEffectDuration(BuffHandle);
    UAbilityTask_WaitDelay* ExpiryTask = UAbilityTask_WaitDelay::WaitDelay(this, FMath::Max(0.01f, Duration));
    ExpiryTask->OnFinish.AddDynamic(this, &USRGameplayAbility_Overcharge::OnBuffExpired);
    ExpiryTask->ReadyForActivation();
}

void USRGameplayAbility_Overcharge::OnBuffExpired()
{
    EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void USRGameplayAbility_Overcharge::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
    if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo(); ASC && BuffHandle.IsValid())
        ASC->RemoveActiveGameplayEffect(BuffHandle);
    BuffHandle.Invalidate();
    const APSBaseCharacter* Hero = ActorInfo ? Cast<APSBaseCharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
    if (UPSCombatComponent* Combat = GetHeroCombatComponent(ActorInfo); Combat && Hero && !Hero->IsPSDead())
    {
        Combat->RestartRechargeIfActive();
        Combat->RefreshAutomaticFireInterval();
    }
    Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

// ---------------------------------------------------------------------------
// X — Stormpiercer
// ---------------------------------------------------------------------------

USRGameplayAbility_Stormpiercer::USRGameplayAbility_Stormpiercer()
{
    NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
    CooldownTag = SRGameplayTags::Tag_SR_Cooldown_X;
    AbilityDisplayName = NSLOCTEXT("SR", "StormpiercerName", "Stormpiercer");
    CooldownDuration = 0.f; // the ultimate charge is the activation cost
}

void USRGameplayAbility_Stormpiercer::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
    APSBaseCharacter* CastingHero = Cast<APSBaseCharacter>(GetAvatarActorFromActorInfo());
    if (!ActorInfo || !ActorInfo->IsNetAuthority() || !CastingHero || !CastingHero->GetCombatComponent()
        || !CommitAbility(Handle, ActorInfo, ActivationInfo))
    {
        EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
        return;
    }
    // Use the shared instant Gameplay Effect for an authoritative, single charge spend.
    if (!UPSAttributeLibrary::TrySpendUltimateCharge(CastingHero))
    {
        EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
        return;
    }

    PulsesExecuted = 0;
    SRDebug::Message(SRDebug::ECategory::Ability, TEXT("Server"), FString::Printf(TEXT("Stormpiercer windup %.2fs"), WindupDuration), 0.8f, FColor::Magenta);

    // Presentation: growing charge glow in the gauntlets on every client.
    if (APSBaseCharacter* Hero = Cast<APSBaseCharacter>(GetAvatarActorFromActorInfo()))
    {
        FTransform ChargeOrigin;
        if (Hero->GetCombatComponent() && Hero->GetCombatComponent()->GetFireOriginTransform(ChargeOrigin))
        {
            if (USRCombatPresentationComponent* Presentation = Hero->GetPresentationComponent())
            {
                Presentation->PlayStormpiercerWindup(ChargeOrigin.GetLocation());
            }
        }
    }
    PlayAbilityMontage();

    UAbilityTask_WaitDelay* WindupTask = UAbilityTask_WaitDelay::WaitDelay(this, WindupDuration);
    WindupTask->OnFinish.AddDynamic(this, &USRGameplayAbility_Stormpiercer::OnWindupFinished);
    WindupTask->ReadyForActivation();
}

void USRGameplayAbility_Stormpiercer::OnWindupFinished()
{
    StartNextPulse();
}

void USRGameplayAbility_Stormpiercer::StartNextPulse()
{
    if (!IsActive()) return;
    if (PulsesExecuted >= PulseCount)
    {
        SRDebug::Message(SRDebug::ECategory::Ability, TEXT("Server"), TEXT("Ultimate complete"), 0.8f, FColor::Magenta);
        EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, false, false);
        return;
    }
    APSBaseCharacter* ActiveHero = Cast<APSBaseCharacter>(GetAvatarActorFromActorInfo());
    if (!ActiveHero || !ActiveHero->HasAuthority() || ActiveHero->IsPSDead())
    {
        EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
        return;
    }
    ExecuteBeamPulse(++PulsesExecuted, ActiveHero);
    if (PulsesExecuted >= PulseCount)
    {
        EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
        return;
    }
    bPulsePending = true;
    UAbilityTask_WaitDelay* SpacingTask = UAbilityTask_WaitDelay::WaitDelay(this, PulseSpacing);
    SpacingTask->OnFinish.AddDynamic(this, &USRGameplayAbility_Stormpiercer::OnPulseDelayFinished);
    SpacingTask->ReadyForActivation();
}

void USRGameplayAbility_Stormpiercer::OnPulseDelayFinished()
{
    bPulsePending = false;
    StartNextPulse();
}

void USRGameplayAbility_Stormpiercer::ExecuteBeamPulse(int32 PulseIndex, APSBaseCharacter* BeamHero)
{
    // Param-based hero access: safe on the ability instance or CDO (verification calls).
    APSBaseCharacter* Hero = BeamHero;
    UPSCombatComponent* Combat = Hero ? Hero->GetCombatComponent() : nullptr;
    APSPlayerState* HeroState = Hero ? Hero->GetPlayerState<APSPlayerState>() : nullptr;
    UWorld* BeamWorld = Hero ? Hero->GetWorld() : GetWorld();
    if (!Hero || !Hero->HasAuthority() || Hero->IsPSDead() || !Combat || !BeamWorld) return;

    FTransform FireOrigin;
    if (!Combat->GetFireOriginTransform(FireOrigin)) return;
    const FVector Origin = FireOrigin.GetLocation();
    const FVector AimTarget = Combat->ResolveAimTarget(Origin, BeamRange);
    const FVector Direction = (AimTarget - Origin).GetSafeNormal();
    if (Direction.IsNearlyZero() || Direction.ContainsNaN()) return;
    const FVector BeamEnd = Origin + Direction * BeamRange;

    // Collect actual collision shapes across the full width. A narrow wall on
    // one side must not truncate every other lane of the wide beam.
    TArray<AActor*> Candidates;
    SRGameplayQueries::SweepBoxTargets(BeamWorld, Hero, Origin + Direction,
        BeamEnd - Direction, FVector(1.f, BeamRadius, BeamRadius), Direction.ToOrientationQuat(),
        Candidates, SRDebug::IsEnabled(SRDebug::ECategory::Ability));

    // Presentation: wide electric beam between the gauntlet and the wall/end
    // point, multicast to every client from the hero's presentation component.
    if (USRCombatPresentationComponent* Presentation = Hero->GetPresentationComponent())
    {
        Presentation->PlayStormpiercerBeam(Origin, BeamEnd, PulseIndex);
    }

    SRDebug::Message(SRDebug::ECategory::Ability, TEXT("Server"),
        FString::Printf(TEXT("Beam pulse %d/%d candidates=%d"), PulseIndex, PulseCount, Candidates.Num()), 0.5f, FColor::Magenta);

    // One hit per actor in this pulse; the next pulse gets a fresh hit set.
    TSet<TObjectKey<AActor>> DamagedThisPulse;
    auto ConsiderTarget = [&](AActor* Candidate)
    {
        if (!Candidate || Candidate == Hero || DamagedThisPulse.Contains(Candidate)) return;
        if (!SRGameplayQueries::HasCoverFreePath(BeamWorld, Hero, Candidate, Origin)) return;

        bool bEnemy = false;
        const float Applied = Combat->ApplyCombatDamage(Candidate, DamagePerPulse, Hero, bEnemy);
        DamagedThisPulse.Add(Candidate);
        SRDebug::Log(SRDebug::ECategory::Ability, TEXT("Server"),
            FString::Printf(TEXT("Beam candidate=%s applied=%.1f"), *GetNameSafe(Candidate), Applied));
        if (Applied > 0.f)
        {
            SRDebug::Message(SRDebug::ECategory::Damage, TEXT("Server"),
                FString::Printf(TEXT("Pulse %d -> %s for %.0f"), PulseIndex, *GetNameSafe(Candidate), Applied), 0.5f, FColor::Magenta);
            // Minimap-only last-known-position reveal for enemy PLAYER pawns.
            const APawn* TargetPawn = Cast<APawn>(Candidate);
            APSPlayerState* TargetState = TargetPawn ? TargetPawn->GetPlayerState<APSPlayerState>() : nullptr;
            const bool bEnemyPlayerPawn = bEnemy && TargetState && HeroState && TargetState->TeamId != HeroState->TeamId;
            if (bEnemyPlayerPawn && HeroState)
            {
                int32 RevealId;
                if (HeroState->AddTeamPing(Candidate->GetActorLocation(), SRGameplayTags::Tag_SR_Ping_Reveal, RevealLifetime, RevealId))
                {
                    SRDebug::Message(SRDebug::ECategory::Minimap, TEXT("Server"),
                        FString::Printf(TEXT("Reveal created #%d for %s last-known position"), RevealId, *GetNameSafe(Candidate)), 0.8f, FColor::Red);
                }
            }
        }
    };
    for (AActor* Candidate : Candidates) ConsiderTarget(Candidate);
}

void USRGameplayAbility_Stormpiercer::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
    bPulsePending = false;
    SRDebug::Log(SRDebug::ECategory::Ability, TEXT("Server"), bWasCancelled ? TEXT("Ultimate cancelled") : TEXT("Ultimate ended"));
    Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
