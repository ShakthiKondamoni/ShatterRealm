#include "Abilities/SRRosterAbilities.h"
#include "Abilities/SRGameplayTags.h"

USRGameplayAbility_InfernoPunch::USRGameplayAbility_InfernoPunch()
{ ConfigureAction(ESRHeroAction::InfernoPunch, TEXT("Inferno Punch"), SRGameplayTags::Tag_SR_Cooldown_Q, 8.f, false); }

USRGameplayAbility_FlameStep::USRGameplayAbility_FlameStep()
{ ConfigureAction(ESRHeroAction::FlameStep, TEXT("Flame Step"), SRGameplayTags::Tag_SR_Cooldown_E, 10.f, false); }

USRGameplayAbility_HeatSurge::USRGameplayAbility_HeatSurge()
{ ConfigureAction(ESRHeroAction::HeatSurge, TEXT("Heat Surge"), SRGameplayTags::Tag_SR_Cooldown_C, 18.f, false); }

USRGameplayAbility_PhoenixMeteorfall::USRGameplayAbility_PhoenixMeteorfall()
{ ConfigureAction(ESRHeroAction::PhoenixMeteorfall, TEXT("Phoenix Meteorfall"), SRGameplayTags::Tag_SR_Cooldown_X, 0.f, true); }

USRGameplayAbility_MirrorDouble::USRGameplayAbility_MirrorDouble()
{ ConfigureAction(ESRHeroAction::MirrorDouble, TEXT("Mirror Double"), SRGameplayTags::Tag_SR_Cooldown_Q, 12.f, false); }

USRGameplayAbility_FadeStep::USRGameplayAbility_FadeStep()
{ ConfigureAction(ESRHeroAction::FadeStep, TEXT("Fade Step"), SRGameplayTags::Tag_SR_Cooldown_E, 14.f, false); }

USRGameplayAbility_GlassScreen::USRGameplayAbility_GlassScreen()
{ ConfigureAction(ESRHeroAction::GlassScreen, TEXT("Glass Screen"), SRGameplayTags::Tag_SR_Cooldown_C, 16.f, false); }

USRGameplayAbility_PhantomProcession::USRGameplayAbility_PhantomProcession()
{ ConfigureAction(ESRHeroAction::PhantomProcession, TEXT("Phantom Procession"), SRGameplayTags::Tag_SR_Cooldown_X, 0.f, true); }

USRGameplayAbility_StoneKnuckle::USRGameplayAbility_StoneKnuckle()
{ ConfigureAction(ESRHeroAction::StoneKnuckle, TEXT("Stone Knuckle"), SRGameplayTags::Tag_SR_Cooldown_Q, 8.f, false); }

USRGameplayAbility_FaultDash::USRGameplayAbility_FaultDash()
{ ConfigureAction(ESRHeroAction::FaultDash, TEXT("Fault Dash"), SRGameplayTags::Tag_SR_Cooldown_E, 12.f, false); }

USRGameplayAbility_QuakeBurst::USRGameplayAbility_QuakeBurst()
{ ConfigureAction(ESRHeroAction::QuakeBurst, TEXT("Quake Burst"), SRGameplayTags::Tag_SR_Cooldown_C, 14.f, false); }

USRGameplayAbility_FaultlineTitan::USRGameplayAbility_FaultlineTitan()
{ ConfigureAction(ESRHeroAction::FaultlineTitan, TEXT("Faultline Titan"), SRGameplayTags::Tag_SR_Cooldown_X, 0.f, true); }

USRGameplayAbility_VitalPulse::USRGameplayAbility_VitalPulse()
{ ConfigureAction(ESRHeroAction::VitalPulse, TEXT("Vital Pulse"), SRGameplayTags::Tag_SR_Cooldown_Q, 8.f, false); }

USRGameplayAbility_BriarSnare::USRGameplayAbility_BriarSnare()
{ ConfigureAction(ESRHeroAction::BriarSnare, TEXT("Briar Snare"), SRGameplayTags::Tag_SR_Cooldown_E, 12.f, false); }

USRGameplayAbility_BarkWard::USRGameplayAbility_BarkWard()
{ ConfigureAction(ESRHeroAction::BarkWard, TEXT("Bark Ward"), SRGameplayTags::Tag_SR_Cooldown_C, 14.f, false); }

USRGameplayAbility_LastSeed::USRGameplayAbility_LastSeed()
{ ConfigureAction(ESRHeroAction::LastSeed, TEXT("Last Seed"), SRGameplayTags::Tag_SR_Cooldown_X, 0.f, true); }

USRGameplayAbility_TidalMend::USRGameplayAbility_TidalMend()
{ ConfigureAction(ESRHeroAction::TidalMend, TEXT("Tidal Mend"), SRGameplayTags::Tag_SR_Cooldown_Q, 9.f, false); }

USRGameplayAbility_MistVeil::USRGameplayAbility_MistVeil()
{ ConfigureAction(ESRHeroAction::MistVeil, TEXT("Mist Veil"), SRGameplayTags::Tag_SR_Cooldown_E, 14.f, false); }

USRGameplayAbility_Undertow::USRGameplayAbility_Undertow()
{ ConfigureAction(ESRHeroAction::Undertow, TEXT("Undertow"), SRGameplayTags::Tag_SR_Cooldown_C, 14.f, false); }

USRGameplayAbility_TidalCurtain::USRGameplayAbility_TidalCurtain()
{ ConfigureAction(ESRHeroAction::TidalCurtain, TEXT("Tidal Curtain"), SRGameplayTags::Tag_SR_Cooldown_X, 0.f, true); }

USRGameplayAbility_Sunbeam::USRGameplayAbility_Sunbeam()
{ ConfigureAction(ESRHeroAction::Sunbeam, TEXT("Sunbeam"), SRGameplayTags::Tag_SR_Cooldown_Q, 8.f, false); }

USRGameplayAbility_RadiantWard::USRGameplayAbility_RadiantWard()
{ ConfigureAction(ESRHeroAction::RadiantWard, TEXT("Radiant Ward"), SRGameplayTags::Tag_SR_Cooldown_E, 12.f, false); }

USRGameplayAbility_SolarFlare::USRGameplayAbility_SolarFlare()
{ ConfigureAction(ESRHeroAction::SolarFlare, TEXT("Solar Flare"), SRGameplayTags::Tag_SR_Cooldown_C, 14.f, false); }

USRGameplayAbility_DawnIntervention::USRGameplayAbility_DawnIntervention()
{ ConfigureAction(ESRHeroAction::DawnIntervention, TEXT("Dawn Intervention"), SRGameplayTags::Tag_SR_Cooldown_X, 0.f, true); }

USRGameplayAbility_HealingSeedling::USRGameplayAbility_HealingSeedling()
{ ConfigureAction(ESRHeroAction::HealingSeedling, TEXT("Healing Seedling"), SRGameplayTags::Tag_SR_Cooldown_Q, 10.f, false); }

USRGameplayAbility_ThornGuard::USRGameplayAbility_ThornGuard()
{ ConfigureAction(ESRHeroAction::ThornGuard, TEXT("Thorn Guard"), SRGameplayTags::Tag_SR_Cooldown_E, 14.f, false); }

USRGameplayAbility_RootCage::USRGameplayAbility_RootCage()
{ ConfigureAction(ESRHeroAction::RootCage, TEXT("Root Cage"), SRGameplayTags::Tag_SR_Cooldown_C, 16.f, false); }

USRGameplayAbility_WorldseedDominion::USRGameplayAbility_WorldseedDominion()
{ ConfigureAction(ESRHeroAction::WorldseedDominion, TEXT("Worldseed Dominion"), SRGameplayTags::Tag_SR_Cooldown_X, 0.f, true); }

USRGameplayAbility_RewindMend::USRGameplayAbility_RewindMend()
{ ConfigureAction(ESRHeroAction::RewindMend, TEXT("Rewind Mend"), SRGameplayTags::Tag_SR_Cooldown_Q, 10.f, false); }

USRGameplayAbility_TimeBubble::USRGameplayAbility_TimeBubble()
{ ConfigureAction(ESRHeroAction::TimeBubble, TEXT("Time Bubble"), SRGameplayTags::Tag_SR_Cooldown_E, 14.f, false); }

USRGameplayAbility_TimeFracture::USRGameplayAbility_TimeFracture()
{ ConfigureAction(ESRHeroAction::TimeFracture, TEXT("Time Fracture"), SRGameplayTags::Tag_SR_Cooldown_C, 12.f, false); }

USRGameplayAbility_ParadoxEcho::USRGameplayAbility_ParadoxEcho()
{ ConfigureAction(ESRHeroAction::ParadoxEcho, TEXT("Paradox Echo"), SRGameplayTags::Tag_SR_Cooldown_X, 0.f, true); }

USRGameplayAbility_IronRam::USRGameplayAbility_IronRam()
{ ConfigureAction(ESRHeroAction::IronRam, TEXT("Iron Ram"), SRGameplayTags::Tag_SR_Cooldown_Q, 10.f, false); }

USRGameplayAbility_BulwarkPlate::USRGameplayAbility_BulwarkPlate()
{ ConfigureAction(ESRHeroAction::BulwarkPlate, TEXT("Bulwark Plate"), SRGameplayTags::Tag_SR_Cooldown_E, 14.f, false); }

USRGameplayAbility_FortressStance::USRGameplayAbility_FortressStance()
{ ConfigureAction(ESRHeroAction::FortressStance, TEXT("Fortress Stance"), SRGameplayTags::Tag_SR_Cooldown_C, 18.f, false); }

USRGameplayAbility_IronVanguard::USRGameplayAbility_IronVanguard()
{ ConfigureAction(ESRHeroAction::IronVanguard, TEXT("Iron Vanguard"), SRGameplayTags::Tag_SR_Cooldown_X, 0.f, true); }

USRGameplayAbility_ShardSpike::USRGameplayAbility_ShardSpike()
{ ConfigureAction(ESRHeroAction::ShardSpike, TEXT("Shard Spike"), SRGameplayTags::Tag_SR_Cooldown_Q, 10.f, false); }

USRGameplayAbility_PrismWall::USRGameplayAbility_PrismWall()
{ ConfigureAction(ESRHeroAction::PrismWall, TEXT("Prism Wall"), SRGameplayTags::Tag_SR_Cooldown_E, 14.f, false); }

USRGameplayAbility_CrystalBind::USRGameplayAbility_CrystalBind()
{ ConfigureAction(ESRHeroAction::CrystalBind, TEXT("Crystal Bind"), SRGameplayTags::Tag_SR_Cooldown_C, 14.f, false); }

USRGameplayAbility_PrismSkyfall::USRGameplayAbility_PrismSkyfall()
{ ConfigureAction(ESRHeroAction::PrismSkyfall, TEXT("Prism Skyfall"), SRGameplayTags::Tag_SR_Cooldown_X, 0.f, true); }
