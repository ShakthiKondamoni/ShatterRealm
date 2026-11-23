#pragma once
#include "CoreMinimal.h"
#include "Abilities/SRGameplayAbility.h"
#include "Abilities/SRHeroMechanicsTypes.h"
#include "SRHeroActionAbility.generated.h"

class APSBaseCharacter;
class ASRHeroField;

/** Native execution policies shared by independently configurable, concrete hero abilities. */
UCLASS(Abstract)
class PSGAMEPLAY_API USRHeroActionAbility : public USRGameplayAbility
{
    GENERATED_BODY()
public:
    USRHeroActionAbility();
    virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
        FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
    bool SubmitMapTarget(const FVector& Requested);
    void CancelFromTargeting();
    const FSRHeroActionConfig& GetConfig() const { return Config; }
protected:
    void ConfigureAction(ESRHeroAction Action, const TCHAR* Name, FGameplayTag SlotCooldown, float Cooldown, bool bUltimate = false);
    virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
    virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Hero|Mechanics") FSRHeroActionConfig Config;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Hero|Mechanics") bool bUsesUltimateCharge = false;
private:
    bool CommitAction();
    void Finish(bool bCancelled = false);
    void StartTicking(float Duration);
    void TickAction();
    void CompleteChannel();
    AActor* SelectAlly(bool bDowned, bool bAllowSelf = false) const;
    void Punch(const FVector& Origin, const FVector& Direction, float Reach);
    bool SpawnBarrierGroup(const FVector& Ground);
    void SpawnMovingVolume();
    APSBaseCharacter* Caster() const;
    FTimerHandle ActionTimer;
    TArray<FActiveGameplayEffectHandle> OwnedEffects;
    TArray<TWeakObjectPtr<AActor>> ChannelTargets;
    TArray<FVector> MapSelections;
    TSet<TWeakObjectPtr<AActor>> DashHits;
    TWeakObjectPtr<ASRHeroField> PendingTree;
    FVector Direction = FVector::ForwardVector, PreviousLocation = FVector::ZeroVector;
    double StartedAt = 0., EndsAt = 0., LastTickAt = 0.;
    float ChannelHealth = 0.f, HealingAccumulator = 0.f;
    bool bOwnsDash = false, bWaitingForMap = false;
};
