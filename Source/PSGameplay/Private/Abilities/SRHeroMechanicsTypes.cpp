#include "Abilities/SRHeroMechanicsTypes.h"

FSRHeroActionConfig SRDefaultActionConfig(ESRHeroAction Action)
{
    FSRHeroActionConfig C;
    C.Action = Action;
    switch (Action)
    {
    case ESRHeroAction::BlinkCut: C.Range = 800.f; C.Width = 300.f; C.Height = 200.f; C.Damage = 40.f; break;
    case ESRHeroAction::RiftDagger: C.Range = 8000.f; C.Radius = 125.f; C.Height = 400.f; C.Speed = 5000.f; C.Damage = 25.f; break;
    case ESRHeroAction::VoidSnare: C.Range = 2000.f; C.Radius = 400.f; C.Duration = 5.f; C.EffectDuration = 1.5f; C.Damage = 0.f; break;
    case ESRHeroAction::RiftReaper: C.Range = 7000.f; C.Width = 1600.f; C.Height = 300.f; C.Speed = 2500.f; C.Damage = 85.f; C.EffectDuration = 3.f; break;
    case ESRHeroAction::InfernoPunch: C.Range = 400.f; C.Width = 650.f; C.Height = 250.f; C.Damage = 45.f; C.Knockback = 650.f; break;
    case ESRHeroAction::FlameStep: C.Range = 1000.f; C.Duration = 0.4f; C.Damage = 0.f; break;
    case ESRHeroAction::HeatSurge: C.Duration = 6.f; C.DamageScale = 1.2f; C.FireRateScale = 1.2f; C.RechargeScale = 1.15f; break;
    case ESRHeroAction::PhoenixMeteorfall: C.Range = 25000.f; C.Radius = 5000.f; C.WarningDuration = 3.f; C.Damage = 140.f; C.EdgeDamage = 45.f; break;
    case ESRHeroAction::MirrorDouble: C.Range = 1800.f; C.Speed = 350.f; C.Duration = 6.f; C.StructureHealth = 100.f; break;
    case ESRHeroAction::FadeStep: C.Duration = 3.f; C.MovementScale = 1.15f; break;
    case ESRHeroAction::GlassScreen: C.Range = 1000.f; C.Width = 800.f; C.Height = 300.f; C.Duration = 8.f; break;
    case ESRHeroAction::PhantomProcession: C.Range = 10000.f; C.Duration = 15.f; C.Segments = 2; C.StructureHealth = 100.f; break;
    case ESRHeroAction::StoneKnuckle: C.Range = 350.f; C.Width = 450.f; C.Height = 250.f; C.Damage = 40.f; C.Knockback = 700.f; break;
    case ESRHeroAction::FaultDash: C.Range = 1000.f; C.Width = 300.f; C.Height = 200.f; C.Duration = 0.5f; C.Damage = 20.f; C.Knockback = 600.f; break;
    case ESRHeroAction::QuakeBurst: C.Radius = 550.f; C.Damage = 35.f; C.StunDuration = 0.5f; break;
    case ESRHeroAction::FaultlineTitan: C.Range = 5000.f; C.Width = 1000.f; C.Height = 240.f; C.Speed = 1500.f; C.Damage = 65.f; C.Knockback = 600.f; C.LaunchHeight = 450.f; C.Duration = 6.f; C.Segments = 6; C.StructureHealth = 200.f; break;
    case ESRHeroAction::VitalPulse: C.Radius = 800.f; C.Healing = 30.f; break;
    case ESRHeroAction::BriarSnare: C.Range = 3000.f; C.Radius = 800.f; C.Speed = 1400.f; C.ProjectileGravity = 0.6f; C.Duration = 6.f; C.EffectDuration = 2.f; C.Branches = 5; break;
    case ESRHeroAction::BarkWard: C.Range = 1800.f; C.DamageTakenScale = 0.7f; C.Duration = 5.f; break;
    case ESRHeroAction::LastSeed: C.Range = 1800.f; C.Radius = 1000.f; C.ChannelDuration = 2.5f; C.Duration = 10.f; C.Healing = 12.f; C.DamageTakenScale = 0.7f; C.StructureHealth = 400.f; break;
    case ESRHeroAction::TidalMend: C.Range = 2500.f; C.Width = 800.f; C.Height = 150.f; C.Speed = 1000.f; C.Healing = 35.f; C.Damage = 0.f; break;
    case ESRHeroAction::MistVeil: C.Range = 1200.f; C.Width = 1000.f; C.Height = 500.f; C.Duration = 7.f; C.StructureHealth = 0.f; break;
    case ESRHeroAction::Undertow: C.Range = 2000.f; C.Radius = 600.f; C.Speed = 450.f; C.Duration = 4.f; C.Damage = 0.f; break;
    case ESRHeroAction::TidalCurtain: C.Range = 6500.f; C.Width = 3200.f; C.Height = 800.f; C.Speed = 1300.f; C.Damage = 60.f; C.StunDuration = 1.f; C.VisionDuration = 4.f; break;
    case ESRHeroAction::Sunbeam: C.Range = 1800.f; C.Healing = 20.f; C.ChannelDuration = 3.f; C.Duration = 3.f; C.Interval = 0.2f; break;
    case ESRHeroAction::RadiantWard: C.Range = 1800.f; C.WardHealth = 75.f; C.Duration = 6.f; break;
    case ESRHeroAction::SolarFlare: C.Range = 5000.f; C.Speed = 1200.f; C.ProjectileGravity = 1.f; C.Radius = 1200.f; C.EffectDuration = 3.f; break;
    case ESRHeroAction::DawnIntervention: C.Range = 1800.f; C.Radius = 1800.f; C.ChannelDuration = 3.f; C.Duration = 6.f; C.Healing = 60.f; C.WardHealth = 60.f; C.DamageTakenScale = 0.7f; break;
    case ESRHeroAction::HealingSeedling: C.Range = 2000.f; C.Radius = 600.f; C.Healing = 10.f; C.Duration = 10.f; C.StructureHealth = 200.f; break;
    case ESRHeroAction::ThornGuard: C.Range = 1800.f; C.DamageTakenScale = 0.7f; C.Duration = 5.f; break;
    case ESRHeroAction::RootCage: C.Range = 2000.f; C.Radius = 250.f; C.Duration = 6.f; C.EffectDuration = 2.f; C.Segments = 6; C.Height = 300.f; C.StructureHealth = 180.f; break;
    case ESRHeroAction::WorldseedDominion: C.Range = 3000.f; C.Radius = 1800.f; C.Duration = 12.f; C.Healing = 8.f; C.EffectDuration = 1.5f; C.Segments = 6; C.StructureHealth = 250.f; break;
    case ESRHeroAction::RewindMend: C.Range = 1800.f; C.Healing = 50.f; C.EffectDuration = 4.f; break;
    case ESRHeroAction::TimeBubble: C.Range = 2000.f; C.Radius = 600.f; C.Duration = 6.f; C.MovementScale = 0.4f; break;
    case ESRHeroAction::TimeFracture: C.Range = 2000.f; C.Radius = 600.f; C.Duration = 3.f; C.MovementScale = 0.65f; C.EffectDuration = 3.f; break;
    case ESRHeroAction::ParadoxEcho: C.Duration = 12.f; C.Range = 0.f; break;
    case ESRHeroAction::IronRam: C.Range = 1000.f; C.Width = 300.f; C.Height = 220.f; C.Duration = 0.65f; C.Damage = 45.f; C.Knockback = 700.f; break;
    case ESRHeroAction::BulwarkPlate: C.Range = 1000.f; C.Width = 700.f; C.Height = 300.f; C.Duration = 8.f; C.StructureHealth = 450.f; break;
    case ESRHeroAction::FortressStance: C.Duration = 5.f; C.DamageTakenScale = 0.5f; C.MovementScale = 0.65f; C.KnockbackScale = 0.2f; break;
    case ESRHeroAction::IronVanguard: C.Width = 2000.f; C.Height = 400.f; C.Range = 2500.f; C.Duration = 6.f; C.Speed = 416.6666666666667f; C.StructureHealth = 800.f; break;
    case ESRHeroAction::ShardSpike: C.Range = 3500.f; C.Speed = 1600.f; C.ProjectileGravity = 0.65f; C.Radius = 400.f; C.Height = 60.f; C.Duration = 3.f; C.Interval = 0.5f; C.Damage = 12.f; break;
    case ESRHeroAction::PrismWall: C.Range = 1500.f; C.Width = 1200.f; C.Height = 350.f; C.Segments = 4; C.StructureHealth = 200.f; C.Duration = 8.f; break;
    case ESRHeroAction::CrystalBind: C.Range = 3500.f; C.Speed = 2000.f; C.Radius = 450.f; C.EffectDuration = 2.5f; C.Duration = 0.25f; break;
    case ESRHeroAction::PrismSkyfall: C.Range = 18000.f; C.Radius = 800.f; C.WarningDuration = 2.f; C.Damage = 160.f; C.EdgeDamage = 50.f; break;
    }
    if (Action == ESRHeroAction::DawnIntervention) C.ReviveShield = 60.f;
    return C;
}
