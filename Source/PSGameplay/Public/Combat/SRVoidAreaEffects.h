#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SRVoidAreaEffects.generated.h"

class USphereComponent;
class UNiagaraSystem;
class APSBaseCharacter;

/**
 * Void Snare field: persistent ground area that ROOTS enemy characters once
 * each during its lifetime. Team-safe (caster and teammates unaffected),
 * replicated for remote visuals. Root is a GAS Gameplay Effect
 * (UGE_SRRoot, SR.Data.RootDuration set-by-caller).
 */
UCLASS()
class PSGAMEPLAY_API ASRVoidSnareField : public AActor
{
    GENERATED_BODY()

public:
    ASRVoidSnareField();

    /** Server: initialize the field; begins the lifetime timer and roots current occupants. */
    void InitializeField(AActor* InCaster, float InRadius, float InLifetime, float InRootDuration);

protected:
    virtual void BeginPlay() override;

    UFUNCTION() void OnOverlapBegin(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
        UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

    /** Server: applies the root GE to one enemy (once per actor per field). */
    void RootEnemy(AActor* Enemy);

    UPROPERTY(VisibleAnywhere, Category="SR|Snare")
    TObjectPtr<USphereComponent> FieldSphere;

    UPROPERTY(VisibleAnywhere, Category="SR|Snare")
    TObjectPtr<UStaticMeshComponent> FieldVisual;

    /** Caster (for team filtering). */
    TWeakObjectPtr<AActor> CasterActor;

    /** Enemies already rooted by this field — one root per enemy per lifetime. */
    TSet<TObjectKey<AActor>> RootedActors;

    float RootDuration = 1.5f;
    bool bInitialized = false;
};

/**
 * Rift Reaper wave: a replicated advancing void wave. Swept sphere queries find
 * enemies along the travel path; each enemy takes damage once and is rooted.
 * The wave stops early on world obstruction.
 */
UCLASS()
class PSGAMEPLAY_API ASRVoidReaperWave : public AActor
{
    GENERATED_BODY()

public:
    ASRVoidReaperWave();

    /** Server: initializes travel and starts the advance loop. */
    void InitializeWave(AActor* InCaster, const FVector& InDirection, float InSpeed,
        float InMaxDistance, float InRadius, float InDamage, float InRootDuration);

protected:
    virtual void Tick(float DeltaSeconds) override;

    /** Server: advances the wave one step with a swept query and applies effects. */
    void AdvanceWave(float DeltaSeconds);

    /** Server: damages + roots one enemy (once per actor per wave). */
    void ApplyWaveToEnemy(AActor* Enemy, const FVector& HitLocation);

    UPROPERTY(VisibleAnywhere, Category="SR|Reaper")
    TObjectPtr<USphereComponent> WaveSphere;

    UPROPERTY(VisibleAnywhere, Category="SR|Reaper")
    TObjectPtr<UStaticMeshComponent> WaveVisual;

    TWeakObjectPtr<AActor> CasterActor;
    FVector WaveDirection = FVector::ForwardVector;
    float WaveSpeed = 2500.f;
    float MaxTravelDistance = 7000.f;
    float WaveRadius = 800.f;
    float WaveDamage = 85.f;
    float RootDuration = 3.f;
    float TraveledDistance = 0.f;
    bool bInitialized = false;

    /** Enemies damaged/rooted by this wave — strictly one application each. */
    TSet<TObjectKey<AActor>> AffectedActors;
};
