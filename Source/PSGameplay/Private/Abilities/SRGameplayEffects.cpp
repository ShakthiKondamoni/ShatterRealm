#include "Abilities/SRGameplayEffects.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
#include "GAS/PSAttributeSet.h"
#include "Abilities/SRGameplayTags.h"

namespace
{
FSetByCallerFloat CallerFloat(const FGameplayTag& DataTag)
{
    FSetByCallerFloat Caller;
    Caller.DataTag = DataTag;
    return Caller;
}

void AddAttributeModifier(UGameplayEffect* Effect, const FGameplayAttribute& Attribute, EGameplayModOp::Type Operation, const FGameplayEffectModifierMagnitude& Magnitude)
{
    FGameplayModifierInfo& Info = Effect->Modifiers.AddDefaulted_GetRef();
    Info.Attribute = Attribute;
    Info.ModifierOp = Operation;
    Info.ModifierMagnitude = Magnitude;
}
}

UGE_SRAttributeInit::UGE_SRAttributeInit()
{
    DurationPolicy = EGameplayEffectDurationType::Instant;
    AddAttributeModifier(this, UPSAttributeSet::GetMaxHealthAttribute(), EGameplayModOp::Override, FGameplayEffectModifierMagnitude(FScalableFloat(100.f)));
    AddAttributeModifier(this, UPSAttributeSet::GetHealthAttribute(), EGameplayModOp::Override, FGameplayEffectModifierMagnitude(FScalableFloat(100.f)));
    AddAttributeModifier(this, UPSAttributeSet::GetMaxShieldAttribute(), EGameplayModOp::Override, FGameplayEffectModifierMagnitude(FScalableFloat(100.f)));
    AddAttributeModifier(this, UPSAttributeSet::GetShieldAttribute(), EGameplayModOp::Override, FGameplayEffectModifierMagnitude(FScalableFloat(100.f)));
    AddAttributeModifier(this, UPSAttributeSet::GetMaxUltimateChargeAttribute(), EGameplayModOp::Override, FGameplayEffectModifierMagnitude(FScalableFloat(100.f)));
    AddAttributeModifier(this, UPSAttributeSet::GetUltimateChargeAttribute(), EGameplayModOp::Override, FGameplayEffectModifierMagnitude(FScalableFloat(0.f)));
}

UGE_SRAbilityCooldown::UGE_SRAbilityCooldown()
{
    DurationPolicy = EGameplayEffectDurationType::HasDuration;
    DurationMagnitude = FGameplayEffectModifierMagnitude(CallerFloat(SRGameplayTags::Tag_Data_Cooldown));

    // Granted tags are configured through effect components since 5.3: the deprecated
    // InheritableOwnedTagsContainer is ignored by tag queries and cooldown checks.
    UTargetTagsGameplayEffectComponent* TargetTags = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("TargetTagsComponent"));
    FInheritedTagContainer TagMods;
    // Cooldown owning tags are supplied per ability via Spec.DynamicGrantedTags.
    // Do not grant SR.Cooldown here, or one ability will block every slot.
    TargetTags->SetAndApplyTargetTagChanges(TagMods);
    GEComponents.Add(TargetTags);
}

UGE_SRDamage::UGE_SRDamage()
{
    DurationPolicy = EGameplayEffectDurationType::Instant;
    AddAttributeModifier(this, UPSAttributeSet::GetIncomingDamageAttribute(), EGameplayModOp::Additive, FGameplayEffectModifierMagnitude(CallerFloat(SRGameplayTags::Tag_Data_Damage)));
}

UGE_SRHeal::UGE_SRHeal()
{
    DurationPolicy = EGameplayEffectDurationType::Instant;
    AddAttributeModifier(this, UPSAttributeSet::GetHealthAttribute(), EGameplayModOp::Additive, FGameplayEffectModifierMagnitude(CallerFloat(SRGameplayTags::Tag_Data_Heal)));
}

UGE_SRShieldRestore::UGE_SRShieldRestore()
{
    DurationPolicy = EGameplayEffectDurationType::Instant;
    AddAttributeModifier(this, UPSAttributeSet::GetShieldAttribute(), EGameplayModOp::Additive, FGameplayEffectModifierMagnitude(CallerFloat(SRGameplayTags::Tag_Data_Shield)));
}

UGE_SRShockSlow::UGE_SRShockSlow()
{
    DurationPolicy = EGameplayEffectDurationType::HasDuration;
    DurationMagnitude = FGameplayEffectModifierMagnitude(CallerFloat(SRGameplayTags::Tag_Data_SlowDuration));
    AddAttributeModifier(this, UPSAttributeSet::GetMoveSpeedMultiplierAttribute(), EGameplayModOp::Override, FGameplayEffectModifierMagnitude(CallerFloat(SRGameplayTags::Tag_Data_SlowMultiplier)));
}

UGE_SROvercharge::UGE_SROvercharge()
{
    DurationPolicy = EGameplayEffectDurationType::HasDuration;
    DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(6.f));

    // Granted tags route through effect components (see GE_SRAbilityCooldown).
    UTargetTagsGameplayEffectComponent* TargetTags = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("OverchargeTagsComponent"));
    FInheritedTagContainer TagMods;
    TagMods.AddTag(SRGameplayTags::Tag_Status_Buff_Overcharge);
    TargetTags->SetAndApplyTargetTagChanges(TagMods);
    GEComponents.Add(TargetTags);
}

UGE_SRRoot::UGE_SRRoot()
{
    // Duration supplied by the applier via SetByCaller (SR.Data.RootDuration).
    DurationPolicy = EGameplayEffectDurationType::HasDuration;
    DurationMagnitude = FGameplayEffectModifierMagnitude(CallerFloat(SRGameplayTags::Tag_Data_RootDuration));

    // Rooted state tag granted for the effect's duration (see the Overcharge pattern).
    UTargetTagsGameplayEffectComponent* TargetTags = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("RootTagsComponent"));
    FInheritedTagContainer TagMods;
    TagMods.AddTag(SRGameplayTags::Tag_State_Rooted);
    TargetTags->SetAndApplyTargetTagChanges(TagMods);
    GEComponents.Add(TargetTags);
}
