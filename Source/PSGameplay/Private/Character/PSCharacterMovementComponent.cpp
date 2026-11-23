#include "Character/PSCharacterMovementComponent.h"
#include "GAS/PSAttributeSet.h"
#include "Character/PSBaseCharacter.h"
#include "GAS/PSPlayerState.h"

class FPSSavedMove final : public FSavedMove_Character
{
    using Super = FSavedMove_Character;
public:
    bool bSprint = false, bCrouchHeld = false, bSlideHeld = false;
    bool bSliding = false, bSlideConsumed = false;
    float SlideElapsed = 0.f;
    bool bDashActive = false;
    FVector DashDirection = FVector::ZeroVector;
    float DashSpeed = 0.f, DashRemaining = 0.f;
    int32 DashObstructedTicks = 0;

    virtual void Clear() override
    {
        Super::Clear();
        bSprint = bCrouchHeld = bSlideHeld = bSliding = bSlideConsumed = false;
        SlideElapsed = 0.f;
        bDashActive = false; DashDirection = FVector::ZeroVector;
        DashSpeed = DashRemaining = 0.f; DashObstructedTicks = 0;
    }
    virtual uint8 GetCompressedFlags() const override
    {
        return Super::GetCompressedFlags()
            | (bSprint ? FLAG_Custom_0 : 0)
            | (bSlideHeld ? FLAG_Custom_1 : 0)
            | (bCrouchHeld ? FLAG_Custom_2 : 0);
    }
    virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const override
    {
        const FPSSavedMove* Other = static_cast<const FPSSavedMove*>(NewMove.Get());
        // Keep slide substeps separate: friction and expiry depend on their exact delta.
        if (bDashActive || Other->bDashActive || bSliding || Other->bSliding || bSprint != Other->bSprint || bCrouchHeld != Other->bCrouchHeld
            || bSlideHeld != Other->bSlideHeld || bSlideConsumed != Other->bSlideConsumed) return false;
        return Super::CanCombineWith(NewMove, Character, MaxDelta);
    }
    virtual void SetMoveFor(ACharacter* Character, float Delta, const FVector& Accel, FNetworkPredictionData_Client_Character& ClientData) override
    {
        Super::SetMoveFor(Character, Delta, Accel, ClientData);
        const auto* Move = CastChecked<UPSCharacterMovementComponent>(Character->GetCharacterMovement());
        bSprint = Move->bPSSprintRequested; bCrouchHeld = Move->bPSCrouchHeld;
        bSlideHeld = Move->bPSSlideHeld; bSliding = Move->bPSSliding;
        bSlideConsumed = Move->bPSSlideConsumed; SlideElapsed = Move->PSSlideElapsed;
        bDashActive = Move->bPSLeapDashActive;
        DashDirection = Move->PSLeapDashDirection;
        DashSpeed = Move->PSLeapDashSpeed;
        DashRemaining = Move->PSLeapDashRemaining;
        DashObstructedTicks = Move->PSLeapDashObstructedTicks;
    }
    virtual void PrepMoveFor(ACharacter* Character) override
    {
        Super::PrepMoveFor(Character);
        auto* Move = CastChecked<UPSCharacterMovementComponent>(Character->GetCharacterMovement());
        Move->bPSSprintRequested = bSprint; Move->bPSCrouchHeld = bCrouchHeld;
        Move->bPSSlideHeld = bSlideHeld; Move->bPSSliding = bSliding;
        Move->bPSSlideConsumed = bSlideConsumed; Move->PSSlideElapsed = SlideElapsed;
        Move->bPSLeapDashActive = bDashActive;
        Move->PSLeapDashDirection = DashDirection;
        Move->PSLeapDashSpeed = DashSpeed;
        Move->PSLeapDashRemaining = DashRemaining;
        Move->PSLeapDashObstructedTicks = DashObstructedTicks;
    }
};

class FPSClientPredictionData final : public FNetworkPredictionData_Client_Character
{
    using Super = FNetworkPredictionData_Client_Character;
public:
    explicit FPSClientPredictionData(const UCharacterMovementComponent& Movement) : Super(Movement) {}
    virtual FSavedMovePtr AllocateNewMove() override { return FSavedMovePtr(new FPSSavedMove()); }
};

UPSCharacterMovementComponent::UPSCharacterMovementComponent()
{
    GetNavAgentPropertiesRef().bCanCrouch = true;
    MaxWalkSpeed = PSRunSpeed;
    MaxWalkSpeedCrouched = PSCrouchSpeed;
}

float UPSCharacterMovementComponent::GetMaxSpeed() const
{
    if (const APSBaseCharacter* Character = Cast<APSBaseCharacter>(CharacterOwner); Character && Character->IsPSDead()) return 0.f;
    // Rooted: zero movement speed blocks walking/sprint/slide; aiming and firing
    // remain possible. The tag expires with the root Gameplay Effect.
    if (const APSBaseCharacter* Character = Cast<APSBaseCharacter>(CharacterOwner);
        Character && Character->IsPSRooted()) return 0.f;

    if (bPSLeapDashActive) return PSLeapDashSpeed;

    // GAS-driven speed multiplier (shock slow, future buffs). 1.0 when no ASC/attribute exists.
    float SpeedMultiplier = 1.f;
    if (const APSBaseCharacter* Character = Cast<APSBaseCharacter>(CharacterOwner))
    {
        if (const APSPlayerState* State = Character->GetPlayerState<APSPlayerState>())
        {
            if (const UPSAttributeSet* Attributes = State->PSAttributes.Get())
            {
                SpeedMultiplier = Attributes->GetMoveSpeedMultiplier();
            }
        }
    }

    if (IsMovingOnGround())
    {
        if (bPSSliding) return FMath::Max(PSSprintSpeed, Velocity.Size2D()) * SpeedMultiplier;
        if (IsCrouching()) return PSCrouchSpeed * SpeedMultiplier;
        return (bPSSprintRequested ? PSSprintSpeed : PSRunSpeed) * SpeedMultiplier;
    }
    return Super::GetMaxSpeed();
}

void UPSCharacterMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
    Super::UpdateFromCompressedFlags(Flags);
    bPSSprintRequested = (Flags & FSavedMove_Character::FLAG_Custom_0) != 0;
    bPSSlideHeld = (Flags & FSavedMove_Character::FLAG_Custom_1) != 0;
    bPSCrouchHeld = (Flags & FSavedMove_Character::FLAG_Custom_2) != 0;
}

FNetworkPredictionData_Client* UPSCharacterMovementComponent::GetPredictionData_Client() const
{
    if (!ClientPredictionData)
    {
        auto* MutableThis = const_cast<UPSCharacterMovementComponent*>(this);
        MutableThis->ClientPredictionData = new FPSClientPredictionData(*this);
    }
    return ClientPredictionData;
}

bool UPSCharacterMovementComponent::CanStartPSSlide() const
{
    const auto* Character = Cast<APSBaseCharacter>(CharacterOwner);
    return Character && !Character->IsPSDead() && !Character->IsPSRooted() && !Character->IsPSTraversing() && bPSSprintRequested
        && !bPSSliding && !IsCrouching() && IsMovingOnGround()
        && Velocity.Size2D() >= PSSlideEntrySpeed;
}

void UPSCharacterMovementComponent::UpdateCharacterStateBeforeMovement(float DeltaSeconds)
{
    APSBaseCharacter* Character = Cast<APSBaseCharacter>(CharacterOwner);
    if (!Character) { Super::UpdateCharacterStateBeforeMovement(DeltaSeconds); return; }

    if (!bPSSlideHeld) bPSSlideConsumed = false;
    if (bPSSliding)
    {
        if (bPSSlideHeld) bPSSlideConsumed = true;
        PSSlideElapsed += DeltaSeconds;
        if (!bPSSprintRequested || !IsMovingOnGround() || Character->IsPSDead() || Character->IsPSRooted() || PSSlideElapsed >= PSSlideDuration || Velocity.Size2D() < PSSlideExitSpeed)
            bPSSliding = false;
    }
    else if (bPSSlideHeld && !bPSSlideConsumed)
    {
        bPSSlideConsumed = true;
        // Check the standing sprint before Super applies this press's crouch request.
        if (CanStartPSSlide())
        {
            bPSSliding = true;
            PSSlideElapsed = 0.f;
        }
    }
    bWantsToCrouch = bPSCrouchHeld || bPSSliding;
    Character->SyncPSSampleInputState();
    Super::UpdateCharacterStateBeforeMovement(DeltaSeconds);
    if (bPSSliding && !IsCrouching()) bPSSliding = false;
    Character->SetPSSlidePresentation(bPSSliding);
}

void UPSCharacterMovementComponent::PhysWalking(float DeltaTime, int32 Iterations)
{
    if (!bPSSliding) { Super::PhysWalking(DeltaTime, Iterations); return; }
    // Retain CMC floor checks, stepping, collision sweeps and network prediction.
    // Crouch provides the short capsule; low friction provides the slide.
    TGuardValue<float> FrictionGuard(GroundFriction, PSSlideFriction);
    TGuardValue<float> BrakingGuard(BrakingDecelerationWalking, PSSlideBraking);
    // This engine setting is a bitfield, so it cannot be bound to TGuardValue's reference.
    const bool bSavedSeparateBraking = bUseSeparateBrakingFriction;
    bUseSeparateBrakingFriction = false;
    const FVector SavedAcceleration = Acceleration;
    Acceleration = FVector::ZeroVector;
    Super::PhysWalking(DeltaTime, Iterations);
    Acceleration = SavedAcceleration;
    bUseSeparateBrakingFriction = bSavedSeparateBraking;
}

// ---------------------------------------------------------------------------
// Leap dash (Static Leap)
// ---------------------------------------------------------------------------

bool UPSCharacterMovementComponent::StartPSLeapDash(const FVector& Direction, float Speed, float Duration)
{
    const FVector Dir = Direction.GetSafeNormal2D();
    const APSBaseCharacter* Hero = Cast<APSBaseCharacter>(CharacterOwner);
    if (bPSLeapDashActive || Dir.ContainsNaN() || Dir.IsNearlyZero() || !FMath::IsFinite(Speed)
        || !FMath::IsFinite(Duration) || Speed <= 0.f || Duration <= 0.f
        || (Hero && (Hero->IsPSDead() || Hero->IsPSRooted()))) return false;

    bPSLeapDashActive = true;
    PSLeapDashDirection = Dir;
    PSLeapDashSpeed = Speed;
    PSLeapDashRemaining = Duration;
    PSLeapDashObstructedTicks = 0;
    return true;
}

void UPSCharacterMovementComponent::EndPSLeapDash(bool bObstructed)
{
    if (!bPSLeapDashActive) return;
    bPSLeapDashActive = false;

    // Blend back into running speed instead of stopping dead at the end of the dash.
    const float ExitSpeed = FMath::Min(PSLeapDashSpeed * PSLeapDashExitSpeedFraction, GetMaxSpeed());
    Velocity.X = PSLeapDashDirection.X * ExitSpeed;
    Velocity.Y = PSLeapDashDirection.Y * ExitSpeed;

    OnPSLeapDashFinished.Broadcast(bObstructed);
}

void UPSCharacterMovementComponent::CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration)
{
    // The dash replaces the whole velocity model: constant speed, immune to
    // friction and braking, exactly as commanded. Runs inside the normal
    // prediction loop, so server and owning client stay in lockstep.
    if (bPSLeapDashActive && (IsMovingOnGround() || IsFalling()))
    {
        Velocity.X = PSLeapDashDirection.X * PSLeapDashSpeed;
        Velocity.Y = PSLeapDashDirection.Y * PSLeapDashSpeed;
        return;
    }
    Super::CalcVelocity(DeltaTime, Friction, bFluid, BrakingDeceleration);
}

void UPSCharacterMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    // Countdown runs before movement so the dash duration is exact; the
    // obstruction check runs AFTER movement and compares pre- vs post-move
    // displacement, so it always reflects this tick's real motion (a pre-move
    // check reads a stale location and false-triggers on the first tick).
    if (bPSLeapDashActive)
    {
        PSLeapDashRemaining -= DeltaTime;
        if (PSLeapDashRemaining <= 0.f)
        {
            EndPSLeapDash(false);
        }
    }
    const FVector PreMoveLocation = UpdatedComponent ? UpdatedComponent->GetComponentLocation() : FVector::ZeroVector;
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (!bPSLeapDashActive) return;

    const APSBaseCharacter* Hero = Cast<APSBaseCharacter>(CharacterOwner);
    if (!UpdatedComponent || !CharacterOwner || (!IsMovingOnGround() && !IsFalling())
        || (Hero && (Hero->IsPSDead() || Hero->IsPSRooted())))
    {
        // Losing the capsule or entering a disabled movement state ends the drive.
        EndPSLeapDash(false);
        return;
    }
    UpdatePSLeapDashObstruction(PreMoveLocation, DeltaTime);
}

void UPSCharacterMovementComponent::UpdatePSLeapDashObstruction(const FVector& PreMoveLocation, float DeltaTime)
{
    // Early obstruction: this tick moved far less than the commanded velocity,
    // for consecutive ticks — the dash is grinding against a wall. Two bad
    // ticks (instead of a time window) keeps the check stable at any tick rate.
    const FVector Location = UpdatedComponent->GetComponentLocation();
    const float ActualDisplacement = FVector::Dist2D(Location, PreMoveLocation);
    const float ExpectedDisplacement = PSLeapDashSpeed * DeltaTime;
    if (ExpectedDisplacement > 1.f && ActualDisplacement < ExpectedDisplacement * PSLeapDashObstructionFraction)
    {
        ++PSLeapDashObstructedTicks;
        if (PSLeapDashObstructedTicks >= PSLeapDashObstructionTicks)
        {
            EndPSLeapDash(true);
        }
    }
    else
    {
        PSLeapDashObstructedTicks = 0;
    }
}
