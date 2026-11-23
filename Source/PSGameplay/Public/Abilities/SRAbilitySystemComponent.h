#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "SRAbilitySystemComponent.generated.h"

class USRHeroDataAsset;

/**
 * Shared Ability System Component for every hero. Lives on the PlayerState
 * (owner) with the possessed character as the avatar. Adds the two pieces the
 * plain engine class does not provide:
 *
 *  1. Hero slot management: grants/removes the Q/E/C/X abilities described by
 *     a USRHeroDataAsset, tracked by input tag, server-authoritative, with
 *     duplicate prevention and clean regrant after respawn or hero change.
 *     Shared (non-hero) abilities granted through other paths are untouched.
 *  2. Input-tag routing: Enhanced Input forwards Pressed/Released for the
 *     Input.Ability.* tags here; the ASC activates whichever granted ability
 *     carries the tag. No per-hero input handlers exist anywhere.
 */
UCLASS()
class PSGAMEPLAY_API UPSAbilitySystemComponent : public UAbilitySystemComponent
{
    GENERATED_BODY()

public:
    /**
     * Server: synchronizes the granted Q/E/C/X abilities with the data asset.
     * Missing abilities are granted (tagged with their slot input tag), stale
     * ones are removed, already-correct ones are left untouched. Safe to call
     * on every (re)initialization; never duplicates grants.
     * Returns true when every configured slot is granted as requested.
     */
    bool InitializeHeroAbilitySlots(const USRHeroDataAsset* HeroData);

    /**
     * Server: grants a gameplay ability and marks it with the input tag.
     * Returns an invalid handle when rejected or already granted with the
     * same class and tag.
     */
    FGameplayAbilitySpecHandle GrantAbilityWithInputTag(TSubclassOf<UGameplayAbility> AbilityClass, FGameplayTag InputTag, int32 AbilityLevel = 1);

    /** Server: removes every granted ability carrying the given input tag. */
    void ClearAbilitiesWithInputTag(FGameplayTag InputTag);

    /** True when an ability with this class and input tag is already granted. */
    bool HasAbilityWithInputTag(TSubclassOf<UGameplayAbility> AbilityClass, FGameplayTag InputTag) const;

    /** Routing entry points for the shared character's Enhanced Input bindings. */
    void NotifyAbilityInputPressed(FGameplayTag InputTag);
    void NotifyAbilityInputReleased(FGameplayTag InputTag);

private:
    /** Server-side handles of the currently granted hero slot abilities, keyed by slot input tag. */
    TMap<FGameplayTag, FGameplayAbilitySpecHandle> HeroAbilityHandles;
};
