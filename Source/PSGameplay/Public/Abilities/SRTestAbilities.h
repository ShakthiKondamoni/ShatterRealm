#pragma once

#include "CoreMinimal.h"
#include "Abilities/SRGameplayAbility.h"
#include "SRTestAbilities.generated.h"

/**
 * Simple verification ability for the shared slot foundation: applies 15
 * damage through the shared GE_SRDamage effect (shield-first consumption is
 * observable on the attributes) with a 3 second cooldown. Assigned to Zyrex's
 * Q slot through DA_Zyrex only — no code references this class.
 */
UCLASS()
class PSGAMEPLAY_API USRTestAbility : public USRGameplayAbility
{
    GENERATED_BODY()

public:
    USRTestAbility();
};

/**
 * Verification ultimate: exercises the charge gate and consumption path of
 * USRGameplayAbility_Ultimate with no additional effects.
 */
UCLASS()
class PSGAMEPLAY_API USRTestUltimate : public USRGameplayAbility_Ultimate
{
    GENERATED_BODY()

public:
    USRTestUltimate();
};
