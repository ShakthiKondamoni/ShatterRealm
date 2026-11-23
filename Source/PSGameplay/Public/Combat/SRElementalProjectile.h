#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/NetSerialization.h"
#include "SRElementalProjectile.generated.h"

class USphereComponent;
class UProjectileMovementComponent;
class UStaticMeshComponent;
class UPointLightComponent;
class APSBaseCharacter;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSRProjectileImpacted, FVector, ImpactPoint, bool, bDirectHit);

/**
 * Reusable server-authoritative elemental projectile (ability service, not the
 * hitscan primary). Direct hit applies direct damage once; world or direct
 * impact triggers a splash query (each additional enemy at most once, never
 * double-applied to the direct target). Slow is a temporary GAS effect.
 */
UCLASS()
class PSGAMEPLAY_API ASRElementalProjectile : public AActor
{
    GENERATED_BODY()

public:
    ASRElementalProjectile();

    /** Server: sets speed, range/lifespan and instigating hero; called right after spawn. */
    void InitializeProjectile(const FVector& Direction, float Speed, float Range, APSBaseCharacter* InstigatingHero);

    /** Switch this instance to a throwable with bounce, gravity and a timed explosion. Server only. */
    void ConfigureGrenade(float FuseSeconds, float GravityScale, float BounceCoefficient);

    // Data-driven gameplay configuration -----------------------------------
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|Projectile") float DirectDamage = 45.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|Projectile") float SplashDamage = 25.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|Projectile") float SplashRadius = 300.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|Projectile") float SlowMultiplier = 0.75f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|Projectile") float SlowDuration = 2.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|Projectile") float InitialSpeed = 4000.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|Projectile") float MaxRange = 8000.f;

    UPROPERTY(BlueprintAssignable, Category="SR|Projectile") FSRProjectileImpacted OnProjectileImpacted;

protected:
    virtual void BeginPlay() override;

    UFUNCTION() void OnImpact(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
        FVector NormalImpulse, const FHitResult& Hit);

    /** Server: one authoritative impact resolution (direct target may be null for world impacts). */
    void ResolveImpact(const FVector& ImpactPoint, AActor* DirectTarget, const FVector& Normal);

    /** Applies splash to enemies within radius, excluding the direct target, respecting friendly fire. */
    void ApplySplash(const FVector& Center, AActor* DirectTarget);

    void ApplySlow(AActor* Target);
    void DestroyProjectile(const FString& Reason);
    void DetonateGrenade();

    /** Electric detonation burst on every client at the impact point. */
    UFUNCTION(NetMulticast, Unreliable)
    void MulticastImpactFX(FVector_NetQuantize100 ImpactPoint, float Radius);

    UPROPERTY(VisibleAnywhere, Category="SR|Projectile") TObjectPtr<USphereComponent> CollisionComponent;
    UPROPERTY(VisibleAnywhere, Category="SR|Projectile") TObjectPtr<UStaticMeshComponent> VisualComponent;
    UPROPERTY(VisibleAnywhere, Category="SR|Projectile") TObjectPtr<UPointLightComponent> GlowLight;
    UPROPERTY(VisibleAnywhere, Category="SR|Projectile") TObjectPtr<UProjectileMovementComponent> ProjectileMovement;

    TWeakObjectPtr<APSBaseCharacter> InstigatingHero;
    TSet<TObjectKey<AActor>> DamagedActors;
    bool bImpactResolved = false;
    bool bGrenadeMode = false;
    FTimerHandle GrenadeFuseTimer;
};
