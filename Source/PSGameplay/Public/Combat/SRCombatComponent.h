#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SRCombatComponent.generated.h"

class APSBaseCharacter;
class USRWeaponDataAsset;
enum class EPSDamageRegion : uint8;
class ASRElementalProjectile;
class UAbilitySystemComponent;
class UGameplayEffect;
class UAnimMontage;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSRChargesChanged, int32, NewCharges, int32, MaxCharges);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSRRechargeChanged, bool, bIsRecharging, float, RechargeStartTime);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSRConfirmedShot, int32, ShotId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FSRCombatHitConfirmed, int32, ShotId, AActor*, HitActor, float, Distance, float, DamageApplied);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSRCombatStateChanged);

/** Server-side combat state, replicated to the owning player for HUD presentation. */
USTRUCT()
struct FSRCombatReplicatedState
{
    GENERATED_BODY()

    UPROPERTY() int32 PrimaryCharges = 0;
    UPROPERTY() bool bRecharging = false;
    UPROPERTY() float RechargeStartTime = 0.f;
    UPROPERTY() float EffectiveRechargeDuration = 0.f;
    UPROPERTY() bool bFiring = false;
    UPROPERTY() int32 LastConfirmedShotId = 0;
};

/**
 * Shared primary-combat component (hitscan primary, charges, recharge,
 * projectile service, GAS damage application). Holds no hero identity —
 * everything configurable comes from USRWeaponDataAsset. The server owns all
 * gameplay decisions (fire rate, charges, hits, damage); owning clients get
 * responsive predicted cosmetics and deduplicated confirmed events.
 */
UCLASS(ClassGroup=(ProjectS), meta=(BlueprintSpawnableComponent))
class PSGAMEPLAY_API UPSCombatComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UPSCombatComponent();

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    // ------------------------------------------------------------------
    // Requests (called from input handlers on the owning client or server)
    // ------------------------------------------------------------------

    /** LMB pressed: begins automatic (or single) firing. Validated server-side. */
    UFUNCTION(BlueprintCallable, Category="SR|Combat")
    void RequestStartPrimaryFire();

    /** LMB released: stops firing. */
    UFUNCTION(BlueprintCallable, Category="SR|Combat")
    void RequestStopPrimaryFire();

    /** R pressed: requests a primary recharge (rejected at full/while recharging). */
    UFUNCTION(BlueprintCallable, Category="SR|Combat")
    void RequestPrimaryRecharge();

    // ------------------------------------------------------------------
    // Queries
    // ------------------------------------------------------------------

    UFUNCTION(BlueprintPure, Category="SR|Combat") int32 GetCurrentCharges() const { return ReplicatedState.PrimaryCharges; }
    UFUNCTION(BlueprintPure, Category="SR|Combat") int32 GetMaxCharges() const;
    UFUNCTION(BlueprintPure, Category="SR|Combat") bool IsRecharging() const { return ReplicatedState.bRecharging; }
    UFUNCTION(BlueprintPure, Category="SR|Combat") float GetRechargeProgress01() const;
    UFUNCTION(BlueprintPure, Category="SR|Combat") bool IsFiring() const { return ReplicatedState.bFiring; }
    UFUNCTION(BlueprintPure, Category="SR|Combat") float GetEffectiveFireInterval() const;
    USRWeaponDataAsset* GetWeaponData() const { return WeaponData; }
    /** Hero pipeline hookup: assigns the weapon configuration (null clears it). */
    void SetWeaponData(USRWeaponDataAsset* NewWeaponData);
    /** Trace channel used for hitscan and beam volume queries. */
    UFUNCTION(BlueprintPure, Category="SR|Combat") ECollisionChannel GetTraceChannel() const;

    /** Effective recharge duration in seconds with all multipliers applied. */
    float GetEffectiveRechargeDuration() const;

    /** Camera aim resolution shared with abilities: returns the intended world target point. */
    FVector ResolveAimTarget(const FVector& FireOrigin, float Range, FHitResult* OutCameraHit = nullptr) const;

    /** Server-side active-camera basis (first- or third-person) used for aim validation and as the fallback shot basis. */
    void GetServerAimBasis(FVector& OutOrigin, FVector& OutDirection) const;

    /** Camera-ray resolution from an explicit origin/direction (validated client-submitted aim). */
    FVector ResolveAimTargetFrom(const FVector& CameraLoc, const FVector& CameraDir, float Range, FHitResult* OutCameraHit = nullptr) const;

    /** Body-part multiplier lookup (weapon-configured; pure). */
    float GetRegionMultiplier(EPSDamageRegion Region) const;
    /** Debug/test region name. */
    const TCHAR* GetRegionName(EPSDamageRegion Region) const;
    /** Linear range-falloff multiplier (pure; also used by ComputeShotDamage). */
    static float ComputeRangeMultiplier(const USRWeaponDataAsset* Weapon, float Distance);

    /** Returns the world transform of the configured firing socket (falls back to the mesh). */
    bool GetFireOriginTransform(FTransform& OutTransform) const;

    // ------------------------------------------------------------------
    // Services for abilities
    // ------------------------------------------------------------------

    /** Applies combat damage through GAS (shield-first). Returns the damage actually applied; 0 when rejected. */
    float ApplyCombatDamage(AActor* Target, float BaseDamage, AActor* DamageInstigator, bool& bOutTargetWasEnemy);

    /** Spawns a server-authoritative elemental projectile; returns null when rejected. */
    ASRElementalProjectile* SpawnAbilityProjectile(TSubclassOf<ASRElementalProjectile> ProjectileClass, const FVector& Location,
        const FVector& Direction, AActor* AbilityInstigator, float SpeedOverride = 0.f, float RangeOverride = 0.f);

    /** Cancels an active recharge (ability interruption); leaves a consistent state and logs the reason. */
    void CancelPrimaryRecharge(const FString& Reason);

    /** Restores charges to full once; called by the server timer and safe to call directly. */
    void FinishPrimaryRecharge();

    /** Restarts an active recharge (used when Overcharge changes the effective duration mid-recharge). */
    void RestartRechargeIfActive();
    /** Re-time an ongoing automatic primary-fire loop when a temporary buff changes its cadence. */
    void RefreshAutomaticFireInterval();

    /** Combat-state gate shared with abilities (dead, tactical map open, recharging-blocked-fire...). */
    bool CanAttack() const;

    /** Executes one authoritative shot when the fire interval allows; used by the shot timer and tests. */
    bool TryExecutePrimaryShot();

    /**
     * Pure damage calculation (server + tests): BaseDamage x BodyPartMultiplier x
     * RangeMultiplier with linear falloff between FullDamageRange and MinDamageRange.
     * Kept static and side-effect free so automation can verify every parameter.
     */
    static float ComputeShotDamage(const USRWeaponDataAsset* Weapon, float Distance, EPSDamageRegion Region);

    /**
     * Resolves the local (owning-client or listen-host) active camera origin and
     * view direction for aim submission. Returns false when no active camera and
     * no controller view exist.
     */
    bool GetLocalAimView(FVector& OutOrigin, FVector& OutDirection) const;

    // ------------------------------------------------------------------
    // Client fire prediction (owning clients; the host is authoritative and
    // keeps the direct path). Predicts visual/camera effects + ammo display
    // ONLY - hits, damage and body parts remain server-resolved (Task 02).
    // ------------------------------------------------------------------

    /** One locally predicted shot: gates, instant cosmetics + recoil, predicted
     *  ammo decrement, pending record, and the aim/shot submission to the server. */
    void PredictShot();

    /** HUD-facing ammunition: authoritative charges minus in-flight predicted shots. */
    UFUNCTION(BlueprintPure, Category="SR|Combat")
    int32 GetDisplayCharges() const;

    /** In-flight predicted shots awaiting confirmation. */
    UFUNCTION(BlueprintPure, Category="SR|Combat")
    int32 GetPendingPredictedShotCount() const { return PendingPredictedShots.Num(); }

    /** Clears prediction state (weapon change / death / respawn / tests). */
    void ResetFirePrediction(const FString& Reason);

    // Test hooks (never called by gameplay code): drive the acknowledgment core
    // without a networked client role.
    int32 PushPredictedShotForTest(double PredictedRealTime);
    bool AcknowledgePredictedShotForTest(int32 ServerShotId, int32 ClientShotId);
    /** Core acknowledgment: duplicate guard + pending match; true when a prediction was drained. */
    bool ResolveShotAcknowledgment(int32 ServerShotId, int32 ClientShotId);

    // Test/dev accessors (never called by gameplay code).
    void SetPrimaryChargesForTest(int32 NewCharges) { ReplicatedState.PrimaryCharges = NewCharges; OnRep_ReplicatedState(); }
    void SetLastShotRealTimeForTest(double Time) { LastShotRealTime = Time; }
    bool StartRechargeForTest() { const bool bWasRecharging = ReplicatedState.bRecharging; StartRecharge(TEXT("test")); return !bWasRecharging && ReplicatedState.bRecharging; }
    bool CanStartRechargeForTest(FString& OutReason) const { return CanStartRecharge(OutReason); }
    bool FinishPrimaryRechargeGuarded() { FinishPrimaryRecharge(); return ReplicatedState.bRecharging; }
    UAbilitySystemComponent* GetOwnerASCForTest() const { return GetOwnerASC(); }
    AActor* GetOwnerActorForTest() const { return GetOwner(); }
    FString GetLastRechargeRejectReasonForTest() const { return LastRechargeRejectReason; }

    // ------------------------------------------------------------------
    // Events for HUD / cosmetics
    // ------------------------------------------------------------------

    UPROPERTY(BlueprintAssignable, Category="SR|Combat") FSRChargesChanged OnChargesChanged;
    UPROPERTY(BlueprintAssignable, Category="SR|Combat") FSRRechargeChanged OnRechargeChanged;
    UPROPERTY(BlueprintAssignable, Category="SR|Combat") FSRConfirmedShot OnConfirmedShot;
    UPROPERTY(BlueprintAssignable, Category="SR|Combat") FSRCombatHitConfirmed OnHitConfirmed;
    UPROPERTY(BlueprintAssignable, Category="SR|Combat") FSRCombatStateChanged OnCombatStateChanged;

    // ------------------------------------------------------------------
    // Tuning (multipliers are read when the Overcharge buff tag is active)
    // ------------------------------------------------------------------

    /** Primary fire-rate multiplier while Status.Buff.Overcharge is active. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|Combat", meta=(ClampMin="0.1"))
    float OverchargeFireRateMultiplier = 1.35f;

    /** Recharge-speed multiplier while Status.Buff.Overcharge is active (1.25 => duration / 1.25). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|Combat", meta=(ClampMin="0.1"))
    float OverchargeRechargeMultiplier = 1.25f;

    /** Ultimate charge gained per confirmed damage instance on a valid enemy. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|Combat", meta=(ClampMin="0"))
    float UltimateChargePerConfirmedHit = 2.f;

protected:
    /** Weapon configuration (usually set from the hero data pipeline). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|Combat")
    TObjectPtr<USRWeaponDataAsset> WeaponData;

private:
    // Server flow
    void StartFiring();
    void StopFiring(const FString& Reason);
    void ExecutePrimaryShot();
    void StartRecharge(const FString& RequestReason);
    bool CanStartRecharge(FString& OutRejectReason) const;
    void ApplyCosmeticShot(int32 ShotId, const FVector& Origin, const FVector& ImpactPoint, bool bHit);
    void HandleImpactCosmetic(int32 ShotId, const FVector& ImpactPoint);
    void GainUltimateCharge(AActor* DamageTarget);

    UFUNCTION(Server, Reliable, WithValidation)
    void ServerStartFiring();

    /** Owning client submits its camera origin + aim direction + predicted shot
     *  id for the burst. The server validates plausibility (origin near the pawn,
     *  direction close to the server view) before using them; server traces always
     *  decide hits, damage and body parts. */
    UFUNCTION(Server, Reliable, WithValidation)
    void ServerStartFiringWithAim(FVector_NetQuantize10 CameraOrigin, FVector_NetQuantizeNormal AimDirection, int32 ClientShotId);

    /** Unreliable per-predicted-shot aim refresh while an automatic burst is
     *  active (bounded by the fire rate). */
    UFUNCTION(Server, Unreliable)
    void ServerUpdateFireAim(FVector_NetQuantize10 CameraOrigin, FVector_NetQuantizeNormal AimDirection, int32 ClientShotId);

    UFUNCTION(Server, Reliable)
    void ServerStopFiring();

    UFUNCTION(Server, Reliable, WithValidation)
    void ServerRequestRecharge();

    /** Confirmed-shot cosmetic + acknowledgment. Reliable is justified: the rate
     *  is bounded by the fire rate (a few per second), the payload is tiny, and
     *  reliable delivery keeps prediction reconciliation ordered and complete. */
    UFUNCTION(NetMulticast, Reliable)
    void MulticastShotCosmetic(int32 ShotId, FVector_NetQuantize100 Origin, FVector_NetQuantize100 ImpactPoint, bool bHit, int32 ClientShotId);

    UFUNCTION() void OnRep_ReplicatedState();

    APSBaseCharacter* GetOwnerCharacter() const;
    UAbilitySystemComponent* GetOwnerASC() const;
    APlayerController* GetOwnerController() const;

    /** Server-authoritative combat state; owner-replicated for HUD presentation. */
    UPROPERTY(ReplicatedUsing=OnRep_ReplicatedState)
    FSRCombatReplicatedState ReplicatedState;

    FTimerHandle FireTimerHandle;
    FTimerHandle RechargeTimerHandle;
    double LastShotRealTime = 0.0;
    bool bPrimaryChargesInitialized = false;
    int32 NextShotId = 1;
    FString LastRechargeRejectReason;
    /** Guards the one-shot recharge cosmetic (restarted mid-recharge must not replay it). */
    bool bPresentedRecharge = false;
    /** Shot ids predicted locally by the owning client (dedupe against confirmed multicasts). */
    TArray<int32> PredictedShotIds;

    // Fire-prediction state (owning client)
    struct FPendingPredictedShot
    {
        int32 ClientShotId = 0;
        double PredictedRealTime = 0.0;
    };
    TArray<FPendingPredictedShot> PendingPredictedShots;   // FIFO, bounded
    TArray<int32> ProcessedConfirmedShotIds;               // duplicate-ack ring, bounded
    TArray<int32> SubmittedClientShotIds;                  // server-side FIFO of submitted ids
    int32 NextPredictedShotId = 0;
    bool bLocalFireHeld = false;
    FTimerHandle PredictedFireTimerHandle;
    double LastPredictedShotRealTime = 0.0;

    void ExecutePredictedShot();
    void StopPredictedFireTimer();
    void ScheduleNextPredictedShot();
    void HandleShotAcknowledged(int32 ServerShotId, int32 ClientShotId, const FVector& Impact, bool bHit);
    void TrimStalePendingShots();

    /** Last validated client aim (remote-owning clients only; host derives aim locally). */
    FVector ValidatedShotOrigin = FVector::ZeroVector;
    FVector ValidatedAimDirection = FVector::ZeroVector;
    bool bHasValidatedClientAim = false;
    FTimerHandle AimUpdateTimerHandle;
    double LastAimUpdateRealTime = 0.0;

    /** Validates a client aim submission; fills OutReason on rejection. */
    bool ValidateClientAim(const FVector& CameraOrigin, const FVector& AimDirection, FString& OutReason) const;
    void ApplyValidatedAim(const FVector& CameraOrigin, const FVector& AimDirection, bool bLogAccept);
    void StopAimUpdateTimer();
    /** Owning-client timer callback: pushes throttled aim updates during a burst. */
    void SendAimUpdate();
};
