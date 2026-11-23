#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "SRPingTypes.generated.h"

/**
 * One replicated team marker: a location/danger ping or a last-known-position
 * enemy reveal. Owned by a PlayerState and replicated to clients; consumers
 * filter by team so private markers never reach unauthorized teams' HUDs.
 */
USTRUCT(BlueprintType)
struct FSRPingInfo
{
    GENERATED_BODY()

    /** Monotonic per-PlayerState id. */
    UPROPERTY(BlueprintReadOnly, Category="SR|Ping")
    int32 PingId = 0;

    /** Team the marker belongs to (mirrors the owner's TeamId). */
    UPROPERTY(BlueprintReadOnly, Category="SR|Ping")
    int32 TeamId = 0;

    /** Confirmed world position (server-validated). */
    UPROPERTY(BlueprintReadOnly, Category="SR|Ping")
    FVector Location = FVector::ZeroVector;

    /** SR.Ping.Location / SR.Ping.Danger / SR.Ping.Reveal. */
    UPROPERTY(BlueprintReadOnly, Category="SR|Ping")
    FGameplayTag Category;

    /** Seconds until the server removes the marker. */
    UPROPERTY(BlueprintReadOnly, Category="SR|Ping")
    float ExpirySeconds = 8.f;
};
