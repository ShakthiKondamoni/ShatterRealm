#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "PSAttributeSet.generated.h"

#define PS_ATTRIBUTE_ACCESSORS(Property) \
    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(UPSAttributeSet, Property) \
    GAMEPLAYATTRIBUTE_VALUE_GETTER(Property) \
    GAMEPLAYATTRIBUTE_VALUE_SETTER(Property) \
    GAMEPLAYATTRIBUTE_VALUE_INITTER(Property)

UCLASS()
class PSGAMEPLAY_API UPSAttributeSet : public UAttributeSet
{
    GENERATED_BODY()

public:
    UPSAttributeSet();
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;
    virtual void PostAttributeChange(const FGameplayAttribute& Attribute, float OldValue, float NewValue) override;
    virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;

    UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_Health, Category="ProjectS|Attributes")
    FGameplayAttributeData Health;
    PS_ATTRIBUTE_ACCESSORS(Health)

    UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_MaxHealth, Category="ProjectS|Attributes")
    FGameplayAttributeData MaxHealth;
    PS_ATTRIBUTE_ACCESSORS(MaxHealth)

    UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_Shield, Category="ProjectS|Attributes")
    FGameplayAttributeData Shield;
    PS_ATTRIBUTE_ACCESSORS(Shield)

    UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_MaxShield, Category="ProjectS|Attributes")
    FGameplayAttributeData MaxShield;
    PS_ATTRIBUTE_ACCESSORS(MaxShield)

    UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_UltimateCharge, Category="ProjectS|Attributes")
    FGameplayAttributeData UltimateCharge;
    PS_ATTRIBUTE_ACCESSORS(UltimateCharge)

    UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_MaxUltimateCharge, Category="ProjectS|Attributes")
    FGameplayAttributeData MaxUltimateCharge;
    PS_ATTRIBUTE_ACCESSORS(MaxUltimateCharge)

    // Meta attribute: damage effects write here; the server consumes it shield-first.
    UPROPERTY(BlueprintReadOnly, Category="ProjectS|Attributes")
    FGameplayAttributeData IncomingDamage;
    PS_ATTRIBUTE_ACCESSORS(IncomingDamage)

    /** Multiplier over the movement component's speeds (1 = normal). Duration effects like the shock slow modify it. */
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_MoveSpeedMultiplier, Category="ProjectS|Attributes")
    FGameplayAttributeData MoveSpeedMultiplier;
    PS_ATTRIBUTE_ACCESSORS(MoveSpeedMultiplier)

    UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_DamageDealtMultiplier, Category="ProjectS|Attributes")
    FGameplayAttributeData DamageDealtMultiplier;
    PS_ATTRIBUTE_ACCESSORS(DamageDealtMultiplier)

    UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_FireRateMultiplier, Category="ProjectS|Attributes")
    FGameplayAttributeData FireRateMultiplier;
    PS_ATTRIBUTE_ACCESSORS(FireRateMultiplier)

    UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_RechargeRateMultiplier, Category="ProjectS|Attributes")
    FGameplayAttributeData RechargeRateMultiplier;
    PS_ATTRIBUTE_ACCESSORS(RechargeRateMultiplier)

    UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_DamageTakenMultiplier, Category="ProjectS|Attributes")
    FGameplayAttributeData DamageTakenMultiplier;
    PS_ATTRIBUTE_ACCESSORS(DamageTakenMultiplier)

    UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_KnockbackMultiplier, Category="ProjectS|Attributes")
    FGameplayAttributeData KnockbackMultiplier;
    PS_ATTRIBUTE_ACCESSORS(KnockbackMultiplier)

protected:
    UFUNCTION() void OnRep_DamageDealtMultiplier(const FGameplayAttributeData& OldValue);
    UFUNCTION() void OnRep_FireRateMultiplier(const FGameplayAttributeData& OldValue);
    UFUNCTION() void OnRep_RechargeRateMultiplier(const FGameplayAttributeData& OldValue);
    UFUNCTION() void OnRep_DamageTakenMultiplier(const FGameplayAttributeData& OldValue);
    UFUNCTION() void OnRep_KnockbackMultiplier(const FGameplayAttributeData& OldValue);
    UFUNCTION() void OnRep_Health(const FGameplayAttributeData& OldValue);
    UFUNCTION() void OnRep_MaxHealth(const FGameplayAttributeData& OldValue);
    UFUNCTION() void OnRep_Shield(const FGameplayAttributeData& OldValue);
    UFUNCTION() void OnRep_MaxShield(const FGameplayAttributeData& OldValue);
    UFUNCTION() void OnRep_UltimateCharge(const FGameplayAttributeData& OldValue);
    UFUNCTION() void OnRep_MaxUltimateCharge(const FGameplayAttributeData& OldValue);
    UFUNCTION() void OnRep_MoveSpeedMultiplier(const FGameplayAttributeData& OldValue);
};

