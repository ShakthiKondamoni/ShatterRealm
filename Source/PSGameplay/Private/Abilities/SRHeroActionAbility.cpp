#include "Abilities/SRHeroActionAbility.h"
#include "Abilities/SRHeroGameplayLibrary.h"
#include "Abilities/SRGameplayTags.h"
#include "Character/PSBaseCharacter.h"
#include "Character/PSCharacterMovementComponent.h"
#include "Character/SRHeroGameplayComponent.h"
#include "Combat/SRHeroGameplayActors.h"
#include "Combat/SRCombatComponent.h"
#include "Combat/SRGameplayQueries.h"
#include "GAS/PSAttributeSet.h"
#include "GAS/PSAttributeLibrary.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"
#include "DrawDebugHelpers.h"
#include "Debug/SRDebug.h"

USRHeroActionAbility::USRHeroActionAbility()
{ NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly; }
void USRHeroActionAbility::ConfigureAction(ESRHeroAction Action, const TCHAR* Name, FGameplayTag SlotCooldown, float Cooldown, bool bUltimate)
{
    Config = SRDefaultActionConfig(Action); AbilityDisplayName = FText::FromString(Name);
    CooldownTag = SlotCooldown; CooldownDuration = Cooldown; bUsesUltimateCharge = bUltimate;
}
APSBaseCharacter* USRHeroActionAbility::Caster() const { return Cast<APSBaseCharacter>(GetAvatarActorFromActorInfo()); }
bool USRHeroActionAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
    if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags)) return false;
    const UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
    const UPSAttributeSet* Attributes = ASC ? ASC->GetSet<UPSAttributeSet>() : nullptr;
    const AActor* Avatar = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
    const auto* State = Avatar ? Avatar->FindComponentByClass<USRHeroGameplayComponent>() : nullptr;
    if (Config.Action == ESRHeroAction::ParadoxEcho && State && State->HasEcho()) return false;
    return !bUsesUltimateCharge || (Attributes && Attributes->GetMaxUltimateCharge() > 0.f
        && Attributes->GetUltimateCharge() >= Attributes->GetMaxUltimateCharge());
}
bool USRHeroActionAbility::CommitAction()
{
    APSBaseCharacter* Character = Caster();
    const UAbilitySystemComponent* ASC = Character ? Character->GetAbilitySystemComponent() : nullptr;
    if (!Character || !Character->HasAuthority() || !SRHeroGameplay::IsAlive(Character) || !ASC
        || ASC->HasMatchingGameplayTag(SRGameplayTags::Tag_State_Stunned)
        || !CommitAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo)
        || (bUsesUltimateCharge && !UPSAttributeLibrary::TrySpendUltimateCharge(Character)))
    { Finish(true); return false; }
    SRDebug::Log(SRDebug::ECategory::Ability, TEXT("Server"), FString::Printf(TEXT("Accepted %s"), *AbilityDisplayName.ToString()));
    return true;
}
void USRHeroActionAbility::Finish(bool bCancelled)
{ EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, bCancelled); }
void USRHeroActionAbility::CancelFromTargeting() { if (IsActive()) Finish(true); }
void USRHeroActionAbility::StartTicking(float Duration)
{
    StartedAt = LastTickAt = GetWorld()->GetTimeSeconds(); EndsAt = StartedAt + FMath::Max(0.05f, Duration);
    HealingAccumulator = 0.f;
    GetWorld()->GetTimerManager().SetTimer(ActionTimer, this, &ThisClass::TickAction, 0.05f, true);
}
AActor* USRHeroActionAbility::SelectAlly(bool bDowned, bool bAllowSelf) const
{
    APSBaseCharacter* Character = Caster();
    if (!Character) return nullptr;
    AActor* Best = nullptr;
    float BestScore = bDowned ? -2.f : 0.6f;
    const FVector View = Character->GetPawnViewLocation(), Aim = Character->GetControlRotation().Vector();
    for (AActor* Target : SRHeroGameplay::InRadius(Character, Character->GetActorLocation(), Config.Range))
    {
        if (Target == Character || !SRHeroGameplay::SameTeam(Character, Target)) continue;
        const USRHeroGameplayComponent* State = Target->FindComponentByClass<USRHeroGameplayComponent>();
        if (bDowned ? (!State || State->GetLifeState() != ESRHeroLifeState::Downed) : !SRHeroGameplay::IsAlive(Target)) continue;
        if (!SRHeroGameplay::Cover(Character, Target, View)) continue;
        const float Score = FVector::DotProduct(Aim, (Target->GetActorLocation() - View).GetSafeNormal())
            - static_cast<float>(FVector::Dist(View, Target->GetActorLocation())) / FMath::Max(1.f, Config.Range) * 0.1f;
        if (Score > BestScore) { Best = Target; BestScore = Score; }
    }
    if (!Best && bAllowSelf && !bDowned && SRHeroGameplay::IsAlive(Character)) return Character;
    return Best;
}
void USRHeroActionAbility::Punch(const FVector& Origin, const FVector& Forward, float Reach)
{
    APSBaseCharacter* Character = Caster(); if (!Character) return;
    TArray<AActor*> Targets;
    SRGameplayQueries::SweepBoxTargets(GetWorld(), Character, Origin + Forward, Origin + Forward * Reach,
        FVector(1.f, Config.Width * 0.5f, Config.Height * 0.5f), Forward.ToOrientationQuat(), Targets,
        Config.bDrawDebug || SRDebug::IsEnabled(SRDebug::ECategory::Ability));
    for (AActor* Target : Targets)
        if (SRHeroGameplay::IsEnemy(Character, Target) && SRHeroGameplay::Cover(Character, Target, Origin))
            if (SRHeroGameplay::Damage(Character, Target, Config.Damage) > 0.f)
                SRHeroGameplay::Push(Target, Forward, Config.Knockback); // Strict attack direction, never radial.
}
bool USRHeroActionAbility::SpawnBarrierGroup(const FVector& Ground)
{
    TArray<FTransform> Placement;
    APSBaseCharacter* Character = Caster();
    if (!SRHeroGameplay::BarrierFormation(Character, Ground, Config, Placement)) return false;
    FSRHeroActionConfig Section = Config;
    Section.Width = Config.Width / Placement.Num();
    for (const FTransform& Transform : Placement)
    {
        FActorSpawnParameters Params; Params.Owner = Character;
        ASRHeroBarrier* Barrier = GetWorld()->SpawnActor<ASRHeroBarrier>(ASRHeroBarrier::StaticClass(), Transform, Params);
        if (Barrier) Barrier->Initialize(Character, Section);
    }
    return true;
}
void USRHeroActionAbility::SpawnMovingVolume()
{
    APSBaseCharacter* Character = Caster(); if (!Character) return;
    FVector Origin = Character->GetActorLocation() + Direction * 80.f;
    if (Config.Action == ESRHeroAction::RiftDagger)
    {
        FTransform Muzzle;
        if (Character->GetCombatComponent()->GetFireOriginTransform(Muzzle))
        {
            Origin = Muzzle.GetLocation();
            Direction = (Character->GetCombatComponent()->ResolveAimTarget(Origin, Config.Range) - Origin).GetSafeNormal();
        }
    }
    else
    {
        FVector Ground;
        if (SRHeroGameplay::Ground(GetWorld(), Character, Origin, Ground)) Origin.Z = Ground.Z + Config.Height * 0.5f + 3.f;
    }
    FActorSpawnParameters Params; Params.Owner = Character; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ASRHeroMovingVolume* Volume = GetWorld()->SpawnActor<ASRHeroMovingVolume>(Origin, Direction.Rotation(), Params);
    if (Volume) Volume->Initialize(Character, Config, Direction);
}
void USRHeroActionAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
    APSBaseCharacter* Character = Caster();
    if (!ActorInfo || !ActorInfo->IsNetAuthority() || !Character || !Character->GetCombatComponent())
    { Finish(true); return; }
    USRHeroGameplayComponent* State = Character->FindComponentByClass<USRHeroGameplayComponent>();
    if (!State) { Finish(true); return; }
    Direction = SRHeroGameplay::Facing(Character);
    if (Config.Action == ESRHeroAction::BlinkCut || Config.Action == ESRHeroAction::FlameStep || Config.Action == ESRHeroAction::FaultDash)
    {
        const FVector Input = Character->GetLastMovementInputVector().GetSafeNormal2D();
        if (!Input.IsNearlyZero()) Direction = Input;
    }
    if (Direction.IsNearlyZero()) Direction = Character->GetActorForwardVector();
    MapSelections.Reset(); ChannelTargets.Reset(); DashHits.Reset(); OwnedEffects.Reset(); PendingTree.Reset();
    bOwnsDash = bWaitingForMap = false;
    FVector Ground;
    switch (Config.Action)
    {
    case ESRHeroAction::PhoenixMeteorfall:
    case ESRHeroAction::PhantomProcession:
    case ESRHeroAction::PrismSkyfall:
        bWaitingForMap = true;
        State->BeginMapSelection(this, Config.Action == ESRHeroAction::PhantomProcession ? 2 : 1);
        StartTicking(20.f); return; // Commit only after valid server-confirmed positions.
    case ESRHeroAction::BlinkCut:
    {
        if (Character->IsPSRooted() || Character->IsPSSliding() || Character->IsPSTraversing()) { Finish(true); return; }
        FVector Desired = Character->GetActorLocation() + Direction * Config.Range;
        const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
        FCollisionQueryParams Params(SCENE_QUERY_STAT(SRBlinkPath), false, Character);
        FHitResult Hit;
        if (GetWorld()->SweepSingleByChannel(Hit, Character->GetActorLocation(), Desired, FQuat::Identity, ECC_Pawn,
            FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight() - 2.f), Params))
        {
            if (Hit.bStartPenetrating) { Finish(true); return; }
            Desired = Hit.Location - Direction * 5.f;
        }
        FVector Destination;
        if (!SRHeroGameplay::SafeDestination(Character, Desired, Destination)
            || FVector::Dist2D(Destination, Character->GetActorLocation()) < 100.f) { Finish(true); return; }
        if (!CommitAction()) return;
        if (Character->TeleportTo(Destination, Character->GetActorRotation(), false, true))
            Punch(Character->GetActorLocation(), Direction, Config.AttackReach);
        Finish(); return;
    }
    case ESRHeroAction::InfernoPunch:
    case ESRHeroAction::StoneKnuckle:
        if (!CommitAction()) return;
        Punch(Character->GetActorLocation(), Direction, Config.Range); Finish(); return;
    case ESRHeroAction::FlameStep:
    case ESRHeroAction::FaultDash:
    case ESRHeroAction::IronRam:
    {
        UPSCharacterMovementComponent* Movement = Character->GetPSMovement();
        if (!Movement || !Movement->IsMovingOnGround() || Movement->IsPSLeapDashActive() || Character->IsPSRooted()
            || Character->IsPSSliding() || Character->IsPSTraversing()) { Finish(true); return; }
        if (!CommitAction()) return;
        bOwnsDash = Movement->StartPSLeapDash(Direction, Config.Range / FMath::Max(0.1f, Config.Duration), Config.Duration);
        if (!bOwnsDash) { Finish(true); return; }
        PreviousLocation = Character->GetActorLocation(); StartTicking(Config.Duration + 0.1f); return;
    }
    case ESRHeroAction::RiftDagger:
    case ESRHeroAction::RiftReaper:
    case ESRHeroAction::FaultlineTitan:
    case ESRHeroAction::TidalMend:
    case ESRHeroAction::TidalCurtain:
        if (!CommitAction()) return;
        SpawnMovingVolume(); Finish(); return;
    case ESRHeroAction::BriarSnare:
    case ESRHeroAction::SolarFlare:
    case ESRHeroAction::ShardSpike:
    case ESRHeroAction::CrystalBind:
    {
        FTransform Origin;
        if (!Character->GetCombatComponent()->GetFireOriginTransform(Origin)) { Finish(true); return; }
        const FVector Aim = Character->GetCombatComponent()->ResolveAimTarget(Origin.GetLocation(), Config.Range);
        FVector ProjectileDirection = (Aim - Origin.GetLocation()).GetSafeNormal();
        if (Config.ProjectileGravity > 0.f) ProjectileDirection = (ProjectileDirection + FVector::UpVector * 0.2f).GetSafeNormal();
        if (ProjectileDirection.IsNearlyZero()) { Finish(true); return; }
        if (!CommitAction()) return;
        FActorSpawnParameters Params; Params.Owner = Character; Params.Instigator = Character;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        ASRHeroProjectile* Projectile = GetWorld()->SpawnActor<ASRHeroProjectile>(Origin.GetLocation(), ProjectileDirection.Rotation(), Params);
        if (Projectile) Projectile->Initialize(Character, Config, ProjectileDirection);
        Finish(); return;
    }
    case ESRHeroAction::HeatSurge:
    case ESRHeroAction::FortressStance:
    case ESRHeroAction::FadeStep:
        if (!CommitAction()) return;
        OwnedEffects.Add(SRHeroGameplay::Buff(Character, Character, Config));
        if (Config.Action == ESRHeroAction::FadeStep)
            OwnedEffects.Add(SRHeroGameplay::Status(Character, Character, SRGameplayTags::Tag_State_Concealed, Config.Duration));
        Character->GetCombatComponent()->RefreshAutomaticFireInterval();
        Character->GetCombatComponent()->RestartRechargeIfActive();
        StartTicking(Config.Duration); return;
    case ESRHeroAction::MirrorDouble:
    {
        FVector Position;
        if (!SRHeroGameplay::SafeDestination(Character, Character->GetActorLocation() + Direction * 180.f, Position)) { Finish(true); return; }
        if (!CommitAction()) return;
        FActorSpawnParameters Params; Params.Owner = Character;
        ASRHeroDecoy* Decoy = GetWorld()->SpawnActor<ASRHeroDecoy>(Position, Direction.Rotation(), Params);
        if (Decoy) Decoy->Initialize(Character, Config.Duration, Config.StructureHealth, true);
        Finish(); return;
    }
    case ESRHeroAction::VitalPulse:
        if (!CommitAction()) return;
        for (AActor* Target : SRHeroGameplay::InRadius(Character, Character->GetActorLocation(), Config.Radius))
            if (SRHeroGameplay::Cover(Character, Target, Character->GetActorLocation())) SRHeroGameplay::Heal(Character, Target, Config.Healing);
        Finish(); return;
    case ESRHeroAction::BarkWard:
    case ESRHeroAction::RadiantWard:
    case ESRHeroAction::ThornGuard:
    case ESRHeroAction::RewindMend:
    {
        AActor* Target = SelectAlly(false, true);
        if (!Target) { Finish(true); return; }
        if (Config.Action == ESRHeroAction::RewindMend)
        {
            auto* TargetState = Target->FindComponentByClass<USRHeroGameplayComponent>();
            const auto* Attributes = SRHeroGameplay::ASC(Target)->GetSet<UPSAttributeSet>();
            if (!TargetState || !Attributes || Attributes->GetHealth() >= Attributes->GetMaxHealth()
                || TargetState->RecentHealthLoss(Config.EffectDuration) <= 0.f) { Finish(true); return; }
            if (!CommitAction()) return;
            TargetState->RewindHealthLoss(Config.Healing, Config.EffectDuration);
        }
        else
        {
            if (!CommitAction()) return;
            if (Config.Action == ESRHeroAction::RadiantWard)
            { if (auto* TargetState = Target->FindComponentByClass<USRHeroGameplayComponent>()) TargetState->AddWard(Config.WardHealth, Config.Duration, Character); }
            else SRHeroGameplay::Buff(Target, Character, Config);
        }
        Finish(); return;
    }
    case ESRHeroAction::Sunbeam:
    case ESRHeroAction::LastSeed:
    case ESRHeroAction::DawnIntervention:
    {
        const bool bRevive = Config.Action != ESRHeroAction::Sunbeam;
        AActor* Target = SelectAlly(bRevive);
        if (!Target) { Finish(true); return; }
        ChannelTargets.Add(Target);
        if (Config.Action == ESRHeroAction::DawnIntervention)
            for (AActor* Other : SRHeroGameplay::InRadius(Character, Character->GetActorLocation(), Config.Range))
            {
                const auto* OtherState = Other->FindComponentByClass<USRHeroGameplayComponent>();
                if (Other != Target && SRHeroGameplay::SameTeam(Character, Other) && OtherState
                    && OtherState->GetLifeState() == ESRHeroLifeState::Downed
                    && SRHeroGameplay::Cover(Character, Other, Character->GetPawnViewLocation()))
                { ChannelTargets.Add(Other); break; }
            }
        if (Config.Action == ESRHeroAction::LastSeed && !SRHeroGameplay::Ground(GetWorld(), Character, Target->GetActorLocation(), Ground))
        { Finish(true); return; }
        if (!CommitAction()) return;
        if (Config.Action == ESRHeroAction::LastSeed)
        {
            FSRHeroActionConfig Seed = Config; Seed.Healing = 0.f; Seed.DamageTakenScale = 1.f;
            Seed.Duration = Config.ChannelDuration + Config.Duration;
            PendingTree = ASRHeroField::Spawn(GetWorld(), Character, Ground, Seed);
            if (!PendingTree.IsValid()) { Finish(true); return; }
        }
        const UPSAttributeSet* Attributes = Character->GetPSAttributes();
        ChannelHealth = Attributes ? Attributes->GetHealth() + Attributes->GetShield() : 0.f;
        StartTicking(Config.ChannelDuration); return;
    }
    case ESRHeroAction::ParadoxEcho:
        if (!SRHeroGameplay::SafeDestination(Character, Character->GetActorLocation(), Ground)) { Finish(true); return; }
        if (!State->CaptureEcho(Config.Duration)) { Finish(true); return; }
        if (!CommitAction()) { State->ClearEcho(); return; }
        Finish(); return;
    case ESRHeroAction::QuakeBurst:
        if (!Character->GetCharacterMovement()->IsMovingOnGround()) { Finish(true); return; }
        if (!CommitAction()) return;
        for (AActor* Target : SRHeroGameplay::InRadius(Character, Character->GetActorLocation(), Config.Radius))
        {
            const ACharacter* EnemyCharacter = Cast<ACharacter>(Target);
            if (!SRHeroGameplay::IsEnemy(Character, Target) || (EnemyCharacter && !EnemyCharacter->GetCharacterMovement()->IsMovingOnGround())
                || !SRHeroGameplay::Cover(Character, Target, Character->GetActorLocation())) continue;
            SRHeroGameplay::Damage(Character, Target, Config.Damage);
            SRHeroGameplay::Status(Target, Character, SRGameplayTags::Tag_State_Stunned, Config.StunDuration);
        }
        Finish(); return;
    default:
        // Remaining actions are explicitly placed fields/barriers, each with its
        // own behavior in ASRHeroField/ASRHeroBarrier.
        if (Config.Action == ESRHeroAction::IronVanguard)
        {
            if (!SRHeroGameplay::Ground(GetWorld(), Character, Character->GetActorLocation() + Direction * 220.f, Ground)) { Finish(true); return; }
        }
        else if (!SRHeroGameplay::AimGround(Character, Config.Range, Ground)) { Finish(true); return; }
        if (Config.Action == ESRHeroAction::GlassScreen || Config.Action == ESRHeroAction::MistVeil
            || Config.Action == ESRHeroAction::BulwarkPlate || Config.Action == ESRHeroAction::PrismWall || Config.Action == ESRHeroAction::IronVanguard
            || Config.Action == ESRHeroAction::RootCage)
        {
            TArray<FTransform> Placement;
            if (!SRHeroGameplay::BarrierFormation(Character, Ground, Config, Placement)) { Finish(true); return; }
        }
        if (!CommitAction()) return;
        if (Config.Action == ESRHeroAction::GlassScreen || Config.Action == ESRHeroAction::MistVeil
            || Config.Action == ESRHeroAction::BulwarkPlate || Config.Action == ESRHeroAction::PrismWall || Config.Action == ESRHeroAction::IronVanguard)
            SpawnBarrierGroup(Ground);
        else ASRHeroField::Spawn(GetWorld(), Character, Ground, Config);
        Finish(); return;
    }
}
bool USRHeroActionAbility::SubmitMapTarget(const FVector& Requested)
{
    APSBaseCharacter* Character = Caster();
    if (!IsActive() || !bWaitingForMap || !Character || !Character->HasAuthority() || Requested.ContainsNaN()) return false;
    FVector Ground;
    if (!SRHeroGameplay::Ground(GetWorld(), Character, Requested, Ground) || FVector::Dist(Character->GetActorLocation(), Ground) > Config.Range) return false;
    const bool bDecoys = Config.Action == ESRHeroAction::PhantomProcession;
    if (bDecoys)
    {
        FVector Safe;
        if (!SRHeroGameplay::SafeDestination(Character, Ground, Safe)) return false;
        if (MapSelections.Num() && FVector::Dist2D(MapSelections[0], Safe) < 150.f) return false;
        MapSelections.Add(Safe);
        if (MapSelections.Num() < 2) return true;
        for (const FVector& Position : MapSelections)
            if (!SRHeroGameplay::SafeDestination(Character, Position, Safe))
            { MapSelections.Reset(); Character->FindComponentByClass<USRHeroGameplayComponent>()->BeginMapSelection(this, 2); return false; }
    }
    if (!CommitAction()) return false;
    bWaitingForMap = false;
    if (bDecoys)
    {
        TArray<ASRHeroDecoy*> Decoys;
        for (const FVector& Position : MapSelections)
        {
            FActorSpawnParameters Params; Params.Owner = Character;
            ASRHeroDecoy* Decoy = GetWorld()->SpawnActor<ASRHeroDecoy>(Position, Character->GetActorRotation(), Params);
            if (Decoy) { Decoy->Initialize(Character, Config.Duration, Config.StructureHealth, false); Decoys.Add(Decoy); }
        }
        Character->FindComponentByClass<USRHeroGameplayComponent>()->RegisterSwapDecoys(Decoys);
    }
    else ASRHeroField::Spawn(GetWorld(), Character, Ground, Config);
    Character->FindComponentByClass<USRHeroGameplayComponent>()->FinishMapSelection();
    Finish(); return true;
}
void USRHeroActionAbility::TickAction()
{
    APSBaseCharacter* Character = Caster();
    const double Now = GetWorld()->GetTimeSeconds();
    const float Step = static_cast<float>(Now - LastTickAt); LastTickAt = Now;
    if (!Character || !SRHeroGameplay::IsAlive(Character)) { Finish(true); return; }
    UAbilitySystemComponent* ASC = Character->GetAbilitySystemComponent();
    if (!ASC || ASC->HasMatchingGameplayTag(SRGameplayTags::Tag_State_Stunned)) { Finish(true); return; }
    if (bWaitingForMap) { if (Now >= EndsAt) Finish(true); return; }
    if (bOwnsDash)
    {
        if (Config.Action != ESRHeroAction::FlameStep)
        {
            TArray<AActor*> Targets;
            SRGameplayQueries::SweepBoxTargets(GetWorld(), Character, PreviousLocation, Character->GetActorLocation(),
                FVector(60.f,Config.Width * 0.5f,Config.Height * 0.5f), Direction.ToOrientationQuat(), Targets, Config.bDrawDebug);
            for (AActor* Target : Targets)
                if (!DashHits.Contains(Target) && SRHeroGameplay::IsEnemy(Character, Target)
                    && SRHeroGameplay::Cover(Character, Target, Character->GetActorLocation()))
                {
                    DashHits.Add(Target); SRHeroGameplay::Damage(Character, Target, Config.Damage);
                    SRHeroGameplay::Push(Target, Direction, Config.Knockback);
                }
        }
        PreviousLocation = Character->GetActorLocation();
        if (!Character->GetPSMovement()->IsPSLeapDashActive() || Now >= EndsAt) Finish();
        return;
    }
    if (ChannelTargets.Num())
    {
        const UPSAttributeSet* Attributes = Character->GetPSAttributes();
        const float HealthNow = Attributes ? Attributes->GetHealth() + Attributes->GetShield() : 0.f;
        if (HealthNow < ChannelHealth - UE_SMALL_NUMBER || (Config.Action == ESRHeroAction::LastSeed && !PendingTree.IsValid()))
        { Finish(true); return; }
        ChannelHealth = HealthNow;
        for (const auto& Candidate : ChannelTargets)
        {
            AActor* Target = Candidate.Get();
            const auto* TargetState = Target ? Target->FindComponentByClass<USRHeroGameplayComponent>() : nullptr;
            const bool bValidState = Config.Action == ESRHeroAction::Sunbeam ? SRHeroGameplay::IsAlive(Target)
                : TargetState && TargetState->GetLifeState() == ESRHeroLifeState::Downed;
            if (!Target || !bValidState || !SRHeroGameplay::SameTeam(Character, Target)
                || FVector::Dist(Character->GetActorLocation(), Target->GetActorLocation()) > Config.Range
                || !SRHeroGameplay::Cover(Character, Target, Character->GetPawnViewLocation()))
            { Finish(true); return; }
        }
        if (Config.Action == ESRHeroAction::Sunbeam)
        {
            HealingAccumulator += Step;
            if (HealingAccumulator >= Config.Interval)
            { SRHeroGameplay::Heal(Character, ChannelTargets[0].Get(), Config.Healing * HealingAccumulator); HealingAccumulator = 0.f; }
        }
        if (Now >= EndsAt) { CompleteChannel(); Finish(); }
        return;
    }
    if (Config.Action == ESRHeroAction::FadeStep && !ASC->HasMatchingGameplayTag(SRGameplayTags::Tag_State_Concealed))
    { Finish(true); return; }
    if (Now >= EndsAt) Finish();
}
void USRHeroActionAbility::CompleteChannel()
{
    APSBaseCharacter* Character = Caster(); if (!Character || Config.Action == ESRHeroAction::Sunbeam) return;
    int32 Revived = 0;
    for (const auto& Candidate : ChannelTargets)
        if (AActor* Target = Candidate.Get())
            if (auto* State = Target->FindComponentByClass<USRHeroGameplayComponent>())
                if (State->Revive(Config.ReviveHealth, Config.ReviveShield)) ++Revived;
    if (!Revived) return;
    if (Config.Action == ESRHeroAction::LastSeed && PendingTree.IsValid())
    { PendingTree->Initialize(Character, Config); PendingTree.Reset(); }
    else if (Config.Action == ESRHeroAction::DawnIntervention)
    {
        FVector Ground;
        if (SRHeroGameplay::Ground(GetWorld(), Character, Character->GetActorLocation(), Ground))
            ASRHeroField::Spawn(GetWorld(), Character, Ground, Config);
    }
}
void USRHeroActionAbility::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
    if (UWorld* World = GetWorld()) World->GetTimerManager().ClearTimer(ActionTimer);
    APSBaseCharacter* Character = Caster();
    if (Character)
    {
        if (UAbilitySystemComponent* ASC = Character->GetAbilitySystemComponent())
            for (FActiveGameplayEffectHandle Effect : OwnedEffects) ASC->RemoveActiveGameplayEffect(Effect);
        if (bOwnsDash && Character->GetPSMovement()) Character->GetPSMovement()->EndPSLeapDash(bWasCancelled);
        if (auto* State = Character->FindComponentByClass<USRHeroGameplayComponent>())
            if (bWaitingForMap) State->FinishMapSelection();
        if (Character->GetCombatComponent() && !Character->IsPSDead())
        { Character->GetCombatComponent()->RefreshAutomaticFireInterval(); Character->GetCombatComponent()->RestartRechargeIfActive(); }
    }
    if (PendingTree.IsValid()) PendingTree->Destroy();
    PendingTree.Reset(); OwnedEffects.Reset(); bOwnsDash = bWaitingForMap = false;
    if (bWasCancelled) SRDebug::Log(SRDebug::ECategory::Ability, TEXT("Server"), FString::Printf(TEXT("Cancelled/rejected %s"), *AbilityDisplayName.ToString()));
    Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
