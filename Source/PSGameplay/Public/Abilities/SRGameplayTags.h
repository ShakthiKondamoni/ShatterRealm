#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"
#include "NativeGameplayTags.h"

/** Logging for the shared ability foundation (grants, input routing, activation). */
DECLARE_LOG_CATEGORY_EXTERN(LogProjectSAbilities, Log, All);

/**
 * Native gameplay tags for the shared ability foundation. Native tags need no
 * project ini registration and are always valid; append-only ordering applies.
 */

namespace SRGameplayTags
{
PSGAMEPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tag_State_Warded);
PSGAMEPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tag_Data_HeroDamage);
PSGAMEPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tag_Data_HeroFireRate);
PSGAMEPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tag_Data_HeroRecharge);
PSGAMEPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tag_Data_HeroDamageTaken);
PSGAMEPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tag_Data_HeroMovement);
PSGAMEPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tag_Data_HeroKnockback);
PSGAMEPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tag_State_ControlImmune);
PSGAMEPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tag_State_DamageImmune);
PSGAMEPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tag_State_Stunned);
PSGAMEPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tag_State_Downed);
PSGAMEPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tag_State_Concealed);
PSGAMEPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tag_State_Blinded);
PSGAMEPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tag_State_Nearsighted);
PSGAMEPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tag_State_TemporalEcho);
// One input tag per ability slot. The shared character forwards Enhanced Input
// to the ASC using these tags; hero data assets assign abilities per slot.
PSGAMEPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tag_Input_Ability_Q);
PSGAMEPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tag_Input_Ability_E);
PSGAMEPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tag_Input_Ability_C);
PSGAMEPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tag_Input_Ability_X);

// SetByCaller data tags shared by the reusable Gameplay Effects.
PSGAMEPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tag_Data_Damage);
PSGAMEPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tag_Data_Heal);
PSGAMEPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tag_Data_Shield);
PSGAMEPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tag_Data_Cooldown);

// Legacy fallback cooldown for custom abilities, plus independent Q/E/C/X tags.
PSGAMEPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tag_SR_Cooldown);
PSGAMEPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tag_SR_Cooldown_Q);
PSGAMEPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tag_SR_Cooldown_E);
PSGAMEPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tag_SR_Cooldown_C);
PSGAMEPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tag_SR_Cooldown_X);


// Buff tag applied by the Overcharge duration effect; the Combat Component
// reads this single authoritative marker for its effective fire/recharge rates.
PSGAMEPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tag_Status_Buff_Overcharge);

// SetByCaller data for the shock-slow effect (applied by projectiles/abilities).
PSGAMEPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tag_Data_SlowDuration);
PSGAMEPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tag_Data_SlowMultiplier);

// SetByCaller duration for the root effect (Void Snare / Rift Reaper).
PSGAMEPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tag_Data_RootDuration);

// Rooted state granted by the root Gameplay Effect; the character movement
// component reads this to block walking/sprint/slide/vault while rooted.
PSGAMEPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tag_State_Rooted);

// Ping categories replicated through each player's PlayerState.
PSGAMEPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tag_SR_Ping_Location);
PSGAMEPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tag_SR_Ping_Danger);
PSGAMEPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tag_SR_Ping_Reveal);
}
