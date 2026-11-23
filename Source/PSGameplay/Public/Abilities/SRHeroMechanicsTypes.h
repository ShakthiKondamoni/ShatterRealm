#pragma once
#include "CoreMinimal.h"
#include "SRHeroMechanicsTypes.generated.h"

UENUM(BlueprintType)
enum class ESRHeroAction : uint8
{
    BlinkCut,
    RiftDagger,
    VoidSnare,
    RiftReaper,
    InfernoPunch,
    FlameStep,
    HeatSurge,
    PhoenixMeteorfall,
    MirrorDouble,
    FadeStep,
    GlassScreen,
    PhantomProcession,
    StoneKnuckle,
    FaultDash,
    QuakeBurst,
    FaultlineTitan,
    VitalPulse,
    BriarSnare,
    BarkWard,
    LastSeed,
    TidalMend,
    MistVeil,
    Undertow,
    TidalCurtain,
    Sunbeam,
    RadiantWard,
    SolarFlare,
    DawnIntervention,
    HealingSeedling,
    ThornGuard,
    RootCage,
    WorldseedDominion,
    RewindMend,
    TimeBubble,
    TimeFracture,
    ParadoxEcho,
    IronRam,
    BulwarkPlate,
    FortressStance,
    IronVanguard,
    ShardSpike,
    PrismWall,
    CrystalBind,
    PrismSkyfall
};

/** All lengths are centimeters; all times are seconds. Each concrete ability owns its own editable copy. */
USTRUCT(BlueprintType)
struct PSGAMEPLAY_API FSRHeroActionConfig
{
    GENERATED_BODY()
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) ESRHeroAction Action = ESRHeroAction::BlinkCut;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="1")) float AttackReach = 250.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="1")) float GrowthSpeed = 250.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="1")) float ReviveHealth = 60.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0")) float ReviveShield = 0.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0")) float Damage = 35.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0")) float EdgeDamage = 20.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0")) float Healing = 0.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0")) float Radius = 400.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0")) float Range = 2000.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0")) float Width = 600.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0")) float Height = 200.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0")) float Speed = 1800.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0")) float Duration = 6.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0")) float EffectDuration = 2.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0")) float StunDuration = 0.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0")) float VisionDuration = 0.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0")) float WarningDuration = 0.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0")) float Interval = 0.5.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0")) float ChannelDuration = 0.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0")) float Knockback = 500.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0")) float LaunchHeight = 0.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0")) float DamageScale = 1.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0")) float FireRateScale = 1.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0")) float RechargeScale = 1.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0")) float DamageTakenScale = 1.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0")) float MovementScale = 1.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0")) float KnockbackScale = 1.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0")) float WardHealth = 0.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0")) float ProjectileGravity = 0.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0")) float Fuse = 2.5.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0")) float FuseReduction = 0.4.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0")) float MinimumFuse = 0.3.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0")) float StructureHealth = 250.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="1", ClampMax="16")) int32 Segments = 4;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="1", ClampMax="16")) int32 Branches = 5;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) bool bDrawDebug = false;
};
PSGAMEPLAY_API FSRHeroActionConfig SRDefaultActionConfig(ESRHeroAction Action);
