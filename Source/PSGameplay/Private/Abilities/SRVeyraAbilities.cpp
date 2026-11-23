#include "Abilities/SRVeyraAbilities.h"
#include "Abilities/SRGameplayTags.h"

USRGameplayAbility_BlinkCut::USRGameplayAbility_BlinkCut()
{ ConfigureAction(ESRHeroAction::BlinkCut, TEXT("Blink Cut"), SRGameplayTags::Tag_SR_Cooldown_Q, 10.f, false); }

USRGameplayAbility_RiftDagger::USRGameplayAbility_RiftDagger()
{ ConfigureAction(ESRHeroAction::RiftDagger, TEXT("Rift Dagger"), SRGameplayTags::Tag_SR_Cooldown_E, 8.f, false); }

USRGameplayAbility_VoidSnare::USRGameplayAbility_VoidSnare()
{ ConfigureAction(ESRHeroAction::VoidSnare, TEXT("Void Snare"), SRGameplayTags::Tag_SR_Cooldown_C, 16.f, false); }

USRGameplayAbility_RiftReaper::USRGameplayAbility_RiftReaper()
{ ConfigureAction(ESRHeroAction::RiftReaper, TEXT("Rift Reaper"), SRGameplayTags::Tag_SR_Cooldown_X, 0.f, true); }
