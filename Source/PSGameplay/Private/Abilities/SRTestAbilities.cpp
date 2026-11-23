#include "Abilities/SRTestAbilities.h"
#include "Abilities/SRGameplayEffects.h"
#include "Abilities/SRGameplayTags.h"

USRTestAbility::USRTestAbility()
{
    ActivationEffect = UGE_SRDamage::StaticClass();
    ActivationEffectDataTag = SRGameplayTags::Tag_Data_Damage;
    ActivationEffectMagnitude = 15.f;
    CooldownDuration = 3.f;
}

USRTestUltimate::USRTestUltimate()
{
    // Observable activation effect (+10 shield) so the automation test can tell an
    // activated ultimate from a rejected one; no cooldown: the charge gate is the
    // activation cost being verified.
    ActivationEffect = UGE_SRShieldRestore::StaticClass();
    ActivationEffectDataTag = SRGameplayTags::Tag_Data_Shield;
    ActivationEffectMagnitude = 10.f;
    CooldownDuration = 0.f;
}
