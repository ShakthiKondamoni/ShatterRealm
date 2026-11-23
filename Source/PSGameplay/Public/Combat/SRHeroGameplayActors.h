#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "Subsystems/WorldSubsystem.h"
#include "Abilities/SRHeroMechanicsTypes.h"
#include "SRHeroGameplayActors.generated.h"

class UBoxComponent;
class USphereComponent;
class UProjectileMovementComponent;
class UStaticMeshComponent;
class APSBaseCharacter;

UCLASS()
class PSGAMEPLAY_API ASRHeroBarrier : public AActor
{
    GENERATED_BODY()
public:
    ASRHeroBarrier();
    void Initialize(APSBaseCharacter* InCaster, const FSRHeroActionConfig& InConfig);
    static bool CanPlace(UWorld* World, APSBaseCharacter* Caster, const FVector& Ground, const FRotator& Rotation, float Width, float Height);
    bool Crosses(const FVector& Start, const FVector& End) const;
    ESRHeroAction GetAction() const { return Config.Action; }
    virtual void Tick(float DeltaSeconds) override;
    virtual float TakeDamage(float Amount, const FDamageEvent& Event, AController* Instigator, AActor* Causer) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
protected:
    UFUNCTION() void OnRep_Config();
    UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> Collision;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Geometry;
    UPROPERTY(ReplicatedUsing=OnRep_Config) FSRHeroActionConfig Config;
    UPROPERTY(Replicated) float Health = 250.f;
    UPROPERTY(Replicated) TObjectPtr<APSBaseCharacter> Caster;
private:
    float Elapsed = 0.f, Traveled = 0.f;
};

UCLASS()
class PSGAMEPLAY_API ASRHeroMovingVolume : public AActor
{
    GENERATED_BODY()
public:
    ASRHeroMovingVolume();
    void Initialize(APSBaseCharacter* InCaster, const FSRHeroActionConfig& InConfig, const FVector& InDirection);
    virtual void Tick(float DeltaSeconds) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
protected:
    UPROPERTY(Replicated) FSRHeroActionConfig Config;
    UPROPERTY(Replicated) TObjectPtr<APSBaseCharacter> Caster;
    UPROPERTY(Replicated) FVector Direction = FVector::ForwardVector;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Geometry;
private:
    TSet<TWeakObjectPtr<AActor>> HitActors;
    float Traveled = 0.f;
    bool bInitialized = false;
};

UCLASS()
class PSGAMEPLAY_API ASRHeroField : public AActor
{
    GENERATED_BODY()
public:
    ASRHeroField();
    void Initialize(APSBaseCharacter* InCaster, const FSRHeroActionConfig& InConfig);
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual float TakeDamage(float Amount, const FDamageEvent& Event, AController* Instigator, AActor* Causer) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    static ASRHeroField* Spawn(UWorld* World, APSBaseCharacter* Caster, const FVector& Ground, const FSRHeroActionConfig& Config);
    const FSRHeroActionConfig& GetConfig() const { return Config; }
    bool IsTimeBubble() const { return Config.Action == ESRHeroAction::TimeBubble; }
protected:
    UFUNCTION() void OnFieldEntry(UPrimitiveComponent* Component, AActor* OtherActor, UPrimitiveComponent* OtherComponent,
        int32 BodyIndex, bool bFromSweep, const FHitResult& Hit);
    UPROPERTY(VisibleAnywhere) TObjectPtr<USphereComponent> Collision;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> FieldGeometry;
    UPROPERTY(Replicated) FSRHeroActionConfig Config;
    UPROPERTY(Replicated) TObjectPtr<APSBaseCharacter> Caster;
    UPROPERTY(Replicated) float Health = 250.f;
private:
    void Evaluate(float Step);
    void BuildStructures();
    TSet<TWeakObjectPtr<AActor>> Affected;
    TArray<TWeakObjectPtr<AActor>> Children;
    float Elapsed = 0.f, Accumulator = 0.f;
    bool bInitialized = false, bImpactResolved = false;
};

USTRUCT()
struct FSRVineSegment
{
    GENERATED_BODY()
    UPROPERTY() FVector Start = FVector::ZeroVector;
    UPROPERTY() FVector End = FVector::ZeroVector;
};
struct FSRGrowingVine { FVector Tip, Direction; float Traveled = 0.f; bool bForked = false; };

UCLASS()
class PSGAMEPLAY_API ASRHeroVines : public AActor
{
    GENERATED_BODY()
public:
    ASRHeroVines();
    void Initialize(APSBaseCharacter* InCaster, const FSRHeroActionConfig& InConfig);
    virtual void Tick(float DeltaSeconds) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
protected:
    UPROPERTY(Replicated) TArray<FSRVineSegment> Segments;
    UPROPERTY(Replicated) FSRHeroActionConfig Config;
    UPROPERTY(Replicated) TObjectPtr<APSBaseCharacter> Caster;
private:
    TArray<FSRGrowingVine> Branches;
    TSet<TWeakObjectPtr<AActor>> Rooted;
    FRandomStream Random;
    float Accumulator = 0.f;
};

UCLASS()
class PSGAMEPLAY_API ASRHeroProjectile : public AActor
{
    GENERATED_BODY()
public:
    ASRHeroProjectile();
    void Initialize(APSBaseCharacter* InCaster, const FSRHeroActionConfig& InConfig, const FVector& Direction);
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
protected:
    virtual void BeginPlay() override;
    UFUNCTION() void OnImpact(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, FVector Impulse, const FHitResult& Hit);
    UFUNCTION() void OnBounce(const FHitResult& Hit, const FVector& ImpactVelocity);
    UPROPERTY(VisibleAnywhere) TObjectPtr<USphereComponent> Collision;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UProjectileMovementComponent> Movement;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> ProjectileGeometry;
    UPROPERTY(Replicated) FSRHeroActionConfig Config;
    UPROPERTY(Replicated) TObjectPtr<APSBaseCharacter> Caster;
private:
    void Detonate();
    FTimerHandle FuseTimer;
    double LastBounceTime = -1.;
    bool bResolved = false;
};

UCLASS()
class PSGAMEPLAY_API ASRHeroDecoy : public ACharacter
{
    GENERATED_BODY()
public:
    ASRHeroDecoy();
    void Initialize(APSBaseCharacter* InCaster, float Lifetime, float Health, bool bMove);
    virtual void Tick(float DeltaSeconds) override;
    virtual float TakeDamage(float Amount, const FDamageEvent& Event, AController* Instigator, AActor* Causer) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
protected:
    UFUNCTION() void OnRep_Caster();
    UPROPERTY(ReplicatedUsing=OnRep_Caster) TObjectPtr<APSBaseCharacter> Caster;
    UPROPERTY(Replicated) float Health = 100.f;
    UPROPERTY(Replicated) bool bMoving = false;
    UPROPERTY(Replicated, BlueprintReadOnly) bool bPretendFiring = false;
private:
    float Elapsed = 0.f;
};

struct FSRProjectileTimeState { float AppliedScale = 1.f; };

/** One coordinator per world prevents overlapping time bubbles from overwriting velocities. */
UCLASS()
class PSGAMEPLAY_API USRProjectileTimeSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    virtual bool IsTickable() const override;
    void RegisterBubble(ASRHeroField* Bubble);
    void UnregisterBubble(ASRHeroField* Bubble);
private:
    TArray<TWeakObjectPtr<ASRHeroField>> Bubbles;
    TMap<TWeakObjectPtr<UProjectileMovementComponent>, FSRProjectileTimeState> Projectiles;
};
