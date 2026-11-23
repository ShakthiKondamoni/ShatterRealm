#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Character/PSBaseCharacter.h"
#include "GAS/PSAttributeSet.h"
#include "GAS/PSPlayerState.h"
#include "Abilities/SRAbilitySystemComponent.h"
#include "Abilities/SRGameplayTags.h"
#include "Data/SRHeroDataAsset.h"
#include "Abilities/SRTestAbilities.h"
#include "AbilitySystemComponent.h"

namespace
{
/** Hand-built game world with a PlayerState ASC (attribute set registered explicitly) and a BP_Zyrex avatar. */
struct FSRFixture
{
    UWorld* World = nullptr;
    APSPlayerState* State = nullptr;
    APSBaseCharacter* Character = nullptr;
    UPSAbilitySystemComponent* ASC = nullptr;

    ~FSRFixture() { if (World) World->DestroyWorld(false); }
};

bool InitFixture(FAutomationTestBase& Test, FSRFixture& Out)
{
    const UWorld::InitializationValues Init = UWorld::InitializationValues()
        .AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
    Out.World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
    if (!Test.TestNotNull(TEXT("Transient game world"), Out.World)) return false;

    UClass* PawnClass = LoadClass<APSBaseCharacter>(nullptr,
        TEXT("/Game/ProjectS/Characters/Shared/Heroes/Zyrex/BP_Zyrex.BP_Zyrex_C"));
    if (!Test.TestNotNull(TEXT("BP_Zyrex playable class"), PawnClass)) return false;

    Out.State = Out.World->SpawnActor<APSPlayerState>();
    if (!Test.TestNotNull(TEXT("PlayerState"), Out.State)) return false;
    Out.ASC = Out.State->PSAbilitySystem;
    if (!Test.TestNotNull(TEXT("Hero ability system component"), Out.ASC)) return false;
    // Deterministic registration (see PSAttributeTests for the same pattern).
    Out.ASC->AddAttributeSetSubobject(Out.State->PSAttributes.Get());
    Out.ASC->InitAbilityActorInfo(Out.State, Out.State);

    Out.Character = Out.World->SpawnActor<APSBaseCharacter>(PawnClass);
    if (!Test.TestNotNull(TEXT("BP_Zyrex avatar"), Out.Character)) return false;
    Out.ASC->InitAbilityActorInfo(Out.State, Out.Character);
    return true;
}

int32 CountSpecsWithTag(const UPSAbilitySystemComponent* ASC, const FGameplayTag& Tag)
{
    int32 Count = 0;
    for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
    {
        Count += Spec.DynamicAbilityTags.HasTagExact(Tag) ? 1 : 0;
    }
    return Count;
}

/** Local data asset so slot tests do not depend on the shipped DA_Zyrex configuration. */
USRHeroDataAsset* MakeLocalHeroData(UObject* Outer, TSubclassOf<UGameplayAbility> Q, TSubclassOf<UGameplayAbility> E,
    TSubclassOf<UGameplayAbility> C, TSubclassOf<UGameplayAbility> X)
{
    USRHeroDataAsset* Data = NewObject<USRHeroDataAsset>(Outer, NAME_None, RF_Transient);
    Data->HeroID = TEXT("TestHero");
    Data->AbilitySlots.AbilityQ = Q;
    Data->AbilitySlots.AbilityE = E;
    Data->AbilitySlots.AbilityC = C;
    Data->AbilitySlots.AbilityX = X;
    return Data;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSHeroSlotsGrantTest, "ProjectS.Ability.HeroSlotsGrantFromDataAsset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSHeroSlotsGrantTest::RunTest(const FString& Parameters)
{
    FSRFixture F;
    if (!InitFixture(*this, F)) return false;

    USRHeroDataAsset* HeroData = F.Character->GetHeroData();
    if (!TestNotNull(TEXT("BP_Zyrex references DA_Zyrex"), HeroData)) return false;

    // Data-asset-driven grants, tagged with their slot input tags.
    TestTrue(TEXT("Slots initialized from DA_Zyrex"), F.ASC->InitializeHeroAbilitySlots(HeroData));
    TestEqual(TEXT("Q slot granted once"), CountSpecsWithTag(F.ASC, SRGameplayTags::Tag_Input_Ability_Q),
        HeroData->AbilitySlots.AbilityQ ? 1 : 0);
    TestEqual(TEXT("E slot matches data asset"), CountSpecsWithTag(F.ASC, SRGameplayTags::Tag_Input_Ability_E),
        HeroData->AbilitySlots.AbilityE ? 1 : 0);
    TestEqual(TEXT("X slot matches data asset"), CountSpecsWithTag(F.ASC, SRGameplayTags::Tag_Input_Ability_X),
        HeroData->AbilitySlots.AbilityX ? 1 : 0);

    // Duplicate prevention across re-initialization (possession, respawn).
    F.ASC->InitializeHeroAbilitySlots(HeroData);
    TestEqual(TEXT("Regrant does not duplicate Q"), CountSpecsWithTag(F.ASC, SRGameplayTags::Tag_Input_Ability_Q),
        HeroData->AbilitySlots.AbilityQ ? 1 : 0);

    // Removing the ability references removes the granted slot abilities.
    TestTrue(TEXT("Clearing slots with no hero data"), F.ASC->InitializeHeroAbilitySlots(nullptr));
    TestEqual(TEXT("Q cleared"), CountSpecsWithTag(F.ASC, SRGameplayTags::Tag_Input_Ability_Q), 0);
    TestEqual(TEXT("E cleared"), CountSpecsWithTag(F.ASC, SRGameplayTags::Tag_Input_Ability_E), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSSlotInputActivationTest, "ProjectS.Ability.SlotInputActivation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSSlotInputActivationTest::RunTest(const FString& Parameters)
{
    // ---- Fixture 1: the same test ability on Q and E (data-only configuration, no code changes). ----
    {
        FSRFixture F;
        if (!InitFixture(*this, F)) return false;
        USRHeroDataAsset* Data = MakeLocalHeroData(F.ASC, USRTestAbility::StaticClass(), USRTestAbility::StaticClass(), nullptr, nullptr);
        TestTrue(TEXT("Slots granted"), F.ASC->InitializeHeroAbilitySlots(Data));
        TestEqual(TEXT("Q granted"), CountSpecsWithTag(F.ASC, SRGameplayTags::Tag_Input_Ability_Q), 1);
        TestEqual(TEXT("E granted"), CountSpecsWithTag(F.ASC, SRGameplayTags::Tag_Input_Ability_E), 1);

        UPSAttributeSet* Attributes = F.State->PSAttributes.Get();
        if (!TestNotNull(TEXT("Attribute set"), Attributes)) return false;

        // Q activates the ability: 15 damage flows through GE_SRDamage into IncomingDamage.
        F.ASC->NotifyAbilityInputPressed(SRGameplayTags::Tag_Input_Ability_Q);
        F.ASC->NotifyAbilityInputReleased(SRGameplayTags::Tag_Input_Ability_Q);
        TestEqual(TEXT("Q activation applied damage"), Attributes->GetShield(), 85.f);

        // Diagnostics: every active effect and its owning tags, then the cooldown query itself.
        const TArray<FActiveGameplayEffectHandle> AllActive = F.ASC->GetActiveEffects(FGameplayEffectQuery());
        AddInfo(FString::Printf(TEXT("ActiveGE count=%d"), AllActive.Num()));
        for (const FActiveGameplayEffectHandle& Handle : AllActive)
        {
            if (const FActiveGameplayEffect* Active = F.ASC->GetActiveGameplayEffect(Handle))
            {
                AddInfo(FString::Printf(TEXT("ActiveGE: %s duration=%.2f owningTags=%s"),
                    *GetNameSafe(Active->Spec.Def), Active->GetDuration(),
                    *Active->Spec.Def->InheritableOwnedTagsContainer.CombinedTags.ToString()));
            }
        }
        const TArray<FActiveGameplayEffectHandle> CooldownMatches = F.ASC->GetActiveEffects(
            FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(SRGameplayTags::Tag_SR_Cooldown)));
        AddInfo(FString::Printf(TEXT("Cooldown tag matches=%d"), CooldownMatches.Num()));
        TestTrue(TEXT("Cooldown effect applied"), CooldownMatches.Num() > 0);

        // Cooldown blocks an immediate re-activation.
        F.ASC->NotifyAbilityInputPressed(SRGameplayTags::Tag_Input_Ability_Q);
        F.ASC->NotifyAbilityInputReleased(SRGameplayTags::Tag_Input_Ability_Q);
        TestEqual(TEXT("Cooldown blocks immediate repeat"), Attributes->GetShield(), 85.f);

        // The same class under the E slot cannot bypass the Q cooldown (shared cooldown tag).
        F.ASC->NotifyAbilityInputPressed(SRGameplayTags::Tag_Input_Ability_E);
        F.ASC->NotifyAbilityInputReleased(SRGameplayTags::Tag_Input_Ability_E);
        TestEqual(TEXT("E slot cannot bypass the shared class cooldown"), Attributes->GetShield(), 85.f);
    }

    // ---- Fixture 2: fresh world with no cooldown running — the E slot alone activates the same data-assigned ability. ----
    {
        FSRFixture F;
        if (!InitFixture(*this, F)) return false;
        USRHeroDataAsset* Data = MakeLocalHeroData(F.ASC, nullptr, USRTestAbility::StaticClass(), nullptr, nullptr);
        TestTrue(TEXT("E-only slot granted"), F.ASC->InitializeHeroAbilitySlots(Data));
        TestEqual(TEXT("E granted"), CountSpecsWithTag(F.ASC, SRGameplayTags::Tag_Input_Ability_E), 1);
        TestEqual(TEXT("Q empty"), CountSpecsWithTag(F.ASC, SRGameplayTags::Tag_Input_Ability_Q), 0);

        UPSAttributeSet* Attributes = F.State->PSAttributes.Get();
        if (!TestNotNull(TEXT("Attribute set"), Attributes)) return false;

        F.ASC->NotifyAbilityInputPressed(SRGameplayTags::Tag_Input_Ability_E);
        F.ASC->NotifyAbilityInputReleased(SRGameplayTags::Tag_Input_Ability_E);
        TestEqual(TEXT("E activation applied damage"), Attributes->GetShield(), 85.f);

        // Pressing an empty slot (Q) must do nothing.
        F.ASC->NotifyAbilityInputPressed(SRGameplayTags::Tag_Input_Ability_Q);
        F.ASC->NotifyAbilityInputReleased(SRGameplayTags::Tag_Input_Ability_Q);
        TestEqual(TEXT("Empty slot press is a no-op"), Attributes->GetShield(), 85.f);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSUltimateChargeGateTest, "ProjectS.Ability.UltimateChargeGate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSUltimateChargeGateTest::RunTest(const FString& Parameters)
{
    FSRFixture F;
    if (!InitFixture(*this, F)) return false;

    USRHeroDataAsset* Data = MakeLocalHeroData(F.ASC, nullptr, nullptr, nullptr, USRTestUltimate::StaticClass());
    TestTrue(TEXT("Ultimate slot granted"), F.ASC->InitializeHeroAbilitySlots(Data));
    TestEqual(TEXT("X granted"), CountSpecsWithTag(F.ASC, SRGameplayTags::Tag_Input_Ability_X), 1);

    UPSAttributeSet* Attributes = F.State->PSAttributes.Get();
    if (!TestNotNull(TEXT("Attribute set"), Attributes)) return false;
    // Observable activation effect (+10 shield); lower the shield so restoration is visible.
    F.ASC->SetNumericAttributeBase(UPSAttributeSet::GetShieldAttribute(), 80.f);

    // Uncharged: X is rejected, no effect, no consumption.
    F.ASC->NotifyAbilityInputPressed(SRGameplayTags::Tag_Input_Ability_X);
    F.ASC->NotifyAbilityInputReleased(SRGameplayTags::Tag_Input_Ability_X);
    TestEqual(TEXT("Uncharged ultimate does nothing"), Attributes->GetShield(), 80.f);
    TestEqual(TEXT("Uncharged ultimate consumes nothing"), Attributes->GetUltimateCharge(), 0.f);

    // Fully charged: X activates, shield restored, charge consumed.
    F.ASC->SetNumericAttributeBase(UPSAttributeSet::GetUltimateChargeAttribute(), 100.f);
    F.ASC->NotifyAbilityInputPressed(SRGameplayTags::Tag_Input_Ability_X);
    F.ASC->NotifyAbilityInputReleased(SRGameplayTags::Tag_Input_Ability_X);
    TestEqual(TEXT("Charged ultimate applies its effect"), Attributes->GetShield(), 90.f);
    TestEqual(TEXT("Charged ultimate consumes the charge"), Attributes->GetUltimateCharge(), 0.f);

    // Spent: rejected again, nothing changes.
    F.ASC->NotifyAbilityInputPressed(SRGameplayTags::Tag_Input_Ability_X);
    F.ASC->NotifyAbilityInputReleased(SRGameplayTags::Tag_Input_Ability_X);
    TestEqual(TEXT("Spent ultimate does nothing"), Attributes->GetShield(), 90.f);
    return true;
}

#endif
