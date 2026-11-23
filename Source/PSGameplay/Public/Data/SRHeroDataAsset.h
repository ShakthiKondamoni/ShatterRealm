#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Abilities/SRGameplayTags.h"
#include "SRHeroDataAsset.generated.h"

class UAnimInstance;
class UGameplayAbility;
class UMaterialInterface;
class USkeletalMesh;
class UTexture2D;

/**
 * A hero's high-level identity. Roles are gameplay archetypes; elements are
 * the fictional affinities the full hero roster will be built around.
 */
UENUM(BlueprintType)
enum class ESRHeroRole : uint8
{
    Duelist UMETA(DisplayName = "Duelist"),
    Healer  UMETA(DisplayName = "Healer"),
    Tank    UMETA(DisplayName = "Tank"),
    Controller UMETA(DisplayName = "Controller")
};

/**
 * Elemental identity for every hero in the planned roster. Append-only:
 * never reorder or remove entries, existing assets serialize values by index.
 */
UENUM(BlueprintType)
enum class ESRHeroElement : uint8
{
    Electricity UMETA(DisplayName = "Electricity"),
    Void        UMETA(DisplayName = "Void"),
    Fire        UMETA(DisplayName = "Fire"),
    Illusion    UMETA(DisplayName = "Illusion"),
    Earth       UMETA(DisplayName = "Earth"),
    Nature      UMETA(DisplayName = "Nature"),
    Water       UMETA(DisplayName = "Water"),
    Solar       UMETA(DisplayName = "Solar"),
    Time        UMETA(DisplayName = "Time"),
    Crystal     UMETA(DisplayName = "Crystal"),
    Lava        UMETA(DisplayName = "Lava"),
    Magnetism   UMETA(DisplayName = "Magnetism"),
    Ice         UMETA(DisplayName = "Ice"),
    Wind        UMETA(DisplayName = "Wind"),
    Shadow      UMETA(DisplayName = "Shadow"),
    Gravity     UMETA(DisplayName = "Gravity"),
    Ash         UMETA(DisplayName = "Ash"),
    Spatial     UMETA(DisplayName = "Spatial"),
    Iron        UMETA(DisplayName = "Iron")
};

/**
 * Reserved for future per-hero movement tuning. Nothing consumes this yet:
 * the shared character movement component keeps its own defaults until a
 * later step explicitly reads these values.
 */
USTRUCT(BlueprintType)
struct FSRHeroMovementConfig
{
    GENERATED_BODY()

    /** Reserved: multiplier over the shared run speed once movement modifiers are implemented. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRHero|Movement", meta=(ClampMin="0.0", ClampMax="10.0", UIMin="0.1", UIMax="5.0"))
    float RunSpeedScale = 1.f;

    /** Reserved: multiplier over the shared sprint speed once movement modifiers are implemented. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRHero|Movement", meta=(ClampMin="0.0", ClampMax="10.0", UIMin="0.1", UIMax="5.0"))
    float SprintSpeedScale = 1.f;

    /** Reserved: multiplier over the shared crouch speed once movement modifiers are implemented. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRHero|Movement", meta=(ClampMin="0.0", ClampMax="10.0", UIMin="0.1", UIMax="5.0"))
    float CrouchSpeedScale = 1.f;

    /** Reserved: multiplier over the shared jump velocity once movement modifiers are implemented. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRHero|Movement", meta=(ClampMin="0.0", ClampMax="10.0", UIMin="0.1", UIMax="5.0"))
    float JumpVelocityScale = 1.f;
};

/**
 * Hero ability slots. Each slot accepts one Gameplay Ability class; the shared
 * character's ability system grants whatever is configured here on the server
 * and activates it through the matching input tag. Empty slots stay unused.
 */
USTRUCT(BlueprintType)
struct FSRHeroAbilitySlots
{
    GENERATED_BODY()

    /** Q — Ability 1. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRHero|Abilities",
        meta=(AllowedClasses="/Script/GameplayAbilities.GameplayAbility", DisplayName="Ability Q"))
    TSubclassOf<UGameplayAbility> AbilityQ;

    /** E — Ability 2. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRHero|Abilities",
        meta=(AllowedClasses="/Script/GameplayAbilities.GameplayAbility", DisplayName="Ability E"))
    TSubclassOf<UGameplayAbility> AbilityE;

    /** C — Ability 3. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRHero|Abilities",
        meta=(AllowedClasses="/Script/GameplayAbilities.GameplayAbility", DisplayName="Ability C"))
    TSubclassOf<UGameplayAbility> AbilityC;

    /** X — Ultimate. Fully charged activation and charge consumption are enforced by USRGameplayAbility_Ultimate. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRHero|Abilities",
        meta=(AllowedClasses="/Script/GameplayAbilities.GameplayAbility", DisplayName="Ability X (Ultimate)"))
    TSubclassOf<UGameplayAbility> AbilityX;

    /** Returns the ability class bound to a slot input tag (Input.Ability.Q/E/C/X); null when empty or not a slot tag. */
    TSubclassOf<UGameplayAbility> GetAbilityForSlot(FGameplayTag InputTag) const
    {
        if (InputTag == SRGameplayTags::Tag_Input_Ability_Q) return AbilityQ;
        if (InputTag == SRGameplayTags::Tag_Input_Ability_E) return AbilityE;
        if (InputTag == SRGameplayTags::Tag_Input_Ability_C) return AbilityC;
        if (InputTag == SRGameplayTags::Tag_Input_Ability_X) return AbilityX;
        return nullptr;
    }
};

/**
 * Shared, combat-agnostic description of one hero: identity, appearance,
 * ability slots and reserved movement tuning. Characters read this at
 * BeginPlay to apply their mesh, materials and Animation Blueprint, and the
 * ability system reads the slots on the server. Gameplay Effects used by an
 * ability live on the ability classes; nothing here hardcodes a hero's
 * behavior.
 */
UCLASS(BlueprintType)
class PSGAMEPLAY_API USRHeroDataAsset : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    // ------------------------------------------------------------------
    // Identity
    // ------------------------------------------------------------------

    /** Stable identifier, unique per hero (e.g. "Zyrex"). Also used as the primary asset id. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRHero|Identity", meta=(DisplayName="Hero ID"))
    FName HeroID = NAME_None;

    /** Name shown in UI (HUD, roster, respawn screens). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRHero|Identity", meta=(DisplayName="Display Name"))
    FText DisplayName = NSLOCTEXT("SRHero", "DefaultDisplayName", "Unnamed Hero");

    /** Short flavor title shown under the display name ("The Stormrunner"). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRHero|Identity", meta=(DisplayName="Hero Title"))
    FText HeroTitle;

    /** Longer description for roster/select screens. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRHero|Identity", meta=(DisplayName="Description"))
    FText Description;

    /** Gameplay archetype of this hero. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRHero|Identity", meta=(DisplayName="Hero Role"))
    ESRHeroRole HeroRole = ESRHeroRole::Duelist;

    /** Elemental identity of this hero. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRHero|Identity", meta=(DisplayName="Hero Element"))
    ESRHeroElement HeroElement = ESRHeroElement::Electricity;

    // ------------------------------------------------------------------
    // Appearance
    // ------------------------------------------------------------------

    /**
     * Skeletal mesh applied to the character at BeginPlay. Soft-referenced so
     * editor tools that only read identity data do not force the mesh to load.
     * Leave unset to keep the mesh inherited from the character Blueprint.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRHero|Appearance",
        meta=(AllowedClasses="/Script/Engine.SkeletalMesh", DisplayName="Character Skeletal Mesh"))
    TSoftObjectPtr<USkeletalMesh> CharacterSkeletalMesh;

    /**
     * Animation Blueprint instantiated on the character mesh at BeginPlay.
     * Leave unset to keep the Animation Blueprint inherited from the
     * character Blueprint.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRHero|Appearance",
        meta=(AllowedClasses="/Script/Engine.AnimInstance", DisplayName="Animation Blueprint Class"))
    TSoftClassPtr<UAnimInstance> AnimationBlueprintClass;

    /**
     * Optional material list applied by slot index on the character mesh.
     * Entries beyond the mesh's material slot count are skipped with a warning.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRHero|Appearance",
        meta=(AllowedClasses="/Script/Engine.MaterialInterface", DisplayName="Material Overrides"))
    TArray<TSoftObjectPtr<UMaterialInterface>> MaterialOverrides;

    /** Optional portrait image (texture) for roster and HUD use. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRHero|Appearance",
        meta=(AllowedClasses="/Script/Engine.Texture2D", DisplayName="Portrait"))
    TSoftObjectPtr<UTexture2D> Portrait;

    // ------------------------------------------------------------------
    // Abilities
    // ------------------------------------------------------------------

    /** Q/E/C/X ability slots granted to this hero by the shared ability system on the server. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRHero|Abilities", meta=(DisplayName="Ability Slots"))
    FSRHeroAbilitySlots AbilitySlots;

    // ------------------------------------------------------------------
    // Movement (reserved)
    // ------------------------------------------------------------------

    /** Reserved per-hero movement tuning. Not applied by the current character movement code. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SRHero|Movement", meta=(DisplayName="Movement Config"))
    FSRHeroMovementConfig MovementConfig;

    // ------------------------------------------------------------------
    // Queries
    // ------------------------------------------------------------------

    /** True when this asset carries the minimum identity needed to spawn a hero. */
    UFUNCTION(BlueprintPure, Category="SRHero")
    bool IsSRHeroDataValid() const
    {
        return !HeroID.IsNone() && !DisplayName.IsEmpty();
    }

    /** Primary asset id "SRHero:<HeroID>" so future systems can find heroes through the Asset Manager. */
    virtual FPrimaryAssetId GetPrimaryAssetId() const override
    {
        return FPrimaryAssetId(TEXT("SRHero"), HeroID);
    }
};
