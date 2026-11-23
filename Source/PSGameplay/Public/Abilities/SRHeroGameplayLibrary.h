#pragma once
#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "Abilities/SRHeroMechanicsTypes.h"
#include "SRHeroGameplayLibrary.generated.h"

class APSBaseCharacter;
class UAbilitySystemComponent;

UCLASS()
class PSGAMEPLAY_API UGE_SRHeroModifier : public UGameplayEffect
{
    GENERATED_BODY()
public:
    UGE_SRHeroModifier();
};
UCLASS()
class PSGAMEPLAY_API UGE_SRHeroStatus : public UGameplayEffect
{
    GENERATED_BODY()
public:
    UGE_SRHeroStatus();
};

namespace SRHeroGameplay
{
    PSGAMEPLAY_API UAbilitySystemComponent* ASC(const AActor* Actor);
    PSGAMEPLAY_API APSBaseCharacter* Hero(const AActor* Actor);
    PSGAMEPLAY_API bool SameTeam(const AActor* A, const AActor* B);
    PSGAMEPLAY_API bool IsAlive(const AActor* Actor);
    PSGAMEPLAY_API bool IsEnemy(const AActor* Source, const AActor* Target);
    PSGAMEPLAY_API bool IsDamageable(const AActor* Actor);
    PSGAMEPLAY_API TArray<AActor*> InRadius(AActor* Source, const FVector& Center, float Radius);
    PSGAMEPLAY_API bool Ground(UWorld* World, const AActor* Ignore, const FVector& Desired, FVector& OutGround);
    PSGAMEPLAY_API bool AimGround(APSBaseCharacter* Caster, float Range, FVector& OutGround);
    PSGAMEPLAY_API bool SafeDestination(APSBaseCharacter* Caster, const FVector& Desired, FVector& OutLocation, AActor* OtherIgnored = nullptr);
    PSGAMEPLAY_API bool Cover(const AActor* Source, AActor* Target, const FVector& Start);
    PSGAMEPLAY_API float Damage(APSBaseCharacter* Source, AActor* Target, float Amount);
    PSGAMEPLAY_API bool Heal(APSBaseCharacter* Source, AActor* Target, float Amount);
    PSGAMEPLAY_API FActiveGameplayEffectHandle Buff(AActor* Target, AActor* Source, const FSRHeroActionConfig& Config, float Duration = -1.f);
    PSGAMEPLAY_API FActiveGameplayEffectHandle Status(AActor* Target, AActor* Source, FGameplayTag Tag, float Duration);
    PSGAMEPLAY_API void Push(AActor* Target, const FVector& Direction, float Strength, float Upward = 0.f);
    PSGAMEPLAY_API bool MoveSafely(AActor* Target, const FVector& Delta);
    PSGAMEPLAY_API FVector Facing(const APSBaseCharacter* Caster);
    PSGAMEPLAY_API float HitscanTransmission(UWorld* World, const FVector& Start, const FVector& End);
    PSGAMEPLAY_API bool BarrierFormation(APSBaseCharacter* Caster, const FVector& Center, const FSRHeroActionConfig& Config, TArray<FTransform>& OutTransforms);
}
