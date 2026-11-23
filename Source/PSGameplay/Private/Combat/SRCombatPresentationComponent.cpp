#include "Combat/SRCombatPresentationComponent.h"
#include "Combat/SRPresentationActors.h"
#include "Combat/SRWeaponDataAsset.h"
#include "Character/PSBaseCharacter.h"
#include "Abilities/SRGameplayTags.h"
#include "Debug/SRDebug.h"
#include "AbilitySystemComponent.h"
#include "GameplayTagContainer.h"
#include "NiagaraFunctionLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/MeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"

namespace
{
// Electric palette shared by every provisional Zyrex effect.
const FLinearColor ElectricCyan(0.15f, 0.75f, 1.f);
const FLinearColor ElectricWhite(0.85f, 0.95f, 1.f);
const FLinearColor StormBlue(0.35f, 0.45f, 1.f);
const FLinearColor ShieldBlue(0.25f, 0.6f, 1.f);

constexpr float MuzzleFlashLifetime = 0.08f;
constexpr float TracerLifetime = 0.07f;
constexpr float ImpactBurstLifetime = 0.18f;

const TCHAR* GauntletPiecePrefix = TEXT("Gauntlet");
}

USRCombatPresentationComponent::USRCombatPresentationComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    // Multicast cosmetics require a replicated component channel on the pawn.
    SetIsReplicatedByDefault(true);
}

void USRCombatPresentationComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
}

void USRCombatPresentationComponent::InitializeForCharacter(APSBaseCharacter* Character)
{
    BoundCharacter = Character;
    WeaponData = Character ? Character->WeaponData.Get() : nullptr;
    CollectGauntletMaterials();
    SetGauntletGlow(0.15f);
    BindOverchargeWatcher();
}

// ---------------------------------------------------------------------------
// Primary fire
// ---------------------------------------------------------------------------

void USRCombatPresentationComponent::PlayShotCosmetic(const FVector& Origin, const FVector& Impact, bool bHit, int32 ShotId)
{
    SpawnBurst(Origin, 6.f, 18.f, ElectricWhite, MuzzleFlashLifetime, true);
    SpawnBeam(Origin, Impact, 2.5f, ElectricCyan, TracerLifetime, false);
    if (bHit)
    {
        SpawnBurst(Impact, 6.f, 26.f, ElectricWhite, ImpactBurstLifetime, true);
    }

    const USoundBase* FiringSound = WeaponData.IsValid() ? WeaponData->FiringSound : nullptr;
    if (FiringSound)
    {
        PlaySound(FiringSound, Origin);
    }
    else
    {
        WarnMissingAsset(TEXT("WeaponData->FiringSound"));
    }

    if (ShotId > 0) PlayFireRecoil(ShotId);
    SetGauntletGlow(1.f);
}

void USRCombatPresentationComponent::PlayRechargeCosmetic(const FVector& Origin)
{
    SpawnBurst(Origin, 10.f, 30.f, ElectricCyan, 0.5f, true);
    const USoundBase* RechargeSound = WeaponData.IsValid() ? WeaponData->RechargeSound : nullptr;
    if (RechargeSound)
    {
        PlaySound(RechargeSound, Origin);
    }
    else
    {
        WarnMissingAsset(TEXT("WeaponData->RechargeSound"));
    }
    SetGauntletGlow(1.f);
}

// ---------------------------------------------------------------------------
// Abilities
// ---------------------------------------------------------------------------

void USRCombatPresentationComponent::PlayLeapTakeoff(const FVector& Location, const FVector& Direction)
{
    if (GetOwnerRole() == ROLE_Authority)
    {
        MulticastLeapFX(Location, Direction, false, 0.f);
    }
}

void USRCombatPresentationComponent::PlayLeapLand(const FVector& Location, float BurstRadius)
{
    if (GetOwnerRole() == ROLE_Authority)
    {
        MulticastLeapFX(Location, FVector::UpVector, true, BurstRadius);
    }
}

void USRCombatPresentationComponent::MulticastLeapFX_Implementation(FVector_NetQuantize10 Location, FVector_NetQuantizeNormal Direction, bool bLanding, float Radius)
{
    if (bLanding)
    {
        SpawnRing(Location, FMath::Max(Radius, 100.f), ElectricCyan, 0.45f, true);
        SpawnBurst(Location, 10.f, FMath::Max(Radius * 0.4f, 60.f), ElectricWhite, 0.3f, true);
    }
    else
    {
        // Takeoff: burst at the feet + short forward spark streak.
        SpawnBurst(Location, 8.f, 34.f, ElectricCyan, 0.25f, true);
        SpawnBeam(Location, Location + Direction.GetSafeNormal() * 180.f, 6.f, ElectricWhite, 0.18f, false);
    }

    const UNiagaraSystem* System = bLanding ? LeapLandVFX : LeapTakeoffVFX;
    SpawnNiagara(System, Location);
    PlaySound(LeapSound, Location);
}

void USRCombatPresentationComponent::PlayStormpiercerWindup(const FVector& Origin)
{
    if (GetOwnerRole() == ROLE_Authority)
    {
        MulticastStormpiercerFX(Origin, Origin, true, 0);
    }
}

void USRCombatPresentationComponent::PlayStormpiercerBeam(const FVector& Start, const FVector& End, int32 PulseIndex)
{
    if (GetOwnerRole() == ROLE_Authority)
    {
        MulticastStormpiercerFX(Start, End, false, PulseIndex);
    }
}

void USRCombatPresentationComponent::MulticastStormpiercerFX_Implementation(FVector_NetQuantize10 Start, FVector_NetQuantize10 End, bool bWindup, int32 PulseIndex)
{
    if (bWindup)
    {
        // Growing charge glow in the gauntlets before the first pulse.
        SpawnBurst(Start, 6.f, 40.f, StormBlue, 0.8f, true);
        SetGauntletGlow(1.f);
    }
    else
    {
        SpawnBeam(Start, End, 55.f, StormBlue, 0.22f, true);
        SpawnBurst(End, 12.f, 70.f, ElectricWhite, 0.3f, true);
        SpawnNiagara(StormpiercerBeamVFX, (Start + End) * 0.5f);
        SRDebug::Message(SRDebug::ECategory::Ability, TEXT("VFX"),
            FString::Printf(TEXT("Stormpiercer beam pulse %d rendered"), PulseIndex), 0.4f, FColor::Cyan);
    }
    PlaySound(StormpiercerSound, Start);
}

// ---------------------------------------------------------------------------
// Hit feedback
// ---------------------------------------------------------------------------

void USRCombatPresentationComponent::PlayHitFeedback(AActor* Target, const FVector& HitLocation, float Amount, bool bShieldHit, bool bLandingBurst, const FVector& HitDirection)
{
    if (GetOwnerRole() != ROLE_Authority) return;

    // Hit reaction: physical knockback is authoritative movement, applied server-side only.
    ACharacter* TargetCharacter = Cast<ACharacter>(Target);
    if (TargetCharacter && TargetCharacter->GetCharacterMovement() && !TargetCharacter->GetCharacterMovement()->IsFalling())
    {
        TargetCharacter->GetCharacterMovement()->AddImpulse(HitDirection.GetSafeNormal2D() * HitReactionImpulse, true);
    }

    MulticastHitFeedback(HitLocation, Amount, bShieldHit, bLandingBurst, HitDirection);
}

void USRCombatPresentationComponent::MulticastHitFeedback_Implementation(FVector_NetQuantize10 HitLocation, float Amount, bool bShieldHit, bool bLandingBurst, FVector_NetQuantizeNormal HitDirection)
{
    SpawnDamageNumber(HitLocation + FVector(0.f, 0.f, 40.f), Amount, bShieldHit, bLandingBurst);
    // Impact direction flash so hits read even when the number is far away.
    SpawnBurst(HitLocation, 5.f, bLandingBurst ? 30.f : 16.f, bShieldHit ? ShieldBlue : ElectricWhite, 0.15f, false);
    PlaySound(HitConfirmSound, HitLocation);
}

// ---------------------------------------------------------------------------
// Camera feedback (owning client)
// ---------------------------------------------------------------------------

void USRCombatPresentationComponent::PlayFireRecoil(int32 ShotId)
{
    ApplyPredictedRecoilForTest(ShotId);
}

void USRCombatPresentationComponent::ApplyPredictedRecoilForTest(int32 ShotId)
{
    const APSBaseCharacter* Character = BoundCharacter.Get();
    if (!Character || !Character->IsLocallyControlled() || ShotId == LastLocalRecoilShotId) return;
    APlayerController* PC = Cast<APlayerController>(Character->GetController());
    if (!PC || !PC->IsLocalController()) return;
    LastLocalRecoilShotId = ShotId;

    const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
    const USRWeaponDataAsset* Data = WeaponData.Get();

    // Weapon identity change resets the pattern (also covers respawn re-init).
    const FString WeaponIdentity = GetNameSafe(Data);
    if (WeaponIdentity != RecoilWeaponIdentity)
    {
        RecoilWeaponIdentity = WeaponIdentity;
        RecoilPatternIndex = 0;
        PendingRecoilPitch = 0.f;
        PendingRecoilYaw = 0.f;
    }

    // Inactivity restarts the pattern from its first entry (pure resolver, tested).
    RecoilPatternIndex = ResolveRecoilPatternIndex(Data, RecoilPatternIndex, Now - LastRecoilWorldTime);
    LastRecoilWorldTime = Now;

    FVector2D Kick(RecoilPitchPerShot, 0.f);
    if (Data && Data->RecoilPattern.Num() > 0)
    {
        Kick = Data->RecoilPattern[FMath::Clamp(RecoilPatternIndex, 0, Data->RecoilPattern.Num() - 1)];
    }
    else
    {
        Kick = FVector2D(RecoilPitchPerShot, FMath::FRandRange(-RecoilYawJitter, RecoilYawJitter));
    }

    // Perspective scaling: first-person kicks full, third-person softer.
    const float PerspectiveScale = Character->IsFirstPersonView()
        ? (Data ? Data->FPRecoilMultiplier : 1.f)
        : (Data ? Data->TPRecoilMultiplier : 0.75f);

    const float RecoverFraction = Data ? FMath::Clamp(Data->RecoilRecoveryFraction, 0.f, 1.f) : 1.f;
    FRotator Rotation = PC->GetControlRotation();
    Rotation.Pitch += Kick.X * PerspectiveScale;
    Rotation.Yaw += Kick.Y * PerspectiveScale;
    PC->SetControlRotation(Rotation);
    // Only the recovering fraction is tracked; the remainder is permanent climb
    // the player counteracts with aim input (standard recoil model).
    PendingRecoilPitch += Kick.X * PerspectiveScale * RecoverFraction;
    PendingRecoilYaw += Kick.Y * PerspectiveScale * RecoverFraction;
    SRDebug::PredictionLog(Character, FString::Printf(TEXT("Recoil applied shot=%d patternIndex=%d kick=(%.2f, %.2f) scale=x%.2f"),
        ShotId, RecoilPatternIndex, Kick.X, Kick.Y, PerspectiveScale));
}

void USRCombatPresentationComponent::PlayLeapCameraImpulse()
{
    const APSBaseCharacter* Character = BoundCharacter.Get();
    if (!Character || !Character->IsLocallyControlled()) return;
    APlayerController* PC = Cast<APlayerController>(Character->GetController());
    if (!PC || LeapCameraDip <= 0.f) return;

    // Launch punch: dip the view, then settle via the same recovery path.
    FRotator ControlRotation = PC->GetControlRotation();
    ControlRotation.Pitch -= LeapCameraDip;
    PC->SetControlRotation(ControlRotation);
    PendingRecoilPitch -= LeapCameraDip;
}

void USRCombatPresentationComponent::UpdateRecoilRecovery(float DeltaTime)
{
    if (FMath::IsNearlyZero(PendingRecoilPitch) && FMath::IsNearlyZero(PendingRecoilYaw)) return;
    // Weapon-configured recovery duration wins; the component value is the fallback.
    const float RecoveryTime = WeaponData.IsValid() && WeaponData->RecoilRecoveryDuration > 0.f
        ? WeaponData->RecoilRecoveryDuration : RecoilRecoveryTime;
    if (RecoveryTime <= 0.f) return;
    const APSBaseCharacter* Character = BoundCharacter.Get();
    if (!Character || !Character->IsLocallyControlled())
    {
        PendingRecoilPitch = 0.f;
        PendingRecoilYaw = 0.f;
        return;
    }
    APlayerController* PC = Cast<APlayerController>(Character->GetController());
    if (!PC)
    {
        PendingRecoilPitch = 0.f;
        PendingRecoilYaw = 0.f;
        return;
    }

    const float Step = PendingRecoilPitch * (DeltaTime / RecoveryTime);
    const float YawStep = PendingRecoilYaw * (DeltaTime / RecoveryTime);
    // Clamp the final step to the remaining amount so recovery settles exactly.
    const float Applied = FMath::Abs(Step) > FMath::Abs(PendingRecoilPitch) ? PendingRecoilPitch : Step;
    PendingRecoilPitch -= Applied;
    const float AppliedYaw = FMath::Abs(YawStep) > FMath::Abs(PendingRecoilYaw) ? PendingRecoilYaw : YawStep;
    PendingRecoilYaw -= AppliedYaw;
    FRotator ControlRotation = PC->GetControlRotation();
    ControlRotation.Pitch -= Applied;
    ControlRotation.Yaw -= AppliedYaw;
    PC->SetControlRotation(ControlRotation);
}

// ---------------------------------------------------------------------------
// Gauntlets + aura
// ---------------------------------------------------------------------------

void USRCombatPresentationComponent::CollectGauntletMaterials()
{
    GauntletMaterials.Reset();
    const AActor* Owner = GetOwner();
    if (!Owner) return;

    TArray<UStaticMeshComponent*> Pieces;
    Owner->GetComponents(Pieces);
    for (UStaticMeshComponent* Piece : Pieces)
    {
        if (!Piece || !Piece->GetName().StartsWith(GauntletPiecePrefix)) continue;
        UMaterialInstanceDynamic* MID = Piece->CreateAndSetMaterialInstanceDynamic(0);
        if (MID) GauntletMaterials.Add(MID);
    }
}

void USRCombatPresentationComponent::SetGauntletGlow(float Intensity)
{
    // BasicShapeMaterial exposes "Color"; a darker steel look when idle,
    // bright electric cyan while glowing. Silently ignored without the param.
    const float Clamped = FMath::Clamp(Intensity, 0.f, 1.f);
    GlowIntensity = Clamped;
    const FVector GlowColor = FMath::Lerp(FVector(0.10f, 0.13f, 0.20f), FVector(0.10f, 0.85f, 1.30f), Clamped);
    for (UMaterialInstanceDynamic* MID : GauntletMaterials)
    {
        if (MID) MID->SetVectorParameterValue(TEXT("Color"), GlowColor);
    }
}

void USRCombatPresentationComponent::SetOverchargeAura(bool bActive)
{
    if (bAuraActive == bActive) return;
    bAuraActive = bActive;

    AActor* Owner = GetOwner();
    if (!Owner) return;

    if (bActive)
    {
        UPointLightComponent* Light = NewObject<UPointLightComponent>(Owner, TEXT("OverchargeAuraLight"));
        Light->SetupAttachment(Owner->GetRootComponent());
        Light->SetRelativeLocation(FVector(0.f, 0.f, 50.f));
        Light->SetLightColor(OverchargeAuraColor);
        Light->SetAttenuationRadius(OverchargeAuraLightRadius);
        Light->SetIntensity(OverchargeAuraLightIntensity);
        Light->SetCastShadows(false);
        Light->RegisterComponent();
        AuraLight = Light;

        SpawnNiagara(OverchargeAuraVFX, Owner->GetActorLocation());
        SetGauntletGlow(1.f);
        PlaySound(OverchargeSound, Owner->GetActorLocation());
        SRDebug::Message(SRDebug::ECategory::GAS, TEXT("VFX"), TEXT("Overcharge aura activated"), 0.6f, FColor::Cyan);
    }
    else
    {
        if (AuraLight.IsValid())
        {
            AuraLight->DestroyComponent();
        }
        AuraLight = nullptr;
        SpawnBurst(Owner->GetActorLocation(), 15.f, 50.f, OverchargeAuraColor, 0.4f, true);
        SetGauntletGlow(0.15f);
        SRDebug::Message(SRDebug::ECategory::GAS, TEXT("VFX"), TEXT("Overcharge aura expired"), 0.6f, FColor::Silver);
    }
}

void USRCombatPresentationComponent::BindOverchargeWatcher()
{
    const APSBaseCharacter* Character = BoundCharacter.Get();
    UAbilitySystemComponent* ASC = Character ? Character->GetAbilitySystemComponent() : nullptr;
    if (!ASC) return;
    // Replicated buff tag: every machine observes the aura state locally,
    // so no extra multicast is needed for Overcharge presentation.
    ASC->RegisterGameplayTagEvent(SRGameplayTags::Tag_Status_Buff_Overcharge, EGameplayTagEventType::NewOrRemoved)
        .AddUObject(this, &USRCombatPresentationComponent::OnOverchargeTagCountChanged);
    SetOverchargeAura(ASC->HasMatchingGameplayTag(SRGameplayTags::Tag_Status_Buff_Overcharge));
}

void USRCombatPresentationComponent::OnOverchargeTagCountChanged(FGameplayTag Tag, int32 NewCount)
{
    SetOverchargeAura(NewCount > 0);
}

void USRCombatPresentationComponent::TickAura(float DeltaTime)
{
    UPointLightComponent* Light = AuraLight.Get();
    if (!Light) return;
    // Electric shimmer while the buff is up.
    AuraPulseTime += DeltaTime;
    const float Pulse = 0.85f + 0.15f * FMath::Sin(AuraPulseTime * 14.f);
    Light->SetIntensity(OverchargeAuraLightIntensity * Pulse);
}

// ---------------------------------------------------------------------------
// Provisional spawners
// ---------------------------------------------------------------------------

void USRCombatPresentationComponent::SpawnBeam(const FVector& Start, const FVector& End, float Thickness, const FLinearColor& Color, float Lifetime, bool bUseLight)
{
    UWorld* World = GetWorld();
    if (!World) return;
    ASRTransientVFX* Effect = World->SpawnActor<ASRTransientVFX>(ASRTransientVFX::StaticClass(), Start, FRotator::ZeroRotator);
    if (Effect) Effect->ConfigureBeam(Start, End, Thickness, Color, Lifetime, bUseLight);
}

void USRCombatPresentationComponent::SpawnBurst(const FVector& Location, float StartRadius, float EndRadius, const FLinearColor& Color, float Lifetime, bool bUseLight)
{
    UWorld* World = GetWorld();
    if (!World) return;
    ASRTransientVFX* Effect = World->SpawnActor<ASRTransientVFX>(ASRTransientVFX::StaticClass(), Location, FRotator::ZeroRotator);
    if (Effect) Effect->ConfigureBurst(Location, StartRadius, EndRadius, Color, Lifetime, bUseLight);
}

void USRCombatPresentationComponent::SpawnRing(const FVector& Location, float EndRadius, const FLinearColor& Color, float Lifetime, bool bUseLight)
{
    UWorld* World = GetWorld();
    if (!World) return;
    ASRTransientVFX* Effect = World->SpawnActor<ASRTransientVFX>(ASRTransientVFX::StaticClass(), Location, FRotator::ZeroRotator);
    if (Effect) Effect->ConfigureRing(Location, EndRadius, Color, Lifetime, bUseLight);
}

void USRCombatPresentationComponent::SpawnNiagara(const UNiagaraSystem* System, const FVector& Location)
{
    if (!System || !GetWorld()) return; // BLOCKED: no authored NS_ assets yet
    UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), const_cast<UNiagaraSystem*>(System), Location);
}

void USRCombatPresentationComponent::SpawnDamageNumber(const FVector& Location, float Amount, bool bShieldHit, bool bLandingBurst)
{
    UWorld* World = GetWorld();
    if (!World) return;
    ASRFloatingDamageText* Number = World->SpawnActor<ASRFloatingDamageText>(ASRFloatingDamageText::StaticClass(), Location, FRotator::ZeroRotator);
    if (Number) Number->ConfigureDamage(Location, Amount, bShieldHit, bLandingBurst);
}

void USRCombatPresentationComponent::ApplyHitReactionLocal(AActor* Target, const FVector& HitLocation, bool bShieldHit)
{
    // Visual flash on the target's materials; materials without the parameter ignore it.
    if (!Target) return;
    UMeshComponent* TargetMesh = Target->FindComponentByClass<UMeshComponent>();
    if (TargetMesh)
    {
        TargetMesh->SetVectorParameterValueOnMaterials(TEXT("HitFlashColor"),
            bShieldHit ? FVector(ShieldBlue) : FVector(ElectricWhite));
    }
}

void USRCombatPresentationComponent::PlaySound(const USoundBase* Sound, const FVector& Location)
{
    if (!Sound || !GetWorld()) return;
    UGameplayStatics::SpawnSoundAtLocation(GetWorld(), const_cast<USoundBase*>(Sound), Location);
}

void USRCombatPresentationComponent::WarnMissingAsset(const TCHAR* Kind) const
{
    // BLOCKED reporting: audio assets do not exist yet; warn once per session.
    if (bWarnedMissingAudio) return;
    bWarnedMissingAudio = true;
    SRDebug::Log(SRDebug::ECategory::Combat, TEXT("VFX"),
        FString::Printf(TEXT("Audio asset BLOCKED (no authored sound): %s — hook wired, plays once assets exist"), Kind));
}

void USRCombatPresentationComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    UpdateRecoilRecovery(DeltaTime);
    TickAura(DeltaTime);

    // Gauntlet glow decays back to its idle level after a fire/recharge pulse.
    const float TargetGlow = bAuraActive ? 1.f : 0.15f;
    if (!FMath::IsNearlyEqual(GlowIntensity, TargetGlow, 0.01f))
    {
        SetGauntletGlow(FMath::FInterpTo(GlowIntensity, TargetGlow, DeltaTime, 6.f));
    }
}

int32 USRCombatPresentationComponent::ResolveRecoilPatternIndex(const USRWeaponDataAsset* Data, int32 CurrentIndex, float SecondsSinceLastShot)
{
    const float ResetDelay = Data ? FMath::Max(0.05f, Data->RecoilResetDelay) : 0.25f;
    if (SecondsSinceLastShot > ResetDelay) return 0; // inactivity restarts the pattern
    if (!Data || Data->RecoilPattern.Num() == 0) return 0;
    return FMath::Min(CurrentIndex + 1, Data->RecoilPattern.Num() - 1); // advance, clamp at last
}
