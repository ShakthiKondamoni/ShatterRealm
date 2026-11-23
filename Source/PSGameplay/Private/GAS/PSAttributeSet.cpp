#include "GAS/PSAttributeSet.h"
#include "GameplayEffectExtension.h"
#include "Character/PSBaseCharacter.h"
#include "Character/SRHeroGameplayComponent.h"
#include "Abilities/SRGameplayTags.h"
#include "Net/UnrealNetwork.h"

UPSAttributeSet::UPSAttributeSet()
{
    InitMaxHealth(100.f); InitHealth(100.f);
    InitMaxShield(100.f); InitShield(100.f);
    InitMaxUltimateCharge(100.f); InitUltimateCharge(0.f);
    InitIncomingDamage(0.f);
    InitMoveSpeedMultiplier(1.f);
    InitDamageDealtMultiplier(1.f);
    InitFireRateMultiplier(1.f);
    InitRechargeRateMultiplier(1.f);
    InitDamageTakenMultiplier(1.f);
    InitKnockbackMultiplier(1.f);
}

void UPSAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME_CONDITION_NOTIFY(UPSAttributeSet, Health, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UPSAttributeSet, MaxHealth, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UPSAttributeSet, Shield, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UPSAttributeSet, MaxShield, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UPSAttributeSet, UltimateCharge, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UPSAttributeSet, MaxUltimateCharge, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UPSAttributeSet, MoveSpeedMultiplier, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UPSAttributeSet, DamageDealtMultiplier, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UPSAttributeSet, FireRateMultiplier, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UPSAttributeSet, RechargeRateMultiplier, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UPSAttributeSet, DamageTakenMultiplier, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UPSAttributeSet, KnockbackMultiplier, COND_None, REPNOTIFY_Always);
}

void UPSAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
    Super::PreAttributeChange(Attribute, NewValue);
    if (!FMath::IsFinite(NewValue)) { NewValue = 0.f; }
    if (Attribute == GetHealthAttribute()) NewValue = FMath::Clamp(NewValue, 0.f, GetMaxHealth());
    else if (Attribute == GetShieldAttribute()) NewValue = FMath::Clamp(NewValue, 0.f, GetMaxShield());
    else if (Attribute == GetUltimateChargeAttribute()) NewValue = FMath::Clamp(NewValue, 0.f, GetMaxUltimateCharge());
    else if (Attribute == GetMoveSpeedMultiplierAttribute()) NewValue = FMath::Clamp(NewValue, 0.05f, 3.f);
    else if (Attribute == GetDamageTakenMultiplierAttribute() || Attribute == GetKnockbackMultiplierAttribute())
        NewValue = FMath::Clamp(NewValue, 0.f, 3.f);
    else if (Attribute == GetDamageDealtMultiplierAttribute() || Attribute == GetFireRateMultiplierAttribute() || Attribute == GetRechargeRateMultiplierAttribute())
        NewValue = FMath::Clamp(NewValue, 0.05f, 5.f);
    else if (Attribute == GetMaxHealthAttribute() || Attribute == GetMaxUltimateChargeAttribute()) NewValue = FMath::Max(1.f, NewValue);
    else if (Attribute == GetMaxShieldAttribute()) NewValue = FMath::Max(0.f, NewValue);
}

void UPSAttributeSet::PostAttributeChange(const FGameplayAttribute& Attribute, float OldValue, float NewValue)
{
    Super::PostAttributeChange(Attribute, OldValue, NewValue);
    // Duration/infinite effects also need to clamp a current value when its maximum shrinks.
    if (Attribute == GetMaxHealthAttribute() && GetHealth() > NewValue) SetHealth(NewValue);
    else if (Attribute == GetMaxShieldAttribute() && GetShield() > NewValue) SetShield(NewValue);
    else if (Attribute == GetMaxUltimateChargeAttribute() && GetUltimateCharge() > NewValue) SetUltimateCharge(NewValue);
}

void UPSAttributeSet::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)
{
    Super::PostGameplayEffectExecute(Data);
    if (Data.EvaluatedData.Attribute == GetIncomingDamageAttribute())
    {
        float Damage = FMath::IsFinite(GetIncomingDamage()) ? FMath::Max(0.f, GetIncomingDamage()) : 0.f;
        SetIncomingDamage(0.f);
        UAbilitySystemComponent* ASC = GetOwningAbilitySystemComponent();
        APSBaseCharacter* Character = ASC ? Cast<APSBaseCharacter>(ASC->GetAvatarActor()) : nullptr;
        USRHeroGameplayComponent* State = Character ? Character->HeroGameplayComponent.Get() : nullptr;
        if (ASC && ASC->GetOwnerRole() == ROLE_Authority && GetHealth() > 0.f && Damage > 0.f
            && !ASC->HasMatchingGameplayTag(SRGameplayTags::Tag_State_DamageImmune))
        {
            if (State) State->BreakConcealment();
            Damage *= FMath::Clamp(GetDamageTakenMultiplier(), 0.f, 3.f);
            if (State) Damage = State->AbsorbWardDamage(Damage);
            const float BeforeHealth = GetHealth();
            const float Absorbed = FMath::Min(GetShield(), Damage);
            const float HealthLoss = FMath::Max(0.f, Damage - Absorbed);
            // Temporal rescue and downed transitions happen before health reaches zero.
            if (!(HealthLoss >= GetHealth() && State && State->InterceptLethalDamage()))
            {
                SetShield(GetShield() - Absorbed);
                SetHealth(FMath::Max(0.f, GetHealth() - HealthLoss));
            }
            if (State) State->RecordHealthLoss(FMath::Max(0.f, BeforeHealth - GetHealth()));
        }
    }
    SetHealth(FMath::Clamp(GetHealth(), 0.f, GetMaxHealth()));
    SetShield(FMath::Clamp(GetShield(), 0.f, GetMaxShield()));
    SetUltimateCharge(FMath::Clamp(GetUltimateCharge(), 0.f, GetMaxUltimateCharge()));
    SetMoveSpeedMultiplier(FMath::Clamp(GetMoveSpeedMultiplier(), 0.05f, 3.f));
}

#define PS_REPNOTIFY(Name) \
void UPSAttributeSet::OnRep_##Name(const FGameplayAttributeData& OldValue) \
{ GAMEPLAYATTRIBUTE_REPNOTIFY(UPSAttributeSet, Name, OldValue); }
PS_REPNOTIFY(Health)
PS_REPNOTIFY(MaxHealth)
PS_REPNOTIFY(Shield)
PS_REPNOTIFY(MaxShield)
PS_REPNOTIFY(UltimateCharge)
PS_REPNOTIFY(MaxUltimateCharge)
PS_REPNOTIFY(MoveSpeedMultiplier)
PS_REPNOTIFY(DamageDealtMultiplier)
PS_REPNOTIFY(FireRateMultiplier)
PS_REPNOTIFY(RechargeRateMultiplier)
PS_REPNOTIFY(DamageTakenMultiplier)
PS_REPNOTIFY(KnockbackMultiplier)
