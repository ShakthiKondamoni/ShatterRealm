#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "AbilitySystemInterface.h"
#include "Data/SRPingTypes.h"
#include "PSPlayerState.generated.h"

class UPSAttributeSet;
class UPSAbilitySystemComponent;
class USRHUDWidget;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FPSAttributeChanged, FName, Attribute, float, NewValue, float, OldValue);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSRTeamPingsChanged, const TArray<FSRPingInfo>&, TeamPings);

UCLASS()
class PSGAMEPLAY_API APSPlayerState : public APlayerState, public IAbilitySystemInterface
{
    GENERATED_BODY()
public:
    APSPlayerState();
    virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
    virtual void PostInitializeComponents() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual void Tick(float DeltaSeconds) override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="ProjectS|GAS")
    TObjectPtr<UPSAbilitySystemComponent> PSAbilitySystem;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="ProjectS|GAS")
    TObjectPtr<UPSAttributeSet> PSAttributes;
    UPROPERTY(BlueprintAssignable, Category="ProjectS|GAS")
    FPSAttributeChanged OnPSAttributeChanged;

    // ------------------------------------------------------------------
    // Team identification and team markers (pings / enemy reveals)
    // ------------------------------------------------------------------

    /** Minimal team affiliation. Derived from the spawn point tag on possession
     *  (SpawnA -> 0, SpawnB -> 1) so ally/enemy filtering has controlled test teams. */
    UPROPERTY(Replicated, BlueprintReadOnly, Category="ProjectS|Team")
    int32 TeamId = 0;

    /** Server: assigns the team derived from the spawn point used by this player. */
    void SetTeamFromSpawnSpot();

    /** Development-only team override (server RPC when called on a client). */
    UFUNCTION(Exec, Category="ProjectS|Team")
    void SRTeamId(int32 NewTeamId);

    /** Selected hero id ("Zyrex"/"Veyra"); the game mode resolves the pawn class
     *  from this on spawn. Defaults are applied by player index when empty. */
    UPROPERTY(Replicated, BlueprintReadOnly, Category="ProjectS|Hero")
    FName SelectedHeroID;

    /** Local requests a ping at the aimed world location (validated on the server). */
    void RequestPingFromAim(FGameplayTag Category);

    /** Local requests a ping at an exact location (expanded-map clicks; validated on the server). */
    void RequestPingAtLocation(const FVector& Location, FGameplayTag Category);

    /** Server-side validated ping creation (also used by ultimate reveals; bypasses rate limit). */
    bool AddTeamPing(const FVector& Location, FGameplayTag Category, float Lifetime, int32& OutPingId);

    /** The marker list relevant to THIS player: authoritative list on the server,
     *  delivered list on owning clients. Consumers (HUD) read this. */
    const TArray<FSRPingInfo>& GetTeamPings() const { return HasAuthority() ? AggregatedTeamPings : DeliveredPings; }

    UPROPERTY(BlueprintAssignable, Category="ProjectS|Team")
    FSRTeamPingsChanged OnTeamPingsChanged;

    /** Locally created HUD widget (owning client only; one instance per player). */
    UPROPERTY(Transient)
    TWeakObjectPtr<USRHUDWidget> LocalHUDWidget;

protected:
    UFUNCTION(Server, Reliable, WithValidation)
    void ServerRequestPing(FVector_NetQuantize100 Location, FGameplayTag Category);

    void PruneExpiredPings();

    // Privacy model (Phase 4): the authoritative ping list lives ONLY on the server.
    // It is delivered via a targeted Client RPC to same-team PlayerStates only, so
    // opposing clients never receive the marker data at the network level.

    /** Server: authoritative marker list (never replicated). */
    UPROPERTY()
    TArray<FSRPingInfo> ServerTeamPings;

    /** Server-side merged team view, rebuilt whenever a teammate adds/removes a marker. */
    UPROPERTY()
    TArray<FSRPingInfo> AggregatedTeamPings;

    /** Owning client: the list last delivered by the server for this team. */
    UPROPERTY()
    TArray<FSRPingInfo> DeliveredPings;

    int32 NextPingId = 1;
    double LastPingRealTime = -1000.0;
    /** Server-only creation timestamps used for marker expiry. */
    TMap<int32, double> PingCreationTimes;
    FTimerHandle PingPruneTimerHandle;

    /** Server: pushes the current team list to every same-team PlayerState. */
    void DeliverPingsToTeam();

    UFUNCTION(Client, Reliable)
    void ClientDeliverTeamPings(const TArray<FSRPingInfo>& Pings);
};

