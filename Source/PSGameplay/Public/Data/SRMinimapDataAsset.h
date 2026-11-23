#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "SRMinimapDataAsset.generated.h"

class UTexture2D;

/**
 * Minimap configuration for one level: world bounds used by the shared world-
 * to-map coordinate conversion, plus an optional top-down map texture. The
 * combat test map builder computes the bounds from the geometry it creates.
 */
UCLASS(BlueprintType)
class PSGAMEPLAY_API USRMinimapDataAsset : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    /** World-space playable bounds. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|Minimap") float MinWorldX = -4000.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|Minimap") float MaxWorldX = 4000.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|Minimap") float MinWorldY = -3000.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|Minimap") float MaxWorldY = 3000.f;

    /** Optional top-down texture; without one the widget draws an honest grid placeholder. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|Minimap")
    TObjectPtr<UTexture2D> MapTexture;

    /** World -> normalized [0,1] map coordinates (Y flipped for UI space). */
    UFUNCTION(BlueprintPure, Category="SR|Minimap")
    FVector2D WorldToNormalized(const FVector& WorldPosition) const
    {
        const float Width = FMath::Max(1.f, MaxWorldX - MinWorldX);
        const float Height = FMath::Max(1.f, MaxWorldY - MinWorldY);
        return FVector2D(
            FMath::Clamp((WorldPosition.X - MinWorldX) / Width, 0.f, 1.f),
            FMath::Clamp(1.f - (WorldPosition.Y - MinWorldY) / Height, 0.f, 1.f));
    }

    /** Normalized map coordinates -> world X/Y (Z resolved by the caller via floor trace). */
    UFUNCTION(BlueprintPure, Category="SR|Minimap")
    FVector NormalizedToWorld(const FVector2D& Normalized) const
    {
        const float Width = FMath::Max(1.f, MaxWorldX - MinWorldX);
        const float Height = FMath::Max(1.f, MaxWorldY - MinWorldY);
        return FVector(MinWorldX + Normalized.X * Width, MinWorldY + (1.f - Normalized.Y) * Height, 0.f);
    }
};
