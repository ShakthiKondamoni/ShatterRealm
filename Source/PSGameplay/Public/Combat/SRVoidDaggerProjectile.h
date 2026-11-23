#pragma once

#include "CoreMinimal.h"
#include "Combat/SRElementalProjectile.h"
#include "SRVoidDaggerProjectile.generated.h"

/**
 * Veyra's Rift Dagger projectile: single-target void dagger. Direct damage
 * only (45), no splash, no slow — unlike the shared elemental projectile.
 */
UCLASS()
class PSGAMEPLAY_API ASRVoidDaggerProjectile : public ASRElementalProjectile
{
    GENERATED_BODY()

public:
    ASRVoidDaggerProjectile();
};
