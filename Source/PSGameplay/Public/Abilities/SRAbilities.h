#pragma once

#include "CoreMinimal.h"
#include "Abilities/SRGameplayAbility.h"
#include "SRAbilities.generated.h"

class APSBaseCharacter;
class ASRElementalProjectile;
class UCurveFloat;

/**
 * Q — Arc Bolt: traveling electrical projectile with direct + splash damage and
 * a movement slow. All gameplay numbers live on the projectile class defaults
 * (data-driven); the ability owns commitment, aiming and spawn authority.
 */
UCLASS()
class PSGAMEPLAY_API USRGameplayAbility_ArcBolt : public USRGameplayAbility
{
    GENERATED_BODY()

public:
    USRGameplayAbility_ArcBolt();

protected:
    virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

    /** Projectile class carrying the damage/splash/slow configuration. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|ArcBolt")
    TSubclassOf<ASRElementalProjectile> ProjectileClass;

    /** Launch speed (world units/second). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|ArcBolt", meta=(ClampMin="1"))
    float ProjectileSpeed = 1400.f;

    /** Maximum travel distance; enforced by projectile lifespan. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|ArcBolt", meta=(ClampMin="1"))
    float MaxProjectileRange = 6000.f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|ArcBolt", meta=(ClampMin="0.2"))
    float GrenadeFuse = 2.2f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|ArcBolt", meta=(ClampMin="0.1"))
    float GrenadeGravityScale = 1.05f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|ArcBolt", meta=(ClampMin="0", ClampMax="0.9"))
    float GrenadeBounciness = 0.4f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|ArcBolt", meta=(ClampMin="0", ClampMax="1.0"))
    float ThrowLobFactor = 0.25f;
};

/**
 * E - Static Leap: predicted airborne leap in the movement (or camera)
 * direction with a one-shot burst on actual landing. Horizontal velocity is enforced by
 * the custom CharacterMovementComponent (friction-immune, collision-swept) —
 * with normal falling gravity and collision sweeps.
 */
UCLASS()
class PSGAMEPLAY_API USRGameplayAbility_StaticLeap : public USRGameplayAbility
{
    GENERATED_BODY()

public:
    USRGameplayAbility_StaticLeap();

protected:
    virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
    virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

    UFUNCTION() void OnLeapDashFinished(bool bWasObstructed);
    UFUNCTION() void OnLanded(EMovementMode NewMovementMode);

    /** Maximum leap travel distance. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|StaticLeap", meta=(ClampMin="100"))
    float MaxLeapDistance = 1200.f;

    /** Dash travel time (dash speed = distance / duration). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|StaticLeap", meta=(ClampMin="0.1", ClampMax="1.5"))
    float LeapDuration = 0.45f;

    /** Landing burst radius. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|StaticLeap", meta=(ClampMin="1"))
    float LandingBurstRadius = 400.f;

    /** Landing burst damage per target. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|StaticLeap", meta=(ClampMin="0"))
    float LandingDamage = 30.f;

    /** Whether the leap may start while falling. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|StaticLeap")
    bool bAllowLeapWhileFalling = false;

public:
    /** Applies the one-shot landing burst damage query around the hero (server). */
    void ExecuteLandingBurstAt(APSBaseCharacter* Hero);

private:
    void ExecuteLandingBurstInternal();
    FVector LeapDirection = FVector::ForwardVector;
    float LeapDistance = 0.f;
    bool bLandingBurstApplied = false;
};

/**
 * C — Overcharge: timed combat buff. A duration Gameplay Effect grants
 * Status.Buff.Overcharge; the Combat Component reads that tag for its
 * effective fire/recharge rates. Cooldown makes self-stacking impossible.
 */
UCLASS()
class PSGAMEPLAY_API USRGameplayAbility_Overcharge : public USRGameplayAbility
{
    GENERATED_BODY()

public:
    USRGameplayAbility_Overcharge();

protected:
    virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
    virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

private:
    UFUNCTION() void OnBuffExpired();
    FActiveGameplayEffectHandle BuffHandle;
};

/**
 * X — Stormpiercer: three wide hitscan beam pulses driven by server-side
 * swept-volume queries with per-pulse dedup, cover checks and minimap-only
 * enemy reveals. The charge gate comes from USRGameplayAbility_Ultimate.
 */
UCLASS()
class PSGAMEPLAY_API USRGameplayAbility_Stormpiercer : public USRGameplayAbility_Ultimate
{
    GENERATED_BODY()

public:
    USRGameplayAbility_Stormpiercer();

protected:
    virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
    virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

    UFUNCTION() void OnWindupFinished();
    UFUNCTION() void OnPulseDelayFinished();

public:
    /** Executes one authoritative beam pulse (server); public for verification tooling. */
    void ExecuteBeamPulse(int32 PulseIndex, APSBaseCharacter* BeamHero);
    int32 GetExecutedPulseCount() const { return PulsesExecuted; }

protected:

    /** Beam range. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|Stormpiercer", meta=(ClampMin="100"))
    float BeamRange = 14000.f;

    /** Beam effective radius. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|Stormpiercer", meta=(ClampMin="1"))
    float BeamRadius = 400.f;

    /** Number of damage pulses. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|Stormpiercer", meta=(ClampMin="1", ClampMax="10"))
    int32 PulseCount = 3;

    /** Damage per pulse. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|Stormpiercer", meta=(ClampMin="0"))
    float DamagePerPulse = 55.f;

    /** Seconds between pulses. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|Stormpiercer", meta=(ClampMin="0.05"))
    float PulseSpacing = 0.25f;

    /** Windup before the first pulse. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|Stormpiercer", meta=(ClampMin="0"))
    float WindupDuration = 0.8f;

    /** Enemy last-known-position reveal lifetime. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|Stormpiercer", meta=(ClampMin="1"))
    float RevealLifetime = 6.f;

private:
    void StartNextPulse();
    int32 PulsesExecuted = 0;
    bool bPulsePending = false;
};
