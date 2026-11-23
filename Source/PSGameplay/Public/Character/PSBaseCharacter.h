#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "InputActionValue.h"
#include "PSBaseCharacter.generated.h"

class UInputAction;
class UInputMappingContext;
class UPSCharacterMovementComponent;
class UPSAttributeSet;
class UAnimSequence;
class UAnimMontage;
class UAnimInstance;
class UCameraComponent;
class USpringArmComponent;
class UGameplayAbility;
class UGameplayEffect;
class ULocalPlayer;
class UActorComponent;
class USRHeroDataAsset;
class USRWeaponDataAsset;
class UPSCombatComponent;
class USREquipmentComponent;
class USRCombatPresentationComponent;
class USRHeroGameplayComponent;
class UBoxComponent;

/** Primary damage regions resolved by server-side character hitboxes. */
enum class EPSDamageRegion : uint8 { Torso, Head, Pelvis, Arm, Leg };

PSGAMEPLAY_API DECLARE_LOG_CATEGORY_EXTERN(LogProjectSCharacter, Log, All);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPSSlideChanged, bool, bSliding);

UCLASS(Blueprintable)
class PSGAMEPLAY_API APSBaseCharacter : public ACharacter, public IAbilitySystemInterface
{
    GENERATED_BODY()
public:
    APSBaseCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="ProjectS|Gameplay") TObjectPtr<USRHeroGameplayComponent> HeroGameplayComponent;
    virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void PossessedBy(AController* NewController) override;
    virtual void UnPossessed() override;
    virtual void OnRep_PlayerState() override;
    virtual void OnRep_Controller() override;
    virtual void PawnClientRestart() override;
    /** Pawn-eye view for aiming when no PlayerController camera exists. */
    FVector GetPawnViewLocation() const;
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    UFUNCTION(BlueprintPure, Category="ProjectS|GAS") UPSAttributeSet* GetPSAttributes() const;
    UFUNCTION(BlueprintPure, Category="ProjectS|GAS") bool IsPSDead() const;
    /** True while a root Gameplay Effect (SR.State.Rooted) is active: movement blocked, aiming/firing allowed. */
    UFUNCTION(BlueprintPure, Category="ProjectS|GAS") bool IsPSRooted() const;
    UFUNCTION(BlueprintPure, Category="ProjectS|Movement") bool IsPSSprinting() const;
    UFUNCTION(BlueprintPure, Category="ProjectS|Movement") bool IsPSSliding() const { return bPSReplicatedSliding; }
    UFUNCTION(BlueprintPure, Category="ProjectS|Movement") UPSCharacterMovementComponent* GetPSMovement() const;
    UFUNCTION(BlueprintPure, Category="ProjectS|Traversal") bool IsPSTraversing() const;
    UFUNCTION(BlueprintCallable, Category="ProjectS|Traversal") bool TryPSTraversal();
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="ProjectS|Traversal") bool bPSEnableTraversal = true;

    UFUNCTION(BlueprintPure, Category="ProjectS|Hero") USRHeroDataAsset* GetHeroData() const;
    /** Identity, mesh, materials and Animation Blueprint of this hero. Unset keeps the inherited presentation. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="ProjectS|Hero") TObjectPtr<USRHeroDataAsset> HeroData;

    /** Signature weapon configuration driving the shared Combat Component. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, ReplicatedUsing=OnRep_WeaponData, Category="ProjectS|Hero") TObjectPtr<USRWeaponDataAsset> WeaponData;
    UFUNCTION() void OnRep_WeaponData();

    UFUNCTION(BlueprintPure, Category="ProjectS|Combat") UPSCombatComponent* GetCombatComponent() const { return CombatComponent; }
    UFUNCTION(BlueprintPure, Category="ProjectS|Combat") USREquipmentComponent* GetEquipmentComponent() const { return EquipmentComponent; }
    UFUNCTION(BlueprintPure, Category="ProjectS|Combat") USRCombatPresentationComponent* GetPresentationComponent() const { return PresentationComponent; }

    /** Analytically intersect a shot ray with the server pose's body-part hitboxes.
     *  The main character capsule remains the normal targeting collision shape.
     *  Hitboxes deliberately have NoCollision to avoid interfering with movement/cameras.
     */
    bool ResolveDamageRegion(const FVector& ShotStart, const FVector& ShotEnd,
        EPSDamageRegion& OutRegion, FVector& OutImpactPoint) const;

    /**
     * Analytic ray/OBB intersection in world space; returns the entry fraction
     * along the segment in [0,1]. Exposed statically so automation can verify
     * hit geometry without a live skeleton; reused by ResolveDamageRegion.
     */
    static bool DamageBoxRayHit(const FTransform& BoxTransform, const FVector& BoxExtent,
        const FVector& Start, const FVector& End, float& OutFraction);

    /** Enable debug rendering of server-side body hitboxes with ps.ShowHitboxes 1. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="ProjectS|Combat|Hitboxes")
    TArray<TObjectPtr<UBoxComponent>> PSDamageHitboxes;

    /** Names of hitboxes already warned about invalid bone attachments (dev-only dedupe). */
    mutable TArray<FName> WarnedInvalidHitboxes;

    /** Broadcast on the owning client when the tactical map opens/closes (M). */
    DECLARE_MULTICAST_DELEGATE_OneParam(FSRTacticalMapToggled, bool /*bOpen*/)
    FSRTacticalMapToggled OnTacticalMapToggled;

    /** Development-only: set the ultimate charge directly (0..100) for testing. */
    UFUNCTION(Exec, Category="ProjectS|Combat")
    void SRSetUltimateCharge(float NewCharge);

    /** Development-only: activate a granted ability slot (Q/E/C/X) without input. */
    UFUNCTION(Exec, Category="ProjectS|Combat")
    void SRTestAbility(const FString& Slot);

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="ProjectS|Input") TObjectPtr<UInputMappingContext> PSMappingContext;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="ProjectS|Input") TObjectPtr<UInputAction> PSMoveAction;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="ProjectS|Input") TObjectPtr<UInputAction> PSLookAction;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="ProjectS|Input") TObjectPtr<UInputAction> PSCrouchAction;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="ProjectS|Input") TObjectPtr<UInputAction> PSSprintAction;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="ProjectS|Input") TObjectPtr<UInputAction> PSSlideAction;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="ProjectS|Input") TObjectPtr<UInputAction> PSJumpAction;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="ProjectS|Input") TObjectPtr<UInputAction> PSAbilityQAction;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="ProjectS|Input") TObjectPtr<UInputAction> PSAbilityEAction;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="ProjectS|Input") TObjectPtr<UInputAction> PSAbilityCAction;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="ProjectS|Input") TObjectPtr<UInputAction> PSAbilityXAction;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="ProjectS|Input") TObjectPtr<UInputAction> PSPrimaryFireAction;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="ProjectS|Input") TObjectPtr<UInputAction> PSRechargeAction;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="ProjectS|Input") TObjectPtr<UInputAction> PSTacticalMapAction;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="ProjectS|Input") TObjectPtr<UInputAction> PSPingAction;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="ProjectS|Input") TObjectPtr<UInputAction> PSTogglePerspectiveAction;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="ProjectS|Input") float PSMouseSensitivity = 0.15f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="ProjectS|GAS") TArray<TSubclassOf<UGameplayAbility>> PSStartupAbilities;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="ProjectS|GAS") TSubclassOf<UGameplayEffect> PSDefaultAttributeEffect;

    // A slot montage overlays slide poses on the existing motion matching ABP.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="ProjectS|Animation") TObjectPtr<UAnimSequence> PSSlideAnimation;

    UPROPERTY(BlueprintAssignable, Category="ProjectS|Movement") FPSSlideChanged OnPSSlideChanged;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="ProjectS|Camera") TObjectPtr<USpringArmComponent> PSFallbackSpringArm;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="ProjectS|Camera") TObjectPtr<UCameraComponent> PSFallbackCamera;
    /** Locally active first-person camera (V toggles view). Attached to the capsule, not the mesh. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="ProjectS|Camera") TObjectPtr<UCameraComponent> PSFirstPersonCamera;

    /** Local-only viewmodel. Assign a skeletal mesh in BP_Zyrex; use the same skeleton as the supplied arms animations. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="ProjectS|First Person") TObjectPtr<USkeletalMeshComponent> PSFirstPersonArms;

    /** Looped arms pose; used directly when no dedicated FP Anim Blueprint is assigned. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="ProjectS|First Person|Animation") TObjectPtr<UAnimSequence> PSFPIdleSequence;
    /** One-shot fire animation (AnimSequence) on the same skeleton as PSFirstPersonArms. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="ProjectS|First Person|Animation") TObjectPtr<UAnimSequence> PSFPFireSequence;
    /** Optional FP Anim Blueprint with a looping idle state and a Slot node for smooth fire montage blending. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="ProjectS|First Person|Animation") TSubclassOf<UAnimInstance> PSFPAnimInstanceClass;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="ProjectS|First Person|Animation") FName PSFPFireSlot = TEXT("DefaultSlot");
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="ProjectS|First Person|Animation", meta=(ClampMin="0")) float PSFPFireBlendIn = 0.07f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="ProjectS|First Person|Animation", meta=(ClampMin="0")) float PSFPFireBlendOut = 0.12f;
    /** Call after a server-confirmed primary shot; only animates locally owned FP arms. */
    void PlayPSFirstPersonFireAnimation();
    UFUNCTION(BlueprintPure, Category="ProjectS|Camera") bool IsFirstPersonView() const { return bPSFirstPersonView; }

    void SyncPSSampleInputState();
    void SetPSSlidePresentation(bool bSliding);
    void InitializeHeroPresentation();
    void InitializeCombatSystems();
    void InitializeLocalHUD();
    void SetTacticalMapOpen(bool bOpen);
    bool IsTacticalMapOpen() const { return bTacticalMapOpen; }
#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
    UPROPERTY(ReplicatedUsing=OnRep_PSSliding) bool bPSReplicatedSliding = false;
    UPROPERTY(Replicated) bool bPSReplicatedSprint = false;
    UFUNCTION() void OnRep_PSSliding();
    void InitializePSAbilitySystem();
    void EnsurePSInputObjects();
    void InstallPSMapping();
    void RemovePSMapping();
    void UpdatePSSlideAnimation();
    UActorComponent* FindPSTraversalComponent() const;
    void InputPSMove(const FInputActionValue& Value);
    void InputPSLook(const FInputActionValue& Value);
    void InputPSCrouchStarted(const FInputActionValue& Value);
    void InputPSCrouchEnded(const FInputActionValue& Value);
    void InputPSSprintToggle(const FInputActionValue& Value);
    void InputPSSlideStarted(const FInputActionValue& Value);
    void InputPSSlideEnded(const FInputActionValue& Value);
    void InputPSJumpStarted(const FInputActionValue& Value);
    void InputPSJumpEnded(const FInputActionValue& Value);
    void InputPSAbilityQStarted(const FInputActionValue& Value);
    void InputPSAbilityQEnded(const FInputActionValue& Value);
    void InputPSAbilityEStarted(const FInputActionValue& Value);
    void InputPSAbilityEEnded(const FInputActionValue& Value);
    void InputPSAbilityCStarted(const FInputActionValue& Value);
    void InputPSAbilityCEnded(const FInputActionValue& Value);
    void InputPSAbilityXStarted(const FInputActionValue& Value);
    void InputPSAbilityXEnded(const FInputActionValue& Value);
    void InputPSPrimaryFireStarted(const FInputActionValue& Value);
    void InputPSPrimaryFireEnded(const FInputActionValue& Value);
    void InputPSRechargeStarted(const FInputActionValue& Value);
    void InputPSTacticalMapStarted(const FInputActionValue& Value);
    void InputPSPingStarted(const FInputActionValue& Value);
    void InputPSTogglePerspective(const FInputActionValue& Value);
    void SetPSPerspective(bool bFirstPerson);
    void ApplyPSPerspective();
    void SetupPSFirstPersonArmsAnimation();
    void RestorePSFirstPersonIdle();
    FTimerHandle PSFPFireFallbackHandle;
    UFUNCTION(Server, Reliable) void ServerSetPSPerspective(bool bFirstPerson);
    UPROPERTY(Transient) TObjectPtr<UCameraComponent> PSThirdPersonCamera;
    /** Server maintains the same view mode for authoritative aim traces. */
    bool bPSFirstPersonView = false;

    UPROPERTY(Transient) TObjectPtr<UAnimMontage> PSActiveSlideMontage;
    TWeakObjectPtr<ULocalPlayer> PSInputLocalPlayer;
    TWeakObjectPtr<UAbilitySystemComponent> PSInitializedASC;
    mutable TWeakObjectPtr<UActorComponent> PSTraversalComponent;
    FVector2D PSLastMoveInput = FVector2D::ZeroVector;
    FVector2D PSLastLookInput = FVector2D::ZeroVector;
    double PSNextDebugTime = 0.0;
    bool bPSPresentedSlide = false;
    bool bPSDeathHandled = false;
    bool bPSTraversalWarningLogged = false;
    bool bTacticalMapOpen = false;
    /** Replicated ownership of the combat state; owner-replicated inside the component. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="ProjectS|Combat")
    TObjectPtr<UPSCombatComponent> CombatComponent;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="ProjectS|Combat")
    TObjectPtr<USREquipmentComponent> EquipmentComponent;
    /** Cosmetic presentation hub (multicast VFX, damage numbers, camera feedback). */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="ProjectS|Combat")
    TObjectPtr<USRCombatPresentationComponent> PresentationComponent;
};

