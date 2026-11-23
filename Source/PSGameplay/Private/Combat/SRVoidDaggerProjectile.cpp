#include "Combat/SRVoidDaggerProjectile.h"
#include "Components/SphereComponent.h"

ASRVoidDaggerProjectile::ASRVoidDaggerProjectile()
{
    // Veyra config: single-target 45 damage, no splash, no slow, 12cm radius.
    DirectDamage = 45.f;
    SplashDamage = 0.f;
    SplashRadius = 0.f;
    SlowMultiplier = 1.f; // 1 = no slow (ApplySlow skips when multiplier >= 1)
    SlowDuration = 0.f;
    InitialSpeed = 5000.f;
    MaxRange = 8000.f;
    CollisionComponent->InitSphereRadius(12.f);
}
