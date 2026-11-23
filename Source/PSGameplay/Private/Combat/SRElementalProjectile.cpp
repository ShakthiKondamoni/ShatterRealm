#include "Combat/SRElementalProjectile.h"
#include "Combat/SRCombatComponent.h"
#include "Combat/SRPresentationActors.h"
#include "Character/SRCombatDummy.h"
#include "Debug/SRDebug.h"
#include "Abilities/SRGameplayEffects.h"
#include "Abilities/SRGameplayTags.h"
#include "GAS/PSAttributeSet.h"
#include "Character/PSBaseCharacter.h"
#include "GAS/PSPlayerState.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SphereComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

ASRElementalProjectile::ASRElementalProjectile()
{
    PrimaryActorTick.bCanEverTick = false;
    bReplicates = true;
    SetReplicateMovement(true);
    NetUpdateFrequency = 30.f;

    CollisionComponent = CreateDefaultSubobject<USphereComponent>(TEXT("ProjectileCollision"));
    CollisionComponent->InitSphereRadius(17.5f);
    CollisionComponent->SetCollisionProfileName(TEXT("BlockAllDynamic"));
    CollisionComponent->SetNotifyRigidBodyCollision(true);
    RootComponent = CollisionComponent;

    VisualComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ProjectileVisual"));
    VisualComponent->SetupAttachment(RootComponent);
    VisualComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    VisualComponent->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")));
    VisualComponent->SetWorldScale3D(FVector(0.35f));

    // Electric glow while in flight (provisional; a Niagara trail slots in here).
    GlowLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("ProjectileGlow"));
    GlowLight->SetupAttachment(RootComponent);
    GlowLight->SetLightColor(FLinearColor(0.15f, 0.75f, 1.f));
    GlowLight->SetIntensity(14.f);
    GlowLight->SetAttenuationRadius(400.f);
    GlowLight->SetCastShadows(false);

    ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
    ProjectileMovement->InitialSpeed = InitialSpeed;
    ProjectileMovement->MaxSpeed = InitialSpeed;
    ProjectileMovement->bRotationFollowsVelocity = true;
    ProjectileMovement->bShouldBounce = false;
    ProjectileMovement->ProjectileGravityScale = 0.f;
}

void ASRElementalProjectile::BeginPlay()
{
    Super::BeginPlay();
    if (HasAuthority())
    {
        CollisionComponent->OnComponentHit.AddDynamic(this, &ASRElementalProjectile::OnImpact);
    }
    else
    {
        ProjectileMovement->SetComponentTickEnabled(false);
        CollisionComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }
}

void ASRElementalProjectile::InitializeProjectile(const FVector& Direction, float Speed, float Range, APSBaseCharacter* InstigatingHeroPawn)
{
    InstigatingHero = InstigatingHeroPawn;
    ProjectileMovement->InitialSpeed = Speed;
    ProjectileMovement->MaxSpeed = Speed;
    ProjectileMovement->Velocity = Direction.GetSafeNormal() * Speed;
    if (InstigatingHeroPawn)
    {
        CollisionComponent->IgnoreActorWhenMoving(InstigatingHeroPawn, true);
        InstigatingHeroPawn->GetCapsuleComponent()->IgnoreActorWhenMoving(this, true);
    }

    // Range enforcement via lifespan: the projectile cannot outlive its configured range.
    const float Lifetime = (Speed > 0.f) ? (Range / Speed) : 2.f;
    SetLifeSpan(FMath::Max(Lifetime, 0.1f));

#if !UE_BUILD_SHIPPING
    if (SRDebug::IsEnabled(SRDebug::ECategory::Projectile) && GetWorld())
    {
        DrawDebugSphere(GetWorld(), GetActorLocation(), 20.f, 10, FColor::Cyan, false, 3.f);
        DrawDebugLine(GetWorld(), GetActorLocation(), GetActorLocation() + Direction * 200.f, FColor::Cyan, false, 3.f, 0, 1.f);
    }
#endif
}

void ASRElementalProjectile::ConfigureGrenade(float FuseSeconds, float GravityScale, float BounceCoefficient)
{
    if (!HasAuthority() || bImpactResolved || !ProjectileMovement) return;
    bGrenadeMode = true;
    ProjectileMovement->bShouldBounce = true;
    ProjectileMovement->Bounciness = FMath::Clamp(BounceCoefficient, 0.f, 0.9f);
    ProjectileMovement->Friction = 0.65f;
    ProjectileMovement->BounceVelocityStopSimulatingThreshold = 25.f;
    ProjectileMovement->ProjectileGravityScale = FMath::Max(0.1f, GravityScale);
    SetLifeSpan(0.f); // Range-based projectile lifetime must not cancel the grenade fuse.
    const float SafeFuse = FMath::Clamp(FuseSeconds, 0.2f, 10.f);
    GetWorldTimerManager().SetTimer(GrenadeFuseTimer, this, &ASRElementalProjectile::DetonateGrenade, SafeFuse, false);
}

void ASRElementalProjectile::DetonateGrenade()
{
    if (HasAuthority() && !bImpactResolved)
    {
        ResolveImpact(GetActorLocation(), nullptr, FVector::UpVector);
    }
}

void ASRElementalProjectile::OnImpact(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
    FVector NormalImpulse, const FHitResult& Hit)
{
    if (!HasAuthority() || bImpactResolved) return;
    if (bGrenadeMode) return; // Grenades bounce and detonate on their fuse.
    ResolveImpact(Hit.ImpactPoint, OtherActor, Hit.ImpactNormal);
}

void ASRElementalProjectile::ResolveImpact(const FVector& ImpactPoint, AActor* DirectTarget, const FVector& Normal)
{
    if (!HasAuthority() || bImpactResolved) return;
    bImpactResolved = true;

    APSBaseCharacter* Hero = InstigatingHero.Get();
    UPSCombatComponent* Combat = Hero ? Hero->GetCombatComponent() : nullptr;
    if (!Combat)
    {
        DestroyProjectile(TEXT("No combat component"));
        return;
    }

    SRDebug::Message(SRDebug::ECategory::Projectile, TEXT("Server"),
        FString::Printf(TEXT("Impact at %s direct=%s"), *ImpactPoint.ToString(), *GetNameSafe(DirectTarget)), 0.4f, FColor::Cyan);
    SRDebug::Log(SRDebug::ECategory::Projectile, TEXT("Server"), FString::Printf(TEXT("Impact %s normal=%s"), *ImpactPoint.ToString(), *Normal.ToString()));

#if !UE_BUILD_SHIPPING
    if (GetWorld() && SRDebug::IsEnabled(SRDebug::ECategory::Projectile))
    {
        DrawDebugPoint(GetWorld(), ImpactPoint, 16.f, FColor::Yellow, false, 3.f);
        DrawDebugSphere(GetWorld(), ImpactPoint, SplashRadius, 12, FColor::Orange, false, 3.f);
    }
#endif

    // Direct hit: exactly one damage instance to the direct target.
    if (DirectTarget && (Cast<APawn>(DirectTarget) || Cast<APSCombatDummy>(DirectTarget)))
    {
        bool bEnemy = false;
        const float Applied = Combat->ApplyCombatDamage(DirectTarget, DirectDamage, Hero, bEnemy);
        if (Applied > 0.f)
        {
            DamagedActors.Add(DirectTarget);
            ApplySlow(DirectTarget);
            SRDebug::Message(SRDebug::ECategory::Damage, TEXT("Server"), FString::Printf(TEXT("Direct=%.1f"), Applied), 0.4f, FColor::Green);
        }
    }

    // Splash: nearby enemies, never the direct target, never anyone twice.
    ApplySplash(ImpactPoint, DirectTarget);

    // Presentation: electric detonation burst visible on every client
    // (direct-target damage numbers already arrive via the hit-feedback path).
    MulticastImpactFX(ImpactPoint, SplashRadius);

    OnProjectileImpacted.Broadcast(ImpactPoint, DirectTarget != nullptr);
    DestroyProjectile(TEXT("Impact resolved"));
}

void ASRElementalProjectile::MulticastImpactFX_Implementation(FVector_NetQuantize100 ImpactPoint, float Radius)
{
    UWorld* World = GetWorld();
    if (!World) return;
    ASRTransientVFX* Burst = World->SpawnActor<ASRTransientVFX>(ASRTransientVFX::StaticClass(), ImpactPoint, FRotator::ZeroRotator);
    if (Burst)
    {
        Burst->ConfigureBurst(ImpactPoint, 12.f, FMath::Max(Radius * 0.35f, 60.f), FLinearColor(0.15f, 0.75f, 1.f), 0.3f, true);
    }
}

void ASRElementalProjectile::ApplySplash(const FVector& Center, AActor* DirectTarget)
{
    APSBaseCharacter* Hero = InstigatingHero.Get();
    UPSCombatComponent* Combat = Hero ? Hero->GetCombatComponent() : nullptr;
    if (!Combat || SplashDamage <= 0.f || SplashRadius <= 0.f) return;

    TArray<FOverlapResult> Overlaps;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(SRSplash), false, GetOwner());
    GetWorld()->OverlapMultiByObjectType(Overlaps, Center, FQuat::Identity,
        FCollisionObjectQueryParams::InitType::AllDynamicObjects, FCollisionShape::MakeSphere(SplashRadius), Params);

    for (const FOverlapResult& Overlap : Overlaps)
    {
        AActor* Candidate = Overlap.GetActor();
        if (!Candidate || Candidate == DirectTarget || Candidate == GetOwner()) continue;
        if (DamagedActors.Contains(Candidate)) continue;
        if (Cast<APawn>(Candidate) == nullptr && Cast<APSCombatDummy>(Candidate) == nullptr) continue;

        // Cover check: splash does not reach targets fully occluded from the impact point.
        FHitResult LOS;
        FCollisionQueryParams LOSParams(SCENE_QUERY_STAT(SRSplashLOS), false, GetOwner());
        LOSParams.AddIgnoredActor(this);
        if (DirectTarget) LOSParams.AddIgnoredActor(DirectTarget);
        const bool bBlocked = GetWorld()->LineTraceSingleByChannel(LOS, Center, Candidate->GetActorLocation(),
            ECollisionChannel::ECC_Visibility, LOSParams);
        if (bBlocked && LOS.GetActor() != Candidate) continue;

        bool bEnemy = false;
        const float Applied = Combat->ApplyCombatDamage(Candidate, SplashDamage, Hero, bEnemy);
        if (Applied > 0.f)
        {
            DamagedActors.Add(Candidate);
            ApplySlow(Candidate);
            SRDebug::Message(SRDebug::ECategory::Damage, TEXT("Server"), FString::Printf(TEXT("Splash %.1f -> %s"), Applied, *GetNameSafe(Candidate)), 0.4f, FColor::Green);
        }
    }
}

void ASRElementalProjectile::ApplySlow(AActor* Target)
{
    if (SlowDuration <= 0.f || SlowMultiplier >= 1.f) return;
    UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target);
    if (!TargetASC) return;

    FGameplayEffectSpecHandle Spec = TargetASC->MakeOutgoingSpec(UGE_SRShockSlow::StaticClass(), 1.f, TargetASC->MakeEffectContext());
    if (!Spec.IsValid()) return;
    Spec.Data->SetSetByCallerMagnitude(SRGameplayTags::Tag_Data_SlowDuration, SlowDuration);
    Spec.Data->SetSetByCallerMagnitude(SRGameplayTags::Tag_Data_SlowMultiplier, SlowMultiplier);
    TargetASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
    SRDebug::Message(SRDebug::ECategory::Damage, TEXT("Server"), FString::Printf(TEXT("Slow %.0f%% for %.1fs -> %s"), (1.f - SlowMultiplier) * 100.f, SlowDuration, *GetNameSafe(Target)), 0.4f, FColor::Yellow);
}

void ASRElementalProjectile::DestroyProjectile(const FString& Reason)
{
    SRDebug::Log(SRDebug::ECategory::Projectile, TEXT("Server"), FString::Printf(TEXT("Destroyed: %s"), *Reason));
    Destroy();
}
