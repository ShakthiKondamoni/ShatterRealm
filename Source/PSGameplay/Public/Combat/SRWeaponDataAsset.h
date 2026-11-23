#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/EngineTypes.h"
#include "SRWeaponDataAsset.generated.h"

class UAnimMontage;
class UNiagaraSystem;
class USoundBase;
class USkeletalMesh;

UENUM(BlueprintType)
enum class ESRFireMode : uint8
{
    /** Fires continuously while the input is held. */
    Automatic,
    /** Fires once per press. */
    Single
};

/**
 * Primary attack configuration for one hero's signature weapon. The shared
 * Combat Component reads everything from here — new weapon configurations
 * never require component changes. Placeholder visuals (engine meshes /
 * missing Niagara) are acceptable: gameplay values are authoritative.
 */
UCLASS(BlueprintType)
class PSGAMEPLAY_API USRWeaponDataAsset : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    USRWeaponDataAsset()
    {
        RecoilPattern = {
            FVector2D(0.65f, 0.0f), FVector2D(0.8f, 0.12f),
            FVector2D(0.95f, -0.16f), FVector2D(1.05f, 0.24f),
            FVector2D(1.15f, -0.26f), FVector2D(1.1f, 0.19f)
        };
    }

    /** Weapon name shown by the HUD (e.g. "Spark Shot"). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRWeapon", meta=(DisplayName="Weapon Name"))
    FText WeaponDisplayName = NSLOCTEXT("SR", "DefaultWeaponName", "Elemental Weapon");
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRWeapon")
    FText SignatureWeaponName;

    // ------------------------------------------------------------------
    // Firing
    // ------------------------------------------------------------------

    /** Fire mode: automatic fires while held, single fires once per press. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRWeapon|Firing")
    ESRFireMode FireMode = ESRFireMode::Automatic;

    /** Base damage per shot, applied through GAS (shield-first). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRWeapon|Firing", meta=(ClampMin="0"))
    float Damage = 22.f;

    /** Damage multiplier when an authoritative hit reports the head bone. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRWeapon|Damage", meta=(ClampMin="1.0"))
    float HeadshotMultiplier = 1.75f;

    /** Default multiplier for chest, abdomen, spine and pelvis hits. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRWeapon|Damage", meta=(ClampMin="0"))
    float TorsoDamageMultiplier = 1.f;

    /** Damage remains at full strength up to this distance (centimeters). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRWeapon|Damage", meta=(ClampMin="0", Units="cm"))
    float FullDamageRange = 2000.f;

    /** At this distance damage reaches its minimum multiplier; must be > FullDamageRange. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRWeapon|Damage", meta=(ClampMin="0", Units="cm"))
    float MinDamageRange = 9000.f;

    /** Minimum range multiplier at/beyond MinDamageRange (0.65 = 65% of base damage). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRWeapon|Damage", meta=(ClampMin="0", ClampMax="1"))
    float MinDamageMultiplier = 0.65f;

    /** Damage multiplier for arm and leg hits; body hits use Damage unchanged. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRWeapon|Damage", meta=(ClampMin="0.0"))
    float LimbDamageMultiplier = 0.85f;

    /** Pelvis/hip multiplier. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRWeapon|Damage", meta=(ClampMin="0"))
    float PelvisDamageMultiplier = 1.0f;

    /** Arm multiplier (default mirrors legacy limb multiplier). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRWeapon|Damage", meta=(ClampMin="0"))
    float ArmDamageMultiplier = 0.85f;

    /** Leg multiplier (default mirrors legacy limb multiplier). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRWeapon|Damage", meta=(ClampMin="0"))
    float LegDamageMultiplier = 0.85f;

    /** Pattern applied locally to the owning player's view per predicted shot.
     *  X = upward kick degrees, Y = yaw degrees. Advances once per shot; when the
     *  burst continues past the end the LAST entry repeats (no wrap-around drift)
     *  until the reset delay restarts the pattern. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRWeapon|Recoil")
    TArray<FVector2D> RecoilPattern;

    /** Fallback single-shot kick when RecoilPattern is empty: upward degrees. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRWeapon|Recoil", meta=(ClampMin="0"))
    float RecoilPitch = 1.2f;

    /** Fallback single-shot kick when RecoilPattern is empty: yaw degrees (randomized +- this value). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRWeapon|Recoil", meta=(ClampMin="0"))
    float RecoilYaw = 0.35f;

    /** Pattern starts over after this much idle time between shots. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRWeapon|Recoil", meta=(ClampMin="0.05"))
    float RecoilResetDelay = 0.25f;

    /** Seconds for the recovering fraction of a kick to settle back. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRWeapon|Recoil", meta=(ClampMin="0.01"))
    float RecoilRecoveryDuration = 0.18f;

    /** Fraction of each kick that gradually recovers; 0 = permanent climb, 1 = full spring-back. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRWeapon|Recoil", meta=(ClampMin="0", ClampMax="1"))
    float RecoilRecoveryFraction = 0.65f;

    /** Recoil scaling while aiming down sights (ADS is not implemented yet; hook kept configurable). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRWeapon|Recoil", meta=(ClampMin="0", ClampMax="2"))
    float ADSRecoilMultiplier = 0.8f;

    /** Recoil scaling for first-person shots on the owning client. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRWeapon|Recoil", meta=(ClampMin="0", ClampMax="2"))
    float FPRecoilMultiplier = 1.0f;

    /** Recoil scaling for third-person shots on the owning client. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRWeapon|Recoil", meta=(ClampMin="0", ClampMax="2"))
    float TPRecoilMultiplier = 0.75f;

    /** Maximum firing range in world units. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRWeapon|Firing", meta=(ClampMin="0"))
    float Range = 12000.f;

    /** Base shots per second. Server-authoritative interval = 1 / EffectiveFireRate. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRWeapon|Firing", meta=(ClampMin="0.1"))
    float FireRate = 7.f;

    /** Trace channel used by aiming and muzzle validation. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRWeapon|Firing")
    TEnumAsByte<ECollisionChannel> TraceChannel = ECollisionChannel::ECC_Camera;

    /** Random spread half-angle in degrees (0 = perfect accuracy). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRWeapon|Firing", meta=(ClampMin="0", ClampMax="45"))
    float SpreadDegrees = 0.f;

    /** Socket (or bone) on the character mesh that shots originate from. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRWeapon|Firing")
    FName FireSocketName = TEXT("hand_r");

    // ------------------------------------------------------------------
    // Server aim validation (client-submitted aim is never trusted blindly)
    // ------------------------------------------------------------------

    /** Maximum angle between a client-submitted aim direction and the server view
     *  before the submission is rejected (shot falls back to the server view). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRWeapon|Validation", meta=(ClampMin="1.0", ClampMax="45.0"))
    float MaxAimDeviationDegrees = 15.f;

    /** Maximum distance between a client-submitted camera origin and the server
     *  pawn view position before the submission is rejected. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRWeapon|Validation", meta=(ClampMin="50", ClampMax="1000"))
    float CameraOriginTolerance = 450.f;

    // ------------------------------------------------------------------
    // Charges / recharge
    // ------------------------------------------------------------------

    /** Maximum primary charges. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRWeapon|Charges", meta=(ClampMin="1"))
    int32 MaxCharges = 30;

    /** Full recharge duration in seconds (base, before multipliers). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRWeapon|Charges", meta=(ClampMin="0.1"))
    float RechargeDuration = 1.8f;

    // ------------------------------------------------------------------
    // Presentation (all optional placeholders)
    // ------------------------------------------------------------------

    /** Firing montage played on the character. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRWeapon|Presentation")
    TObjectPtr<UAnimMontage> FiringMontage;

    /** Recharge montage. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRWeapon|Presentation")
    TObjectPtr<UAnimMontage> RechargeMontage;

    /** Muzzle flash Niagara system. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRWeapon|Presentation")
    TObjectPtr<UNiagaraSystem> MuzzleVFX;

    /** Tracer Niagara system (spawned along the shot path). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRWeapon|Presentation")
    TObjectPtr<UNiagaraSystem> TracerVFX;

    /** Impact Niagara system. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRWeapon|Presentation")
    TObjectPtr<UNiagaraSystem> ImpactVFX;

    /** Firing sound. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRWeapon|Presentation")
    TObjectPtr<USoundBase> FiringSound;

    /** Recharge sound. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRWeapon|Presentation")
    TObjectPtr<USoundBase> RechargeSound;
};
