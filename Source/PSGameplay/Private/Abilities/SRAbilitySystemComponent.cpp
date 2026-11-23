#include "Abilities/SRAbilitySystemComponent.h"
#include "Abilities/SRGameplayTags.h"
#include "Data/SRHeroDataAsset.h"

bool UPSAbilitySystemComponent::InitializeHeroAbilitySlots(const USRHeroDataAsset* HeroData)
{
    if (GetOwnerRole() != ROLE_Authority) return false;

    static const FGameplayTag SlotTags[] = {
        SRGameplayTags::Tag_Input_Ability_Q,
        SRGameplayTags::Tag_Input_Ability_E,
        SRGameplayTags::Tag_Input_Ability_C,
        SRGameplayTags::Tag_Input_Ability_X
    };

    bool bAllSlotsApplied = true;
    for (const FGameplayTag& SlotTag : SlotTags)
    {
        const TSubclassOf<UGameplayAbility> Desired = HeroData ? HeroData->AbilitySlots.GetAbilityForSlot(SlotTag) : nullptr;

        // Drop the currently granted slot ability when the desired one changed or vanished.
        if (const FGameplayAbilitySpecHandle* Existing = HeroAbilityHandles.Find(SlotTag))
        {
            const FGameplayAbilitySpec* ExistingSpec = FindAbilitySpecFromHandle(*Existing);
            const bool bStillCorrect = ExistingSpec && ExistingSpec->Ability && Desired && ExistingSpec->Ability->GetClass() == Desired;
            if (bStillCorrect) continue;
            ClearAbility(*Existing);
            HeroAbilityHandles.Remove(SlotTag);
        }

        if (Desired)
        {
            const FGameplayAbilitySpecHandle Handle = GrantAbilityWithInputTag(Desired, SlotTag);
            if (Handle.IsValid()) HeroAbilityHandles.Add(SlotTag, Handle);
            else bAllSlotsApplied = false;
        }
    }
    return bAllSlotsApplied;
}

FGameplayAbilitySpecHandle UPSAbilitySystemComponent::GrantAbilityWithInputTag(TSubclassOf<UGameplayAbility> AbilityClass, FGameplayTag InputTag, int32 AbilityLevel)
{
    if (GetOwnerRole() != ROLE_Authority || !AbilityClass || !InputTag.IsValid()) return FGameplayAbilitySpecHandle();

    // Duplicate prevention: identical class under the identical input tag is never granted twice.
    if (HasAbilityWithInputTag(AbilityClass, InputTag))
    {
        UE_LOG(LogProjectSAbilities, Verbose, TEXT("Ability %s already granted with input tag %s"), *GetNameSafe(AbilityClass), *InputTag.ToString());
        return FGameplayAbilitySpecHandle();
    }

    FGameplayAbilitySpec Spec(AbilityClass, AbilityLevel, INDEX_NONE, this);
    Spec.DynamicAbilityTags.AddTag(InputTag);
    const FGameplayAbilitySpecHandle Handle = GiveAbility(Spec);
    UE_LOG(LogProjectSAbilities, Display, TEXT("Granted ability %s with input tag %s"), *GetNameSafe(AbilityClass), *InputTag.ToString());
    return Handle;
}

void UPSAbilitySystemComponent::ClearAbilitiesWithInputTag(FGameplayTag InputTag)
{
    if (GetOwnerRole() != ROLE_Authority) return;
    // Collect first: ClearAbility mutates the activatable list while we would be iterating it.
    TArray<FGameplayAbilitySpecHandle> HandlesToRemove;
    for (const FGameplayAbilitySpec& Spec : GetActivatableAbilities())
    {
        if (Spec.DynamicAbilityTags.HasTagExact(InputTag)) HandlesToRemove.Add(Spec.Handle);
    }
    for (const FGameplayAbilitySpecHandle& Handle : HandlesToRemove) ClearAbility(Handle);
    HeroAbilityHandles.Remove(InputTag);
}

bool UPSAbilitySystemComponent::HasAbilityWithInputTag(TSubclassOf<UGameplayAbility> AbilityClass, FGameplayTag InputTag) const
{
    if (!AbilityClass) return false;
    const UGameplayAbility* const Default = AbilityClass->GetDefaultObject<UGameplayAbility>();
    for (const FGameplayAbilitySpec& Spec : GetActivatableAbilities())
    {
        if (Spec.Ability == Default && Spec.DynamicAbilityTags.HasTagExact(InputTag)) return true;
    }
    return false;
}

void UPSAbilitySystemComponent::NotifyAbilityInputPressed(FGameplayTag InputTag)
{
    if (!InputTag.IsValid()) return;
    for (FGameplayAbilitySpec& Spec : GetActivatableAbilities())
    {
        if (!Spec.DynamicAbilityTags.HasTagExact(InputTag)) continue;
        if (Spec.IsActive())
        {
            // Hold support: forward the press to a running ability (WaitInputPress-style tasks listen for this).
            AbilitySpecInputPressed(Spec);
            InvokeReplicatedEvent(EAbilityGenericReplicatedEvent::InputPressed, Spec.Handle, GetPredictionKeyForNewAction());
        }
        else
        {
            TryActivateAbility(Spec.Handle);
        }
    }
}

void UPSAbilitySystemComponent::NotifyAbilityInputReleased(FGameplayTag InputTag)
{
    if (!InputTag.IsValid()) return;
    for (FGameplayAbilitySpec& Spec : GetActivatableAbilities())
    {
        if (!Spec.DynamicAbilityTags.HasTagExact(InputTag)) continue;
        AbilitySpecInputReleased(Spec);
        if (Spec.IsActive())
        {
            InvokeReplicatedEvent(EAbilityGenericReplicatedEvent::InputReleased, Spec.Handle, GetPredictionKeyForNewAction());
        }
    }
}
