#include "Combat/SRPresentationActors.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"

namespace
{
UStaticMesh* LoadShapeMesh(bool bSphere)
{
    return LoadObject<UStaticMesh>(nullptr, bSphere
        ? TEXT("/Engine/BasicShapes/Sphere.Sphere")
        : TEXT("/Engine/BasicShapes/Cube.Cube"));
}

// Engine primitives are 100cm; configure* inputs use world radii.
constexpr float ShapeBaseSize = 100.f;
}

ASRTransientVFX::ASRTransientVFX()
{
    PrimaryActorTick.bCanEverTick = true;
    SetReplicates(false);
    InitialLifeSpan = 0.f; // managed by Tick

    USceneComponent* SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    SetRootComponent(SceneRoot);

    Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
    Mesh->SetupAttachment(SceneRoot);
    Mesh->SetStaticMesh(LoadShapeMesh(false));
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
    Mesh->SetCastShadow(false);
    Mesh->bReceivesDecals = false;

    Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
    Light->SetupAttachment(SceneRoot);
    Light->SetCastShadows(false);
}

void ASRTransientVFX::ApplyColor(const FLinearColor& Color)
{
    // BasicShapeMaterial exposes a "Color" vector parameter; when a material
    // without the parameter is applied this is silently ignored.
    Mesh->SetVectorParameterValueOnMaterials(TEXT("Color"), FVector(Color));
    Light->SetLightColor(Color);
}

void ASRTransientVFX::ConfigureBeam(const FVector& Start, const FVector& End, float Thickness, const FLinearColor& Color, float InLifetime, bool bUseLight)
{
    const FVector Segment = End - Start;
    const float Length = FMath::Max(Segment.Size(), 1.f);
    SetActorLocationAndRotation((Start + End) * 0.5f, Segment.Rotation());
    // Beam thickness scales from full to zero so the beam "dies" as it expires.
    StartScale = FVector(Length, Thickness, Thickness) / ShapeBaseSize;
    EndScale = FVector(Length, Thickness * 0.2f, Thickness * 0.2f) / ShapeBaseSize;
    Elapsed = 0.f;
    Lifetime = FMath::Max(InLifetime, 0.01f);
    ApplyColor(Color);
    StartLightIntensity = bUseLight ? 8.f : 0.f;
    EndLightIntensity = 0.f;
    Light->SetIntensity(StartLightIntensity);
    Light->SetAttenuationRadius(FMath::Min(Length * 0.5f, 800.f));
    Mesh->SetStaticMesh(LoadShapeMesh(false));
}

void ASRTransientVFX::ConfigureBurst(const FVector& Location, float StartRadius, float EndRadius, const FLinearColor& Color, float InLifetime, bool bUseLight)
{
    SetActorLocation(Location);
    SetActorRotation(FRotator::ZeroRotator);
    StartScale = FVector(StartRadius) / ShapeBaseSize;
    EndScale = FVector(EndRadius) / ShapeBaseSize;
    Elapsed = 0.f;
    Lifetime = FMath::Max(InLifetime, 0.01f);
    ApplyColor(Color);
    StartLightIntensity = bUseLight ? 30.f : 0.f;
    EndLightIntensity = 0.f;
    Light->SetIntensity(StartLightIntensity);
    Light->SetAttenuationRadius(FMath::Max(EndRadius * 2.f, 300.f));
    Mesh->SetStaticMesh(LoadShapeMesh(true));
}

void ASRTransientVFX::ConfigureRing(const FVector& Location, float EndRadius, const FLinearColor& Color, float InLifetime, bool bUseLight)
{
    SetActorLocation(Location);
    SetActorRotation(FRotator::ZeroRotator);
    StartScale = FVector(0.3f, 0.3f, 0.02f);
    EndScale = FVector(EndRadius * 2.f, EndRadius * 2.f, 0.05f) / ShapeBaseSize;
    Elapsed = 0.f;
    Lifetime = FMath::Max(InLifetime, 0.01f);
    ApplyColor(Color);
    StartLightIntensity = bUseLight ? 25.f : 0.f;
    EndLightIntensity = 0.f;
    Light->SetIntensity(StartLightIntensity);
    Light->SetAttenuationRadius(EndRadius * 2.f);
    Mesh->SetStaticMesh(LoadShapeMesh(true));
}

void ASRTransientVFX::Tick(float DeltaSeconds)
{
    Elapsed += DeltaSeconds;
    if (Elapsed >= Lifetime)
    {
        Destroy();
        return;
    }

    const float Alpha = FMath::Clamp(Elapsed / Lifetime, 0.f, 1.f);
    SetActorScale3D(FMath::Lerp(StartScale, EndScale, Alpha));
    if (StartLightIntensity > 0.f)
    {
        Light->SetIntensity(FMath::Lerp(StartLightIntensity, EndLightIntensity, Alpha));
    }
}

ASRFloatingDamageText::ASRFloatingDamageText()
{
    PrimaryActorTick.bCanEverTick = true;
    SetReplicates(false);
    SetReplicateMovement(false);

    USceneComponent* SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    SetRootComponent(SceneRoot);

    TextRender = CreateDefaultSubobject<UTextRenderComponent>(TEXT("DamageText"));
    TextRender->SetupAttachment(SceneRoot);
    TextRender->SetHorizontalAlignment(EHTA_Center);
    TextRender->SetVerticalAlignment(EVRTA_TextCenter);
    TextRender->SetWorldSize(22.f);
}

void ASRFloatingDamageText::ConfigureDamage(const FVector& Location, float Amount, bool bShieldHit, bool bLandingBurst)
{
    SetActorLocation(Location);
    // Landing bursts read bigger; shield and health hits are color-differentiated.
    TextRender->SetWorldSize(bLandingBurst ? 30.f : 22.f);
    BaseColor = bLandingBurst ? FColor(255, 240, 160)
        : (bShieldHit ? FColor(120, 220, 255) : FColor(255, 170, 80));
    TextRender->SetText(FText::FromString(FString::Printf(TEXT("%.0f"), Amount)));
    TextRender->SetTextRenderColor(BaseColor);
    Elapsed = 0.f;
}

void ASRFloatingDamageText::Tick(float DeltaSeconds)
{
    Elapsed += DeltaSeconds;
    if (Elapsed >= Lifetime)
    {
        Destroy();
        return;
    }

    // Rise and face the local camera so the number is readable from any angle.
    AddActorWorldOffset(RiseVelocity * DeltaSeconds);
    if (const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
    {
        const FVector CameraLocation = PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraLocation() : PC->GetFocalLocation();
        const FVector Delta = CameraLocation - GetActorLocation();
        if (Delta.SizeSquared2D() > KINDA_SMALL_NUMBER)
        {
            SetActorRotation(Delta.Rotation());
        }
    }

    const float Alpha = FMath::Clamp(Elapsed / Lifetime, 0.f, 1.f);
    TextRender->SetTextRenderColor(FColor(BaseColor.R, BaseColor.G, BaseColor.B, static_cast<uint8>(255.f * (1.f - Alpha))));
}
