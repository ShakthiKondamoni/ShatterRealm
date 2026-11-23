#include "Character/PSBaseCharacter.h"
#include "Character/PSCharacterMovementComponent.h"
#include "Character/SRHeroGameplayComponent.h"
#include "GAS/PSPlayerState.h"
#include "GAS/PSAttributeSet.h"
#include "Abilities/SRAbilitySystemComponent.h"
#include "Combat/SRCombatComponent.h"
#include "Combat/SRCombatPresentationComponent.h"
#include "Debug/SRDebug.h"
#include "Combat/SREquipmentComponent.h"
#include "Abilities/SRGameplayTags.h"
#include "Data/SRHeroDataAsset.h"
#include "Combat/SRWeaponDataAsset.h"
#include "UI/SRHUDWidget.h"
#include "Data/SRMinimapDataAsset.h"
#include "Blueprint/UserWidget.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimationAsset.h"
#include "TimerManager.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "InputCoreTypes.h"
#include "GameplayEffect.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "CoreGlobals.h"
#include "Net/UnrealNetwork.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY(LogProjectSCharacter);
static TAutoConsoleVariable<int32> CVarPSShowHitboxes(TEXT("ps.ShowHitboxes"), 0,
    TEXT("Show server-side primary damage boxes. 0=off, 1=on (non-shipping only)."));

namespace
{
// The array order below is stable: head, upper torso, pelvis, left/right arms,
// left/right thighs, left/right calves. Socket names match UE mannequin skeletons.
constexpr EPSDamageRegion PSHitRegions[] = {
    EPSDamageRegion::Head, EPSDamageRegion::Torso, EPSDamageRegion::Pelvis,
    EPSDamageRegion::Arm, EPSDamageRegion::Arm,
    EPSDamageRegion::Leg, EPSDamageRegion::Leg,
    EPSDamageRegion::Leg, EPSDamageRegion::Leg
};

// Ray/OBB intersection, independent of collision channel and physics asset.
// Returns entry fraction along segment in [0,1].
// Bone-attached boxes follow the animated skeleton; boxes attached to a static
// parent (e.g. the capsule root) are a safe fallback for non-mannequin heroes.
// A skeletal parent with a missing bone invalidates the box (warned upstream).
bool IntersectDamageBox(const UBoxComponent* Box, const FVector& Start,
    const FVector& End, float& OutFraction)
{
    if (!IsValid(Box) || !Box->IsRegistered()) return false;
    const USkeletalMeshComponent* ParentMesh = Cast<USkeletalMeshComponent>(Box->GetAttachParent());
    if (ParentMesh && ParentMesh->GetBoneIndex(Box->GetAttachSocketName()) == INDEX_NONE) return false;
    return APSBaseCharacter::DamageBoxRayHit(Box->GetComponentTransform(), Box->GetUnscaledBoxExtent(), Start, End, OutFraction);
}
}

static TAutoConsoleVariable<int32> CVarPSInputDebug(TEXT("ps.InputDebug"), 1,
    TEXT("0: off, 1: input lifecycle and toggle logs, 2: live values on screen and in log."));

APSBaseCharacter::APSBaseCharacter(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer.SetDefaultSubobjectClass<UPSCharacterMovementComponent>(ACharacter::CharacterMovementComponentName))
{
    PrimaryActorTick.bCanEverTick = true;
    bReplicates = true;
    bUseControllerRotationYaw = false;
    GetCharacterMovement()->bOrientRotationToMovement = true;
    GetCharacterMovement()->RotationRate = FRotator(0.f, 540.f, 0.f);
    PSFallbackSpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("PSFallbackSpringArm"));
    PSFallbackSpringArm->SetupAttachment(GetRootComponent());
    PSFallbackSpringArm->TargetArmLength = 350.f;
    PSFallbackSpringArm->bUsePawnControlRotation = true;
    PSFallbackSpringArm->bDoCollisionTest = true;
    PSFallbackSpringArm->ProbeChannel = ECC_Camera;
    PSFallbackSpringArm->ProbeSize = 18.f; // spherical sweep approximates camera collision volume
    PSFallbackCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("PSFallbackCamera"));
    PSFallbackCamera->SetupAttachment(PSFallbackSpringArm);
    PSFallbackCamera->SetAutoActivate(false);
    PSFirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("PSFirstPersonCamera"));
    PSFirstPersonCamera->SetupAttachment(GetCapsuleComponent());
    PSFirstPersonCamera->SetRelativeLocation(FVector(-8.f, 0.f, 64.f));
    PSFirstPersonCamera->bUsePawnControlRotation = true;
    PSFirstPersonCamera->SetAutoActivate(false);
    PSFirstPersonArms = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("PSFirstPersonArms"));
    PSFirstPersonArms->SetupAttachment(PSFirstPersonCamera);
    PSFirstPersonArms->SetRelativeLocation(FVector(12.f, 0.f, -35.f));
    PSFirstPersonArms->SetOnlyOwnerSee(true);
    PSFirstPersonArms->SetCastShadow(false);
    PSFirstPersonArms->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    PSFirstPersonArms->SetVisibility(false);
    PSFirstPersonArms->SetIsReplicated(false);

    // Gameplay-only body hitboxes follow the animated third-person skeleton.
    // NoCollision is intentional: the capsule can remain the first trace hit,
    // and the server refines that hit with analytic ray/box intersections.
    struct FPSHitboxSpec { const TCHAR* Name; const TCHAR* Bone; FVector Extent; };
    const FPSHitboxSpec Specs[] = {
        {TEXT("PSHit_Head"), TEXT("head"), FVector(13.f, 14.f, 16.f)},
        {TEXT("PSHit_Torso"), TEXT("spine_03"), FVector(23.f, 19.f, 28.f)},
        {TEXT("PSHit_Pelvis"), TEXT("pelvis"), FVector(20.f, 18.f, 17.f)},
        {TEXT("PSHit_ArmL"), TEXT("upperarm_l"), FVector(12.f, 12.f, 24.f)},
        {TEXT("PSHit_ArmR"), TEXT("upperarm_r"), FVector(12.f, 12.f, 24.f)},
        {TEXT("PSHit_ThighL"), TEXT("thigh_l"), FVector(13.f, 13.f, 26.f)},
        {TEXT("PSHit_ThighR"), TEXT("thigh_r"), FVector(13.f, 13.f, 26.f)},
        {TEXT("PSHit_CalfL"), TEXT("calf_l"), FVector(11.f, 11.f, 22.f)},
        {TEXT("PSHit_CalfR"), TEXT("calf_r"), FVector(11.f, 11.f, 22.f)}
    };
    for (const FPSHitboxSpec& Spec : Specs)
    {
        UBoxComponent* Hitbox = CreateDefaultSubobject<UBoxComponent>(FName(Spec.Name));
        Hitbox->SetupAttachment(GetMesh(), FName(Spec.Bone));
        Hitbox->SetBoxExtent(Spec.Extent);
        Hitbox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Hitbox->SetCanEverAffectNavigation(false);
        Hitbox->SetHiddenInGame(true);
        Hitbox->SetIsReplicated(false); // pose replicated by movement/animation, damage decided by server
        PSDamageHitboxes.Add(Hitbox);
    }

    HeroGameplayComponent = CreateDefaultSubobject<USRHeroGameplayComponent>(TEXT("HeroGameplay"));
    CombatComponent = CreateDefaultSubobject<UPSCombatComponent>(TEXT("PSCombatComponent"));
    CombatComponent->SetIsReplicated(true);
    EquipmentComponent = CreateDefaultSubobject<USREquipmentComponent>(TEXT("PSEquipmentComponent"));
    PresentationComponent = CreateDefaultSubobject<USRCombatPresentationComponent>(TEXT("PSCombatPresentationComponent"));
    PresentationComponent->SetIsReplicated(true);
}

bool APSBaseCharacter::ResolveDamageRegion(const FVector& ShotStart, const FVector& ShotEnd,
    EPSDamageRegion& OutRegion, FVector& OutImpactPoint) const
{
    if (!HasAuthority() || IsPSDead()) return false;
    constexpr int32 RegionCount = sizeof(PSHitRegions) / sizeof(PSHitRegions[0]);
    float BestFraction = TNumericLimits<float>::Max();
    bool bFound = false;
    for (int32 Index = 0; Index < PSDamageHitboxes.Num() && Index < RegionCount; ++Index)
    {
        const UBoxComponent* Hitbox = PSDamageHitboxes[Index];
        if (!Hitbox) continue;
#if !UE_BUILD_SHIPPING
        // Development warning: a hitbox attached to a bone this skeleton does not
        // have can never register hits - surface it once per box so skeleton
        // mismatches (non-mannequin heroes) are visible immediately.
        const USkeletalMeshComponent* ParentMesh = Cast<USkeletalMeshComponent>(Hitbox->GetAttachParent());
        if (ParentMesh && ParentMesh->GetBoneIndex(Hitbox->GetAttachSocketName()) == INDEX_NONE &&
            !WarnedInvalidHitboxes.Contains(Hitbox->GetFName()))
        {
            WarnedInvalidHitboxes.Add(Hitbox->GetFName());
            UE_LOG(LogTemp, Warning, TEXT("[ProjectS][Combat] Hitbox '%s' on %s is attached to missing bone '%s' - it will never register hits"),
                *Hitbox->GetName(), *GetName(), *Hitbox->GetAttachSocketName().ToString());
        }
#endif
        float Fraction = 0.f;
        if (IntersectDamageBox(Hitbox, ShotStart, ShotEnd, Fraction) && Fraction < BestFraction)
        {
            BestFraction = Fraction;
            OutRegion = PSHitRegions[Index];
            bFound = true;
        }
#if !UE_BUILD_SHIPPING
        if (CVarPSShowHitboxes.GetValueOnGameThread() > 0)
        {
            DrawDebugBox(GetWorld(), Hitbox->GetComponentLocation(), Hitbox->GetScaledBoxExtent(),
                Hitbox->GetComponentQuat(), FColor::Cyan, false, 0.12f, 0, 0.7f);
        }
#endif
    }
    if (bFound) OutImpactPoint = FMath::Lerp(ShotStart, ShotEnd, BestFraction);
    return bFound;
}

bool APSBaseCharacter::DamageBoxRayHit(const FTransform& BoxTransform, const FVector& BoxExtent,
    const FVector& Start, const FVector& End, float& OutFraction)
{
    const FVector LocalStart = BoxTransform.InverseTransformPosition(Start);
    const FVector LocalEnd = BoxTransform.InverseTransformPosition(End);
    const FVector Direction = LocalEnd - LocalStart;
    const FVector Extent = BoxExtent;
    float Near = 0.f;
    float Far = 1.f;
    for (int32 Axis = 0; Axis < 3; ++Axis)
    {
        const float S = LocalStart[Axis];
        const float D = Direction[Axis];
        const float E = Extent[Axis];
        if (FMath::Abs(D) < 0.0001f)
        {
            if (S < -E || S > E) return false;
            continue;
        }
        const float A = (-E - S) / D;
        const float B = ( E - S) / D;
        Near = FMath::Max(Near, FMath::Min(A, B));
        Far = FMath::Min(Far, FMath::Max(A, B));
        if (Near > Far) return false;
    }
    OutFraction = Near;
    return true;
}

UPSCharacterMovementComponent* APSBaseCharacter::GetPSMovement() const
{ return Cast<UPSCharacterMovementComponent>(GetCharacterMovement()); }

UAbilitySystemComponent* APSBaseCharacter::GetAbilitySystemComponent() const
{
    const APSPlayerState* State = GetPlayerState<APSPlayerState>();
    return State ? State->GetAbilitySystemComponent() : nullptr;
}

UPSAttributeSet* APSBaseCharacter::GetPSAttributes() const
{
    const APSPlayerState* State = GetPlayerState<APSPlayerState>();
    return State ? State->PSAttributes.Get() : nullptr;
}

bool APSBaseCharacter::IsPSDead() const
{
    const UPSAttributeSet* Attributes = GetPSAttributes();
    return Attributes && Attributes->GetHealth() <= 0.f;
}

bool APSBaseCharacter::IsPSRooted() const
{
    const UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
    return ASC && (ASC->HasMatchingGameplayTag(SRGameplayTags::Tag_State_Rooted)
        || ASC->HasMatchingGameplayTag(SRGameplayTags::Tag_State_Stunned)
        || ASC->HasMatchingGameplayTag(SRGameplayTags::Tag_State_Downed));
}

bool APSBaseCharacter::IsPSSprinting() const
{
    const auto* Movement = GetPSMovement();
    const bool bSprint = GetLocalRole() == ROLE_SimulatedProxy ? bPSReplicatedSprint : (Movement && Movement->bPSSprintRequested);
    return bSprint && !bIsCrouched && !IsPSSliding() && !IsPSDead();
}

USRHeroDataAsset* APSBaseCharacter::GetHeroData() const { return HeroData; }

void APSBaseCharacter::InitializeHeroPresentation()
{
    if (!HeroData)
    {
        UE_LOG(LogProjectSCharacter, Log, TEXT("Hero presentation: %s has no HeroData; keeping inherited mesh and animation."), *GetName());
        return;
    }
    if (!HeroData->IsSRHeroDataValid())
    {
        UE_LOG(LogProjectSCharacter, Warning, TEXT("Hero presentation: %s references a hero data asset without HeroID/DisplayName; ignoring it."), *GetName());
        return;
    }

    // Identity is pure data; always report which hero this is.
    UE_LOG(LogProjectSCharacter, Display, TEXT("Hero init: %s | role=%s | element=%s | owner=%s"),
        *HeroData->HeroID.ToString(),
        *UEnum::GetDisplayValueAsText(HeroData->HeroRole).ToString(),
        *UEnum::GetDisplayValueAsText(HeroData->HeroElement).ToString(),
        *GetName());

    USkeletalMeshComponent* MeshComp = GetMesh();
    if (!MeshComp) return;

    // Soft references are resolved here at BeginPlay, never during actor construction.
    if (!HeroData->CharacterSkeletalMesh.IsNull())
    {
        if (USkeletalMesh* HeroMesh = HeroData->CharacterSkeletalMesh.LoadSynchronous())
        {
            // Only touch the mesh when it actually differs, so the sample's
            // authored presentation survives data assets that mirror it.
            if (MeshComp->GetSkeletalMeshAsset() != HeroMesh) MeshComp->SetSkeletalMesh(HeroMesh);
        }
        else
        {
            UE_LOG(LogProjectSCharacter, Warning, TEXT("Hero presentation: mesh '%s' failed to load; keeping inherited mesh."),
                *HeroData->CharacterSkeletalMesh.ToString());
        }
    }

    const int32 NumSlots = MeshComp->GetNumMaterials();
    for (int32 Index = 0; Index < HeroData->MaterialOverrides.Num(); ++Index)
    {
        UMaterialInterface* Material = HeroData->MaterialOverrides[Index].LoadSynchronous();
        if (!Material)
        {
            UE_LOG(LogProjectSCharacter, Warning, TEXT("Hero presentation: material override %d ('%s') failed to load."), Index,
                *HeroData->MaterialOverrides[Index].ToString());
            continue;
        }
        if (Index >= NumSlots)
        {
            UE_LOG(LogProjectSCharacter, Warning, TEXT("Hero presentation: material override %d ('%s') has no matching slot on %s (%d slots); skipping the rest."),
                Index, *Material->GetName(), *GetName(), NumSlots);
            break;
        }
        MeshComp->SetMaterial(Index, Material);
    }

    // Only replaced when valid and different, so preview refreshes never reset
    // a running motion matching instance.
    if (!HeroData->AnimationBlueprintClass.IsNull())
    {
        UClass* AnimClass = HeroData->AnimationBlueprintClass.LoadSynchronous();
        if (AnimClass && AnimClass->IsChildOf(UAnimInstance::StaticClass()))
        {
            if (MeshComp->GetAnimClass() != AnimClass) MeshComp->SetAnimInstanceClass(AnimClass);
        }
        else
        {
            UE_LOG(LogProjectSCharacter, Warning, TEXT("Hero presentation: animation class '%s' is not a UAnimInstance; keeping inherited animation."),
                *HeroData->AnimationBlueprintClass.ToString());
        }
    }
}

#if WITH_EDITOR
void APSBaseCharacter::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
    // Live preview of the hero configuration while designing a hero Blueprint.
    // The CDO stays untouched so nothing leaks into saved packages; gameplay
    // worlds keep their own BeginPlay path.
    if (IsRunningCommandlet() || HasAnyFlags(RF_ClassDefaultObject)) return;
    if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(APSBaseCharacter, HeroData))
        InitializeHeroPresentation();
}
#endif

void APSBaseCharacter::BeginPlay()
{
    Super::BeginPlay();
    SRDebug::NetLog(this, FString::Printf(TEXT("BeginPlay (controller=%s PlayerState=%s)"),
        *GetNameSafe(GetController()), *GetNameSafe(GetPlayerState<APSPlayerState>())));
    // Ensure the active sample Blueprint spring arm also avoids geometry and pawns.
    TArray<USpringArmComponent*> SpringArms;
    GetComponents(SpringArms);
    for (USpringArmComponent* Arm : SpringArms)
    {
        if (!Arm) continue;
        Arm->bDoCollisionTest = true;
        Arm->ProbeChannel = ECC_Camera;
        Arm->ProbeSize = FMath::Max(Arm->ProbeSize, 18.f);
    }
    // Character capsules must block camera sweeps (default presets may ignore Camera).
    if (UCapsuleComponent* Capsule = GetCapsuleComponent())
        Capsule->SetCollisionResponseToChannel(ECC_Camera, ECR_Block);
    // Keep the sample's own camera when present; provide one for bare native children.
    TArray<UCameraComponent*> Cameras;
    GetComponents(Cameras);
    // Preserve the active Game Animation Sample camera for third-person.
    for (UCameraComponent* Camera : Cameras)
    {
        if (Camera && Camera != PSFirstPersonCamera && Camera != PSFallbackCamera && Camera->IsActive())
        { PSThirdPersonCamera = Camera; break; }
    }
    if (!PSThirdPersonCamera)
    {
        PSThirdPersonCamera = PSFallbackCamera;
        PSThirdPersonCamera->Activate();
    }
    ApplyPSPerspective();
    InitializeHeroPresentation();
    InitializeCombatSystems();
    InitializePSAbilitySystem();
    InstallPSMapping();
    InitializeLocalHUD();
}

void APSBaseCharacter::InitializeLocalHUD()
{
    if (!IsLocallyControlled())
    {
        SRDebug::NetLog(this, TEXT("HUD skipped: not locally controlled (remote pawn view)"));
        return;
    }
    APlayerController* LocalPC = Cast<APlayerController>(GetController());
    if (!LocalPC || !LocalPC->IsLocalController())
    {
        SRDebug::NetLog(this, TEXT("HUD deferred: controller not replicated/local yet"));
        return;
    }
    APSPlayerState* State = GetPlayerState<APSPlayerState>();
    if (!State)
    {
        SRDebug::NetLog(this, TEXT("HUD deferred: PlayerState not available yet"));
        return;
    }
    // One HUD per player: recreated only when absent (respawn/possession safe).
    if (State->LocalHUDWidget.IsValid())
    {
        if (USRHUDWidget* HUD = State->LocalHUDWidget.Get())
        {
            HUD->BindToCharacter(this);
            SRDebug::NetLog(this, TEXT("HUD rebound to new pawn (existing widget)"));
            return;
        }
    }
    UUserWidget* HUDAsWidget = nullptr;
    USRHUDWidget* HUD = nullptr;
    // Prefer the Widget Blueprint wrapper (visual arrangement) when it exists.
    if (UClass* WBPClass = LoadClass<UUserWidget>(nullptr, TEXT("/Game/ProjectS/UI/WBP_PlayerHUD.WBP_PlayerHUD_C")))
    {
        HUD = Cast<USRHUDWidget>(CreateWidget<UUserWidget>(LocalPC, WBPClass));
        HUDAsWidget = HUD;
    }
    else
    {
        HUD = CreateWidget<USRHUDWidget>(LocalPC, USRHUDWidget::StaticClass());
        HUDAsWidget = HUD;
    }
    if (!HUD)
    {
        SRDebug::NetLog(this, TEXT("HUD creation FAILED (widget could not be created)"));
        return;
    }
    State->LocalHUDWidget = HUD;
    // Minimap configuration: the combat test map ships one under /Game/ProjectS/UI.
    HUD->MinimapData = LoadObject<USRMinimapDataAsset>(nullptr, TEXT("/Game/ProjectS/UI/DA_MinimapTest.DA_MinimapTest"));
    HUD->AddToViewport();
    HUD->BindToCharacter(this);
    SRDebug::NetLog(this, FString::Printf(TEXT("HUD created and bound: %s (owner PlayerState=%s)"), *GetNameSafe(HUD), *GetNameSafe(State)));
    SRDebug::Message(SRDebug::ECategory::HUD, TEXT("Client"), TEXT("HUD created"), 0.8f, FColor::Green);
}

FVector APSBaseCharacter::GetPawnViewLocation() const
{
    return GetMesh() ? GetMesh()->GetComponentLocation() + FVector(0, 0, 60.f) : GetActorLocation();
}

void APSBaseCharacter::InitializeCombatSystems()
{
    if (CombatComponent)
    {
        // Weapon configuration comes from the hero pipeline; it may be overridden
        // per hero via the WeaponData property in a Blueprint.
        if (WeaponData) CombatComponent->SetWeaponData(WeaponData);
        else if (CombatComponent->GetWeaponData()) CombatComponent->SetWeaponData(CombatComponent->GetWeaponData());
        else SRDebug::Log(SRDebug::ECategory::Combat, TEXT("Init"), TEXT("No WeaponData configured on character or combat component"));
        SRDebug::NetLog(this, FString::Printf(TEXT("Weapon init: weapon=%s authority=%s"),
            *GetNameSafe(CombatComponent->GetWeaponData()), HasAuthority() ? TEXT("server (authoritative ammo)") : TEXT("client (local view)")));
    }
    if (EquipmentComponent && WeaponData)
    {
        EquipmentComponent->EquipWeapon(WeaponData);
    }
    if (PresentationComponent)
    {
        // Gauntlet materials are collected after equipment attach, so this runs last.
        PresentationComponent->InitializeForCharacter(this);
    }
}

void APSBaseCharacter::SetTacticalMapOpen(bool bOpen)
{
    if (bTacticalMapOpen == bOpen) return;
    bTacticalMapOpen = bOpen;
    if (HeroGameplayComponent) HeroGameplayComponent->OnMapVisibilityChanged(bOpen);
    if (bOpen && CombatComponent) CombatComponent->RequestStopPrimaryFire();
    SRDebug::Message(SRDebug::ECategory::HUD, TEXT("Client"), bOpen ? TEXT("Tactical map opened") : TEXT("Tactical map closed"), 0.5f, FColor::Cyan);
}

void APSBaseCharacter::SRSetUltimateCharge(float NewCharge)
{
    // Server-side development command only. Never expose an unrestricted client RPC.
#if UE_BUILD_SHIPPING
    return;
#else
    if (!HasAuthority())
    {
        UE_LOG(LogProjectSCharacter, Warning, TEXT("SRSetUltimateCharge requires server authority"));
        return;
    }

    UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
    const UPSAttributeSet* Attributes = ASC ? ASC->GetSet<UPSAttributeSet>() : nullptr;
    if (!ASC || !Attributes)
    {
        SRDebug::Message(SRDebug::ECategory::GAS, TEXT("Debug"), TEXT("SRSetUltimateCharge: no attributes"), 0.8f, FColor::Orange);
        return;
    }
    const float Clamped = FMath::Clamp(NewCharge, 0.f, Attributes->GetMaxUltimateCharge());
    ASC->SetNumericAttributeBase(UPSAttributeSet::GetUltimateChargeAttribute(), Clamped);
    SRDebug::Message(SRDebug::ECategory::GAS, TEXT("Server"), FString::Printf(TEXT("Ultimate charge set to %.0f"), Clamped), 0.8f, FColor::Green);
#endif
}

void APSBaseCharacter::SRTestAbility(const FString& Slot)
{
    // Development-only command: activates a granted ability slot directly so
    // abilities are verifiable in PIE without injected keyboard input.
    UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
    if (!ASC)
    {
        SRDebug::Message(SRDebug::ECategory::Ability, TEXT("Debug"), TEXT("SRTestAbility: no ASC"), 0.8f, FColor::Orange);
        return;
    }

    FGameplayTag SlotTag;
    const FName SlotName(*Slot.ToUpper());
    if (SlotName == "Q") SlotTag = SRGameplayTags::Tag_Input_Ability_Q;
    else if (SlotName == "E") SlotTag = SRGameplayTags::Tag_Input_Ability_E;
    else if (SlotName == "C") SlotTag = SRGameplayTags::Tag_Input_Ability_C;
    else if (SlotName == "X") SlotTag = SRGameplayTags::Tag_Input_Ability_X;
    else
    {
        SRDebug::Message(SRDebug::ECategory::Ability, TEXT("Debug"), TEXT("SRTestAbility: usage SRTestAbility Q|E|C|X"), 0.8f, FColor::Orange);
        return;
    }

    for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
    {
        if (Spec.Ability && Spec.DynamicAbilityTags.HasTagExact(SlotTag))
        {
            const bool bActivated = ASC->TryActivateAbility(Spec.Handle, true);
            SRDebug::Message(SRDebug::ECategory::Ability, TEXT("Debug"),
                FString::Printf(TEXT("SRTestAbility %s -> %s"), *Slot.ToUpper(), bActivated ? TEXT("ACTIVATED") : TEXT("REJECTED")), 1.f,
                bActivated ? FColor::Green : FColor::Orange);
            return;
        }
    }
    SRDebug::Message(SRDebug::ECategory::Ability, TEXT("Debug"), FString::Printf(TEXT("SRTestAbility %s: no granted ability in slot"), *Slot.ToUpper()), 1.f, FColor::Orange);
}

void APSBaseCharacter::PossessedBy(AController* NewController)
{
    Super::PossessedBy(NewController);
    SRDebug::NetLog(this, FString::Printf(TEXT("PossessedBy controller=%s PlayerState=%s"),
        *GetNameSafe(NewController), *GetNameSafe(GetPlayerState<APSPlayerState>())));
    if (APSPlayerState* State = GetPlayerState<APSPlayerState>())
    {
        State->SetTeamFromSpawnSpot();
    }
    InitializePSAbilitySystem();
    InstallPSMapping();
    InitializeLocalHUD();
}
void APSBaseCharacter::OnRep_PlayerState()
{
    Super::OnRep_PlayerState();
    SRDebug::NetLog(this, FString::Printf(TEXT("OnRep_PlayerState state=%s"), *GetNameSafe(GetPlayerState<APSPlayerState>())));
    InitializePSAbilitySystem();
    InitializeCombatSystems();
    InitializeLocalHUD(); // PlayerState often arrives after BeginPlay on remote clients.
    if (PresentationComponent) PresentationComponent->InitializeForCharacter(this);
}
void APSBaseCharacter::OnRep_WeaponData()
{
    SRDebug::NetLog(this, FString::Printf(TEXT("OnRep_WeaponData weapon=%s"), *GetNameSafe(WeaponData)));
    InitializeCombatSystems();
    if (PresentationComponent) PresentationComponent->InitializeForCharacter(this);
}
void APSBaseCharacter::OnRep_Controller()
{
    Super::OnRep_Controller();
    SRDebug::NetLog(this, FString::Printf(TEXT("OnRep_Controller controller=%s PlayerState=%s"),
        *GetNameSafe(GetController()), *GetNameSafe(GetPlayerState<APSPlayerState>())));
    // Callback arrival order is not guaranteed: the PlayerState may already be
    // here (full GAS init possible) or not (this call safely no-ops and the
    // later OnRep_PlayerState completes initialization).
    InitializePSAbilitySystem();
    InstallPSMapping();
    ApplyPSPerspective();
    InitializeLocalHUD();
}
void APSBaseCharacter::PawnClientRestart()
{
    Super::PawnClientRestart();
    SRDebug::NetLog(this, TEXT("PawnClientRestart (possession/respawn on owning client)"));
    InitializePSAbilitySystem();
    InstallPSMapping();
    ApplyPSPerspective();
    InitializeLocalHUD();
    if (PresentationComponent) PresentationComponent->InitializeForCharacter(this);
}

void APSBaseCharacter::InitializePSAbilitySystem()
{
    UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
    if (!ASC)
    {
        SRDebug::NetLog(this, TEXT("GAS init deferred: no PlayerState/ASC yet"));
        return;
    }
    ASC->InitAbilityActorInfo(GetPlayerState(), this);
    if (HasAuthority() && PSInitializedASC.Get() != ASC)
    {
        if (PSDefaultAttributeEffect)
        {
            FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
            Context.AddSourceObject(this);
            const FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(PSDefaultAttributeEffect, 1.f, Context);
            if (Spec.IsValid()) ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
        }
        for (const TSubclassOf<UGameplayAbility>& Ability : PSStartupAbilities)
        {
            if (Ability && !ASC->FindAbilitySpecFromClass(Ability))
                ASC->GiveAbility(FGameplayAbilitySpec(Ability, 1, INDEX_NONE, this));
        }
        // Hero-specific Q/E/C/X slots from the Hero Data Asset; shared startup
        // abilities above remain independent of whatever the hero defines.
        if (UPSAbilitySystemComponent* HeroASC = Cast<UPSAbilitySystemComponent>(ASC))
            HeroASC->InitializeHeroAbilitySlots(HeroData);
    }
    if (PSInitializedASC.Get() != ASC)
        SRDebug::NetLog(this, FString::Printf(TEXT("GAS actor info initialized: owner=%s avatar=%s (grants are server-authoritative; specs replicate to the owning client)"),
            *GetNameSafe(GetPlayerState()), *GetName()));
    PSInitializedASC = ASC;
}

void APSBaseCharacter::EnsurePSInputObjects()
{
    if (PSMappingContext && PSMoveAction && PSLookAction && PSCrouchAction && PSSprintAction && PSSlideAction && PSJumpAction
        && PSAbilityQAction && PSAbilityEAction && PSAbilityCAction && PSAbilityXAction
        && PSPrimaryFireAction && PSRechargeAction && PSTacticalMapAction && PSPingAction && PSTogglePerspectiveAction) return;
    auto MakeAction = [this](const TCHAR* Name, EInputActionValueType Type)
    {
        UInputAction* Action = NewObject<UInputAction>(this, Name);
        Action->ValueType = Type;
        return Action;
    };
    PSMappingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_PSCharacter_Runtime"));
    PSMoveAction = MakeAction(TEXT("IA_PSMove_Runtime"), EInputActionValueType::Axis2D);
    PSMoveAction->AccumulationBehavior = EInputActionAccumulationBehavior::Cumulative;
    PSLookAction = MakeAction(TEXT("IA_PSLook_Runtime"), EInputActionValueType::Axis2D);
    PSCrouchAction = MakeAction(TEXT("IA_PSCrouch_Runtime"), EInputActionValueType::Boolean);
    PSSprintAction = MakeAction(TEXT("IA_PSSprint_Runtime"), EInputActionValueType::Boolean);
    PSSlideAction = MakeAction(TEXT("IA_PSSlide_Runtime"), EInputActionValueType::Boolean);
    PSJumpAction = MakeAction(TEXT("IA_PSJump_Runtime"), EInputActionValueType::Boolean);
    PSAbilityQAction = MakeAction(TEXT("IA_PSAbilityQ_Runtime"), EInputActionValueType::Boolean);
    PSAbilityEAction = MakeAction(TEXT("IA_PSAbilityE_Runtime"), EInputActionValueType::Boolean);
    PSAbilityCAction = MakeAction(TEXT("IA_PSAbilityC_Runtime"), EInputActionValueType::Boolean);
    PSAbilityXAction = MakeAction(TEXT("IA_PSAbilityX_Runtime"), EInputActionValueType::Boolean);
    PSPrimaryFireAction = MakeAction(TEXT("IA_PSPrimaryFire_Runtime"), EInputActionValueType::Boolean);
    PSRechargeAction = MakeAction(TEXT("IA_PSRecharge_Runtime"), EInputActionValueType::Boolean);
    PSTacticalMapAction = MakeAction(TEXT("IA_PSTacticalMap_Runtime"), EInputActionValueType::Boolean);
    PSPingAction = MakeAction(TEXT("IA_PSPing_Runtime"), EInputActionValueType::Boolean);
    PSTogglePerspectiveAction = MakeAction(TEXT("IA_PSTogglePerspective_Runtime"), EInputActionValueType::Boolean);

    auto MapMove = [this](const FKey& Key, bool bY, bool bNegative)
    {
        FEnhancedActionKeyMapping& Mapping = PSMappingContext->MapKey(PSMoveAction, Key);
        if (bNegative) Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(PSMappingContext));
        if (bY)
        {
            auto* Swizzle = NewObject<UInputModifierSwizzleAxis>(PSMappingContext);
            Swizzle->Order = EInputAxisSwizzle::YXZ;
            Mapping.Modifiers.Add(Swizzle);
        }
    };
    MapMove(EKeys::W, true, false); MapMove(EKeys::S, true, true);
    MapMove(EKeys::A, false, true); MapMove(EKeys::D, false, false);
    PSMappingContext->MapKey(PSLookAction, EKeys::Mouse2D);
    PSMappingContext->MapKey(PSSprintAction, EKeys::LeftShift);
    // One action owns Ctrl, so crouch cannot consume the key before the slide check.
    PSMappingContext->MapKey(PSSlideAction, EKeys::LeftControl);
    PSMappingContext->MapKey(PSJumpAction, EKeys::SpaceBar);
    PSMappingContext->MapKey(PSAbilityQAction, EKeys::Q);
    PSMappingContext->MapKey(PSAbilityEAction, EKeys::E);
    PSMappingContext->MapKey(PSAbilityCAction, EKeys::C);
    PSMappingContext->MapKey(PSAbilityXAction, EKeys::X);
    PSMappingContext->MapKey(PSPrimaryFireAction, EKeys::LeftMouseButton);
    PSMappingContext->MapKey(PSRechargeAction, EKeys::R);
    PSMappingContext->MapKey(PSTacticalMapAction, EKeys::M);
    PSMappingContext->MapKey(PSPingAction, EKeys::MiddleMouseButton);
    PSMappingContext->MapKey(PSTogglePerspectiveAction, EKeys::V);
}

void APSBaseCharacter::InstallPSMapping()
{
    APlayerController* PC = Cast<APlayerController>(Controller);
    if (!PC || !PC->IsLocalController()) return;
    ULocalPlayer* LocalPlayer = PC->GetLocalPlayer();
    UEnhancedInputLocalPlayerSubsystem* Input = LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
    if (!Input) return;
    EnsurePSInputObjects();
    // Remove only the sample's competing bindings, never other gameplay/UI contexts.
    if (const auto* SampleMapping = LoadObject<UInputMappingContext>(nullptr, TEXT("/Game/Input/IMC_Sandbox.IMC_Sandbox")))
        Input->RemoveMappingContext(SampleMapping);
    FModifyContextOptions Options;
    Options.bIgnoreAllPressedKeysUntilRelease = true;
    if (!Input->HasMappingContext(PSMappingContext)) Input->AddMappingContext(PSMappingContext, 100, Options);
    PSInputLocalPlayer = LocalPlayer;
    SRDebug::NetLog(this, TEXT("Input mapping context installed on the local player"));
    if (CVarPSInputDebug.GetValueOnGameThread() > 0)
        UE_LOG(LogProjectSCharacter, Log, TEXT("INPUT READY %s: WASD move, Mouse look, LeftControl crouch/sprint-slide, LeftShift TOGGLE sprint, Space traversal/jump (%d mappings)"), *GetName(), PSMappingContext->GetMappings().Num());
}

void APSBaseCharacter::RemovePSMapping()
{
    if (ULocalPlayer* LocalPlayer = PSInputLocalPlayer.Get())
    {
        // A previously unpossessed pawn can finish EndPlay after its replacement
        // has installed the same asset. It must not clear the replacement's input.
        APlayerController* PC = LocalPlayer->GetPlayerController(GetWorld());
        const auto* Replacement = PC ? Cast<APSBaseCharacter>(PC->GetPawn()) : nullptr;
        if (!Replacement || Replacement == this || Replacement->PSMappingContext != PSMappingContext)
            if (auto* Input = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>()) Input->RemoveMappingContext(PSMappingContext);
    }
    PSInputLocalPlayer.Reset();
}

void APSBaseCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);
    if (HeroGameplayComponent) HeroGameplayComponent->InstallInput(PlayerInputComponent);
    EnsurePSInputObjects();
    UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent);
    if (!ensureMsgf(Input, TEXT("ProjectS requires EnhancedInputComponent"))) return;
    Input->BindAction(PSMoveAction, ETriggerEvent::Triggered, this, &ThisClass::InputPSMove);
    Input->BindAction(PSMoveAction, ETriggerEvent::Completed, this, &ThisClass::InputPSMove);
    Input->BindAction(PSMoveAction, ETriggerEvent::Canceled, this, &ThisClass::InputPSMove);
    Input->BindAction(PSLookAction, ETriggerEvent::Triggered, this, &ThisClass::InputPSLook);
    Input->BindAction(PSLookAction, ETriggerEvent::Completed, this, &ThisClass::InputPSLook);
    Input->BindAction(PSLookAction, ETriggerEvent::Canceled, this, &ThisClass::InputPSLook);
    Input->BindAction(PSCrouchAction, ETriggerEvent::Started, this, &ThisClass::InputPSCrouchStarted);
    Input->BindAction(PSCrouchAction, ETriggerEvent::Completed, this, &ThisClass::InputPSCrouchEnded);
    Input->BindAction(PSCrouchAction, ETriggerEvent::Canceled, this, &ThisClass::InputPSCrouchEnded);
    // Started fires once per press; releasing Shift intentionally has no binding.
    Input->BindAction(PSSprintAction, ETriggerEvent::Started, this, &ThisClass::InputPSSprintToggle);
    Input->BindAction(PSSlideAction, ETriggerEvent::Started, this, &ThisClass::InputPSSlideStarted);
    Input->BindAction(PSSlideAction, ETriggerEvent::Completed, this, &ThisClass::InputPSSlideEnded);
    Input->BindAction(PSSlideAction, ETriggerEvent::Canceled, this, &ThisClass::InputPSSlideEnded);
    Input->BindAction(PSJumpAction, ETriggerEvent::Started, this, &ThisClass::InputPSJumpStarted);
    Input->BindAction(PSJumpAction, ETriggerEvent::Completed, this, &ThisClass::InputPSJumpEnded);
    Input->BindAction(PSJumpAction, ETriggerEvent::Canceled, this, &ThisClass::InputPSJumpEnded);
    // Ability slots: one binding set per slot key, routed by input tag to whichever
    // ability the Hero Data Asset granted. Never hero-specific.
    Input->BindAction(PSAbilityQAction, ETriggerEvent::Started, this, &ThisClass::InputPSAbilityQStarted);
    Input->BindAction(PSAbilityQAction, ETriggerEvent::Completed, this, &ThisClass::InputPSAbilityQEnded);
    Input->BindAction(PSAbilityQAction, ETriggerEvent::Canceled, this, &ThisClass::InputPSAbilityQEnded);
    Input->BindAction(PSAbilityEAction, ETriggerEvent::Started, this, &ThisClass::InputPSAbilityEStarted);
    Input->BindAction(PSAbilityEAction, ETriggerEvent::Completed, this, &ThisClass::InputPSAbilityEEnded);
    Input->BindAction(PSAbilityEAction, ETriggerEvent::Canceled, this, &ThisClass::InputPSAbilityEEnded);
    Input->BindAction(PSAbilityCAction, ETriggerEvent::Started, this, &ThisClass::InputPSAbilityCStarted);
    Input->BindAction(PSAbilityCAction, ETriggerEvent::Completed, this, &ThisClass::InputPSAbilityCEnded);
    Input->BindAction(PSAbilityCAction, ETriggerEvent::Canceled, this, &ThisClass::InputPSAbilityCEnded);
    Input->BindAction(PSAbilityXAction, ETriggerEvent::Started, this, &ThisClass::InputPSAbilityXStarted);
    Input->BindAction(PSAbilityXAction, ETriggerEvent::Completed, this, &ThisClass::InputPSAbilityXEnded);
    Input->BindAction(PSAbilityXAction, ETriggerEvent::Canceled, this, &ThisClass::InputPSAbilityXEnded);
    // Combat, recharge, tactical map and ping inputs.
    Input->BindAction(PSPrimaryFireAction, ETriggerEvent::Started, this, &ThisClass::InputPSPrimaryFireStarted);
    Input->BindAction(PSPrimaryFireAction, ETriggerEvent::Completed, this, &ThisClass::InputPSPrimaryFireEnded);
    Input->BindAction(PSPrimaryFireAction, ETriggerEvent::Canceled, this, &ThisClass::InputPSPrimaryFireEnded);
    Input->BindAction(PSRechargeAction, ETriggerEvent::Started, this, &ThisClass::InputPSRechargeStarted);
    Input->BindAction(PSTacticalMapAction, ETriggerEvent::Started, this, &ThisClass::InputPSTacticalMapStarted);
    Input->BindAction(PSPingAction, ETriggerEvent::Started, this, &ThisClass::InputPSPingStarted);
    Input->BindAction(PSTogglePerspectiveAction, ETriggerEvent::Started, this, &ThisClass::InputPSTogglePerspective);
    InstallPSMapping();
}

void APSBaseCharacter::InputPSMove(const FInputActionValue& Value)
{
    PSLastMoveInput = Value.Get<FVector2D>();
    if (!Controller || IsPSDead() || IsPSTraversing()) return;
    const FRotator Yaw(0.f, Controller->GetControlRotation().Yaw, 0.f);
    AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::X), PSLastMoveInput.Y);
    AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y), PSLastMoveInput.X);
}
void APSBaseCharacter::InputPSLook(const FInputActionValue& Value)
{
    PSLastLookInput = Value.Get<FVector2D>();
    AddControllerYawInput(PSLastLookInput.X * PSMouseSensitivity);
    AddControllerPitchInput(-PSLastLookInput.Y * PSMouseSensitivity);
}
void APSBaseCharacter::InputPSCrouchStarted(const FInputActionValue&)
{
    if (IsPSDead() || IsPSTraversing()) return;
    if (auto* Move = GetPSMovement()) Move->bPSCrouchHeld = true;
    Crouch(); SyncPSSampleInputState();
    if (CVarPSInputDebug.GetValueOnGameThread()) UE_LOG(LogProjectSCharacter, Log, TEXT("LeftControl: crouch pressed"));
}
void APSBaseCharacter::InputPSCrouchEnded(const FInputActionValue&)
{
    if (auto* Move = GetPSMovement())
    {
        Move->bPSCrouchHeld = false;
        if (!Move->bPSSliding) UnCrouch();
    }
    SyncPSSampleInputState();
    if (CVarPSInputDebug.GetValueOnGameThread()) UE_LOG(LogProjectSCharacter, Log, TEXT("LeftControl: crouch released"));
}
void APSBaseCharacter::InputPSSprintToggle(const FInputActionValue&)
{
    if (IsPSDead()) return;
    if (auto* Move = GetPSMovement())
    {
        Move->bPSSprintRequested = !Move->bPSSprintRequested;
        SyncPSSampleInputState();
        if (CVarPSInputDebug.GetValueOnGameThread()) UE_LOG(LogProjectSCharacter, Log, TEXT("LeftShift: sprint toggled %s"), Move->bPSSprintRequested ? TEXT("ON") : TEXT("OFF"));
    }
}
void APSBaseCharacter::InputPSSlideStarted(const FInputActionValue&)
{
    if (IsPSDead() || IsPSTraversing()) return;
    if (auto* Move = GetPSMovement())
    {
        // CMC decides between slide and crouch on both client and server.
        // Do not call Crouch() here: that would stop sprinting before the gate runs.
        Move->bPSSlideHeld = true;
        Move->bPSCrouchHeld = true;
        if (CVarPSInputDebug.GetValueOnGameThread())
            UE_LOG(LogProjectSCharacter, Log, TEXT("LeftControl: %s requested, sprint=%d speed=%.1f"),
                Move->CanStartPSSlide() ? TEXT("slide") : TEXT("crouch"), Move->bPSSprintRequested, GetVelocity().Size2D());
    }
}
void APSBaseCharacter::InputPSSlideEnded(const FInputActionValue& Value)
{
    if (auto* Move = GetPSMovement()) Move->bPSSlideHeld = false;
    InputPSCrouchEnded(Value);
}
void APSBaseCharacter::InputPSJumpStarted(const FInputActionValue&)
{
    if (IsPSDead() || IsPSSliding() || IsPSTraversing()) return;
    StopJumping();
    if (!TryPSTraversal()) Jump();
}
void APSBaseCharacter::InputPSJumpEnded(const FInputActionValue&) { StopJumping(); }

namespace
{
// Slot input routing: every ability key forwards the same input tag the ASC used to
// grant the slot's ability, so no per-hero handler code exists.
void ForwardAbilityInputToASC(const APSBaseCharacter* Character, const FGameplayTag& InputTag, const bool bPressed)
{
    UPSAbilitySystemComponent* ASC = Character ? Cast<UPSAbilitySystemComponent>(Character->GetAbilitySystemComponent()) : nullptr;
    if (!ASC) return;
    if (bPressed) ASC->NotifyAbilityInputPressed(InputTag);
    else ASC->NotifyAbilityInputReleased(InputTag);
}
}

void APSBaseCharacter::InputPSAbilityQStarted(const FInputActionValue&) { ForwardAbilityInputToASC(this, SRGameplayTags::Tag_Input_Ability_Q, true); }
void APSBaseCharacter::InputPSAbilityQEnded(const FInputActionValue&) { ForwardAbilityInputToASC(this, SRGameplayTags::Tag_Input_Ability_Q, false); }
void APSBaseCharacter::InputPSAbilityEStarted(const FInputActionValue&) { ForwardAbilityInputToASC(this, SRGameplayTags::Tag_Input_Ability_E, true); }
void APSBaseCharacter::InputPSAbilityEEnded(const FInputActionValue&) { ForwardAbilityInputToASC(this, SRGameplayTags::Tag_Input_Ability_E, false); }
void APSBaseCharacter::InputPSAbilityCStarted(const FInputActionValue&) { ForwardAbilityInputToASC(this, SRGameplayTags::Tag_Input_Ability_C, true); }
void APSBaseCharacter::InputPSAbilityCEnded(const FInputActionValue&) { ForwardAbilityInputToASC(this, SRGameplayTags::Tag_Input_Ability_C, false); }
void APSBaseCharacter::InputPSAbilityXStarted(const FInputActionValue&) { ForwardAbilityInputToASC(this, SRGameplayTags::Tag_Input_Ability_X, true); }
void APSBaseCharacter::InputPSAbilityXEnded(const FInputActionValue&) { ForwardAbilityInputToASC(this, SRGameplayTags::Tag_Input_Ability_X, false); }

void APSBaseCharacter::InputPSPrimaryFireStarted(const FInputActionValue&)
{
    if (bTacticalMapOpen) { SRDebug::Log(SRDebug::ECategory::Input, TEXT("Client"), TEXT("LMB ignored: tactical map open")); return; }
    if (CombatComponent) CombatComponent->RequestStartPrimaryFire();
}
void APSBaseCharacter::InputPSPrimaryFireEnded(const FInputActionValue&)
{
    if (CombatComponent) CombatComponent->RequestStopPrimaryFire();
}
void APSBaseCharacter::InputPSRechargeStarted(const FInputActionValue&)
{
    if (CombatComponent) CombatComponent->RequestPrimaryRecharge();
}
void APSBaseCharacter::InputPSTacticalMapStarted(const FInputActionValue&)
{
    // The HUD widget listens to this state through the character (created lazily).
    SetTacticalMapOpen(!bTacticalMapOpen);
    OnTacticalMapToggled.Broadcast(bTacticalMapOpen);
}
void APSBaseCharacter::InputPSTogglePerspective(const FInputActionValue&)
{
    if (!IsLocallyControlled() || IsPSDead()) return;
    SetPSPerspective(!bPSFirstPersonView);
    SRDebug::NetLog(this, FString::Printf(TEXT("Perspective toggled locally -> %s"), bPSFirstPersonView ? TEXT("first-person") : TEXT("third-person")));
    if (!HasAuthority()) ServerSetPSPerspective(bPSFirstPersonView);
}

void APSBaseCharacter::ServerSetPSPerspective_Implementation(bool bFirstPerson)
{
    // Only the owning client's pawn may invoke this RPC; Unreal enforces ownership.
    SRDebug::NetLog(this, FString::Printf(TEXT("Server received perspective RPC -> %s"), bFirstPerson ? TEXT("first-person") : TEXT("third-person")));
    SetPSPerspective(bFirstPerson);
}

void APSBaseCharacter::SetPSPerspective(bool bFirstPerson)
{
    bPSFirstPersonView = bFirstPerson;
    ApplyPSPerspective();
}

void APSBaseCharacter::ApplyPSPerspective()
{
    if (!PSFirstPersonCamera) return;
    // Find the Blueprint third-person camera if BeginPlay initialized too early.
    if (!PSThirdPersonCamera)
    {
        TArray<UCameraComponent*> Cameras;
        GetComponents(Cameras);
        for (UCameraComponent* Camera : Cameras)
        {
            if (Camera && Camera != PSFirstPersonCamera && Camera != PSFallbackCamera && Camera->IsActive())
            { PSThirdPersonCamera = Camera; break; }
        }
        if (!PSThirdPersonCamera) PSThirdPersonCamera = PSFallbackCamera;
    }
    if (PSThirdPersonCamera) PSThirdPersonCamera->SetActive(!bPSFirstPersonView);
    PSFirstPersonCamera->SetActive(bPSFirstPersonView);
    // Hide third-person geometry from its owner in first-person, not from teammates.
    if (GetMesh()) GetMesh()->SetOwnerNoSee(IsLocallyControlled() && bPSFirstPersonView);
    if (PSFirstPersonArms)
    {
        PSFirstPersonArms->SetVisibility(IsLocallyControlled() && bPSFirstPersonView);
        if (IsLocallyControlled() && bPSFirstPersonView) SetupPSFirstPersonArmsAnimation();
    }
}

void APSBaseCharacter::SetupPSFirstPersonArmsAnimation()
{
    if (!PSFirstPersonArms || !PSFirstPersonArms->GetSkeletalMeshAsset()) return;
    if (PSFPAnimInstanceClass)
    {
        if (PSFirstPersonArms->GetAnimClass() != PSFPAnimInstanceClass)
        {
            PSFirstPersonArms->SetAnimInstanceClass(PSFPAnimInstanceClass);
        }
    }
    else if (PSFPIdleSequence && !GetWorldTimerManager().IsTimerActive(PSFPFireFallbackHandle))
    {
        PSFirstPersonArms->PlayAnimation(PSFPIdleSequence, true);
    }
}

void APSBaseCharacter::RestorePSFirstPersonIdle()
{
    if (PSFirstPersonArms && !PSFPAnimInstanceClass && PSFPIdleSequence)
        PSFirstPersonArms->PlayAnimation(PSFPIdleSequence, true);
}

void APSBaseCharacter::PlayPSFirstPersonFireAnimation()
{
    if (!IsLocallyControlled() || !bPSFirstPersonView || !PSFirstPersonArms ||
        !PSFirstPersonArms->GetSkeletalMeshAsset() || !PSFPFireSequence) return;

    if (PSFPAnimInstanceClass)
    {
        UAnimInstance* Anim = PSFirstPersonArms->GetAnimInstance();
        if (Anim)
        {
            // Requires matching Slot node in the first-person AnimBP anim graph.
            Anim->PlaySlotAnimationAsDynamicMontage(PSFPFireSequence, PSFPFireSlot,
                PSFPFireBlendIn, PSFPFireBlendOut, 1.f, 1, 0.f);
            return;
        }
    }

    // Basic fallback for projects that have not yet created an FP Animation Blueprint.
    // For seamless blends, assign PSFPAnimInstanceClass and an idle -> DefaultSlot graph.
    PSFirstPersonArms->PlayAnimation(PSFPFireSequence, false);
    GetWorldTimerManager().ClearTimer(PSFPFireFallbackHandle);
    GetWorldTimerManager().SetTimer(PSFPFireFallbackHandle, this,
        &ThisClass::RestorePSFirstPersonIdle,
        FMath::Max(0.05f, PSFPFireSequence->GetPlayLength()), false);
}

void APSBaseCharacter::InputPSPingStarted(const FInputActionValue&)
{
    if (bTacticalMapOpen || !IsLocallyControlled()) return;
    APSPlayerState* State = GetPlayerState<APSPlayerState>();
    if (!State) return;

    // MMB marks a location; Alt+MMB marks danger. Sprint/slide bindings are unaffected.
    const APlayerController* PC = Cast<APlayerController>(GetController());
    const bool bDanger = PC && (PC->IsInputKeyDown(EKeys::LeftAlt) || PC->IsInputKeyDown(EKeys::RightAlt));
    State->RequestPingFromAim(bDanger ? SRGameplayTags::Tag_SR_Ping_Danger : SRGameplayTags::Tag_SR_Ping_Location);
}

void APSBaseCharacter::SyncPSSampleInputState()
{
    const auto* Move = GetPSMovement();
    if (!Move || !IsPlayerControlled()) return;
    // The sample uses a user-defined struct with GUID-suffixed member names.
    // Retaining it preserves the sample interfaces, choosers and ABP unchanged.
    FStructProperty* State = FindFProperty<FStructProperty>(GetClass(), TEXT("CharacterInputState"));
    if (!State) return;
    void* StateMemory = State->ContainerPtrToValuePtr<void>(this);
    const bool bSprint = GetLocalRole() == ROLE_SimulatedProxy ? bPSReplicatedSprint : Move->bPSSprintRequested;
    const bool bCrouch = GetLocalRole() == ROLE_SimulatedProxy ? bIsCrouched : (Move->bPSCrouchHeld || Move->bPSSliding);
    if (HasAuthority()) bPSReplicatedSprint = Move->bPSSprintRequested;
    for (TFieldIterator<FBoolProperty> It(State->Struct); It; ++It)
    {
        const FString Name = It->GetName();
        if (Name.StartsWith(TEXT("WantsToSprint"))) It->SetPropertyValue_InContainer(StateMemory, bSprint && !bCrouch);
        else if (Name.StartsWith(TEXT("WantsToCrouch"))) It->SetPropertyValue_InContainer(StateMemory, bCrouch);
    }
}

void APSBaseCharacter::SetPSSlidePresentation(bool bSliding)
{
    if (GetLocalRole() == ROLE_SimulatedProxy) return;
    if (bPSReplicatedSliding == bSliding) return;
    bPSReplicatedSliding = bSliding;
    UpdatePSSlideAnimation();
    if (HasAuthority()) ForceNetUpdate();
}
void APSBaseCharacter::OnRep_PSSliding() { UpdatePSSlideAnimation(); }
void APSBaseCharacter::UpdatePSSlideAnimation()
{
    if (bPSPresentedSlide == bPSReplicatedSliding) return;
    bPSPresentedSlide = bPSReplicatedSliding;
    if (UAnimInstance* Anim = GetMesh()->GetAnimInstance())
    {
        if (bPSPresentedSlide && PSSlideAnimation)
        {
            PSActiveSlideMontage = Anim->PlaySlotAnimationAsDynamicMontage(PSSlideAnimation, TEXT("DefaultSlot"), 0.15f, 0.2f, 1.f, 10);
            if (FAnimMontageInstance* Instance = Anim->GetActiveInstanceForMontage(PSActiveSlideMontage))
                Instance->PushDisableRootMotion();
        }
        else if (PSActiveSlideMontage)
        {
            Anim->Montage_Stop(0.2f, PSActiveSlideMontage);
            PSActiveSlideMontage = nullptr;
        }
    }
    OnPSSlideChanged.Broadcast(bPSPresentedSlide);
}

void APSBaseCharacter::Tick(float DeltaSeconds)
{
    SyncPSSampleInputState();
    Super::Tick(DeltaSeconds);
    if (IsPSDead() && !bPSDeathHandled)
    {
        bPSDeathHandled = true;
        if (auto* Movement = GetPSMovement()) Movement->bPSSprintRequested = false;
        if (HasAuthority())
        {
            GetCharacterMovement()->StopMovementImmediately();
            GetCharacterMovement()->DisableMovement();
            if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent()) ASC->CancelAllAbilities();
        }
    }
    if (IsLocallyControlled() && CVarPSInputDebug.GetValueOnGameThread() >= 2 && GetWorld()->GetTimeSeconds() >= PSNextDebugTime)
    {
        PSNextDebugTime = GetWorld()->GetTimeSeconds() + 0.5;
        const FString Debug = FString::Printf(TEXT("ProjectS INPUT move=(%.2f,%.2f) look=(%.2f,%.2f) speed=%.0f sprint=%d crouch=%d slide=%d traversal=%d GAS=%s"),
            PSLastMoveInput.X, PSLastMoveInput.Y, PSLastLookInput.X, PSLastLookInput.Y, GetVelocity().Size2D(),
            IsPSSprinting(), bIsCrouched, IsPSSliding(), IsPSTraversing(), GetAbilitySystemComponent() ? TEXT("ready") : TEXT("missing"));
        UE_LOG(LogProjectSCharacter, Log, TEXT("%s"), *Debug);
        if (GEngine) GEngine->AddOnScreenDebugMessage(static_cast<uint64>(GetUniqueID()), 0.6f, FColor::Green, Debug);
    }
}

void APSBaseCharacter::UnPossessed()
{
    if (HeroGameplayComponent) HeroGameplayComponent->RemoveInput();
    RemovePSMapping();
    if (auto* Move = GetPSMovement())
    { Move->bPSSprintRequested = Move->bPSCrouchHeld = Move->bPSSlideHeld = false; }
    if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent(); ASC && ASC->GetAvatarActor() == this)
    {
        // The PlayerState/ASC survives its pawn. End avatar-bound tasks/effects
        // before clearing the actor info they need for cleanup.
        if (HasAuthority())
        {
            ASC->CancelAllAbilities();
            if (HeroGameplayComponent) HeroGameplayComponent->ClearAvatarEffects(ASC);
        }
        ASC->ClearActorInfo();
    }
    Super::UnPossessed();
}
void APSBaseCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    GetWorldTimerManager().ClearTimer(PSFPFireFallbackHandle);
    RemovePSMapping();
    if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent(); HasAuthority() && ASC && ASC->GetAvatarActor() == this)
    {
        ASC->CancelAllAbilities();
        if (HeroGameplayComponent) HeroGameplayComponent->ClearAvatarEffects(ASC);
        ASC->ClearActorInfo();
    }
    Super::EndPlay(EndPlayReason);
}
void APSBaseCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(APSBaseCharacter, WeaponData);
    DOREPLIFETIME(APSBaseCharacter, bPSReplicatedSliding);
    DOREPLIFETIME_CONDITION(APSBaseCharacter, bPSReplicatedSprint, COND_SimulatedOnly);
}
