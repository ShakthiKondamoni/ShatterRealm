#include "Combat/SRHeroGameplayActors.h"
#include "Abilities/SRHeroGameplayLibrary.h"
#include "Abilities/SRGameplayTags.h"
#include "Character/PSBaseCharacter.h"
#include "Character/SRHeroGameplayComponent.h"
#include "Components/SphereComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Debug/SRDebug.h"
#include "DrawDebugHelpers.h"

ASRHeroProjectile::ASRHeroProjectile()
{
    bReplicates = true; SetReplicateMovement(true);
    Collision = CreateDefaultSubobject<USphereComponent>(TEXT("ProjectileCollision"));
    Collision->InitSphereRadius(15.f); Collision->SetCollisionProfileName(TEXT("BlockAllDynamic"));
    Collision->SetNotifyRigidBodyCollision(true); SetRootComponent(Collision);
    Movement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
    Movement->bRotationFollowsVelocity = true;
    Movement->bForceSubStepping = true;
    ProjectileGeometry = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ProjectileGeometry"));
    ProjectileGeometry->SetupAttachment(Collision);
    ProjectileGeometry->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    ProjectileGeometry->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")));
    ProjectileGeometry->SetRelativeScale3D(FVector(0.3f));
}
void ASRHeroProjectile::BeginPlay()
{
    Super::BeginPlay();
    if (!HasAuthority())
    {
        Movement->SetComponentTickEnabled(false);
        Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }
}
void ASRHeroProjectile::Initialize(APSBaseCharacter* InCaster, const FSRHeroActionConfig& InConfig, const FVector& Direction)
{
    if (!HasAuthority() || !InCaster) return;
    Caster = InCaster; SetOwner(InCaster); SetInstigator(InCaster); Config = InConfig;
    Collision->IgnoreActorWhenMoving(Caster, true);
    TArray<AActor*> Attached; Caster->GetAttachedActors(Attached);
    for (AActor* Actor : Attached) Collision->IgnoreActorWhenMoving(Actor, true);
    Movement->InitialSpeed = Movement->MaxSpeed = Config.Speed;
    Movement->Velocity = Direction.GetSafeNormal() * Config.Speed;
    Movement->ProjectileGravityScale = Config.ProjectileGravity;
    Collision->OnComponentHit.AddDynamic(this, &ThisClass::OnImpact);
    if (Config.Action == ESRHeroAction::SolarFlare)
    {
        Movement->bShouldBounce = true; Movement->Bounciness = 0.55f;
        Movement->BounceVelocityStopSimulatingThreshold = 15.f;
        Movement->OnProjectileBounce.AddDynamic(this, &ThisClass::OnBounce);
        GetWorldTimerManager().SetTimer(FuseTimer, this, &ThisClass::Detonate, Config.Fuse, false);
        SetLifeSpan(Config.Fuse + 0.5f);
    }
    else SetLifeSpan(Config.Range / FMath::Max(1.f, Config.Speed) + 0.5f);
}
void ASRHeroProjectile::OnImpact(UPrimitiveComponent*, AActor*, UPrimitiveComponent*, FVector, const FHitResult& Hit)
{
    if (!HasAuthority() || bResolved || Config.Action == ESRHeroAction::SolarFlare) return;
    bResolved = true;
    FVector Ground;
    if (Caster && SRHeroGameplay::Ground(GetWorld(), Caster, Hit.ImpactPoint, Ground))
    {
        if (Config.Action == ESRHeroAction::BriarSnare)
        {
            FActorSpawnParameters Params; Params.Owner = Caster;
            ASRHeroVines* Vines = GetWorld()->SpawnActor<ASRHeroVines>(Ground + FVector(0,0,20), FRotator::ZeroRotator, Params);
            if (Vines) Vines->Initialize(Caster, Config);
        }
        else ASRHeroField::Spawn(GetWorld(), Caster, Ground, Config);
    }
    Destroy();
}
void ASRHeroProjectile::OnBounce(const FHitResult&, const FVector& ImpactVelocity)
{
    if (!HasAuthority() || bResolved || Config.Action != ESRHeroAction::SolarFlare) return;
    const double Now = GetWorld()->GetTimeSeconds();
    if (Now - LastBounceTime < 0.12 || ImpactVelocity.Size() < 100.f) return;
    LastBounceTime = Now;
    const float Remaining = GetWorldTimerManager().GetTimerRemaining(FuseTimer);
    if (Remaining > Config.MinimumFuse)
        GetWorldTimerManager().SetTimer(FuseTimer, this, &ThisClass::Detonate,
            FMath::Max(Config.MinimumFuse, Remaining - Config.FuseReduction), false);
}
void ASRHeroProjectile::Detonate()
{
    if (!HasAuthority() || bResolved || !Caster) return;
    bResolved = true;
    for (AActor* Target : SRHeroGameplay::InRadius(Caster, GetActorLocation(), Config.Radius))
    {
        if (!SRHeroGameplay::IsEnemy(Caster, Target)) continue;
        FCollisionQueryParams Params(SCENE_QUERY_STAT(SRFlashCover), false, Caster); Params.AddIgnoredActor(this);
        FHitResult Cover;
        const APSBaseCharacter* Character = Cast<APSBaseCharacter>(Target);
        const FVector Eyes = Character ? Character->GetPawnViewLocation() : Target->GetActorLocation();
        if (GetWorld()->LineTraceSingleByChannel(Cover, GetActorLocation(), Eyes, ECC_Visibility, Params) && Cover.GetActor() != Target) continue;
        const FVector Look = Character ? Character->GetControlRotation().Vector() : Target->GetActorForwardVector();
        const float Facing = FMath::Clamp((FVector::DotProduct(Look, (GetActorLocation() - Eyes).GetSafeNormal()) + 1.f) * 0.5f, 0.f, 1.f);
        const float Distance = FMath::Clamp(1.f - FVector::Dist(Eyes, GetActorLocation()) / Config.Radius, 0.f, 1.f);
        const float Strength = Distance * FMath::Lerp(0.15f, 1.f, Facing * Facing);
        if (auto* State = Target->FindComponentByClass<USRHeroGameplayComponent>())
            State->SetBlind(Strength, Config.EffectDuration * FMath::Max(0.15f, Strength), Caster);
    }
    Destroy();
}
void ASRHeroProjectile::EndPlay(const EEndPlayReason::Type Reason)
{ GetWorldTimerManager().ClearTimer(FuseTimer); Super::EndPlay(Reason); }
void ASRHeroProjectile::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ASRHeroProjectile, Config); DOREPLIFETIME(ASRHeroProjectile, Caster);
}

ASRHeroDecoy::ASRHeroDecoy()
{
    bReplicates = true; SetReplicateMovement(true);
    PrimaryActorTick.bCanEverTick = true;
    GetCapsuleComponent()->InitCapsuleSize(42.f, 96.f);
    GetCharacterMovement()->bRunPhysicsWithNoController = true;
    GetCharacterMovement()->MaxWalkSpeed = 350.f;
}
void ASRHeroDecoy::Initialize(APSBaseCharacter* InCaster, float Lifetime, float InHealth, bool bMove)
{
    if (!HasAuthority()) return;
    Caster = InCaster; SetOwner(InCaster); Health = InHealth; bMoving = bMove;
    SetLifeSpan(Lifetime); OnRep_Caster(); ForceNetUpdate();
}
void ASRHeroDecoy::OnRep_Caster()
{
    if (!Caster || !Caster->GetMesh()) return;
    GetMesh()->SetSkeletalMeshAsset(Caster->GetMesh()->GetSkeletalMeshAsset());
    GetMesh()->SetRelativeTransform(Caster->GetMesh()->GetRelativeTransform());
    for (int32 Index = 0; Index < Caster->GetMesh()->GetNumMaterials(); ++Index) GetMesh()->SetMaterial(Index, Caster->GetMesh()->GetMaterial(Index));
    // Reuse an existing character pose; decoys never have an ASC or Combat Component.
    GetMesh()->SetLeaderPoseComponent(Caster->GetMesh());
}
void ASRHeroDecoy::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (Caster && !GetMesh()->GetSkeletalMeshAsset()) OnRep_Caster();
#if !UE_BUILD_SHIPPING
    if (bPretendFiring && SRDebug::IsEnabled(SRDebug::ECategory::Ability))
        DrawDebugLine(GetWorld(), GetActorLocation() + FVector(0,0,40),
            GetActorLocation() + FVector(0,0,40) + GetActorForwardVector() * 1200.f, FColor::Silver, false, 0.04f);
#endif
    if (!HasAuthority() || !bMoving) return;
    Elapsed += DeltaSeconds;
    bPretendFiring = FMath::Fmod(Elapsed, 1.5f) < 0.12f;
    AddMovementInput(GetActorForwardVector(), 1.f, true);
    if (FMath::Sin(Elapsed * 0.8f) > 0.95f) AddActorWorldRotation(FRotator(0,25.f * DeltaSeconds,0));
}
float ASRHeroDecoy::TakeDamage(float Amount, const FDamageEvent&, AController*, AActor* Causer)
{
    if (!HasAuthority() || !FMath::IsFinite(Amount) || Amount <= 0.f || SRHeroGameplay::SameTeam(Caster, Causer)) return 0.f;
    const float Applied = FMath::Min(Health, Amount); Health -= Applied; ForceNetUpdate();
    if (Health <= 0.f) Destroy();
    return Applied;
}
void ASRHeroDecoy::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ASRHeroDecoy, Caster); DOREPLIFETIME(ASRHeroDecoy, Health); DOREPLIFETIME(ASRHeroDecoy, bMoving);
    DOREPLIFETIME(ASRHeroDecoy, bPretendFiring);
}
