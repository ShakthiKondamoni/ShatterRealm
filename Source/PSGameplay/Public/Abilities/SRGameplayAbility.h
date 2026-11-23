#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "SRGameplayAbility.generated.h"

class UAnimMontage;
class UPSAttributeSet;

/**
 * Shared base Gameplay Ability for every hero ability. Covers activation
 * checks, commitment (cost + cooldown through the native GAS path), an
 * optional activation Gameplay Effect, montage hooks and clean completion.
 *
 * Configuration is data-driven: hero Data Assets reference ability subclasses
 * per slot; no hero-specific code touches the shared character.
 *
 * Design intent for future reusable subclasses (projectiles, dashes, shields,
 * healing, deployables, area effects): override ActivateAbility, call
 * CommitAbility for cost/cooldown, use the provided helpers for effects and
 * montages, and always EndAbility.
 */
UCLASS()
class PSGAMEPLAY_API USRGameplayAbility : public UGameplayAbility
{
    GENERATED_BODY()

public:
    /** Friendly name shown by the HUD slot bound to this ability. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|Ability", meta=(DisplayName="Ability Name"))
    FText AbilityDisplayName;

public:
    USRGameplayAbility();

    virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
        FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

    /** Default implementation: commit, apply the activation effect on the server, play the montage, end. */
    virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

    /** Queries only this ability's configured cooldown tag. */
    virtual const FGameplayTagContainer* GetCooldownTags() const override;

    /** Applies CooldownGameplayEffectClass with SetByCaller duration = CooldownDuration. */
    virtual void ApplyCooldown(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo) const override;

protected:
    /**
     * Instant Gameplay Effect applied to the owner on successful activation.
     * Server-authoritative; magnitudes using ActivationEffectDataTag are fed
     * from ActivationEffectMagnitude (harmless on effects without that tag).
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|Effects",
        meta=(AllowedClasses="/Script/GameplayAbilities.GameplayEffect", DisplayName="Activation Effect"))
    TSubclassOf<UGameplayEffect> ActivationEffect;

    /** SetByCaller data tag used to feed ActivationEffectMagnitude (e.g. Data.Damage). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|Effects", meta=(Categories="Data", DisplayName="Activation Effect Data Tag"))
    FGameplayTag ActivationEffectDataTag;

    /** Magnitude passed to the activation effect under ActivationEffectDataTag. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|Effects", meta=(DisplayName="Activation Effect Magnitude"))
    float ActivationEffectMagnitude = 0.f;

    /** Cooldown duration in seconds; <= 0 disables the cooldown. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|Cooldown", meta=(ClampMin="0", DisplayName="Cooldown Duration"))
    float CooldownDuration = 0.f;

    /** Unique granted cooldown tag for this ability (set a unique tag on custom hero abilities). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|Cooldown")
    FGameplayTag CooldownTag;

    /** Montage played on the owning character when the ability activates. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|Animation", meta=(DisplayName="Ability Montage"))
    TObjectPtr<UAnimMontage> AbilityMontage;

    /** Montage playback rate for AbilityMontage. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|Animation", meta=(ClampMin="0.1", DisplayName="Montage Play Rate"))
    float MontagePlayRate = 1.f;

    /** Applies ActivationEffect to the owner with the configured SetByCaller data (server only). */
    void ApplyActivationEffect(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo);

    /** Plays AbilityMontage on the avatar's mesh through the ASC (prediction-aware). */
    void PlayAbilityMontage();

    /** Convenience accessor for the shared attribute set. */
    const UPSAttributeSet* GetSRAttributes() const;

private:
    /** Scratch container returned by GetCooldownTags (mutable: queried from const contexts). */
    mutable FGameplayTagContainer SRCooldownTags;
};

/**
 * Ultimate-slot ability base: activation requires a fully charged ultimate
 * (UltimateCharge >= MaxUltimateCharge) and a successful activation consumes
 * the charge server-side. Invalid activations never consume charge because
 * the gate runs in CanActivateAbility before any commit.
 */
UCLASS()
class PSGAMEPLAY_API USRGameplayAbility_Ultimate : public USRGameplayAbility
{
    GENERATED_BODY()

public:
    /** Fully charged check, evaluated on the server and the owning client with replicated attributes. */
    virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
        FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

protected:
    /** Commits, consumes the full charge on the server, then runs the shared activation path. */
    virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
};
