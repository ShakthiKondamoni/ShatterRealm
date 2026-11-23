#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "SRGameplayEffects.generated.h"

class UPSAttributeSet;

/**
 * Instant Gameplay Effect that brings every attribute to the shared baseline
 * (100 Health / 100 MaxHealth / 100 Shield / 100 MaxShield / 0 UltimateCharge /
 * 100 MaxUltimateCharge). Assigned through the character's
 * PSDefaultAttributeEffect so initialization is explicit and inspectable.
 * Hero-specific maximums can later be data-driven without changing code.
 */
UCLASS()
class PSGAMEPLAY_API UGE_SRAttributeInit : public UGameplayEffect
{
    GENERATED_BODY()

public:
    UGE_SRAttributeInit();
};

/**
 * Shared ability-cooldown Gameplay Effect: HasDuration with a SetByCaller
 * duration (Data.Cooldown); ability specs add individual granted tags so queries find
 * it. Ability classes configure the duration; per-ability cooldown tags can
 * later be added with dedicated subclasses.
 */
UCLASS()
class PSGAMEPLAY_API UGE_SRAbilityCooldown : public UGameplayEffect
{
    GENERATED_BODY()

public:
    UGE_SRAbilityCooldown();
};

/** Instant damage: writes Data.Damage into the IncomingDamage meta attribute (shield-first consumption). */
UCLASS()
class PSGAMEPLAY_API UGE_SRDamage : public UGameplayEffect
{
    GENERATED_BODY()

public:
    UGE_SRDamage();
};

/** Instant healing: adds Data.Heal to Health (clamped by the AttributeSet). */
UCLASS()
class PSGAMEPLAY_API UGE_SRHeal : public UGameplayEffect
{
    GENERATED_BODY()

public:
    UGE_SRHeal();
};

/** Instant shield restoration: adds Data.Shield to Shield (clamped by the AttributeSet). */
UCLASS()
class PSGAMEPLAY_API UGE_SRShieldRestore : public UGameplayEffect
{
    GENERATED_BODY()

public:
    UGE_SRShieldRestore();
};

/**
 * Shock slow: temporary movement-speed reduction. Duration and multiplier both
 * arrive via SetByCaller (Data.SlowDuration / Data.SlowMultiplier); expiry
 * automatically restores the base MoveSpeedMultiplier.
 */
UCLASS()
class PSGAMEPLAY_API UGE_SRShockSlow : public UGameplayEffect
{
    GENERATED_BODY()

public:
    UGE_SRShockSlow();
};

/** Overcharge buff: 6-second timed effect granting Status.Buff.Overcharge. */
UCLASS()
class PSGAMEPLAY_API UGE_SROvercharge : public UGameplayEffect
{
    GENERATED_BODY()

public:
    UGE_SROvercharge();
};

/**
 * Root: timed effect granting SR.State.Rooted. The character movement
 * component reads that tag to block walking/sprint/slide/vault; aiming and
 * firing remain possible. Duration comes from the SetByCaller tag
 * SR.Data.RootDuration (Void Snare 1.5s, Rift Reaper 3s).
 */
UCLASS()
class PSGAMEPLAY_API UGE_SRRoot : public UGameplayEffect
{
    GENERATED_BODY()

public:
    UGE_SRRoot();
};
