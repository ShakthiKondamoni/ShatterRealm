#include "Abilities/SRGameplayAbility.h"
#include "GAS/PSAttributeSet.h"
#include "Abilities/SRGameplayEffects.h"
#include "Abilities/SRGameplayTags.h"
#include "AbilitySystemComponent.h"

USRGameplayAbility::USRGameplayAbility()
{
    // Standard hero-ability defaults: predicted activation, activation through
    // the Input.Ability.* tags only (no world triggers). The ASC on the
    // PlayerState replicates owner-only, so ability replication needs no extra policy.
    NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;

    // Default shared cooldown effect; abilities may override the class.
    CooldownGameplayEffectClass = UGE_SRAbilityCooldown::StaticClass();
    CooldownTag = SRGameplayTags::Tag_SR_Cooldown; // fallback for custom abilities without a configured unique tag
    AbilityDisplayName = FText::FromString(GetName().Replace(TEXT("SRGameplayAbility_"), TEXT("")));
}

bool USRGameplayAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
    if (!ActorInfo || !Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags)) return false;
    const UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();
    const UPSAttributeSet* Attributes = ASC ? ASC->GetSet<UPSAttributeSet>() : nullptr;
    return Attributes && Attributes->GetHealth() > 0.f
        && !ASC->HasMatchingGameplayTag(SRGameplayTags::Tag_State_Stunned)
        && !ASC->HasMatchingGameplayTag(SRGameplayTags::Tag_State_Downed);
}

void USRGameplayAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
    if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
    {
        // Cost or cooldown rejected the commit; nothing was consumed or applied.
        UE_LOG(LogProjectSAbilities, Verbose, TEXT("Ability %s failed to commit"), *GetName());
        EndAbility(Handle, ActorInfo, ActivationInfo, true /*bWasCancelled*/, false);
        return;
    }

    ApplyActivationEffect(Handle, ActorInfo, ActivationInfo);
    PlayAbilityMontage();

    // The foundation abilities are instant: effects and montage are queued, then the ability ends.
    EndAbility(Handle, ActorInfo, ActivationInfo, false, false);
}

const FGameplayTagContainer* USRGameplayAbility::GetCooldownTags() const
{
    // Base implementation returns ActivationOwnedTags; extend with the shared cooldown tag.
    SRCooldownTags.Reset();
    // Only this ability's cooldown tag is queried; no global shared-cooldown gate.
    if (CooldownTag.IsValid()) SRCooldownTags.AddTag(CooldownTag);
    return &SRCooldownTags;
}

void USRGameplayAbility::ApplyCooldown(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo) const
{
    if (CooldownDuration <= 0.f || !CooldownGameplayEffectClass) return;
    UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
    if (!ASC) return;

    const FGameplayEffectSpecHandle SpecHandle = MakeOutgoingGameplayEffectSpec(CooldownGameplayEffectClass, GetAbilityLevel(Handle, ActorInfo));
    if (!SpecHandle.IsValid())
    {
        UE_LOG(LogProjectSAbilities, Warning, TEXT("ApplyCooldown: spec build failed for %s"), *GetNameSafe(CooldownGameplayEffectClass));
        return;
    }
    SpecHandle.Data->SetSetByCallerMagnitude(SRGameplayTags::Tag_Data_Cooldown, CooldownDuration);
    // The GE grants no global cooldown tag; the per-ability tag is added here.
    if (CooldownTag.IsValid()) SpecHandle.Data->DynamicGrantedTags.AddTag(CooldownTag);
    const FActiveGameplayEffectHandle AppliedHandle = ASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data);
    UE_LOG(LogProjectSAbilities, Log, TEXT("ApplyCooldown: %s dur=%.2f specDuration=%.2f applied=%d"),
        *GetNameSafe(CooldownGameplayEffectClass), CooldownDuration, SpecHandle.Data->GetDuration(), AppliedHandle.IsValid());
}

void USRGameplayAbility::ApplyActivationEffect(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo)
{
    UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
    if (!ASC || !ActivationEffect) return;
    if (ASC->GetOwnerRole() != ROLE_Authority)
    {
        // Instant effects are applied on the server only; results replicate through the AttributeSet.
        return;
    }

    const FGameplayEffectSpecHandle SpecHandle = MakeOutgoingGameplayEffectSpec(ActivationEffect, GetAbilityLevel(Handle, ActorInfo));
    if (!SpecHandle.IsValid()) return;
    if (ActivationEffectDataTag.IsValid())
    {
        SpecHandle.Data->SetSetByCallerMagnitude(ActivationEffectDataTag, ActivationEffectMagnitude);
    }
    ASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data);
}

void USRGameplayAbility::PlayAbilityMontage()
{
    if (!AbilityMontage) return;
    UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
    if (!ASC) return;
    // Hook point: long-running abilities should instead use UAbilityTask_PlayMontageAndWait
    // so activation lasts exactly as long as the montage. The foundation ends immediately.
    ASC->PlayMontage(this, CurrentActivationInfo, AbilityMontage, MontagePlayRate, NAME_None);
}

const UPSAttributeSet* USRGameplayAbility::GetSRAttributes() const
{
    UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
    return ASC ? ASC->GetSet<UPSAttributeSet>() : nullptr;
}

bool USRGameplayAbility_Ultimate::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
    if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags)) return false;

    const UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
    const UPSAttributeSet* Attributes = ASC ? ASC->GetSet<UPSAttributeSet>() : nullptr;
    if (!Attributes) return false;
    // Evaluated with replicated attributes on the server and the owning client; the server result is authoritative.
    return Attributes->GetMaxUltimateCharge() > 0.f && Attributes->GetUltimateCharge() >= Attributes->GetMaxUltimateCharge();
}

void USRGameplayAbility_Ultimate::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
    UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
    const UPSAttributeSet* Attributes = ASC ? ASC->GetSet<UPSAttributeSet>() : nullptr;
    const bool bServer = ASC && ASC->GetOwnerRole() == ROLE_Authority;

    if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
    {
        EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
        return;
    }

    // CanActivateAbility guaranteed a full charge, so a valid activation always lands here;
    // invalid activations were rejected before any commit and never consume charge.
    if (bServer && Attributes)
    {
        ASC->SetNumericAttributeBase(UPSAttributeSet::GetUltimateChargeAttribute(),
            FMath::Max(0.f, Attributes->GetUltimateCharge() - Attributes->GetMaxUltimateCharge()));
    }

    ApplyActivationEffect(Handle, ActorInfo, ActivationInfo);
    PlayAbilityMontage();
    EndAbility(Handle, ActorInfo, ActivationInfo, false, false);
}
