#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AbilitySystemInterface.h"
#include "SRCombatDummy.generated.h"

class UAbilitySystemComponent;
class UPSAttributeSet;
class UTextRenderComponent;
class UStaticMeshComponent;
struct FOnAttributeChangeData;

/**
 * GAS-enabled damage target for combat testing. Owns its own ASC + AttributeSet,
 * shows live Health/Shield on an attached text renderer and logs every attribute
 * change. Placed by the combat test map builder.
 */
UCLASS()
class PSGAMEPLAY_API APSCombatDummy : public AActor, public IAbilitySystemInterface
{
    GENERATED_BODY()

public:
    APSCombatDummy();

    virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
    virtual void PostInitializeComponents() override;
    virtual void BeginPlay() override;

    UFUNCTION(BlueprintPure, Category="SR|Test") UPSAttributeSet* GetAttributes() const { return DummyAttributes.Get(); }
    UFUNCTION(BlueprintPure, Category="SR|Test") float GetDummyHealth() const;
    UFUNCTION(BlueprintPure, Category="SR|Test") float GetDummyShield() const;

protected:
    void UpdateDisplay(const FOnAttributeChangeData& UnusedData);

    UPROPERTY(VisibleAnywhere, Category="SR|Test") TObjectPtr<UAbilitySystemComponent> DummyAbilitySystem;
    UPROPERTY(VisibleAnywhere, Category="SR|Test") TObjectPtr<UPSAttributeSet> DummyAttributes;
    UPROPERTY(VisibleAnywhere, Category="SR|Test") TObjectPtr<UStaticMeshComponent> BodyMesh;
    UPROPERTY(VisibleAnywhere, Category="SR|Test") TObjectPtr<UTextRenderComponent> StatusText;
};
