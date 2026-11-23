#pragma once
#include "CoreMinimal.h"
#include "Data/SRHeroDataAsset.h"

class APawn;

struct PSGAMEPLAY_API FSRHeroDefinition
{
    FName ID;
    ESRHeroRole Role;
    ESRHeroElement Element;
    FText PrimaryName, SignatureWeapon;
    float Damage, Range, FireRate;
    int32 Charges;
    float Recharge;
    TSubclassOf<UGameplayAbility> Abilities[4];
};
namespace SRHeroRoster
{
    PSGAMEPLAY_API const TArray<FSRHeroDefinition>& All();
    PSGAMEPLAY_API const FSRHeroDefinition* Find(FName HeroID);
    PSGAMEPLAY_API FString HeroAssetRoot(FName HeroID);
    PSGAMEPLAY_API UClass* LoadPawnClass(FName HeroID);
}
