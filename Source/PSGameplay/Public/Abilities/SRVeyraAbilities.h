#pragma once
#include "CoreMinimal.h"
#include "Abilities/SRHeroActionAbility.h"
#include "SRVeyraAbilities.generated.h"

UCLASS()
class PSGAMEPLAY_API USRGameplayAbility_BlinkCut : public USRHeroActionAbility
{
    GENERATED_BODY()
public:
    USRGameplayAbility_BlinkCut();
};

UCLASS()
class PSGAMEPLAY_API USRGameplayAbility_RiftDagger : public USRHeroActionAbility
{
    GENERATED_BODY()
public:
    USRGameplayAbility_RiftDagger();
};

UCLASS()
class PSGAMEPLAY_API USRGameplayAbility_VoidSnare : public USRHeroActionAbility
{
    GENERATED_BODY()
public:
    USRGameplayAbility_VoidSnare();
};

UCLASS()
class PSGAMEPLAY_API USRGameplayAbility_RiftReaper : public USRHeroActionAbility
{
    GENERATED_BODY()
public:
    USRGameplayAbility_RiftReaper();
};
