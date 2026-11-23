#include "Abilities/SRGameplayTags.h"

DEFINE_LOG_CATEGORY(LogProjectSAbilities);

// Slot input tags (see SRGameplayTags.h).
namespace SRGameplayTags
{
UE_DEFINE_GAMEPLAY_TAG(Tag_State_Warded, "SR.State.Warded");
UE_DEFINE_GAMEPLAY_TAG(Tag_Data_HeroDamage, "SR.Data.HeroDamage");
UE_DEFINE_GAMEPLAY_TAG(Tag_Data_HeroFireRate, "SR.Data.HeroFireRate");
UE_DEFINE_GAMEPLAY_TAG(Tag_Data_HeroRecharge, "SR.Data.HeroRecharge");
UE_DEFINE_GAMEPLAY_TAG(Tag_Data_HeroDamageTaken, "SR.Data.HeroDamageTaken");
UE_DEFINE_GAMEPLAY_TAG(Tag_Data_HeroMovement, "SR.Data.HeroMovement");
UE_DEFINE_GAMEPLAY_TAG(Tag_Data_HeroKnockback, "SR.Data.HeroKnockback");
UE_DEFINE_GAMEPLAY_TAG(Tag_State_ControlImmune, "SR.State.ControlImmune");
UE_DEFINE_GAMEPLAY_TAG(Tag_State_DamageImmune, "SR.State.DamageImmune");
UE_DEFINE_GAMEPLAY_TAG(Tag_State_Stunned, "SR.State.Stunned");
UE_DEFINE_GAMEPLAY_TAG(Tag_State_Downed, "SR.State.Downed");
UE_DEFINE_GAMEPLAY_TAG(Tag_State_Concealed, "SR.State.Concealed");
UE_DEFINE_GAMEPLAY_TAG(Tag_State_Blinded, "SR.State.Blinded");
UE_DEFINE_GAMEPLAY_TAG(Tag_State_Nearsighted, "SR.State.Nearsighted");
UE_DEFINE_GAMEPLAY_TAG(Tag_State_TemporalEcho, "SR.State.TemporalEcho");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(Tag_Input_Ability_Q, "Input.Ability.Q", "Ability slot 1 (Q key)");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(Tag_Input_Ability_E, "Input.Ability.E", "Ability slot 2 (E key)");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(Tag_Input_Ability_C, "Input.Ability.C", "Ability slot 3 (C key)");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(Tag_Input_Ability_X, "Input.Ability.X", "Ultimate slot (X key)");

// SetByCaller data tags (see SRGameplayTags.h).
UE_DEFINE_GAMEPLAY_TAG_COMMENT(Tag_Data_Damage, "Data.Damage", "Damage amount passed by the instigating Gameplay Effect");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(Tag_Data_Heal, "Data.Heal", "Healing amount passed by the instigating Gameplay Effect");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(Tag_Data_Shield, "Data.Shield", "Shield restoration amount passed by the instigating Gameplay Effect");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(Tag_Data_Cooldown, "Data.Cooldown", "Cooldown duration passed by the instigating Gameplay Effect");

UE_DEFINE_GAMEPLAY_TAG_COMMENT(Tag_SR_Cooldown, "SR.Cooldown", "Shared ability-cooldown tag granted by GE_SRAbilityCooldown");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(Tag_SR_Cooldown_Q, "SR.Cooldown.Q", "Independent cooldown for Q ability slot");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(Tag_SR_Cooldown_E, "SR.Cooldown.E", "Independent cooldown for E ability slot");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(Tag_SR_Cooldown_C, "SR.Cooldown.C", "Independent cooldown for C ability slot");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(Tag_SR_Cooldown_X, "SR.Cooldown.X", "Independent cooldown for X ability slot");


UE_DEFINE_GAMEPLAY_TAG_COMMENT(Tag_Status_Buff_Overcharge, "Status.Buff.Overcharge", "Overcharge combat buff is active");

UE_DEFINE_GAMEPLAY_TAG_COMMENT(Tag_Data_SlowDuration, "Data.SlowDuration", "Slow effect duration in seconds");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(Tag_Data_SlowMultiplier, "Data.SlowMultiplier", "Movement speed multiplier while slowed");

UE_DEFINE_GAMEPLAY_TAG_COMMENT(Tag_Data_RootDuration, "SR.Data.RootDuration", "SetByCaller duration for the root effect");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(Tag_State_Rooted, "SR.State.Rooted", "Character movement blocked (rooted); aiming and firing still allowed");

UE_DEFINE_GAMEPLAY_TAG_COMMENT(Tag_SR_Ping_Location, "SR.Ping.Location", "Team location ping");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(Tag_SR_Ping_Danger, "SR.Ping.Danger", "Team danger ping");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(Tag_SR_Ping_Reveal, "SR.Ping.Reveal", "Enemy last-known-position reveal");
}
