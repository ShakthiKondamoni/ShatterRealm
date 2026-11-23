#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SRPresentationActors.generated.h"

class UPointLightComponent;
class UStaticMeshComponent;
class UTextRenderComponent;

/**
 * Ephemeral colored effect built from engine basic shapes (no authored VFX
 * required): a stretched beam between two points, an expanding burst, or a
 * flat landing ring. Every combat presentation event falls back to one of
 * these shapes while dedicated Niagara assets are still BLOCKED. Lifespan and
 * growth are driven by Configure*; the actor destroys itself when done.
 */
UCLASS()
class PSGAMEPLAY_API ASRTransientVFX : public AActor
{
    GENERATED_BODY()

public:
    ASRTransientVFX();

    /** Glowing bar between two world points (tracers, Stormpiercer beams). */
    void ConfigureBeam(const FVector& Start, const FVector& End, float Thickness, const FLinearColor& Color, float Lifetime, bool bUseLight);

    /** Expanding blob at a location (muzzle flash, impacts, projectile splash). */
    void ConfigureBurst(const FVector& Location, float StartRadius, float EndRadius, const FLinearColor& Color, float Lifetime, bool bUseLight);

    /** Flat expanding ring on the floor (Static Leap landing). */
    void ConfigureRing(const FVector& Location, float EndRadius, const FLinearColor& Color, float Lifetime, bool bUseLight);

protected:
    virtual void Tick(float DeltaSeconds) override;

private:
    void ApplyColor(const FLinearColor& Color);

    UPROPERTY(VisibleAnywhere, Category="SR|VFX")
    TObjectPtr<UStaticMeshComponent> Mesh;

    UPROPERTY(VisibleAnywhere, Category="SR|VFX")
    TObjectPtr<UPointLightComponent> Light;

    float Elapsed = 0.f;
    float Lifetime = 0.25f;
    FVector StartScale = FVector::OneVector;
    FVector EndScale = FVector::OneVector;
    float StartLightIntensity = 0.f;
    float EndLightIntensity = 0.f;
};

/**
 * World-space floating damage number: rises, faces the camera and fades.
 * Shield hits render cyan, health hits orange, ability bursts yellow-white.
 * Uses the default engine font — no authored assets.
 */
UCLASS()
class PSGAMEPLAY_API ASRFloatingDamageText : public AActor
{
    GENERATED_BODY()

public:
    ASRFloatingDamageText();

    void ConfigureDamage(const FVector& Location, float Amount, bool bShieldHit, bool bLandingBurst);

protected:
    virtual void Tick(float DeltaSeconds) override;

private:
    UPROPERTY(VisibleAnywhere, Category="SR|VFX")
    TObjectPtr<UTextRenderComponent> TextRender;

    float Elapsed = 0.f;
    float Lifetime = 0.9f;
    FVector RiseVelocity = FVector(0.f, 0.f, 70.f);
    FColor BaseColor = FColor::White;
};
