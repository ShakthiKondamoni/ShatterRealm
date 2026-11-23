#include "Data/SRHeroRoster.h"
#include "Abilities/SRAbilities.h"
#include "Abilities/SRVeyraAbilities.h"
#include "Abilities/SRRosterAbilities.h"
#include "Character/PSBaseCharacter.h"

const TArray<FSRHeroDefinition>& SRHeroRoster::All()
{
    // Unspecified balance values are initial editable defaults, never per-shot constants.
    static const TArray<FSRHeroDefinition> Heroes = {
        { TEXT("Zyrex"), ESRHeroRole::Duelist, ESRHeroElement::Electricity, FText::FromString(TEXT("Spark Shot")), FText::FromString(TEXT("Right-Hand Crystal Gauntlet")), 22.f, 12000.f, 7.f, 30, 1.8f, { USRGameplayAbility_ArcBolt::StaticClass(), USRGameplayAbility_StaticLeap::StaticClass(), USRGameplayAbility_Overcharge::StaticClass(), USRGameplayAbility_Stormpiercer::StaticClass() } },
        { TEXT("Veyra"), ESRHeroRole::Duelist, ESRHeroElement::Void, FText::FromString(TEXT("Void Blades")), FText::FromString(TEXT("Void Dagger")), 28.f, 10000.f, 4.f, 24, 2.f, { USRGameplayAbility_BlinkCut::StaticClass(), USRGameplayAbility_RiftDagger::StaticClass(), USRGameplayAbility_VoidSnare::StaticClass(), USRGameplayAbility_RiftReaper::StaticClass() } },
        { TEXT("Kaizen"), ESRHeroRole::Duelist, ESRHeroElement::Fire, FText::FromString(TEXT("Ember Fists")), FText::FromString(TEXT("Ember Gauntlets")), 24.f, 10000.f, 5.f, 28, 2.f, { USRGameplayAbility_InfernoPunch::StaticClass(), USRGameplayAbility_FlameStep::StaticClass(), USRGameplayAbility_HeatSurge::StaticClass(), USRGameplayAbility_PhoenixMeteorfall::StaticClass() } },
        { TEXT("Nyvara"), ESRHeroRole::Duelist, ESRHeroElement::Illusion, FText::FromString(TEXT("Spectral Blades")), FText::FromString(TEXT("Mirror Fan")), 23.f, 12000.f, 5.f, 26, 1.9f, { USRGameplayAbility_MirrorDouble::StaticClass(), USRGameplayAbility_FadeStep::StaticClass(), USRGameplayAbility_GlassScreen::StaticClass(), USRGameplayAbility_PhantomProcession::StaticClass() } },
        { TEXT("Torrik"), ESRHeroRole::Duelist, ESRHeroElement::Earth, FText::FromString(TEXT("Stone Fists")), FText::FromString(TEXT("Stone Bracers")), 32.f, 11000.f, 3.f, 20, 2.2f, { USRGameplayAbility_StoneKnuckle::StaticClass(), USRGameplayAbility_FaultDash::StaticClass(), USRGameplayAbility_QuakeBurst::StaticClass(), USRGameplayAbility_FaultlineTitan::StaticClass() } },
        { TEXT("Elira"), ESRHeroRole::Healer, ESRHeroElement::Nature, FText::FromString(TEXT("Life Spark")), FText::FromString(TEXT("Flowering Staff")), 19.f, 12000.f, 5.f, 30, 1.8f, { USRGameplayAbility_VitalPulse::StaticClass(), USRGameplayAbility_BriarSnare::StaticClass(), USRGameplayAbility_BarkWard::StaticClass(), USRGameplayAbility_LastSeed::StaticClass() } },
        { TEXT("Mirelle"), ESRHeroRole::Healer, ESRHeroElement::Water, FText::FromString(TEXT("Water Lance")), FText::FromString(TEXT("Water Vessel")), 20.f, 12000.f, 5.f, 28, 1.9f, { USRGameplayAbility_TidalMend::StaticClass(), USRGameplayAbility_MistVeil::StaticClass(), USRGameplayAbility_Undertow::StaticClass(), USRGameplayAbility_TidalCurtain::StaticClass() } },
        { TEXT("Solira"), ESRHeroRole::Healer, ESRHeroElement::Solar, FText::FromString(TEXT("Solar Ray")), FText::FromString(TEXT("Sun Disk")), 21.f, 13000.f, 5.f, 28, 2.f, { USRGameplayAbility_Sunbeam::StaticClass(), USRGameplayAbility_RadiantWard::StaticClass(), USRGameplayAbility_SolarFlare::StaticClass(), USRGameplayAbility_DawnIntervention::StaticClass() } },
        { TEXT("Nymel"), ESRHeroRole::Healer, ESRHeroElement::Nature, FText::FromString(TEXT("Thorn Bolt")), FText::FromString(TEXT("Seed Staff")), 20.f, 12000.f, 4.5f, 28, 1.8f, { USRGameplayAbility_HealingSeedling::StaticClass(), USRGameplayAbility_ThornGuard::StaticClass(), USRGameplayAbility_RootCage::StaticClass(), USRGameplayAbility_WorldseedDominion::StaticClass() } },
        { TEXT("Caeli"), ESRHeroRole::Healer, ESRHeroElement::Time, FText::FromString(TEXT("Chrono Orb")), FText::FromString(TEXT("Chronometer")), 21.f, 13000.f, 4.5f, 26, 1.9f, { USRGameplayAbility_RewindMend::StaticClass(), USRGameplayAbility_TimeBubble::StaticClass(), USRGameplayAbility_TimeFracture::StaticClass(), USRGameplayAbility_ParadoxEcho::StaticClass() } },
        { TEXT("Brontar"), ESRHeroRole::Tank, ESRHeroElement::Iron, FText::FromString(TEXT("Iron Barrage")), FText::FromString(TEXT("Heavy Iron Gauntlets")), 36.f, 11000.f, 2.7f, 20, 2.4f, { USRGameplayAbility_IronRam::StaticClass(), USRGameplayAbility_BulwarkPlate::StaticClass(), USRGameplayAbility_FortressStance::StaticClass(), USRGameplayAbility_IronVanguard::StaticClass() } },
        { TEXT("Korrum"), ESRHeroRole::Tank, ESRHeroElement::Crystal, FText::FromString(TEXT("Prism Bolt")), FText::FromString(TEXT("Crystal Hammer")), 33.f, 12000.f, 3.f, 22, 2.2f, { USRGameplayAbility_ShardSpike::StaticClass(), USRGameplayAbility_PrismWall::StaticClass(), USRGameplayAbility_CrystalBind::StaticClass(), USRGameplayAbility_PrismSkyfall::StaticClass() } }
    };
    return Heroes;
}
const FSRHeroDefinition* SRHeroRoster::Find(FName HeroID)
{
    for (const FSRHeroDefinition& Definition : All()) if (Definition.ID == HeroID) return &Definition;
    return nullptr;
}
FString SRHeroRoster::HeroAssetRoot(FName HeroID)
{ return TEXT("/Game/ProjectS/Characters/Shared/Heroes/") + HeroID.ToString() + TEXT("/"); }
UClass* SRHeroRoster::LoadPawnClass(FName HeroID)
{
    if (!Find(HeroID)) return nullptr;
    const FString Name = TEXT("BP_") + HeroID.ToString();
    UClass* Loaded = LoadClass<APawn>(nullptr, *(HeroAssetRoot(HeroID) + Name + TEXT(".") + Name + TEXT("_C")));
    return Loaded && Loaded->IsChildOf(APSBaseCharacter::StaticClass()) ? Loaded : nullptr;
}
