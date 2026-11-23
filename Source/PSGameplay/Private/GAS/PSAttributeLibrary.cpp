#include "GAS/PSAttributeLibrary.h"
#include "GAS/PSAttributeSet.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"
#include "Character/SRHeroGameplayComponent.h"

namespace
{
bool ApplyDelta(AActor* Target, const FGameplayAttribute& Attribute, float Amount, AActor* Source)
{
    if (!IsValid(Target) || !Target->HasAuthority() || !FMath::IsFinite(Amount) || FMath::IsNearlyZero(Amount)) return false;
    UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target);
    if (!ASC || !ASC->HasAttributeSetForAttribute(Attribute)) return false;
    UGameplayEffect* Effect = NewObject<UGameplayEffect>(GetTransientPackage());
    Effect->DurationPolicy = EGameplayEffectDurationType::Instant;
    FGameplayModifierInfo& Modifier = Effect->Modifiers.AddDefaulted_GetRef();
    Modifier.Attribute = Attribute;
    Modifier.ModifierOp = EGameplayModOp::Additive;
    Modifier.ModifierMagnitude = FScalableFloat(Amount);
    FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
    Context.AddSourceObject(IsValid(Source) ? Source : Target);
    Context.AddInstigator(IsValid(Source) ? Source : Target, IsValid(Source) ? Source : Target);
    const FGameplayEffectSpec Spec(Effect, Context, 1.f);
    ASC->ApplyGameplayEffectSpecToSelf(Spec);
    return true;
}
}

bool UPSAttributeLibrary::ApplyDamage(AActor* Target, float Amount, AActor* Source)
{ return Amount > 0.f && ApplyDelta(Target, UPSAttributeSet::GetIncomingDamageAttribute(), Amount, Source); }
bool UPSAttributeLibrary::Heal(AActor* Target, float Amount, AActor* Source)
{
    const USRHeroGameplayComponent* State = Target ? Target->FindComponentByClass<USRHeroGameplayComponent>() : nullptr;
    return (!State || State->GetLifeState() == ESRHeroLifeState::Alive)
        && Amount > 0.f && ApplyDelta(Target, UPSAttributeSet::GetHealthAttribute(), Amount, Source);
}
bool UPSAttributeLibrary::RestoreShield(AActor* Target, float Amount, AActor* Source)
{ return Amount > 0.f && ApplyDelta(Target, UPSAttributeSet::GetShieldAttribute(), Amount, Source); }
bool UPSAttributeLibrary::AddUltimateCharge(AActor* Target, float Amount)
{ return Amount > 0.f && ApplyDelta(Target, UPSAttributeSet::GetUltimateChargeAttribute(), Amount, Target); }
bool UPSAttributeLibrary::TrySpendUltimateCharge(AActor* Target)
{
    if (!IsValid(Target) || !Target->HasAuthority()) return false;
    const UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target);
    const UPSAttributeSet* Attributes = ASC ? ASC->GetSet<UPSAttributeSet>() : nullptr;
    return Attributes && Attributes->GetHealth() > 0.f && Attributes->GetUltimateCharge() >= Attributes->GetMaxUltimateCharge()
        && ApplyDelta(Target, UPSAttributeSet::GetUltimateChargeAttribute(), -Attributes->GetMaxUltimateCharge(), Target);
}
