#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PSAttributeLibrary.generated.h"

class AActor;

// All writes are authority-only instant Gameplay Effects, including ultimate spending.
UCLASS()
class PSGAMEPLAY_API UPSAttributeLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="ProjectS|GAS")
    static bool ApplyDamage(AActor* Target, float Amount, AActor* Source);
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="ProjectS|GAS")
    static bool Heal(AActor* Target, float Amount, AActor* Source);
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="ProjectS|GAS")
    static bool RestoreShield(AActor* Target, float Amount, AActor* Source);
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="ProjectS|GAS")
    static bool AddUltimateCharge(AActor* Target, float Amount);
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="ProjectS|GAS")
    static bool TrySpendUltimateCharge(AActor* Target);
};

