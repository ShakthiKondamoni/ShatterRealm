#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "PSCharacterMovementComponent.generated.h"

/** Broadcast on server and owning client when a leap dash completes (early obstruction included). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPSLeapDashFinished, bool, bWasObstructed);

UCLASS()
class PSGAMEPLAY_API UPSCharacterMovementComponent : public UCharacterMovementComponent
{
    GENERATED_BODY()
public:
    UPSCharacterMovementComponent();
    virtual float GetMaxSpeed() const override;
    virtual void UpdateFromCompressedFlags(uint8 Flags) override;
    virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;
    virtual void UpdateCharacterStateBeforeMovement(float DeltaSeconds) override;
    virtual void PhysWalking(float DeltaTime, int32 Iterations) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
    virtual void CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration) override;
    UFUNCTION(BlueprintPure, Category="ProjectS|Slide") bool CanStartPSSlide() const;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="ProjectS|Movement", meta=(ClampMin="0")) float PSRunSpeed = 450.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="ProjectS|Movement", meta=(ClampMin="0")) float PSSprintSpeed = 750.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="ProjectS|Movement", meta=(ClampMin="0")) float PSCrouchSpeed = 180.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="ProjectS|Slide", meta=(ClampMin="0")) float PSSlideEntrySpeed = 500.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="ProjectS|Slide", meta=(ClampMin="0")) float PSSlideExitSpeed = 200.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="ProjectS|Slide", meta=(ClampMin="0.1")) float PSSlideDuration = 1.1f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="ProjectS|Slide", meta=(ClampMin="0")) float PSSlideFriction = 0.3f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="ProjectS|Slide", meta=(ClampMin="0")) float PSSlideBraking = 250.f;

    // ------------------------------------------------------------------
    // Leap drive (Static Leap): constant horizontal velocity driven by the
    // CharacterMovement pipeline. Velocity is enforced every tick in
    // CalcVelocity, so friction and braking cannot fight it; displacement is
    // normal walking/falling physics supplies collision and vertical gravity.
    // ------------------------------------------------------------------

    /** Broadcast when the dash ends (also on early obstruction); bind for landing logic. */
    UPROPERTY(BlueprintAssignable, Category="ProjectS|Leap")
    FPSLeapDashFinished OnPSLeapDashFinished;

    /** Starts the dash; returns false when direction/speed/duration are invalid. */
    bool StartPSLeapDash(const FVector& Direction, float Speed, float Duration);

    /** Ends the dash immediately and restores exit momentum. */
    void EndPSLeapDash(bool bObstructed);

    UFUNCTION(BlueprintPure, Category="ProjectS|Leap")
    bool IsPSLeapDashActive() const { return bPSLeapDashActive; }

    /** Fraction of commanded displacement per tick below which the dash counts as blocked. */
    UPROPERTY(EditDefaultsOnly, Category="ProjectS|Leap", meta=(ClampMin="0.01", ClampMax="1"))
    float PSLeapDashObstructionFraction = 0.25f;

    /** Consecutive blocked ticks before the dash ends early. */
    UPROPERTY(EditDefaultsOnly, Category="ProjectS|Leap", meta=(ClampMin="1"))
    int32 PSLeapDashObstructionTicks = 2;

    /** Exit speed fraction relative to dash speed (smooth blend back into running). */
    UPROPERTY(EditDefaultsOnly, Category="ProjectS|Leap", meta=(ClampMin="0", ClampMax="1"))
    float PSLeapDashExitSpeedFraction = 0.4f;

    // Carried in saved moves, so clients cannot set a speed with an RPC.
    bool bPSSprintRequested = false;
    bool bPSCrouchHeld = false;
    bool bPSSlideHeld = false;
    bool bPSSlideConsumed = false;
    bool bPSSliding = false;
    float PSSlideElapsed = 0.f;

private:
    friend class FPSSavedMove;
    void UpdatePSLeapDashObstruction(const FVector& PreMoveLocation, float DeltaTime);

    bool bPSLeapDashActive = false;
    FVector PSLeapDashDirection = FVector::ZeroVector;
    float PSLeapDashSpeed = 0.f;
    float PSLeapDashRemaining = 0.f;
    int32 PSLeapDashObstructedTicks = 0;
};
