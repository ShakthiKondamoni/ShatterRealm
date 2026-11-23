#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "HAL/PlatformProcess.h"
#include "GAS/PSAttributeSet.h"
#include "Character/PSBaseCharacter.h"
#include "GAS/PSPlayerState.h"
#include "Combat/SRCombatComponent.h"
#include "Character/SRCombatDummy.h"
#include "Components/BoxComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Abilities/SRAbilitySystemComponent.h"
#include "Abilities/SRGameplayTags.h"
#include "Combat/SRWeaponDataAsset.h"
#include "Combat/SRCombatPresentationComponent.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"

namespace
{
/** Character + PlayerState + ASC wired in a transient world, weapon config applied. */
struct FSRCombatFixture
{
    UWorld* World = nullptr;
    APSPlayerState* State = nullptr;
    APSBaseCharacter* Character = nullptr;
    UPSCombatComponent* Combat = nullptr;
    UPSAttributeSet* Attributes = nullptr;

    ~FSRCombatFixture() { if (World) World->DestroyWorld(false); }

    bool Init(FAutomationTestBase& Test, float WeaponDamage = 22.f)
    {
        const UWorld::InitializationValues Init = UWorld::InitializationValues()
            .AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
        World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
        if (!Test.TestNotNull(TEXT("Transient combat world"), World)) return false;
        // Begin the world so spawned actors run their BeginPlay (combat init, HUD, dummy registration).
        World->BeginPlay();

        State = World->SpawnActor<APSPlayerState>();
        if (!Test.TestNotNull(TEXT("PlayerState"), State)) return false;
        State->PSAbilitySystem->AddAttributeSetSubobject(State->PSAttributes.Get());
        State->PSAbilitySystem->InitAbilityActorInfo(State, State);
        Attributes = State->PSAttributes.Get();
        if (!Test.TestNotNull(TEXT("Attribute set"), Attributes)) return false;

        Character = World->SpawnActor<APSBaseCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
        if (!Test.TestNotNull(TEXT("Character"), Character)) return false;
        Character->SetPlayerState(State); // death checks resolve attributes via the PlayerState
        State->PSAbilitySystem->InitAbilityActorInfo(State, Character);

        Combat = Character->GetCombatComponent();
        if (!Test.TestNotNull(TEXT("Combat component"), Combat)) return false;

        USRWeaponDataAsset* Weapon = NewObject<USRWeaponDataAsset>(Combat, NAME_None, RF_Transient);
        Weapon->Damage = WeaponDamage;
        Weapon->FireRate = 7.f;
        Weapon->MaxCharges = 30;
        Weapon->RechargeDuration = 1.8f;
        Character->WeaponData = Weapon;
        Combat->SetWeaponData(Weapon);
        return true;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSCombatShieldFirstDamageTest, "ProjectS.Combat.ShieldFirstDamage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSCombatShieldFirstDamageTest::RunTest(const FString& Parameters)
{
    FSRCombatFixture F;
    if (!F.Init(*this)) return false;

    // Damage dummy: shield absorbs first, then health.
    APSCombatDummy* Dummy = F.World->SpawnActor<APSCombatDummy>(FVector(500, 0, 100), FRotator::ZeroRotator);
    if (!TestNotNull(TEXT("Combat dummy"), Dummy)) return false;

    UAbilitySystemComponent* DummyASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Dummy);
    if (!TestNotNull(TEXT("Dummy ASC"), DummyASC)) return false;
    DummyASC->AddAttributeSetSubobject(Dummy->GetAttributes());
    TestEqual(TEXT("Dummy spawned sets after add"), DummyASC->GetSpawnedAttributes().Num(), 1);
    TestTrue(TEXT("Dummy ASC registered for IncomingDamage"), DummyASC->HasAttributeSetForAttribute(UPSAttributeSet::GetIncomingDamageAttribute()));
    TestTrue(TEXT("Dummy has authority"), Dummy->HasAuthority());
    TestTrue(TEXT("Instigator has authority"), F.Character->HasAuthority());

    bool bEnemy = false;
    const float Applied1 = F.Combat->ApplyCombatDamage(Dummy, 22.f, F.Character, bEnemy);
    TestTrue(TEXT("Damage accepted on dummy"), bEnemy);
    TestEqual(TEXT("First hit: shield absorbs"), Dummy->GetDummyShield(), 78.f);
    TestEqual(TEXT("First hit: health untouched"), Dummy->GetDummyHealth(), 100.f);
    TestEqual(TEXT("Applied equals requested"), Applied1, 22.f);

    // Overflow: shield 78 - 100 damage => shield 0, health 78.
    F.Combat->ApplyCombatDamage(Dummy, 100.f, F.Character, bEnemy);
    TestEqual(TEXT("Overflow: shield depleted"), Dummy->GetDummyShield(), 0.f);
    TestEqual(TEXT("Overflow: health reduced by remainder"), Dummy->GetDummyHealth(), 78.f);

    // Dead targets take no further damage.
    F.Combat->ApplyCombatDamage(Dummy, 1000.f, F.Character, bEnemy);
    TestEqual(TEXT("Lethal damage clamps health"), Dummy->GetDummyHealth(), 0.f);
    const float AfterDeath = F.Combat->ApplyCombatDamage(Dummy, 22.f, F.Character, bEnemy);
    TestEqual(TEXT("Dead target takes no damage"), AfterDeath, 0.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSCombatFriendlyFireTest, "ProjectS.Combat.FriendlyFireRejected",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSCombatFriendlyFireTest::RunTest(const FString& Parameters)
{
    FSRCombatFixture F;
    if (!F.Init(*this)) return false;

    // Another hero on the same team (default 0): friendly fire must be rejected.
    APSBaseCharacter* Ally = F.World->SpawnActor<APSBaseCharacter>(FVector(500, 0, 100), FRotator::ZeroRotator);
    if (!TestNotNull(TEXT("Ally character"), Ally)) return false;
    F.State->PSAbilitySystem->InitAbilityActorInfo(F.State, Ally);

    bool bEnemy = false;
    const float Applied = F.Combat->ApplyCombatDamage(Ally, 22.f, F.Character, bEnemy);
    TestFalse(TEXT("Ally is not an enemy"), bEnemy);
    TestEqual(TEXT("Friendly fire rejected"), Applied, 0.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSCombatFireRateAndChargesTest, "ProjectS.Combat.FireRateAndCharges",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSCombatFireRateAndChargesTest::RunTest(const FString& Parameters)
{
    FSRCombatFixture F;
    if (!F.Init(*this)) return false;
    F.Combat->SetPrimaryChargesForTest(30);

    // Sequential shots respect the authoritative fire interval (7/s => ~0.1429s).
    F.Combat->SetLastShotRealTimeForTest(FPlatformTime::Seconds());
    FPlatformProcess::Sleep(0.15f);
    TestTrue(TEXT("First shot after interval fires"), F.Combat->TryExecutePrimaryShot());
    TestEqual(TEXT("Charge consumed"), F.Combat->GetCurrentCharges(), 29);

    // Immediate second shot is rejected by the rate gate.
    TestFalse(TEXT("Second shot within interval rejected"), F.Combat->TryExecutePrimaryShot());
    TestEqual(TEXT("Rate gate consumed no charge"), F.Combat->GetCurrentCharges(), 29);

    // Out of charges: no shot possible.
    F.Combat->SetPrimaryChargesForTest(0);
    FPlatformProcess::Sleep(0.15f);
    TestFalse(TEXT("No charges: shot rejected"), F.Combat->TryExecutePrimaryShot());

    // Recharge restores to full exactly once.
    TestTrue(TEXT("Recharge accepted at zero charges"), F.Combat->StartRechargeForTest());
    TestTrue(TEXT("Recharging state"), F.Combat->IsRecharging());
    TestFalse(TEXT("Second recharge rejected"), F.Combat->StartRechargeForTest());
    F.Combat->FinishPrimaryRecharge();
    TestEqual(TEXT("Recharge restored charges"), F.Combat->GetCurrentCharges(), 30);
    TestFalse(TEXT("Recharge state cleared"), F.Combat->IsRecharging());
    TestFalse(TEXT("Recharge completion only once"), F.Combat->FinishPrimaryRechargeGuarded());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSCombatRechargeRejectionTest, "ProjectS.Combat.RechargeRejection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSCombatRechargeRejectionTest::RunTest(const FString& Parameters)
{
    FSRCombatFixture F;
    if (!F.Init(*this)) return false;
    F.Combat->SetPrimaryChargesForTest(30); // already full

    FString Reason;
    TestFalse(TEXT("Recharge at full charges rejected"), F.Combat->CanStartRechargeForTest(Reason));
    TestTrue(TEXT("Rejection reason mentions full"), Reason.Contains(TEXT("full")));
    return true;
}

// ---------------------------------------------------------------------------
// Task 02: hitscan damage model, hitbox geometry and shot validation
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSShotDamageCalculationTest, "ProjectS.Combat.ShotDamageCalculation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSShotDamageCalculationTest::RunTest(const FString& Parameters)
{
    // Task 02 default weapon parameters (asset defaults).
    USRWeaponDataAsset* Weapon = NewObject<USRWeaponDataAsset>();
    TestEqual(TEXT("base damage default"), Weapon->Damage, 22.f);
    TestEqual(TEXT("head multiplier default"), Weapon->HeadshotMultiplier, 1.75f);
    TestEqual(TEXT("torso multiplier default"), Weapon->TorsoDamageMultiplier, 1.f);
    TestEqual(TEXT("pelvis multiplier default"), Weapon->PelvisDamageMultiplier, 1.f);
    TestEqual(TEXT("arm multiplier default"), Weapon->ArmDamageMultiplier, 0.85f);
    TestEqual(TEXT("leg multiplier default"), Weapon->LegDamageMultiplier, 0.85f);
    TestEqual(TEXT("full damage range default"), Weapon->FullDamageRange, 2000.f);
    TestEqual(TEXT("falloff end default"), Weapon->MinDamageRange, 9000.f);
    TestEqual(TEXT("min damage multiplier default"), Weapon->MinDamageMultiplier, 0.65f);

    // Body-part multipliers inside the full-damage range.
    TestEqual(TEXT("head at 5m"), UPSCombatComponent::ComputeShotDamage(Weapon, 500, EPSDamageRegion::Head), 22.f * 1.75f);
    TestEqual(TEXT("torso at 5m"), UPSCombatComponent::ComputeShotDamage(Weapon, 500, EPSDamageRegion::Torso), 22.f);
    TestEqual(TEXT("pelvis at 5m"), UPSCombatComponent::ComputeShotDamage(Weapon, 500, EPSDamageRegion::Pelvis), 22.f);
    TestEqual(TEXT("arm at 5m"), UPSCombatComponent::ComputeShotDamage(Weapon, 500, EPSDamageRegion::Arm), 22.f * 0.85f);
    TestEqual(TEXT("leg at 5m"), UPSCombatComponent::ComputeShotDamage(Weapon, 500, EPSDamageRegion::Leg), 22.f * 0.85f);

    // Linear falloff boundaries: full damage up to 20m, 0.65 at 90m, linear between.
    TestEqual(TEXT("full damage at boundary"), UPSCombatComponent::ComputeShotDamage(Weapon, 2000, EPSDamageRegion::Torso), 22.f);
    TestEqual(TEXT("falloff midpoint"), UPSCombatComponent::ComputeShotDamage(Weapon, 5500, EPSDamageRegion::Torso), 22.f * (1.f + 0.65f) * 0.5f);
    TestEqual(TEXT("minimum at falloff end"), UPSCombatComponent::ComputeShotDamage(Weapon, 9000, EPSDamageRegion::Torso), 22.f * 0.65f);
    TestEqual(TEXT("clamped beyond falloff end"), UPSCombatComponent::ComputeShotDamage(Weapon, 20000, EPSDamageRegion::Torso), 22.f * 0.65f);
    TestEqual(TEXT("no weapon yields zero"), UPSCombatComponent::ComputeShotDamage(nullptr, 100, EPSDamageRegion::Torso), 0.f);

    // Custom weapon: falloff parameters are honored, not hard-coded.
    Weapon->FullDamageRange = 100.f;
    Weapon->MinDamageRange = 1100.f;
    Weapon->MinDamageMultiplier = 0.5f;
    TestEqual(TEXT("custom full boundary"), UPSCombatComponent::ComputeShotDamage(Weapon, 100, EPSDamageRegion::Torso), 22.f);
    TestEqual(TEXT("custom midpoint"), UPSCombatComponent::ComputeShotDamage(Weapon, 600, EPSDamageRegion::Torso), 22.f * 0.75f);
    TestEqual(TEXT("custom end"), UPSCombatComponent::ComputeShotDamage(Weapon, 1100, EPSDamageRegion::Torso), 11.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSHitboxRayIntersectionTest, "ProjectS.Combat.HitboxRayIntersection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSHitboxRayIntersectionTest::RunTest(const FString& Parameters)
{
    const FTransform Identity = FTransform::Identity;
    const FVector Extent(50.f, 25.f, 25.f);
    float Fraction = -1.f;

    // Center hit: entry at x=-50 of a [-100,100] segment -> 0.75.
    TestTrue(TEXT("center hit"), APSBaseCharacter::DamageBoxRayHit(Identity, Extent, FVector(-100, 0, 0), FVector(100, 0, 0), Fraction));
    TestEqual(TEXT("center entry fraction"), Fraction, 0.25f);

    // Misses: outside every slab, from behind pointing away, and short of the box.
    TestFalse(TEXT("above miss"), APSBaseCharacter::DamageBoxRayHit(Identity, Extent, FVector(-100, 0, 50), FVector(100, 0, 50), Fraction));
    TestFalse(TEXT("behind miss"), APSBaseCharacter::DamageBoxRayHit(Identity, Extent, FVector(-200, 0, 0), FVector(-300, 0, 0), Fraction));
    TestFalse(TEXT("before box miss"), APSBaseCharacter::DamageBoxRayHit(Identity, Extent, FVector(-100, 0, 0), FVector(-80, 0, 0), Fraction));

    // Start inside the box: entry fraction clamps to 0.
    TestTrue(TEXT("inside hit"), APSBaseCharacter::DamageBoxRayHit(Identity, Extent, FVector(0, 0, 0), FVector(100, 0, 0), Fraction));
    TestEqual(TEXT("inside entry fraction"), Fraction, 0.f);

    // Rotation respected: a 45-degree yaw moves the box's reachable slab in y.
    const FTransform Rotated(FRotator(0.f, 45.f, 0.f));
    TestFalse(TEXT("unrotated miss at y=40"), APSBaseCharacter::DamageBoxRayHit(Identity, Extent, FVector(-100, 40, 0), FVector(100, 40, 0), Fraction));
    TestTrue(TEXT("rotated hit at y=40"), APSBaseCharacter::DamageBoxRayHit(Rotated, Extent, FVector(-100, 40, 0), FVector(100, 40, 0), Fraction));

    // Nearest-entry ordering: a head box closer along the ray wins over a torso
    // box (the rule ResolveDamageRegion uses to prioritize headshots).
    float HeadFraction = 0.f;
    float TorsoFraction = 0.f;
    const FTransform HeadTransform(FVector(60.f, 0.f, 100.f));
    const FTransform TorsoTransform(FVector(100.f, 0.f, 100.f));
    TestTrue(TEXT("head hit"), APSBaseCharacter::DamageBoxRayHit(HeadTransform, FVector(15.f, 15.f, 18.f), FVector(-200, 0, 100), FVector(600, 0, 100), HeadFraction));
    TestTrue(TEXT("torso hit"), APSBaseCharacter::DamageBoxRayHit(TorsoTransform, FVector(25.f, 20.f, 30.f), FVector(-200, 0, 100), FVector(600, 0, 100), TorsoFraction));
    TestTrue(TEXT("head entry is nearer (headshot priority)"), HeadFraction < TorsoFraction);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSHitRegionSelectionTest, "ProjectS.Combat.HitRegionSelection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSHitRegionSelectionTest::RunTest(const FString& Parameters)
{
    FSRCombatFixture F;
    if (!F.Init(*this)) return false;

    // Replace the bone-attached hitboxes (the fixture skeleton has no bones) with
    // root-attached boxes at controlled world positions: index 0 = Head, 1 = Torso
    // (PSHitRegions maps by index in that order).
    F.Character->PSDamageHitboxes.Reset();
    UBoxComponent* HeadBox = NewObject<UBoxComponent>(F.Character, TEXT("TestHeadBox"));
    HeadBox->SetupAttachment(F.Character->GetRootComponent());
    HeadBox->SetBoxExtent(FVector(15.f, 15.f, 18.f));
    UBoxComponent* TorsoBox = NewObject<UBoxComponent>(F.Character, TEXT("TestTorsoBox"));
    TorsoBox->SetupAttachment(F.Character->GetRootComponent());
    TorsoBox->SetBoxExtent(FVector(25.f, 20.f, 30.f));
    F.Character->PSDamageHitboxes.Add(HeadBox);
    F.Character->PSDamageHitboxes.Add(TorsoBox);
    HeadBox->SetWorldTransform(FTransform(FVector(60.f, 0.f, 100.f)));
    TorsoBox->SetWorldTransform(FTransform(FVector(100.f, 0.f, 100.f)));
    HeadBox->RegisterComponent();
    TorsoBox->RegisterComponent();

    // Ray passing through both: the nearer head box wins (headshot prioritization).
    EPSDamageRegion Region = EPSDamageRegion::Torso;
    FVector Impact;
    TestTrue(TEXT("region resolved"), F.Character->ResolveDamageRegion(FVector(-200, 0, 100), FVector(600, 0, 100), Region, Impact));
    TestEqual(TEXT("head region selected (nearest entry)"), Region, EPSDamageRegion::Head);
    TestTrue(TEXT("impact inside head slab"), FMath::Abs(Impact.X - (60.f - 15.f)) < 3.f);

    // Ray missing both boxes: no region resolved (caller falls back to Torso).
    TestFalse(TEXT("ray above misses all"), F.Character->ResolveDamageRegion(FVector(-200, 0, 400), FVector(600, 0, 400), Region, Impact));

    // Torso-only ray: offset in y past the head slab but inside the torso slab.
    TestTrue(TEXT("torso-only ray"), F.Character->ResolveDamageRegion(FVector(-200, 18, 100), FVector(600, 18, 100), Region, Impact));
    TestEqual(TEXT("torso region on torso-only ray"), Region, EPSDamageRegion::Torso);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSShotWorldObstructionTest, "ProjectS.Combat.ShotWorldObstruction",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSShotWorldObstructionTest::RunTest(const FString& Parameters)
{
    FSRCombatFixture F;
    if (!F.Init(*this)) return false;

    APSCombatDummy* Dummy = F.World->SpawnActor<APSCombatDummy>(FVector(500, 0, 100), FRotator::ZeroRotator);
    if (!TestNotNull(TEXT("Dummy"), Dummy)) return false;
    Dummy->GetAbilitySystemComponent()->AddAttributeSetSubobject(Dummy->GetAttributes());

    // Open shot: the pawn-view aim ray (+X) reaches the dummy and applies damage.
    F.Combat->SetPrimaryChargesForTest(30);
    F.Combat->SetLastShotRealTimeForTest(0);
    TestTrue(TEXT("open shot executed"), F.Combat->TryExecutePrimaryShot());
    TestEqual(TEXT("open shot damaged dummy"), Dummy->GetDummyShield(), 78.f);

    // Wall between shooter and dummy: world geometry takes precedence over hits.
    AStaticMeshActor* Wall = F.World->SpawnActor<AStaticMeshActor>(FVector(250, 0, 100), FRotator::ZeroRotator);
    if (!TestNotNull(TEXT("Wall"), Wall)) return false;
    Wall->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
    Wall->GetStaticMeshComponent()->SetWorldScale3D(FVector(1, 8, 2));
    Wall->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
    Wall->SetMobility(EComponentMobility::Static);

    F.Combat->SetLastShotRealTimeForTest(0);
    TestTrue(TEXT("obstructed shot executed"), F.Combat->TryExecutePrimaryShot());
    TestEqual(TEXT("no damage through wall"), Dummy->GetDummyShield(), 78.f);
    TestEqual(TEXT("charge still consumed on blocked shot"), F.Combat->GetCurrentCharges(), 28);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSShotValidationRejectionsTest, "ProjectS.Combat.ShotValidationRejections",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSShotValidationRejectionsTest::RunTest(const FString& Parameters)
{
    FSRCombatFixture F;
    if (!F.Init(*this)) return false;

    // Dead shooter: rejected before any trace or charge consumption.
    F.State->PSAbilitySystem->SetNumericAttributeBase(UPSAttributeSet::GetHealthAttribute(), 0.f);
    TestTrue(TEXT("shooter dead"), F.Character->IsPSDead());
    F.Combat->SetPrimaryChargesForTest(30);
    F.Combat->SetLastShotRealTimeForTest(0);
    TestFalse(TEXT("dead shooter shot rejected"), F.Combat->TryExecutePrimaryShot());
    TestEqual(TEXT("dead shooter charges unchanged"), F.Combat->GetCurrentCharges(), 30);

    // Recover, then a target beyond the configured weapon range is never hit.
    F.State->PSAbilitySystem->SetNumericAttributeBase(UPSAttributeSet::GetHealthAttribute(), 100.f);
    F.Combat->GetWeaponData()->Range = 500.f; // dummy sits at 700cm
    F.Combat->SetLastShotRealTimeForTest(0);
    TestTrue(TEXT("out-of-range shot executes (and misses)"), F.Combat->TryExecutePrimaryShot());
    TestEqual(TEXT("no damage out of range"), F.Combat->GetCurrentCharges(), 29);

    // Fire-rate gate: an immediate second shot is rejected and consumes nothing.
    F.Combat->GetWeaponData()->Range = 12000.f;
    F.Combat->SetLastShotRealTimeForTest(FPlatformTime::Seconds());
    TestFalse(TEXT("rate-gated shot rejected"), F.Combat->TryExecutePrimaryShot());
    TestEqual(TEXT("rate-gated charges unchanged"), F.Combat->GetCurrentCharges(), 29);
    return true;
}

// ---------------------------------------------------------------------------
// Task 03: fire prediction, reconciliation and recoil pattern
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSFirePredictionSequenceTest, "ProjectS.FirePrediction.SequenceAndQueue",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSFirePredictionSequenceTest::RunTest(const FString& Parameters)
{
    FSRCombatFixture F;
    if (!F.Init(*this)) return false;

    // Shot sequence uniqueness: ids are strictly increasing.
    const int32 Id1 = F.Combat->PushPredictedShotForTest(FPlatformTime::Seconds());
    const int32 Id2 = F.Combat->PushPredictedShotForTest(FPlatformTime::Seconds());
    const int32 Id3 = F.Combat->PushPredictedShotForTest(FPlatformTime::Seconds());
    TestTrue(TEXT("sequence ids unique"), Id1 != Id2 && Id2 != Id3 && Id1 != Id3);
    TestTrue(TEXT("sequence ids increase"), Id2 > Id1 && Id3 > Id2);
    TestEqual(TEXT("pending tracks predictions"), F.Combat->GetPendingPredictedShotCount(), 3);

    // Bounded queue: pushing past the cap drops the oldest, never grows unbounded.
    for (int32 Index = 0; Index < 20; ++Index)
    {
        F.Combat->PushPredictedShotForTest(FPlatformTime::Seconds());
    }
    TestEqual(TEXT("pending queue capped"), F.Combat->GetPendingPredictedShotCount(), 16);

    // Reset clears everything (weapon change / death / respawn path).
    F.Combat->ResetFirePrediction(TEXT("test"));
    TestEqual(TEXT("reset clears pending"), F.Combat->GetPendingPredictedShotCount(), 0);

    // Display charges: authoritative minus in-flight predictions, never negative.
    F.Combat->SetPrimaryChargesForTest(3);
    F.Combat->PushPredictedShotForTest(FPlatformTime::Seconds());
    F.Combat->PushPredictedShotForTest(FPlatformTime::Seconds());
    TestEqual(TEXT("display charges = authoritative - pending"), F.Combat->GetDisplayCharges(), 1);
    for (int32 Index = 0; Index < 5; ++Index)
    {
        F.Combat->PushPredictedShotForTest(FPlatformTime::Seconds());
    }
    TestEqual(TEXT("display charges clamp at zero"), F.Combat->GetDisplayCharges(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSFirePredictionAckTest, "ProjectS.FirePrediction.Acknowledgment",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSFirePredictionAckTest::RunTest(const FString& Parameters)
{
    FSRCombatFixture F;
    if (!F.Init(*this)) return false;

    const int32 ClientShot1 = F.Combat->PushPredictedShotForTest(FPlatformTime::Seconds());
    const int32 ClientShot2 = F.Combat->PushPredictedShotForTest(FPlatformTime::Seconds());

    // First acknowledgment matches the prediction and drains one pending slot.
    TestTrue(TEXT("ack matches prediction"), F.Combat->AcknowledgePredictedShotForTest(101, ClientShot1));
    TestEqual(TEXT("pending drained once"), F.Combat->GetPendingPredictedShotCount(), 1);

    // Duplicate acknowledgment of the same server shot is ignored entirely.
    TestFalse(TEXT("duplicate ack rejected"), F.Combat->AcknowledgePredictedShotForTest(101, ClientShot1));
    TestEqual(TEXT("duplicate ack does not drain"), F.Combat->GetPendingPredictedShotCount(), 1);

    // A second, different server shot acknowledges the remaining prediction.
    TestTrue(TEXT("second ack matches"), F.Combat->AcknowledgePredictedShotForTest(102, ClientShot2));
    TestEqual(TEXT("queue empty after acks"), F.Combat->GetPendingPredictedShotCount(), 0);

    // An ack with no matching prediction (client never predicted it) is reported
    // as unmatched without corrupting the queue.
    TestFalse(TEXT("unpredicted ack unmatched"), F.Combat->AcknowledgePredictedShotForTest(103, 999));
    TestEqual(TEXT("queue still empty"), F.Combat->GetPendingPredictedShotCount(), 0);

    // Ammunition reconciliation: predicted display tracks authoritative state
    // through the pending queue, then snaps back after a reset.
    F.Combat->SetPrimaryChargesForTest(30);
    F.Combat->PushPredictedShotForTest(FPlatformTime::Seconds());
    TestEqual(TEXT("predicted display decrements"), F.Combat->GetDisplayCharges(), 29);
    F.Combat->ResetFirePrediction(TEXT("reconcile test"));
    TestEqual(TEXT("authoritative restored after reset"), F.Combat->GetDisplayCharges(), 30);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSRecoilPatternTest, "ProjectS.FirePrediction.RecoilPattern",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSRecoilPatternTest::RunTest(const FString& Parameters)
{
    USRWeaponDataAsset* Weapon = NewObject<USRWeaponDataAsset>();
    Weapon->RecoilPattern = {
        FVector2D(1.0f, 0.0f), FVector2D(1.2f, 0.1f), FVector2D(1.4f, -0.1f)
    };
    Weapon->RecoilResetDelay = 0.25f;

    // Inactivity (gap > reset delay) restarts at the first entry.
    TestEqual(TEXT("inactive restart"), USRCombatPresentationComponent::ResolveRecoilPatternIndex(Weapon, 2, 0.5f), 0);

    // Continuous fire advances the pattern.
    TestEqual(TEXT("advance 0->1"), USRCombatPresentationComponent::ResolveRecoilPatternIndex(Weapon, 0, 0.1f), 1);
    TestEqual(TEXT("advance 1->2"), USRCombatPresentationComponent::ResolveRecoilPatternIndex(Weapon, 1, 0.1f), 2);

    // Pattern end clamps at the last entry (no wrap-around drift).
    TestEqual(TEXT("clamp at last"), USRCombatPresentationComponent::ResolveRecoilPatternIndex(Weapon, 2, 0.1f), 2);
    TestEqual(TEXT("clamp holds"), USRCombatPresentationComponent::ResolveRecoilPatternIndex(Weapon, 2, 0.05f), 2);

    // Kick lookup honors the pattern and perspective multipliers.
    // (Verified through the same weapon: defaults table matches Task 03 spec.)
    TestEqual(TEXT("fallback pitch default"), Weapon->RecoilPitch, 1.2f);
    TestEqual(TEXT("fallback yaw default"), Weapon->RecoilYaw, 0.35f);
    TestEqual(TEXT("reset delay default"), Weapon->RecoilResetDelay, 0.25f);
    TestEqual(TEXT("recovery duration default"), Weapon->RecoilRecoveryDuration, 0.18f);
    TestEqual(TEXT("ADS multiplier default"), Weapon->ADSRecoilMultiplier, 0.8f);
    TestEqual(TEXT("FP multiplier default"), Weapon->FPRecoilMultiplier, 1.0f);
    TestEqual(TEXT("TP multiplier default"), Weapon->TPRecoilMultiplier, 0.75f);

    // Empty pattern falls back to the single-shot kick (resolver stays at 0).
    Weapon->RecoilPattern.Reset();
    TestEqual(TEXT("empty pattern stays at fallback"), USRCombatPresentationComponent::ResolveRecoilPatternIndex(Weapon, 1, 0.1f), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSFireStateTransitionsTest, "ProjectS.FirePrediction.StateTransitions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSFireStateTransitionsTest::RunTest(const FString& Parameters)
{
    FSRCombatFixture F;
    if (!F.Init(*this)) return false;

    // Weapon change resets prediction (queued shots from the old weapon are gone).
    F.Combat->PushPredictedShotForTest(FPlatformTime::Seconds());
    TestEqual(TEXT("pending before weapon change"), F.Combat->GetPendingPredictedShotCount(), 1);
    USRWeaponDataAsset* NewWeapon = NewObject<USRWeaponDataAsset>(F.Combat, NAME_None, RF_Transient);
    NewWeapon->Damage = 10.f;
    NewWeapon->MaxCharges = 12;
    F.Combat->SetWeaponData(NewWeapon);
    TestEqual(TEXT("weapon change resets prediction"), F.Combat->GetPendingPredictedShotCount(), 0);
    TestEqual(TEXT("weapon change resets display"), F.Combat->GetDisplayCharges(), 12);

    // Reload (recharge) blocks predicted firing display: the authoritative flag
    // drives the gates checked by PredictShot.
    F.Combat->SetPrimaryChargesForTest(0);
    TestTrue(TEXT("recharge starts at empty"), F.Combat->StartRechargeForTest());
    TestTrue(TEXT("recharging state set"), F.Combat->IsRecharging());
    TestEqual(TEXT("no display charges while reloading"), F.Combat->GetDisplayCharges(), 0);

    // Recharge completion restores authoritative charges and unblocks display.
    F.Combat->FinishPrimaryRechargeGuarded();
    TestFalse(TEXT("recharge finished"), F.Combat->IsRecharging());
    TestEqual(TEXT("charges restored"), F.Combat->GetDisplayCharges(), 12);
    return true;
}

#endif
