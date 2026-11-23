#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "HAL/PlatformProcess.h"
#include "HAL/IConsoleManager.h"
#include "GAS/PSAttributeSet.h"
#include "Character/PSBaseCharacter.h"
#include "Character/PSCharacterMovementComponent.h"
#include "GAS/PSPlayerState.h"
#include "Abilities/SRAbilities.h"
#include "Abilities/SRAbilitySystemComponent.h"
#include "Combat/SRCombatComponent.h"
#include "Combat/SRCombatPresentationComponent.h"
#include "Combat/SRPresentationActors.h"
#include "Character/SRCombatDummy.h"
#include "Data/SRHeroDataAsset.h"
#include "Combat/SRElementalProjectile.h"
#include "Combat/SRGameplayQueries.h"
#include "Combat/SRHeroGameplayActors.h"
#include "Abilities/SRHeroGameplayLibrary.h"
#include "Abilities/SRRosterAbilities.h"
#include "Abilities/SRVeyraAbilities.h"
#include "Character/SRHeroGameplayComponent.h"
#include "Data/SRHeroRoster.h"
#include "GAS/PSAttributeLibrary.h"
#include "Components/SphereComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Abilities/SRGameplayTags.h"
#include "Combat/SRWeaponDataAsset.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"
#include "GameFramework/PlayerController.h"
#include "EngineUtils.h"

namespace
{
/** Combat fixture with the four real abilities granted through data-asset-style slots. */
struct FSRAbilityFixture
{
    UWorld* World = nullptr;
    APSPlayerState* State = nullptr;
    APSBaseCharacter* Character = nullptr;
    UPSCombatComponent* Combat = nullptr;
    UPSAbilitySystemComponent* ASC = nullptr;
    UPSAttributeSet* Attributes = nullptr;

    ~FSRAbilityFixture() { if (World) World->DestroyWorld(false); }

    bool Init(FAutomationTestBase& Test)
    {
        // Surface gated debug logging for the test run.
        for (const TCHAR* CVarName : { TEXT("ps.Debug.Damage"), TEXT("ps.Debug.Combat"), TEXT("ps.Debug.Ability"), TEXT("ps.Debug.MovementAbility"), TEXT("ps.Debug.Projectile"), TEXT("ps.Debug.Hitscan"), TEXT("ps.Debug.Minimap"), TEXT("ps.Debug.Ping") })
        {
            if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(CVarName)) CVar->Set(1, ECVF_SetByConsole);
        }
        const UWorld::InitializationValues Init = UWorld::InitializationValues()
            .AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
        // Registered world (world context exists) so ability-task timers can fire.
        World = UWorld::CreateWorld(EWorldType::Game, true, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
        if (!Test.TestNotNull(TEXT("Transient ability world"), World)) return false;
        // Begin the world so spawned actors run their BeginPlay.
        World->BeginPlay();

        // Ground plane: characters and dummies must be grounded for movement abilities.
        // Floor top surface at z=0: the 100cm cube scaled 10x spans z -1000..0.
        AStaticMeshActor* Floor = World->SpawnActor<AStaticMeshActor>(FVector(0, 0, -500), FRotator::ZeroRotator);
        if (Floor && Floor->GetStaticMeshComponent())
        {
            Floor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
            Floor->GetStaticMeshComponent()->SetWorldScale3D(FVector(40, 40, 10));
            Floor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
            Floor->SetMobility(EComponentMobility::Static);
        }

        State = World->SpawnActor<APSPlayerState>();
        if (!Test.TestNotNull(TEXT("PlayerState"), State)) return false;
        ASC = State->PSAbilitySystem;
        ASC->AddAttributeSetSubobject(State->PSAttributes.Get());
        ASC->InitAbilityActorInfo(State, State);
        Attributes = State->PSAttributes.Get();

        Character = World->SpawnActor<APSBaseCharacter>(FVector(0, 0, 110), FRotator::ZeroRotator);
        if (!Test.TestNotNull(TEXT("Hero"), Character)) return false;
        Character->HeroGameplayComponent->bCanBeDowned = false; // Legacy tests exercise final elimination.
        // Link the hero to its PlayerState (possession normally does this).
        Character->SetPlayerState(State);
        ASC->InitAbilityActorInfo(State, Character);
        // A PlayerController gives the hero a real view point for aiming.
        APlayerController* PC = World->SpawnActor<APlayerController>();
        if (PC) PC->Possess(Character);
        if (!Test.TestTrue(TEXT("Hero linked to PlayerState ASC"), Character->GetAbilitySystemComponent() == ASC)) return false;
        Combat = Character->GetCombatComponent();
        if (!Test.TestNotNull(TEXT("Combat component"), Combat)) return false;

        USRWeaponDataAsset* Weapon = NewObject<USRWeaponDataAsset>(Combat, NAME_None, RF_Transient);
        Weapon->Damage = 22.f;
        Weapon->FireRate = 7.f;
        Weapon->MaxCharges = 30;
        Weapon->RechargeDuration = 1.8f;
        Character->WeaponData = Weapon;
        Combat->SetWeaponData(Weapon);
        return true;
    }

    bool GrantSlots(FAutomationTestBase& Test, USRHeroDataAsset* Data)
    {
        return Test.TestTrue(TEXT("Slots granted"), ASC->InitializeHeroAbilitySlots(Data));
    }

    bool ActivateSlot(FAutomationTestBase& Test, const FGameplayTag& SlotTag, const TCHAR* What)
    {
        const int32 Before = CountTagged(SlotTag);
        ASC->NotifyAbilityInputPressed(SlotTag);
        ASC->NotifyAbilityInputReleased(SlotTag);
        return Test.TestEqual(What, Before, 1);
    }

    int32 CountTagged(const FGameplayTag& Tag) const
    {
        int32 Count = 0;
        for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
        {
            Count += Spec.DynamicAbilityTags.HasTagExact(Tag) ? 1 : 0;
        }
        return Count;
    }
};

USRHeroDataAsset* MakeZyrexStyleData(UObject* Outer, TSubclassOf<UGameplayAbility> Q, TSubclassOf<UGameplayAbility> E,
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSArcBoltTest, "ProjectS.AbilityCombat.ArcBolt",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSArcBoltTest::RunTest(const FString& Parameters)
{
    FSRAbilityFixture F;
    if (!F.Init(*this)) return false;

    // A dummy directly ahead so the projectile spawns and can be counted.
    APSCombatDummy* Dummy = F.World->SpawnActor<APSCombatDummy>(FVector(300, 0, 120), FRotator::ZeroRotator);
    if (!TestNotNull(TEXT("Dummy"), Dummy)) return false;
    Dummy->GetAbilitySystemComponent()->AddAttributeSetSubobject(Dummy->GetAttributes());

    Dummy->GetAbilitySystemComponent()->AddAttributeSetSubobject(Dummy->GetAttributes());
    USRHeroDataAsset* Data = MakeZyrexStyleData(F.ASC, USRGameplayAbility_ArcBolt::StaticClass(), nullptr, nullptr, nullptr);
    if (!F.GrantSlots(*this, Data)) return false;

    F.ASC->NotifyAbilityInputPressed(SRGameplayTags::Tag_Input_Ability_Q);
    F.ASC->NotifyAbilityInputReleased(SRGameplayTags::Tag_Input_Ability_Q);

    // One authoritative projectile spawned for Q (no duplicates).
    int32 ProjectileCount = 0;
    for (TActorIterator<ASRElementalProjectile> It(F.World); It; ++It) ++ProjectileCount;
    TestEqual(TEXT("Exactly one Arc Bolt projectile"), ProjectileCount, 1);

    // Cooldown started: immediate re-press must not spawn a second projectile.
    F.ASC->NotifyAbilityInputPressed(SRGameplayTags::Tag_Input_Ability_Q);
    F.ASC->NotifyAbilityInputReleased(SRGameplayTags::Tag_Input_Ability_Q);
    ProjectileCount = 0;
    for (TActorIterator<ASRElementalProjectile> It(F.World); It; ++It) ++ProjectileCount;
    TestEqual(TEXT("Cooldown blocks second Arc Bolt"), ProjectileCount, 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSStaticLeapBurstTest, "ProjectS.AbilityCombat.StaticLeapBurst",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSStaticLeapBurstTest::RunTest(const FString& Parameters)
{
    FSRAbilityFixture F;
    if (!F.Init(*this)) return false;

    // Dummies inside and outside the landing burst radius.
    APSCombatDummy* Near = F.World->SpawnActor<APSCombatDummy>(FVector(200, 0, 120), FRotator::ZeroRotator);
    APSCombatDummy* Far = F.World->SpawnActor<APSCombatDummy>(FVector(1200, 0, 120), FRotator::ZeroRotator);
    if (!TestNotNull(TEXT("Near dummy"), Near) || !TestNotNull(TEXT("Far dummy"), Far)) return false;
    // Transient worlds never run BeginPlay: register the dummies' ASCs explicitly.
    Near->GetAbilitySystemComponent()->AddAttributeSetSubobject(Near->GetAttributes());
    Far->GetAbilitySystemComponent()->AddAttributeSetSubobject(Far->GetAttributes());

    USRHeroDataAsset* Data = MakeZyrexStyleData(F.ASC, nullptr, USRGameplayAbility_StaticLeap::StaticClass(), nullptr, nullptr);
    if (!F.GrantSlots(*this, Data)) return false;

    // The landing burst is the ability's damage application on landing: one query,
    // once per target, ignoring self and out-of-radius targets.
    F.ASC->NotifyAbilityInputPressed(SRGameplayTags::Tag_Input_Ability_E);
    F.ASC->NotifyAbilityInputReleased(SRGameplayTags::Tag_Input_Ability_E);

    USRGameplayAbility_StaticLeap* LeapCDO = Cast<USRGameplayAbility_StaticLeap>(
        USRGameplayAbility_StaticLeap::StaticClass()->GetDefaultObject());
    if (!TestNotNull(TEXT("Leap ability default"), LeapCDO)) return false;
    LeapCDO->ExecuteLandingBurstAt(F.Character);

    TestEqual(TEXT("Near dummy damaged once"), Near->GetDummyShield(), 70.f);
    TestEqual(TEXT("Far dummy outside burst radius"), Far->GetDummyShield(), 100.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSOverchargeTest, "ProjectS.AbilityCombat.Overcharge",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSOverchargeTest::RunTest(const FString& Parameters)
{
    FSRAbilityFixture F;
    if (!F.Init(*this)) return false;

    USRHeroDataAsset* Data = MakeZyrexStyleData(F.ASC, nullptr, nullptr, USRGameplayAbility_Overcharge::StaticClass(), nullptr);
    if (!F.GrantSlots(*this, Data)) return false;

    TestEqual(TEXT("Base fire interval"), F.Combat->GetEffectiveFireInterval(), 1.f / 7.f);
    TestEqual(TEXT("Base recharge duration"), F.Combat->GetEffectiveRechargeDuration(), 1.8f);

    F.ASC->NotifyAbilityInputPressed(SRGameplayTags::Tag_Input_Ability_C);
    F.ASC->NotifyAbilityInputReleased(SRGameplayTags::Tag_Input_Ability_C);
    TestTrue(TEXT("Activation ran (cooldown tag present)"), F.ASC->GetTagCount(SRGameplayTags::Tag_SR_Cooldown_C) > 0);
    TestEqual(TEXT("Activating C does not lock Q"), F.ASC->GetTagCount(SRGameplayTags::Tag_SR_Cooldown_Q), 0);
    const TArray<FActiveGameplayEffectHandle> BuffQuery = F.ASC->GetActiveEffects(
        FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(SRGameplayTags::Tag_Status_Buff_Overcharge)));
    AddInfo(FString::Printf(TEXT("Overcharge active effects=%d"), BuffQuery.Num()));
    TestTrue(TEXT("Overcharge tag active"), F.ASC->GetTagCount(SRGameplayTags::Tag_Status_Buff_Overcharge) > 0);
    AddInfo(FString::Printf(TEXT("TAGCHECK count=%d ascMatch=%d owner=%s mult=%.2f"),
        F.ASC->GetTagCount(SRGameplayTags::Tag_Status_Buff_Overcharge),
        F.Character->GetAbilitySystemComponent() == F.ASC ? 1 : 0,
        *GetNameSafe(F.Combat->GetOwnerActorForTest()),
        F.Combat->OverchargeFireRateMultiplier));
    TestEqual(TEXT("Buffed fire interval"), F.Combat->GetEffectiveFireInterval(), (1.f / 7.f) / 1.35f);
    TestEqual(TEXT("Buffed recharge duration"), F.Combat->GetEffectiveRechargeDuration(), 1.8f / 1.25f);

    // Expiry (removing the duration effect) restores base values.
    const TArray<FActiveGameplayEffectHandle> BuffHandles = F.ASC->GetActiveEffects(
        FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(SRGameplayTags::Tag_Status_Buff_Overcharge)));
    if (TestTrue(TEXT("Buff effect found"), BuffHandles.Num() > 0))
    {
        F.ASC->RemoveActiveGameplayEffect(BuffHandles[0]);
    }
    TestEqual(TEXT("Base fire interval restored"), F.Combat->GetEffectiveFireInterval(), 1.f / 7.f);
    TestEqual(TEXT("Base recharge restored"), F.Combat->GetEffectiveRechargeDuration(), 1.8f);

    // Cooldown prevents immediate re-activation.
    F.ASC->NotifyAbilityInputPressed(SRGameplayTags::Tag_Input_Ability_C);
    F.ASC->NotifyAbilityInputReleased(SRGameplayTags::Tag_Input_Ability_C);
    TestEqual(TEXT("Cooldown blocks repeat"), F.ASC->GetTagCount(SRGameplayTags::Tag_Status_Buff_Overcharge), 0);
    return true;
}

AController* PC_Set = nullptr;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSStormpiercerTest, "ProjectS.AbilityCombat.Stormpiercer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSStormpiercerTest::RunTest(const FString& Parameters)
{
    FSRAbilityFixture F;
    if (!F.Init(*this)) return false;

    // Rival hero on another team, straight down the firing lane.
    APSPlayerState* RivalState = F.World->SpawnActor<APSPlayerState>();
    if (!TestNotNull(TEXT("Rival PlayerState"), RivalState)) return false;
    RivalState->PSAbilitySystem->AddAttributeSetSubobject(RivalState->PSAttributes.Get());
    RivalState->PSAbilitySystem->InitAbilityActorInfo(RivalState, RivalState);
    RivalState->TeamId = 1;

    APSBaseCharacter* Rival = F.World->SpawnActor<APSBaseCharacter>(FVector(600, 0, 110), FRotator::ZeroRotator);
    if (!TestNotNull(TEXT("Rival hero"), Rival)) return false;
    Rival->SetPlayerState(RivalState);
    RivalState->PSAbilitySystem->InitAbilityActorInfo(RivalState, Rival);
    // Re-register after the final InitAbilityActorInfo (manual adds are cleared by it).
    RivalState->PSAbilitySystem->AddAttributeSetSubobject(RivalState->PSAttributes.Get());

    USRHeroDataAsset* Data = MakeZyrexStyleData(F.ASC, nullptr, nullptr, nullptr, USRGameplayAbility_Stormpiercer::StaticClass());
    if (!F.GrantSlots(*this, Data)) return false;

    FVector ViewLoc; FRotator ViewRot;
    if (APlayerController* PC = Cast<APlayerController>(F.Character->GetController())) PC->GetPlayerViewPoint(ViewLoc, ViewRot);
    AddInfo(FString::Printf(TEXT("View loc=%s rot=%s"), *ViewLoc.ToString(), *ViewRot.ToString()));
    PC_Set = F.Character->GetController();
    AddInfo(FString::Printf(TEXT("Controller=%s"), *GetNameSafe(PC_Set)));

    // Full ultimate charge required.
    F.ASC->SetNumericAttributeBase(UPSAttributeSet::GetUltimateChargeAttribute(), 100.f);
    F.ASC->NotifyAbilityInputPressed(SRGameplayTags::Tag_Input_Ability_X);
    F.ASC->NotifyAbilityInputReleased(SRGameplayTags::Tag_Input_Ability_X);
    TestEqual(TEXT("Ultimate charge consumed once"), F.Attributes->GetUltimateCharge(), 0.f);

    TestEqual(TEXT("Ultimate charge consumed once"), F.Attributes->GetUltimateCharge(), 0.f);

    // Pulse damage executed through the ability's authoritative beam query (windup
    // pacing and pulse timing are verified in PIE; the query/damage/reveal logic here).
    const FGameplayAbilitySpec* StormpiercerSpec = nullptr;
    for (const FGameplayAbilitySpec& Spec : F.ASC->GetActivatableAbilities())
    {
        if (Spec.Ability && Spec.Ability->GetClass() == USRGameplayAbility_Stormpiercer::StaticClass()) StormpiercerSpec = &Spec;
    }
    if (!TestNotNull(TEXT("Granted Stormpiercer spec"), const_cast<FGameplayAbilitySpec*>(StormpiercerSpec))) return false;
    USRGameplayAbility_Stormpiercer* Stormpiercer = Cast<USRGameplayAbility_Stormpiercer>(StormpiercerSpec->Ability);
    if (!TestNotNull(TEXT("Stormpiercer instance"), Stormpiercer)) return false;
    Stormpiercer->ExecuteBeamPulse(1, F.Character);
    TestEqual(TEXT("Pulse 1: rival shield absorbs"), RivalState->PSAttributes->GetShield(), 45.f);
    Stormpiercer->ExecuteBeamPulse(2, F.Character);
    TestEqual(TEXT("Pulse 2: shield depleted, health damaged"), RivalState->PSAttributes->GetHealth(), 90.f);

    // Minimap-only reveal created for the enemy hit on the shooter's team ping list.
    bool bRevealFound = false;
    for (const FSRPingInfo& Ping : F.State->GetTeamPings())
    {
        bRevealFound |= Ping.Category == SRGameplayTags::Tag_SR_Ping_Reveal;
    }
    TestTrue(TEXT("Enemy last-known-position reveal created"), bRevealFound);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLeapDashTest, "ProjectS.AbilityCombat.LeapDash",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLeapDashTest::RunTest(const FString& Parameters)
{
    FSRAbilityFixture F;
    if (!F.Init(*this)) return false;

    UPSCharacterMovementComponent* CMC = Cast<UPSCharacterMovementComponent>(F.Character->GetCharacterMovement());
    if (!TestNotNull(TEXT("Custom movement component"), CMC)) return false;

    // Invalid dashes are rejected without state.
    TestFalse(TEXT("Zero-direction dash rejected"), CMC->StartPSLeapDash(FVector::ZeroVector, 2000.f, 0.5f));
    TestFalse(TEXT("Zero-duration dash rejected"), CMC->StartPSLeapDash(FVector(1, 0, 0), 2000.f, 0.f));
    TestFalse(TEXT("Dash inactive after rejections"), CMC->IsPSLeapDashActive());

    // Valid dash: velocity is enforced, immune to friction and braking.
    TestTrue(TEXT("Dash started"), CMC->StartPSLeapDash(FVector(1, 0, 0), 2000.f, 0.5f));
    TestTrue(TEXT("Dash active"), CMC->IsPSLeapDashActive());
    CMC->CalcVelocity(0.016f, 8.f, true, 2048.f);
    TestEqual(TEXT("Dash velocity X = speed (friction-immune)"), static_cast<float>(CMC->Velocity.X), 2000.f);
    TestEqual(TEXT("Dash velocity Y = 0"), static_cast<float>(CMC->Velocity.Y), 0.f);
    CMC->CalcVelocity(0.016f, 8.f, true, 2048.f);
    TestEqual(TEXT("Dash velocity constant across ticks"), static_cast<float>(CMC->Velocity.X), 2000.f);

    // Ending restores exit momentum at run speed (fraction capped by GetMaxSpeed).
    CMC->EndPSLeapDash(false);
    TestFalse(TEXT("Dash inactive after end"), CMC->IsPSLeapDashActive());
    TestEqual(TEXT("Exit momentum restored"), static_cast<float>(CMC->Velocity.X), CMC->GetMaxSpeed());

    // Dash displacement contract: Speed * Duration exactly (1200cm at the defaults).
    const float DashSpeed = 1200.f / 0.45f;
    TestTrue(TEXT("Leap-speed dash started"), CMC->StartPSLeapDash(FVector(1, 0, 0), DashSpeed, 0.45f));
    CMC->CalcVelocity(0.016f, 8.f, true, 2048.f);
    TestEqual(TEXT("Leap dash velocity"), static_cast<float>(CMC->Velocity.X), DashSpeed);
    CMC->EndPSLeapDash(true);

    // Full activation path through the ability: direction resolution, floor-safe
    // path sweep and dash start (regression: the ground used to register as a
    // capsule obstruction and instantly reject every leap).
    USRHeroDataAsset* LeapData = MakeZyrexStyleData(F.ASC, nullptr, USRGameplayAbility_StaticLeap::StaticClass(), nullptr, nullptr);
    if (!F.GrantSlots(*this, LeapData)) return false;
    F.ASC->NotifyAbilityInputPressed(SRGameplayTags::Tag_Input_Ability_E);
    F.ASC->NotifyAbilityInputReleased(SRGameplayTags::Tag_Input_Ability_E);
    TestTrue(TEXT("Leap activation started the dash"), CMC->IsPSLeapDashActive());
    CMC->EndPSLeapDash(false);
    TestFalse(TEXT("Dash cleared after forced end"), CMC->IsPSLeapDashActive());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSCombatPresentationTest, "ProjectS.AbilityCombat.CombatPresentation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSCombatPresentationTest::RunTest(const FString& Parameters)
{
    FSRAbilityFixture F;
    if (!F.Init(*this)) return false;

    USRCombatPresentationComponent* Presentation = F.Character->GetPresentationComponent();
    if (!TestNotNull(TEXT("Presentation component"), Presentation)) return false;

    // One confirmed shot renders muzzle flash + tracer + impact burst.
    const FVector Muzzle = F.Character->GetActorLocation() + FVector(0, 0, 60);
    Presentation->PlayShotCosmetic(Muzzle, Muzzle + FVector(1000, 0, 0), true);
    int32 TransientCount = 0;
    for (TActorIterator<ASRTransientVFX> It(F.World); It; ++It) ++TransientCount;
    TestTrue(FString::Printf(TEXT("Shot cosmetics spawned (count=%d)"), TransientCount), TransientCount >= 3);

    // Camera recoil applies only for the local player; the transient fixture's
    // PC is not a local controller, so the pitch must remain untouched there.
    APlayerController* PC = Cast<APlayerController>(F.Character->GetController());
    if (TestNotNull(TEXT("Player controller"), PC))
    {
        const float PitchBefore = PC->GetControlRotation().Pitch;
        Presentation->PlayShotCosmetic(Muzzle, Muzzle + FVector(1000, 0, 0), false);
        if (F.Character->IsLocallyControlled())
        {
            TestTrue(TEXT("Recoil pitch applied"), PC->GetControlRotation().Pitch > PitchBefore);
        }
        else
        {
            TestEqual(TEXT("Recoil skipped for remote possession"), static_cast<float>(PC->GetControlRotation().Pitch), PitchBefore);
        }
    }

    // Hit feedback: damage number + reaction burst (local multicast execution).
    APSCombatDummy* Dummy = F.World->SpawnActor<APSCombatDummy>(FVector(400, 0, 120), FRotator::ZeroRotator);
    if (!TestNotNull(TEXT("Dummy"), Dummy)) return false;
    Dummy->GetAbilitySystemComponent()->AddAttributeSetSubobject(Dummy->GetAttributes());
    const int32 TransientBefore = TransientCount;
    Presentation->PlayHitFeedback(Dummy, Dummy->GetActorLocation(), 22.f, true, false, FVector(1, 0, 0));
    int32 DamageNumbers = 0;
    int32 TransientAfter = 0;
    for (TActorIterator<ASRFloatingDamageText> It(F.World); It; ++It) ++DamageNumbers;
    for (TActorIterator<ASRTransientVFX> It(F.World); It; ++It) ++TransientAfter;
    TestTrue(FString::Printf(TEXT("Damage number spawned (count=%d)"), DamageNumbers), DamageNumbers >= 1);
    TestTrue(FString::Printf(TEXT("Reaction burst spawned (count=%d)"), TransientAfter), TransientAfter >= TransientBefore);

    // Overcharge aura state (tag watcher drives it in gameplay; direct set here).
    Presentation->SetOverchargeAura(true);
    TestTrue(TEXT("Aura active"), Presentation->IsOverchargeAuraActive());
    Presentation->SetOverchargeAura(false);
    TestFalse(TEXT("Aura cleared"), Presentation->IsOverchargeAuraActive());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSArcBoltImpactRegression, "ProjectS.AbilityCombat.Zyrex.ArcBoltFirstImpact",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSArcBoltImpactRegression::RunTest(const FString& Parameters)
{
    FSRAbilityFixture F;
    if (!F.Init(*this)) return false;
    APSCombatDummy* Direct = F.World->SpawnActor<APSCombatDummy>(FVector(500, 0, 200), FRotator::ZeroRotator);
    APSCombatDummy* Splash = F.World->SpawnActor<APSCombatDummy>(FVector(500, 180, 200), FRotator::ZeroRotator);
    APSCombatDummy* Covered = F.World->SpawnActor<APSCombatDummy>(FVector(500, -180, 200), FRotator::ZeroRotator);
    if (!TestNotNull(TEXT("Direct target"), Direct) || !TestNotNull(TEXT("Splash target"), Splash)
        || !TestNotNull(TEXT("Covered target"), Covered)) return false;
    AStaticMeshActor* Wall = F.World->SpawnActor<AStaticMeshActor>(FVector(500, -90, 200), FRotator::ZeroRotator);
    if (!TestNotNull(TEXT("Splash cover"), Wall)) return false;
    Wall->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
    Wall->GetStaticMeshComponent()->SetWorldScale3D(FVector(2, 0.2, 3));
    Wall->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
    ASRElementalProjectile* Bolt = F.Combat->SpawnAbilityProjectile(ASRElementalProjectile::StaticClass(),
        FVector(100, 0, 200), FVector::ForwardVector, F.Character, 1400.f, 6000.f);
    if (!TestNotNull(TEXT("Bolt"), Bolt)) return false;
    UProjectileMovementComponent* Movement = Bolt->FindComponentByClass<UProjectileMovementComponent>();
    USphereComponent* Collision = Bolt->FindComponentByClass<USphereComponent>();
    if (!TestNotNull(TEXT("Projectile movement"), Movement) || !TestNotNull(TEXT("Projectile collision"), Collision)) return false;
    TestFalse(TEXT("Arc Bolt does not bounce"), Movement->bShouldBounce);
    TestEqual(TEXT("Arc Bolt has no lob gravity"), Movement->ProjectileGravityScale, 0.f);

    // Feed the collision event through the same delegate used by projectile movement.
    FHitResult Hit;
    Hit.ImpactPoint = Direct->GetActorLocation();
    Hit.ImpactNormal = -FVector::ForwardVector;
    Collision->OnComponentHit.Broadcast(Collision, Direct, Cast<UPrimitiveComponent>(Direct->GetRootComponent()), FVector::ZeroVector, Hit);
    TestEqual(TEXT("Impact applies 45 direct, without additional splash"), Direct->GetDummyShield(), 55.f);
    TestEqual(TEXT("Adjacent enemy receives 25 splash"), Splash->GetDummyShield(), 75.f);
    TestEqual(TEXT("Cover blocks splash"), Covered->GetDummyShield(), 100.f);
    TestEqual(TEXT("Direct hit applies 25 percent slow"), Direct->GetAttributes()->GetMoveSpeedMultiplier(), 0.75f);
    TestTrue(TEXT("First impact destroys projectile"), Bolt->IsActorBeingDestroyed());
    Collision->OnComponentHit.Broadcast(Collision, Direct, Cast<UPrimitiveComponent>(Direct->GetRootComponent()), FVector::ZeroVector, Hit);
    TestEqual(TEXT("Duplicate impact does not damage again"), Direct->GetDummyShield(), 55.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSWideAttackRegression, "ProjectS.AbilityCombat.Zyrex.WideAttackCollision",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSWideAttackRegression::RunTest(const FString& Parameters)
{
    FSRAbilityFixture F;
    if (!F.Init(*this)) return false;
    APSCombatDummy* Left = F.World->SpawnActor<APSCombatDummy>(FVector(300, 700, 200), FRotator::ZeroRotator);
    APSCombatDummy* Right = F.World->SpawnActor<APSCombatDummy>(FVector(-300, 700, 200), FRotator::ZeroRotator);
    APSCombatDummy* Edge = F.World->SpawnActor<APSCombatDummy>(FVector(450, 1100, 200), FRotator::ZeroRotator);
    APSCombatDummy* Outside = F.World->SpawnActor<APSCombatDummy>(FVector(-520, 1100, 200), FRotator::ZeroRotator);
    if (!Left || !Right || !Edge || !Outside) return false;
    TArray<AActor*> Hits;
    SRGameplayQueries::SweepBoxTargets(F.World, F.Character, FVector(0, 1, 200), FVector(0, 1499, 200),
        FVector(1, 400, 400), FRotator(0, 90, 0).Quaternion(), Hits);
    TestTrue(TEXT("Rotated beam includes left lane"), Hits.Contains(Left));
    TestTrue(TEXT("Rotated beam includes right lane"), Hits.Contains(Right));
    TestTrue(TEXT("Target collision intersecting the edge is hit"), Hits.Contains(Edge));
    TestFalse(TEXT("Target fully outside 8m beam is excluded"), Hits.Contains(Outside));
    TestEqual(TEXT("One candidate per intersecting actor"), Hits.Num(), 3);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSOverchargeLifecycleRegression, "ProjectS.AbilityCombat.Zyrex.OverchargeLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSOverchargeLifecycleRegression::RunTest(const FString& Parameters)
{
    FSRAbilityFixture F;
    if (!F.Init(*this)) return false;
    if (!F.GrantSlots(*this, MakeZyrexStyleData(F.ASC, nullptr, nullptr, USRGameplayAbility_Overcharge::StaticClass(), nullptr))) return false;
    TestTrue(TEXT("Overcharge activates"), F.ASC->TryActivateAbilityByClass(USRGameplayAbility_Overcharge::StaticClass()));
    F.ASC->CancelAllAbilities();
    TestFalse(TEXT("Cancellation removes buff immediately"), F.ASC->HasMatchingGameplayTag(SRGameplayTags::Tag_Status_Buff_Overcharge));
    TestEqual(TEXT("Cancellation restores fire rate"), F.Combat->GetEffectiveFireInterval(), 1.f / 7.f);
    TestEqual(TEXT("Cancellation restores recharge"), F.Combat->GetEffectiveRechargeDuration(), 1.8f);
    F.ASC->RemoveActiveEffectsWithGrantedTags(FGameplayTagContainer(SRGameplayTags::Tag_SR_Cooldown_C));
    TestTrue(TEXT("Overcharge can activate after clearing test cooldown"), F.ASC->TryActivateAbilityByClass(USRGameplayAbility_Overcharge::StaticClass()));
    for (int32 Step = 0; Step < 330; ++Step) F.World->Tick(LEVELTICK_All, 0.02f);
    TestFalse(TEXT("Buff expires after six seconds"), F.ASC->HasMatchingGameplayTag(SRGameplayTags::Tag_Status_Buff_Overcharge));
    TestEqual(TEXT("Expiry restores fire rate"), F.Combat->GetEffectiveFireInterval(), 1.f / 7.f);
    F.ASC->SetNumericAttributeBase(UPSAttributeSet::GetHealthAttribute(), 0.f);
    F.ASC->RemoveActiveEffectsWithGrantedTags(FGameplayTagContainer(SRGameplayTags::Tag_SR_Cooldown_C));
    TestFalse(TEXT("Dead heroes cannot activate a buff"), F.ASC->TryActivateAbilityByClass(USRGameplayAbility_Overcharge::StaticClass()));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSStormpiercerPulseRegression, "ProjectS.AbilityCombat.Zyrex.StormpiercerPulseCount",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSStormpiercerPulseRegression::RunTest(const FString& Parameters)
{
    FSRAbilityFixture F;
    if (!F.Init(*this)) return false;
    if (!F.GrantSlots(*this, MakeZyrexStyleData(F.ASC, nullptr, nullptr, nullptr, USRGameplayAbility_Stormpiercer::StaticClass()))) return false;
    TestFalse(TEXT("Uncharged ultimate rejected"), F.ASC->TryActivateAbilityByClass(USRGameplayAbility_Stormpiercer::StaticClass()));
    F.ASC->SetNumericAttributeBase(UPSAttributeSet::GetUltimateChargeAttribute(), 100.f);
    TestTrue(TEXT("Charged ultimate activates"), F.ASC->TryActivateAbilityByClass(USRGameplayAbility_Stormpiercer::StaticClass()));
    USRGameplayAbility_Stormpiercer* Instance = nullptr;
    for (FGameplayAbilitySpec& Spec : F.ASC->GetActivatableAbilities())
        if (Spec.Ability && Spec.Ability->IsA<USRGameplayAbility_Stormpiercer>()) Instance = Cast<USRGameplayAbility_Stormpiercer>(Spec.GetPrimaryInstance());
    if (!TestNotNull(TEXT("Instanced ultimate"), Instance)) return false;
    TestEqual(TEXT("No pulses during windup"), Instance->GetExecutedPulseCount(), 0);
    TestEqual(TEXT("Charge spent once at activation"), F.Attributes->GetUltimateCharge(), 0.f);
    for (int32 Step = 0; Step < 100; ++Step) F.World->Tick(LEVELTICK_All, 0.02f);
    TestEqual(TEXT("Exactly three timed pulses"), Instance->GetExecutedPulseCount(), 3);
    TestFalse(TEXT("Ultimate ends after third pulse"), Instance->IsActive());
    for (int32 Step = 0; Step < 100; ++Step) F.World->Tick(LEVELTICK_All, 0.02f);
    TestEqual(TEXT("No fourth pulse after completion"), Instance->GetExecutedPulseCount(), 3);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLeapLandingRegression, "ProjectS.AbilityCombat.Zyrex.LeapLanding",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLeapLandingRegression::RunTest(const FString& Parameters)
{
    FSRAbilityFixture F;
    if (!F.Init(*this)) return false;
    if (!F.GrantSlots(*this, MakeZyrexStyleData(F.ASC, nullptr, USRGameplayAbility_StaticLeap::StaticClass(), nullptr, nullptr))) return false;
    UPSCharacterMovementComponent* CMC = CastChecked<UPSCharacterMovementComponent>(F.Character->GetCharacterMovement());
    APSCombatDummy* NearTakeoff = F.World->SpawnActor<APSCombatDummy>(FVector(0, 200, 120), FRotator::ZeroRotator);
    if (!TestNotNull(TEXT("Takeoff target"), NearTakeoff)) return false;
    CMC->SetMovementMode(MOVE_Walking);
    TestTrue(TEXT("Grounded leap activates"), F.ASC->TryActivateAbilityByClass(USRGameplayAbility_StaticLeap::StaticClass()));
    TestTrue(TEXT("Leap enters falling physics"), CMC->IsFalling());
    CMC->EndPSLeapDash(true);
    TestEqual(TEXT("Ending horizontal drive in air does not burst"), NearTakeoff->GetDummyShield(), 100.f);
    CMC->SetMovementMode(MOVE_Walking);
    TestEqual(TEXT("Landing applies one 30 damage burst"), NearTakeoff->GetDummyShield(), 70.f);
    CMC->SetMovementMode(MOVE_Falling);
    CMC->SetMovementMode(MOVE_Walking);
    TestEqual(TEXT("Subsequent landing cannot repeat completed leap burst"), NearTakeoff->GetDummyShield(), 70.f);
    return true;
}

namespace
{
APSBaseCharacter* SpawnRosterTarget(FSRAbilityFixture& F, const FVector& Position, int32 Team)
{
    APSPlayerState* State = F.World->SpawnActor<APSPlayerState>();
    APSBaseCharacter* Target = F.World->SpawnActor<APSBaseCharacter>(Position, FRotator::ZeroRotator);
    if (!State || !Target) return nullptr;
    State->TeamId = Team;
    Target->SetPlayerState(State);
    State->PSAbilitySystem->InitAbilityActorInfo(State, Target);
    State->PSAbilitySystem->AddAttributeSetSubobject(State->PSAttributes.Get());
    Target->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    return Target;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSRosterDefinitionsTest, "ProjectS.Roster.Definitions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPSRosterDefinitionsTest::RunTest(const FString&)
{
    TestEqual(TEXT("Twelve registered heroes"), SRHeroRoster::All().Num(), 12);
    TSet<FName> IDs; TSet<UClass*> Abilities;
    for (const FSRHeroDefinition& Definition : SRHeroRoster::All())
    {
        TestFalse(TEXT("Unique hero identity"), IDs.Contains(Definition.ID)); IDs.Add(Definition.ID);
        TestTrue(TEXT("Valid ranged primary defaults"), Definition.Damage > 0 && Definition.Range >= 10000 && Definition.FireRate > 0 && Definition.Charges > 0 && Definition.Recharge > 0);
        for (const auto& Ability : Definition.Abilities)
        {
            UClass* Class = Ability.Get();
            if (!TestNotNull(TEXT("Concrete ability class"), Class)) return false;
            TestFalse(TEXT("Ability is instantiable"), Class->HasAnyClassFlags(CLASS_Abstract));
            Abilities.Add(Class);
        }
    }
    TestEqual(TEXT("Forty-eight distinct Q/E/C/X classes"), Abilities.Num(), 48);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSRosterWidePunchTest, "ProjectS.Roster.WidePunch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPSRosterWidePunchTest::RunTest(const FString&)
{
    FSRAbilityFixture F; if (!F.Init(*this)) return false;
    auto* Left = F.World->SpawnActor<APSCombatDummy>(FVector(250,230,120), FRotator::ZeroRotator);
    auto* Right = F.World->SpawnActor<APSCombatDummy>(FVector(250,-230,120), FRotator::ZeroRotator);
    if (!Left || !Right) return false;
    if (!F.GrantSlots(*this, MakeZyrexStyleData(F.ASC, USRGameplayAbility_InfernoPunch::StaticClass(), nullptr,nullptr,nullptr))) return false;
    TestTrue(TEXT("Punch activates"), F.ASC->TryActivateAbilityByClass(USRGameplayAbility_InfernoPunch::StaticClass()));
    TestEqual(TEXT("Left enemy receives punch"), Left->GetDummyShield(), 55.f);
    TestEqual(TEXT("Side-by-side right enemy receives punch"), Right->GetDummyShield(), 55.f);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSRosterPiercingTest, "ProjectS.Roster.RiftDaggerPiercing",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPSRosterPiercingTest::RunTest(const FString&)
{
    FSRAbilityFixture F; if (!F.Init(*this)) return false;
    auto* A = F.World->SpawnActor<APSCombatDummy>(FVector(600,70,200), FRotator::ZeroRotator);
    auto* B = F.World->SpawnActor<APSCombatDummy>(FVector(900,-70,200), FRotator::ZeroRotator);
    auto* Wall = F.World->SpawnActor<AStaticMeshActor>(FVector(350,0,200), FRotator::ZeroRotator);
    if (!A || !B || !Wall) return false;
    Wall->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
    Wall->GetStaticMeshComponent()->SetWorldScale3D(FVector(0.2,10,4)); Wall->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
    auto* Volume = F.World->SpawnActor<ASRHeroMovingVolume>(FVector(100,0,200), FRotator::ZeroRotator);
    if (!Volume) return false;
    Volume->Initialize(F.Character, SRDefaultActionConfig(ESRHeroAction::RiftDagger), FVector::ForwardVector);
    Volume->Tick(0.2f);
    TestEqual(TEXT("Pierces wall and damages first target 25"), A->GetDummyShield(), 75.f);
    TestEqual(TEXT("Pierces and damages second target 25"), B->GetDummyShield(), 75.f);
    Volume->Tick(0.01f);
    TestEqual(TEXT("No duplicate damage on subsequent update"), B->GetDummyShield(), 75.f);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSRosterWallTest, "ProjectS.Roster.IndependentWallHealth",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPSRosterWallTest::RunTest(const FString&)
{
    FSRAbilityFixture F; if (!F.Init(*this)) return false;
    APSBaseCharacter* Enemy = SpawnRosterTarget(F, FVector(1500,1200,100), 1); if (!Enemy) return false;
    FSRHeroActionConfig Config = SRDefaultActionConfig(ESRHeroAction::PrismWall); Config.Width = 300; Config.StructureHealth = 200;
    auto* A = F.World->SpawnActor<ASRHeroBarrier>(FVector(600,-300,180), FRotator::ZeroRotator);
    auto* B = F.World->SpawnActor<ASRHeroBarrier>(FVector(600,300,180), FRotator::ZeroRotator);
    if (!A || !B) return false;
    A->Initialize(Enemy,Config); B->Initialize(Enemy,Config);
    SRHeroGameplay::Damage(F.Character,A,250);
    TestTrue(TEXT("Destroyed section removed"), A->IsActorBeingDestroyed());
    TestFalse(TEXT("Adjacent section survives"), B->IsActorBeingDestroyed());
    FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(SRWallTest),false,F.Character);
    TestTrue(TEXT("Intact section still provides cover"), F.World->LineTraceSingleByChannel(Hit,FVector(0,300,150),FVector(1200,300,150),ECC_Camera,Params));
    TestEqual(TEXT("Intact hit is adjacent wall"), Hit.GetActor(), static_cast<AActor*>(B));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSRosterMistTest, "ProjectS.Roster.MistVeilTransmission",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPSRosterMistTest::RunTest(const FString&)
{
    FSRAbilityFixture F; if (!F.Init(*this)) return false;
    auto* Wall = F.World->SpawnActor<ASRHeroBarrier>(FVector(600,0,253),FRotator::ZeroRotator);
    if (!Wall) return false;
    Wall->Initialize(F.Character,SRDefaultActionConfig(ESRHeroAction::MistVeil));
    const FVector Start(0,0,100), End(1200,0,100);
    TestEqual(TEXT("Crossed wall transmits 70 percent"), SRHeroGameplay::HitscanTransmission(F.World,Start,End),0.7f);
    TestEqual(TEXT("Repeated query does not double-count a wall"), SRHeroGameplay::HitscanTransmission(F.World,Start,End),0.7f);
    FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(SRMistTest),false,F.Character);
    TestFalse(TEXT("Weapon trace passes through"), F.World->LineTraceSingleByChannel(Hit,Start,End,ECC_Camera,Params));
    TestTrue(TEXT("Physical projectile channel is blocked"), F.World->SweepSingleByChannel(Hit,Start,End,FQuat::Identity,ECC_WorldDynamic,FCollisionShape::MakeSphere(15),Params));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSRosterReviveTest, "ProjectS.Roster.WardDownedRevival",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPSRosterReviveTest::RunTest(const FString&)
{
    FSRAbilityFixture F; if (!F.Init(*this)) return false;
    auto* State = F.Character->HeroGameplayComponent.Get(); State->bCanBeDowned = true;
    F.ASC->SetNumericAttributeBase(UPSAttributeSet::GetShieldAttribute(),0);
    State->AddWard(75,6,F.Character);
    UPSAttributeLibrary::ApplyDamage(F.Character,100,F.Character);
    TestEqual(TEXT("Ward absorbs before health"), F.Attributes->GetHealth(),75.f);
    TestEqual(TEXT("Ward durability depleted"), State->GetWardHealth(),0.f);
    UPSAttributeLibrary::ApplyDamage(F.Character,200,F.Character);
    TestEqual(TEXT("Lethal damage creates downed state"), State->GetLifeState(),ESRHeroLifeState::Downed);
    TestFalse(TEXT("Ordinary healing cannot revive"), UPSAttributeLibrary::Heal(F.Character,50,F.Character));
    TestTrue(TEXT("Downed teammate can revive"), State->Revive(60,60));
    TestEqual(TEXT("Revived health"), F.Attributes->GetHealth(),60.f);
    TestEqual(TEXT("Revived shield"), F.Attributes->GetShield(),60.f);
    UPSAttributeLibrary::ApplyDamage(F.Character,500,F.Character);
    UPSAttributeLibrary::ApplyDamage(F.Character,5,F.Character);
    TestEqual(TEXT("Further lethal damage eliminates"), State->GetLifeState(),ESRHeroLifeState::Eliminated);
    TestFalse(TEXT("Eliminated players cannot revive"), State->Revive(60,60));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSRosterRewindTest, "ProjectS.Roster.BoundedDamageRewind",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPSRosterRewindTest::RunTest(const FString&)
{
    FSRAbilityFixture F; if (!F.Init(*this)) return false;
    F.ASC->SetNumericAttributeBase(UPSAttributeSet::GetShieldAttribute(),0);
    UPSAttributeLibrary::ApplyDamage(F.Character,65,F.Character);
    auto* State = F.Character->HeroGameplayComponent.Get();
    TestEqual(TEXT("Eligible recent loss"), State->RecentHealthLoss(),65.f);
    TestEqual(TEXT("Heal capped at fifty"), State->RewindHealthLoss(50,4),50.f);
    TestEqual(TEXT("Example restores 35 to 85"), F.Attributes->GetHealth(),85.f);
    TestEqual(TEXT("Already recovered damage consumed"), State->RecentHealthLoss(),15.f);
    for (int32 Index=0; Index<220; ++Index) F.World->Tick(LEVELTICK_All,0.02f);
    TestEqual(TEXT("Old damage expires"), State->RecentHealthLoss(),0.f);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSRosterEchoTest, "ProjectS.Roster.ImmutableLethalSnapshot",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPSRosterEchoTest::RunTest(const FString&)
{
    FSRAbilityFixture F; if (!F.Init(*this)) return false;
    F.ASC->SetNumericAttributeBase(UPSAttributeSet::GetHealthAttribute(),45);
    F.ASC->SetNumericAttributeBase(UPSAttributeSet::GetShieldAttribute(),20);
    F.ASC->SetNumericAttributeBase(UPSAttributeSet::GetUltimateChargeAttribute(),100);
    if (!F.GrantSlots(*this,MakeZyrexStyleData(F.ASC,nullptr,nullptr,nullptr,USRGameplayAbility_ParadoxEcho::StaticClass()))) return false;
    const FVector ActivationPosition = F.Character->GetActorLocation();
    TestTrue(TEXT("Echo activation"), F.ASC->TryActivateAbilityByClass(USRGameplayAbility_ParadoxEcho::StaticClass()));
    F.Character->SetActorLocation(FVector(900,0,110));
    UPSAttributeLibrary::ApplyDamage(F.Character,500,F.Character);
    TestTrue(TEXT("Returns to activation position"), F.Character->GetActorLocation().Equals(ActivationPosition,1.f));
    TestEqual(TEXT("Restores activation health"),F.Attributes->GetHealth(),45.f);
    TestEqual(TEXT("Restores activation shield"),F.Attributes->GetShield(),20.f);
    TestEqual(TEXT("Does not refund ultimate"),F.Attributes->GetUltimateCharge(),0.f);
    TestFalse(TEXT("One-time snapshot consumed"),F.Character->HeroGameplayComponent->HasEcho());
    UPSAttributeLibrary::ApplyDamage(F.Character,500,F.Character);
    TestEqual(TEXT("Second lethal hit is not rescued"),F.Attributes->GetHealth(),0.f);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSRosterTimeTest, "ProjectS.Roster.OverlappingTimeBubbles",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPSRosterTimeTest::RunTest(const FString&)
{
    FSRAbilityFixture F; if (!F.Init(*this)) return false;
    auto* Projectile = F.World->SpawnActor<ASRHeroProjectile>(FVector(500,0,400),FRotator::ZeroRotator);
    if (!Projectile) return false;
    auto Config = SRDefaultActionConfig(ESRHeroAction::ShardSpike); Config.Speed = 1000;
    Projectile->Initialize(F.Character,Config,FVector::ForwardVector);
    auto* Movement = Projectile->FindComponentByClass<UProjectileMovementComponent>(); if (!Movement) return false;
    const float Gravity = Movement->ProjectileGravityScale;
    auto BubbleConfig = SRDefaultActionConfig(ESRHeroAction::TimeBubble); BubbleConfig.MovementScale = 0.4f;
    auto* A = ASRHeroField::Spawn(F.World,F.Character,FVector(500,0,100),BubbleConfig);
    BubbleConfig.MovementScale = 0.25f;
    auto* B = ASRHeroField::Spawn(F.World,F.Character,FVector(600,0,100),BubbleConfig);
    if (!A || !B) return false;
    auto* Time = F.World->GetSubsystem<USRProjectileTimeSubsystem>(); Time->Tick(0);
    TestTrue(TEXT("Strongest overlapping slowdown applied once"),FMath::IsNearlyEqual(Movement->Velocity.Size(),250.,0.1));
    B->Destroy(); Time->Tick(0);
    TestTrue(TEXT("Remaining bubble still applies"),FMath::IsNearlyEqual(Movement->Velocity.Size(),400.,0.1));
    Projectile->SetActorLocation(FVector(3000,0,400)); Time->Tick(0);
    TestTrue(TEXT("Leaving restores speed"),FMath::IsNearlyEqual(Movement->Velocity.Size(),1000.,0.1));
    TestTrue(TEXT("Leaving restores gravity"),FMath::IsNearlyEqual(Movement->ProjectileGravityScale,Gravity,0.001f));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSRosterSpikeTest, "ProjectS.Roster.GroundOnlySpikeDamage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPSRosterSpikeTest::RunTest(const FString&)
{
    FSRAbilityFixture F; if (!F.Init(*this)) return false;
    auto* Grounded = SpawnRosterTarget(F,FVector(600,0,98),1);
    auto* Airborne = SpawnRosterTarget(F,FVector(600,180,500),1);
    if (!Grounded || !Airborne) return false;
    Airborne->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
    auto* Spikes = ASRHeroField::Spawn(F.World,F.Character,FVector(600,0,0),SRDefaultActionConfig(ESRHeroAction::ShardSpike));
    if (!Spikes) return false;
    TestEqual(TEXT("Impact does not apply an instant explosion"),Grounded->GetPSAttributes()->GetShield(),100.f);
    Spikes->Tick(0.6f);
    TestEqual(TEXT("Ground contact takes periodic damage"),Grounded->GetPSAttributes()->GetShield(),88.f);
    TestEqual(TEXT("Airborne target unaffected"),Airborne->GetPSAttributes()->GetShield(),100.f);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSRosterSwapTest, "ProjectS.Roster.DecoySwapWithoutLOS",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPSRosterSwapTest::RunTest(const FString&)
{
    FSRAbilityFixture F; if (!F.Init(*this)) return false;
    auto* A = F.World->SpawnActor<ASRHeroDecoy>(FVector(600,0,100),FRotator::ZeroRotator);
    auto* B = F.World->SpawnActor<ASRHeroDecoy>(FVector(600,700,100),FRotator::ZeroRotator);
    auto* Wall = F.World->SpawnActor<AStaticMeshActor>(FVector(300,0,200),FRotator::ZeroRotator);
    if (!A || !B || !Wall) return false;
    A->Initialize(F.Character,15,100,false); B->Initialize(F.Character,15,100,false);
    Wall->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
    Wall->GetStaticMeshComponent()->SetWorldScale3D(FVector(0.2,10,4)); Wall->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
    F.Character->HeroGameplayComponent->RegisterSwapDecoys({A,B});
    F.Character->HeroGameplayComponent->ServerContextAction();
    TestTrue(TEXT("Swap passes intervening wall with safe endpoints"),FMath::IsNearlyEqual(F.Character->GetActorLocation().X,600.,1.));
    TestTrue(TEXT("Selected decoy exchanges position"),FMath::IsNearlyEqual(A->GetActorLocation().X,0.,1.));
    const FVector After = F.Character->GetActorLocation();
    F.Character->HeroGameplayComponent->ServerContextAction();
    TestTrue(TEXT("Only one successful swap"),F.Character->GetActorLocation().Equals(After));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSRosterHealingTeamsTest, "ProjectS.Roster.HealingTeamFilter",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPSRosterHealingTeamsTest::RunTest(const FString&)
{
    FSRAbilityFixture F; if (!F.Init(*this)) return false;
    auto* Ally = SpawnRosterTarget(F,FVector(300,100,100),0);
    auto* Enemy = SpawnRosterTarget(F,FVector(300,-100,100),1);
    if (!Ally || !Enemy) return false;
    Ally->GetAbilitySystemComponent()->SetNumericAttributeBase(UPSAttributeSet::GetHealthAttribute(),50);
    Enemy->GetAbilitySystemComponent()->SetNumericAttributeBase(UPSAttributeSet::GetHealthAttribute(),50);
    auto* Plant = ASRHeroField::Spawn(F.World,F.Character,FVector(100,0,0),SRDefaultActionConfig(ESRHeroAction::HealingSeedling));
    if (!Plant) return false;
    Plant->Tick(0.5f);
    TestEqual(TEXT("Healing plant heals nearby ally"),Ally->GetPSAttributes()->GetHealth(),55.f);
    TestEqual(TEXT("Healing plant does not heal enemy"),Enemy->GetPSAttributes()->GetHealth(),50.f);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSRosterSnareEntryTest, "ProjectS.Roster.SnareLaterEntrant",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPSRosterSnareEntryTest::RunTest(const FString&)
{
    FSRAbilityFixture F; if (!F.Init(*this)) return false;
    auto* Enemy = SpawnRosterTarget(F,FVector(500,900,100),1);
    auto* Ally = SpawnRosterTarget(F,FVector(500,100,100),0);
    if (!Enemy || !Ally) return false;
    auto* Field = ASRHeroField::Spawn(F.World,F.Character,FVector(500,0,0),SRDefaultActionConfig(ESRHeroAction::VoidSnare));
    if (!Field) return false;
    TestFalse(TEXT("Allied occupants not rooted"),Ally->IsPSRooted());
    Enemy->SetActorLocation(FVector(500,-100,100));
    TestTrue(TEXT("Later entrant triggers root overlap"),Enemy->GetAbilitySystemComponent()->HasMatchingGameplayTag(SRGameplayTags::Tag_State_Rooted));
    TestEqual(TEXT("One root per field"),Enemy->GetAbilitySystemComponent()->GetTagCount(SRGameplayTags::Tag_State_Rooted),1);
    Field->Tick(0.6f);
    TestEqual(TEXT("Remaining inside does not stack roots"),Enemy->GetAbilitySystemComponent()->GetTagCount(SRGameplayTags::Tag_State_Rooted),1);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSRosterAssetsTest, "ProjectS.Roster.ConfiguredAssets",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPSRosterAssetsTest::RunTest(const FString&)
{
    for (const FSRHeroDefinition& Definition : SRHeroRoster::All())
    {
        const FString Root = SRHeroRoster::HeroAssetRoot(Definition.ID);
        auto* Hero = LoadObject<USRHeroDataAsset>(nullptr,*(Root + TEXT("DA_") + Definition.ID.ToString()));
        auto* Weapon = LoadObject<USRWeaponDataAsset>(nullptr,*(Root + TEXT("DA_") + Definition.ID.ToString() + TEXT("Weapon")));
        UClass* PawnClass = SRHeroRoster::LoadPawnClass(Definition.ID);
        if (!TestNotNull(*Definition.ID.ToString(),Hero) || !TestNotNull(TEXT("Hero weapon"),Weapon) || !TestNotNull(TEXT("Hero pawn"),PawnClass)) return false;
        TestEqual(TEXT("Correct hero data identity"),Hero->HeroID,Definition.ID);
        const auto* Defaults = Cast<APSBaseCharacter>(PawnClass->GetDefaultObject());
        if (!TestNotNull(TEXT("Shared character inheritance"),Defaults)) return false;
        TestTrue(TEXT("Character references its own data and weapon"),Defaults->HeroData == Hero && Defaults->WeaponData == Weapon);
        const TSubclassOf<UGameplayAbility> Slots[] = { Hero->AbilitySlots.AbilityQ,Hero->AbilitySlots.AbilityE,Hero->AbilitySlots.AbilityC,Hero->AbilitySlots.AbilityX };
        for (int32 Slot=0; Slot<4; ++Slot)
            TestTrue(TEXT("Configured slot preserves its distinct native mechanic"),Slots[Slot] && Slots[Slot]->IsChildOf(Definition.Abilities[Slot]));
    }
    return true;
}
#endif
