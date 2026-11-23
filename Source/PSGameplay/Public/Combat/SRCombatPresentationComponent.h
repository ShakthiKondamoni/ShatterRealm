#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/NetSerialization.h"
#include "SRCombatPresentationComponent.generated.h"

class APSBaseCharacter;
class UNiagaraSystem;
class USoundBase;
class USRWeaponDataAsset;
class UPointLightComponent;
class USkeletalMeshComponent;

/**
 * Cosmetic presentation hub for hero combat. Gameplay decisions stay in the
 * Combat Component and abilities; this component only renders what happened.
 *
 * Every world effect is data-driven: an authored Niagara/sound slot exists for
 * each event kind, and when the slot is empty a visible provisional effect
 * (engine-primitive shapes + dynamic lights) plays so the full presentation is
 * verifiable in PIE while dedicated VFX/audio assets remain BLOCKED.
 *
 * Network flow:
 * - Server-confirmed world events (leap, beam, hit feedback) multicast to all
 *   clients from this replicated component.
 * - Owner-local feedback (camera recoil, camera impulses, Overcharge aura via
 *   the replicated buff tag, recharge glow via the replicated combat state)
 *   renders locally without extra RPCs.
 */
UCLASS(ClassGroup=(ProjectS), meta=(BlueprintSpawnableComponent))
class PSGAMEPLAY_API USRCombatPresentationComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    USRCombatPresentationComponent();

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    /** Binds the hero, weapon data and the Overcharge tag watcher. Safe to call again. */
    void InitializeForCharacter(APSBaseCharacter* Character);

    // ------------------------------------------------------------------
    // Primary fire (routed through the confirmed-shot cosmetic path)
    // ------------------------------------------------------------------

    /** Muzzle flash + tracer + impact + audio + camera recoil for one shot. */
    void PlayShotCosmetic(const FVector& Origin, const FVector& Impact, bool bHit, int32 ShotId = 0);

    /** Recharge start presentation (called from the replicated recharge state OnRep). */
    void PlayRechargeCosmetic(const FVector& Origin);

    // ------------------------------------------------------------------
    // Abilities (server-confirmed; multicast to all clients)
    // ------------------------------------------------------------------

    void PlayLeapTakeoff(const FVector& Location, const FVector& Direction);
    void PlayLeapLand(const FVector& Location, float BurstRadius);
    void PlayStormpiercerWindup(const FVector& Origin);
    void PlayStormpiercerBeam(const FVector& Start, const FVector& End, int32 PulseIndex);

    // ------------------------------------------------------------------
    // Hit feedback (server-confirmed; multicast to all clients)
    // ------------------------------------------------------------------

    /** Damage number over the target + impact burst + server-side knockback impulse. */
    void PlayHitFeedback(AActor* Target, const FVector& HitLocation, float Amount, bool bShieldHit, bool bLandingBurst, const FVector& HitDirection);

    // ------------------------------------------------------------------
    // Camera feedback (owning client only)
    // ------------------------------------------------------------------

    void PlayFireRecoil(int32 ShotId);
    void PlayLeapCameraImpulse();

    // ------------------------------------------------------------------
    // Gauntlets + aura
    // ------------------------------------------------------------------

    /** Electric gauntlet brightness; 0 restores the base look. */
    void SetGauntletGlow(float Intensity);
    /** Persistent Overcharge aura (driven by the replicated buff tag on every machine). */
    void SetOverchargeAura(bool bActive);
    bool IsOverchargeAuraActive() const { return bAuraActive; }

protected:
    UFUNCTION(NetMulticast, Unreliable)
    void MulticastLeapFX(FVector_NetQuantize10 Location, FVector_NetQuantizeNormal Direction, bool bLanding, float Radius);

    UFUNCTION(NetMulticast, Unreliable)
    void MulticastStormpiercerFX(FVector_NetQuantize10 Start, FVector_NetQuantize10 End, bool bWindup, int32 PulseIndex);

    UFUNCTION(NetMulticast, Unreliable)
    void MulticastHitFeedback(FVector_NetQuantize10 HitLocation, float Amount, bool bShieldHit, bool bLandingBurst, FVector_NetQuantizeNormal HitDirection);

    UFUNCTION() void OnOverchargeTagCountChanged(FGameplayTag Tag, int32 NewCount);

    // ------------------------------------------------------------------
    // Data-driven presentation hooks (BLOCKED: no authored NS_/audio assets yet)
    // ------------------------------------------------------------------

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|Presentation|Hooks")
    TObjectPtr<UNiagaraSystem> LeapTakeoffVFX;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|Presentation|Hooks")
    TObjectPtr<UNiagaraSystem> LeapLandVFX;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|Presentation|Hooks")
    TObjectPtr<UNiagaraSystem> StormpiercerBeamVFX;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|Presentation|Hooks")
    TObjectPtr<UNiagaraSystem> OverchargeAuraVFX;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|Presentation|Hooks")
    TObjectPtr<USoundBase> LeapSound;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|Presentation|Hooks")
    TObjectPtr<USoundBase> StormpiercerSound;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|Presentation|Hooks")
    TObjectPtr<USoundBase> OverchargeSound;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|Presentation|Hooks")
    TObjectPtr<USoundBase> HitConfirmSound;

    // ------------------------------------------------------------------
    // Camera tuning (weapon-data recoil values take precedence when set)
    // ------------------------------------------------------------------

    /** Fallback upward kick per shot when the weapon pattern is empty (degrees). */
    UPROPERTY(EditDefaultsOnly, Category="SR|Presentation|Camera", meta=(ClampMin="0"))
    float RecoilPitchPerShot = 1.2f;

    /** Fallback random horizontal spread per shot (degrees). */
    UPROPERTY(EditDefaultsOnly, Category="SR|Presentation|Camera", meta=(ClampMin="0"))
    float RecoilYawJitter = 0.35f;

    /** Recovery duration when the weapon asset does not specify one (seconds). */
    UPROPERTY(EditDefaultsOnly, Category="SR|Presentation|Camera", meta=(ClampMin="0.01"))
    float RecoilRecoveryTime = 0.18f;

    /** Downward camera dip on Static Leap takeoff. */
    UPROPERTY(EditDefaultsOnly, Category="SR|Presentation|Camera", meta=(ClampMin="0"))
    float LeapCameraDip = 2.5f;

public:
    /** Debug/test hook: the recoil pattern index the next predicted shot will use. */
    UFUNCTION(BlueprintPure, Category="SR|Presentation|Recoil")
    int32 GetRecoilPatternIndexForTest() const { return RecoilPatternIndex; }

    /** Debug/test hook: applies one predicted-shot recoil kick with the current weapon config. */
    void ApplyPredictedRecoilForTest(int32 ShotId);

    /**
     * Pure recoil-pattern progression: returns the pattern entry index to USE for
     * this shot - reset to the first entry after the weapon's inactivity delay,
     * otherwise advance and clamp at the last entry (no wrap-around drift).
     * Static so automation can verify progression and reset timing directly.
     */
    static int32 ResolveRecoilPatternIndex(const USRWeaponDataAsset* Data, int32 CurrentIndex, float SecondsSinceLastShot);

protected:
    // Provisional effect spawners (engine-primitive shapes + dynamic lights).
    void SpawnBeam(const FVector& Start, const FVector& End, float Thickness, const FLinearColor& Color, float Lifetime, bool bUseLight);
    void SpawnBurst(const FVector& Location, float StartRadius, float EndRadius, const FLinearColor& Color, float Lifetime, bool bUseLight);
    void SpawnRing(const FVector& Location, float EndRadius, const FLinearColor& Color, float Lifetime, bool bUseLight);
    void SpawnNiagara(const UNiagaraSystem* System, const FVector& Location);
    void SpawnDamageNumber(const FVector& Location, float Amount, bool bShieldHit, bool bLandingBurst);
    void ApplyHitReactionLocal(AActor* Target, const FVector& HitLocation, bool bShieldHit);
    void PlaySound(const USoundBase* Sound, const FVector& Location);
    void WarnMissingAsset(const TCHAR* Kind) const;

    void BindOverchargeWatcher();
    void UpdateRecoilRecovery(float DeltaTime);
    void TickAura(float DeltaTime);
    void CollectGauntletMaterials();
    FVector GetGauntletLocation() const;

    /** Knockback impulse magnitude applied to damaged pawns (server only). */
    UPROPERTY(EditDefaultsOnly, Category="SR|Presentation|HitReactions", meta=(ClampMin="0"))
    float HitReactionImpulse = 300.f;

    /** Aura light color and strength while Overcharge is active. */
    UPROPERTY(EditDefaultsOnly, Category="SR|Presentation|Overcharge")
    FLinearColor OverchargeAuraColor = FLinearColor(0.2f, 0.7f, 1.f);
    UPROPERTY(EditDefaultsOnly, Category="SR|Presentation|Overcharge", meta=(ClampMin="0"))
    float OverchargeAuraLightIntensity = 12.f;
    UPROPERTY(EditDefaultsOnly, Category="SR|Presentation|Overcharge", meta=(ClampMin="0"))
    float OverchargeAuraLightRadius = 350.f;

    TWeakObjectPtr<APSBaseCharacter> BoundCharacter;
    TWeakObjectPtr<USRWeaponDataAsset> WeaponData;
    TArray<TObjectPtr<UMaterialInstanceDynamic>> GauntletMaterials;
    TWeakObjectPtr<UPointLightComponent> AuraLight;
    float PendingRecoilPitch = 0.f;
    float PendingRecoilYaw = 0.f;
    int32 RecoilPatternIndex = 0;
    float LastRecoilWorldTime = -100.f;
    int32 LastLocalRecoilShotId = INDEX_NONE;
    FString RecoilWeaponIdentity; // weapon change resets the pattern index
    float GlowIntensity = 0.15f;
    bool bAuraActive = false;
    double AuraPulseTime = 0.0;
    mutable bool bWarnedMissingAudio = false;
};
