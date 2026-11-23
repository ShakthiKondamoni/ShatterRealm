#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayEffectTypes.h"
#include "Abilities/SRHeroMechanicsTypes.h"
#include "SRHeroGameplayComponent.generated.h"

class APSBaseCharacter;
class ASRHeroDecoy;
class USRHeroActionAbility;
class UInputAction;
class UInputMappingContext;
class UInputComponent;
class UAbilitySystemComponent;
class ULocalPlayer;

UENUM(BlueprintType)
enum class ESRHeroLifeState : uint8 { Alive, Downed, Eliminated };

struct FSRDamageHistoryEntry { double Time = 0.; float Amount = 0.f; };
struct FSRWardEntry { float Remaining = 0.f; double Expires = 0.; FActiveGameplayEffectHandle Effect; };
struct FSREchoSnapshot
{
    FTransform Transform;
    float Health = 0.f, Shield = 0.f;
    double Expires = 0.;
    bool bValid = false;
};

/** Shared avatar lifecycle, rescue state, bounded damage history and contextual targeting. */
UCLASS(ClassGroup=(ProjectS), meta=(BlueprintSpawnableComponent))
class PSGAMEPLAY_API USRHeroGameplayComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    USRHeroGameplayComponent();
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    void InstallInput(UInputComponent* Input);
    void RemoveInput();
    void ClearAvatarEffects(UAbilitySystemComponent* ASC);
    UFUNCTION(BlueprintPure) ESRHeroLifeState GetLifeState() const { return LifeState; }
    UFUNCTION(BlueprintPure) float GetWardHealth() const { return WardHealth; }
    UFUNCTION(BlueprintPure) float GetBlindStrength() const { return BlindStrength; }
    UFUNCTION(BlueprintPure) bool IsAwaitingMapTarget() const { return bAwaitingMapTarget; }
    UFUNCTION(BlueprintPure) int32 GetMapSelectionsRemaining() const { return MapSelectionsRemaining; }
    UPROPERTY(EditDefaultsOnly, Category="Rescue") bool bCanBeDowned = true;
    UPROPERTY(EditDefaultsOnly, Category="Rescue", meta=(ClampMin="1")) float BleedOutSeconds = 30.f;
    UPROPERTY(EditDefaultsOnly, Category="Decoy", meta=(ClampMin="1", ClampMax="45")) float SwapConeDegrees = 20.f;
    UPROPERTY(EditDefaultsOnly, Category="Decoy", meta=(ClampMin="100")) float SwapRange = 10000.f;

    float AbsorbWardDamage(float Damage);
    void AddWard(float Amount, float Duration, AActor* Source);
    void RecordHealthLoss(float Amount);
    float RecentHealthLoss(float Window = 4.f) const;
    float RewindHealthLoss(float Maximum, float Window);
    bool InterceptLethalDamage();
    bool CaptureEcho(float Duration);
    bool HasEcho() const { return Snapshot.bValid; }
    void ClearEcho();
    bool Revive(float Health, float Shield);
    void BreakConcealment();
    void SetBlind(float Strength, float Duration, AActor* Source);
    FActiveGameplayEffectHandle RefreshAura(AActor* Source, const FSRHeroActionConfig& Config);
    void BeginMapSelection(USRHeroActionAbility* Ability, int32 Count);
    void FinishMapSelection();
    void OnMapVisibilityChanged(bool bOpen);
    UFUNCTION(Server, Reliable) void ServerSubmitMapTarget(FVector_NetQuantize Location);
    UFUNCTION(Server, Reliable) void ServerCancelMapSelection();
    UFUNCTION(Server, Reliable) void ServerContextAction();
    void RegisterSwapDecoys(const TArray<ASRHeroDecoy*>& Decoys);

protected:
    UFUNCTION(Client, Reliable) void ClientSetSelectionMode(bool bEnabled, int32 Count);
    UFUNCTION() void OnRep_Concealed();
    UPROPERTY(Replicated, BlueprintReadOnly) ESRHeroLifeState LifeState = ESRHeroLifeState::Alive;
    UPROPERTY(Replicated, BlueprintReadOnly) float WardHealth = 0.f;
    UPROPERTY(Replicated) float BlindStrength = 0.f;
    UPROPERTY(ReplicatedUsing=OnRep_Concealed) bool bConcealed = false;
    UPROPERTY(Replicated) bool bAwaitingMapTarget = false;
    UPROPERTY(Replicated) int32 MapSelectionsRemaining = 0;
    UPROPERTY(Replicated) bool bSwapAvailable = false;
    UPROPERTY(Transient) TObjectPtr<UInputAction> ContextInput;
    UPROPERTY(Transient) TObjectPtr<UInputMappingContext> ContextMapping;
private:
    APSBaseCharacter* GetHero() const;
    TArray<FSRDamageHistoryEntry> DamageHistory;
    TArray<FSRWardEntry> Wards;
    TMap<TWeakObjectPtr<AActor>, FActiveGameplayEffectHandle> AuraEffects;
    TArray<TWeakObjectPtr<ASRHeroDecoy>> SwapDecoys;
    TWeakObjectPtr<USRHeroActionAbility> PendingMapAbility;
    TWeakObjectPtr<ULocalPlayer> InputPlayer;
    FSREchoSnapshot Snapshot;
    FActiveGameplayEffectHandle DownedEffect, EchoEffect;
    double DownedExpires = 0., BlindExpires = 0.;
};
