#include "Character/SRHeroGameplayComponent.h"
#include "Character/PSBaseCharacter.h"
#include "Abilities/SRHeroActionAbility.h"
#include "Abilities/SRHeroGameplayLibrary.h"
#include "Abilities/SRGameplayTags.h"
#include "Combat/SRHeroGameplayActors.h"
#include "GAS/PSAttributeSet.h"
#include "GAS/PSAttributeLibrary.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

USRHeroGameplayComponent::USRHeroGameplayComponent()
{
    SetIsReplicatedByDefault(true);
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickInterval = 0.05f;
}
APSBaseCharacter* USRHeroGameplayComponent::GetHero() const { return Cast<APSBaseCharacter>(GetOwner()); }
void USRHeroGameplayComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(USRHeroGameplayComponent, LifeState);
    DOREPLIFETIME(USRHeroGameplayComponent, WardHealth);
    DOREPLIFETIME(USRHeroGameplayComponent, bConcealed);
    DOREPLIFETIME_CONDITION(USRHeroGameplayComponent, BlindStrength, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(USRHeroGameplayComponent, bAwaitingMapTarget, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(USRHeroGameplayComponent, MapSelectionsRemaining, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(USRHeroGameplayComponent, bSwapAvailable, COND_OwnerOnly);
}
void USRHeroGameplayComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    APSBaseCharacter* Character = GetHero();
    if (!Character) return;
    if (!Character->HasAuthority()) { if (bConcealed) OnRep_Concealed(); return; }
    const double Now = GetWorld()->GetTimeSeconds();
    UAbilitySystemComponent* ASC = Character->GetAbilitySystemComponent();
    for (int32 Index = Wards.Num() - 1; Index >= 0; --Index)
        if (Wards[Index].Expires <= Now || Wards[Index].Remaining <= 0.f)
        {
            if (ASC) ASC->RemoveActiveGameplayEffect(Wards[Index].Effect);
            Wards.RemoveAtSwap(Index);
        }
    WardHealth = 0.f;
    for (const FSRWardEntry& Ward : Wards) WardHealth += Ward.Remaining;
    DamageHistory.RemoveAll([Now](const FSRDamageHistoryEntry& Entry) { return Entry.Time < Now - 4.; });
    if (BlindExpires <= Now) BlindStrength = 0.f;
    if (Snapshot.bValid && Snapshot.Expires <= Now)
    {
        Snapshot.bValid = false;
        if (ASC) ASC->RemoveActiveGameplayEffect(EchoEffect);
    }
    if (LifeState == ESRHeroLifeState::Downed && DownedExpires <= Now)
    {
        LifeState = ESRHeroLifeState::Eliminated;
        if (ASC)
        {
            ASC->RemoveActiveGameplayEffect(DownedEffect);
            ASC->SetNumericAttributeBase(UPSAttributeSet::GetHealthAttribute(), 0.f);
        }
    }
    const bool bNowConcealed = ASC && ASC->HasMatchingGameplayTag(SRGameplayTags::Tag_State_Concealed);
    if (bConcealed != bNowConcealed) { bConcealed = bNowConcealed; OnRep_Concealed(); }
    for (auto It = AuraEffects.CreateIterator(); It; ++It) if (!It.Key().IsValid()) It.RemoveCurrent();
}
void USRHeroGameplayComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    RemoveInput();
    if (APSBaseCharacter* Character = GetHero())
    {
        if (Character->HasAuthority())
        {
            if (UAbilitySystemComponent* ASC = Character->GetAbilitySystemComponent())
            {
                for (const FSRWardEntry& Ward : Wards) ASC->RemoveActiveGameplayEffect(Ward.Effect);
                for (const auto& Pair : AuraEffects) ASC->RemoveActiveGameplayEffect(Pair.Value);
                ASC->RemoveActiveGameplayEffect(EchoEffect);
                ASC->RemoveActiveGameplayEffect(DownedEffect);
            }
            for (const auto& Decoy : SwapDecoys) if (Decoy.IsValid()) Decoy->Destroy();
        }
    }
    Super::EndPlay(Reason);
}
void USRHeroGameplayComponent::InstallInput(UInputComponent* Input)
{
    APSBaseCharacter* Character = GetHero();
    UEnhancedInputComponent* Enhanced = Cast<UEnhancedInputComponent>(Input);
    APlayerController* PC = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
    if (!Enhanced || !PC || !PC->IsLocalController() || !PC->GetLocalPlayer()) return;
    if (!ContextInput)
    {
        ContextInput = NewObject<UInputAction>(this);
        ContextInput->ValueType = EInputActionValueType::Boolean;
        ContextMapping = NewObject<UInputMappingContext>(this);
        ContextMapping->MapKey(ContextInput, EKeys::Z);
    }
    Enhanced->BindAction(ContextInput, ETriggerEvent::Started, this, &ThisClass::ServerContextAction);
    InputPlayer = PC->GetLocalPlayer();
    if (auto* Subsystem = PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
        Subsystem->AddMappingContext(ContextMapping, 150);
}
void USRHeroGameplayComponent::RemoveInput()
{
    if (ULocalPlayer* Player = InputPlayer.Get())
        if (auto* Subsystem = Player->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
            if (ContextMapping) Subsystem->RemoveMappingContext(ContextMapping);
    InputPlayer.Reset();
}
void USRHeroGameplayComponent::ClearAvatarEffects(UAbilitySystemComponent* ASC)
{
    if (!GetOwner()->HasAuthority() || !ASC) return;
    const TArray<FActiveGameplayEffectHandle> Handles = ASC->GetActiveEffects(FGameplayEffectQuery());
    for (FActiveGameplayEffectHandle Handle : Handles)
        if (const FActiveGameplayEffect* Effect = ASC->GetActiveGameplayEffect(Handle))
            if (Effect->Spec.Def && (Effect->Spec.Def->IsA<UGE_SRHeroModifier>() || Effect->Spec.Def->IsA<UGE_SRHeroStatus>()))
                ASC->RemoveActiveGameplayEffect(Handle);
    Wards.Reset(); WardHealth = 0.f; AuraEffects.Reset();
    Snapshot.bValid = false; BlindStrength = 0.f; bConcealed = false;
    OnRep_Concealed();
}
float USRHeroGameplayComponent::AbsorbWardDamage(float Damage)
{
    if (!GetOwner()->HasAuthority()) return Damage;
    const double Now = GetWorld()->GetTimeSeconds();
    for (FSRWardEntry& Ward : Wards)
    {
        if (Ward.Expires <= Now) continue;
        const float Absorbed = FMath::Min(Ward.Remaining, Damage);
        Ward.Remaining -= Absorbed; Damage -= Absorbed;
        if (Ward.Remaining <= 0.f)
            if (UAbilitySystemComponent* ASC = SRHeroGameplay::ASC(GetOwner())) ASC->RemoveActiveGameplayEffect(Ward.Effect);
        if (Damage <= 0.f) break;
    }
    WardHealth = 0.f;
    for (const FSRWardEntry& Ward : Wards) if (Ward.Expires > Now) WardHealth += Ward.Remaining;
    return Damage;
}
void USRHeroGameplayComponent::AddWard(float Amount, float Duration, AActor* Source)
{
    if (!GetOwner()->HasAuthority() || Amount <= 0.f || Duration <= 0.f || LifeState != ESRHeroLifeState::Alive) return;
    // The durability pool is authoritative avatar state; damage still executes
    // through IncomingDamage in GAS before health/shield consumption.
    FSRWardEntry& Ward = Wards.AddDefaulted_GetRef();
    Ward.Remaining = Amount;
    Ward.Expires = GetWorld()->GetTimeSeconds() + Duration;
    Ward.Effect = SRHeroGameplay::Status(GetOwner(), Source, SRGameplayTags::Tag_State_Warded, Duration);
    WardHealth += Amount;
}
void USRHeroGameplayComponent::RecordHealthLoss(float Amount)
{
    if (!GetOwner()->HasAuthority() || Amount <= 0.f) return;
    DamageHistory.Add({GetWorld()->GetTimeSeconds(), Amount});
    if (DamageHistory.Num() > 128) DamageHistory.RemoveAt(0, DamageHistory.Num() - 128);
}
float USRHeroGameplayComponent::RecentHealthLoss(float Window) const
{
    float Total = 0.f;
    const double Cutoff = GetWorld()->GetTimeSeconds() - FMath::Clamp(Window, 0.f, 4.f);
    for (const FSRDamageHistoryEntry& Entry : DamageHistory) if (Entry.Time >= Cutoff) Total += Entry.Amount;
    return Total;
}
float USRHeroGameplayComponent::RewindHealthLoss(float Maximum, float Window)
{
    APSBaseCharacter* Character = GetHero();
    if (!Character || !Character->HasAuthority() || LifeState != ESRHeroLifeState::Alive) return 0.f;
    const UPSAttributeSet* Attributes = Character->GetPSAttributes();
    if (!Attributes) return 0.f;
    const float Amount = FMath::Min3(Maximum, RecentHealthLoss(Window), Attributes->GetMaxHealth() - Attributes->GetHealth());
    if (Amount <= 0.f || !UPSAttributeLibrary::Heal(Character, Amount, Character)) return 0.f;
    float ToConsume = Amount;
    const double Cutoff = GetWorld()->GetTimeSeconds() - FMath::Clamp(Window, 0.f, 4.f);
    for (FSRDamageHistoryEntry& Entry : DamageHistory)
        if (Entry.Time >= Cutoff) { const float Used = FMath::Min(ToConsume, Entry.Amount); Entry.Amount -= Used; ToConsume -= Used; }
    return Amount;
}
bool USRHeroGameplayComponent::CaptureEcho(float Duration)
{
    APSBaseCharacter* Character = GetHero();
    FVector Safe;
    const UPSAttributeSet* Attributes = Character ? Character->GetPSAttributes() : nullptr;
    if (!Character || !Character->HasAuthority() || !Attributes || Snapshot.bValid
        || LifeState != ESRHeroLifeState::Alive || !SRHeroGameplay::SafeDestination(Character, Character->GetActorLocation(), Safe)) return false;
    Snapshot.Transform = Character->GetActorTransform();
    Snapshot.Health = Attributes->GetHealth(); Snapshot.Shield = Attributes->GetShield();
    Snapshot.Expires = GetWorld()->GetTimeSeconds() + Duration; Snapshot.bValid = true;
    EchoEffect = SRHeroGameplay::Status(Character, Character, SRGameplayTags::Tag_State_TemporalEcho, Duration);
    return true;
}
bool USRHeroGameplayComponent::InterceptLethalDamage()
{
    APSBaseCharacter* Character = GetHero();
    UAbilitySystemComponent* ASC = Character ? Character->GetAbilitySystemComponent() : nullptr;
    if (!Character || !Character->HasAuthority() || !ASC) return false;
    if (Snapshot.bValid && GetWorld()->GetTimeSeconds() < Snapshot.Expires)
    {
        // Immutable activation snapshot. Never restore charge, cooldowns or actors.
        Snapshot.bValid = false;
        ASC->RemoveActiveGameplayEffect(EchoEffect);
        FVector Destination;
        bool bSafe = SRHeroGameplay::SafeDestination(Character, Snapshot.Transform.GetLocation(), Destination);
        if (bSafe)
        {
            const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
            FCollisionQueryParams Params(SCENE_QUERY_STAT(SREchoExactPosition), false, Character);
            if (!GetWorld()->OverlapBlockingTestByChannel(Snapshot.Transform.GetLocation(), Snapshot.Transform.GetRotation(), ECC_Pawn,
                FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()), Params))
                Destination = Snapshot.Transform.GetLocation();
        }
        for (int32 Attempt = 0; !bSafe && Attempt < 8; ++Attempt)
        {
            const float Angle = Attempt * PI * 0.25f;
            bSafe = SRHeroGameplay::SafeDestination(Character, Snapshot.Transform.GetLocation()
                + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * 150.f, Destination);
        }
        if (bSafe && Character->TeleportTo(Destination, Snapshot.Transform.Rotator(), false, true))
        {
            Character->GetCharacterMovement()->StopMovementImmediately();
            ASC->SetNumericAttributeBase(UPSAttributeSet::GetHealthAttribute(), Snapshot.Health);
            ASC->SetNumericAttributeBase(UPSAttributeSet::GetShieldAttribute(), Snapshot.Shield);
            LifeState = ESRHeroLifeState::Alive;
            return true;
        }
    }
    if (LifeState == ESRHeroLifeState::Alive && bCanBeDowned)
    {
        ASC->CancelAllAbilities();
        ClearAvatarEffects(ASC);
        LifeState = ESRHeroLifeState::Downed;
        DownedExpires = GetWorld()->GetTimeSeconds() + BleedOutSeconds;
        ASC->SetNumericAttributeBase(UPSAttributeSet::GetHealthAttribute(), 1.f);
        ASC->SetNumericAttributeBase(UPSAttributeSet::GetShieldAttribute(), 0.f);
        DownedEffect = SRHeroGameplay::Status(Character, Character, SRGameplayTags::Tag_State_Downed, BleedOutSeconds);
        Character->GetCharacterMovement()->StopMovementImmediately();
        return true;
    }
    LifeState = ESRHeroLifeState::Eliminated;
    return false;
}
void USRHeroGameplayComponent::ClearEcho()
{
    if (!GetOwner()->HasAuthority()) return;
    Snapshot.bValid = false;
    if (UAbilitySystemComponent* ASC = SRHeroGameplay::ASC(GetOwner())) ASC->RemoveActiveGameplayEffect(EchoEffect);
}
bool USRHeroGameplayComponent::Revive(float Health, float Shield)
{
    APSBaseCharacter* Character = GetHero();
    UAbilitySystemComponent* ASC = Character ? Character->GetAbilitySystemComponent() : nullptr;
    if (!Character || !Character->HasAuthority() || !ASC || LifeState != ESRHeroLifeState::Downed
        || GetWorld()->GetTimeSeconds() >= DownedExpires) return false;
    ASC->RemoveActiveGameplayEffect(DownedEffect);
    LifeState = ESRHeroLifeState::Alive;
    const UPSAttributeSet* Attributes = Character->GetPSAttributes();
    if (Attributes)
    {
        UPSAttributeLibrary::Heal(Character, FMath::Max(0.f, Health - Attributes->GetHealth()), Character);
        UPSAttributeLibrary::RestoreShield(Character, FMath::Max(0.f, Shield - Attributes->GetShield()), Character);
    }
    return true;
}
void USRHeroGameplayComponent::BreakConcealment()
{
    if (UAbilitySystemComponent* ASC = SRHeroGameplay::ASC(GetOwner()); GetOwner()->HasAuthority() && ASC)
        ASC->RemoveActiveEffectsWithGrantedTags(FGameplayTagContainer(SRGameplayTags::Tag_State_Concealed));
}
void USRHeroGameplayComponent::SetBlind(float Strength, float Duration, AActor* Source)
{
    if (!GetOwner()->HasAuthority()) return;
    if (SRHeroGameplay::Status(GetOwner(), Source, SRGameplayTags::Tag_State_Blinded, Duration).IsValid())
    {
        BlindStrength = FMath::Max(BlindStrength, FMath::Clamp(Strength, 0.f, 1.f));
        BlindExpires = FMath::Max(BlindExpires, GetWorld()->GetTimeSeconds() + Duration);
    }
}
void USRHeroGameplayComponent::OnRep_Concealed()
{
    APSBaseCharacter* Character = GetHero();
    APlayerController* Viewer = GetWorld()->GetFirstPlayerController();
    if (Character && Character->GetMesh())
        Character->GetMesh()->SetVisibility(!bConcealed || (Viewer && SRHeroGameplay::SameTeam(Viewer->GetPawn(), Character)), true);
}
FActiveGameplayEffectHandle USRHeroGameplayComponent::RefreshAura(AActor* Source, const FSRHeroActionConfig& Config)
{
    UAbilitySystemComponent* ASC = SRHeroGameplay::ASC(GetOwner());
    if (!ASC || !GetOwner()->HasAuthority() || !Source) return {};
    if (auto* Previous = AuraEffects.Find(Source)) ASC->RemoveActiveGameplayEffect(*Previous);
    AActor* Caster = SRHeroGameplay::Hero(Source);
    if (!Caster) Caster = Source->GetOwner();
    const FActiveGameplayEffectHandle Applied = SRHeroGameplay::Buff(GetOwner(), Caster, Config, FMath::Max(0.35f, Config.Interval + 0.1f));
    AuraEffects.Add(Source, Applied);
    return Applied;
}
void USRHeroGameplayComponent::BeginMapSelection(USRHeroActionAbility* Ability, int32 Count)
{
    if (!GetOwner()->HasAuthority() || !Ability) return;
    if (PendingMapAbility.IsValid() && PendingMapAbility.Get() != Ability) PendingMapAbility->CancelFromTargeting();
    PendingMapAbility = Ability; bAwaitingMapTarget = true; MapSelectionsRemaining = Count;
    ClientSetSelectionMode(true, Count);
}
void USRHeroGameplayComponent::FinishMapSelection()
{
    if (!GetOwner()->HasAuthority()) return;
    PendingMapAbility.Reset(); bAwaitingMapTarget = false; MapSelectionsRemaining = 0;
    ClientSetSelectionMode(false, 0);
}
void USRHeroGameplayComponent::ClientSetSelectionMode_Implementation(bool bEnabled, int32 Count)
{
    bAwaitingMapTarget = bEnabled; MapSelectionsRemaining = Count;
    if (APSBaseCharacter* Character = GetHero())
    {
        Character->SetTacticalMapOpen(bEnabled);
        Character->OnTacticalMapToggled.Broadcast(bEnabled);
    }
}
void USRHeroGameplayComponent::OnMapVisibilityChanged(bool bOpen)
{ if (!bOpen && bAwaitingMapTarget) ServerCancelMapSelection(); }
void USRHeroGameplayComponent::ServerSubmitMapTarget_Implementation(FVector_NetQuantize Location)
{
    if (Location.ContainsNaN() || !bAwaitingMapTarget) return;
    if (USRHeroActionAbility* Ability = PendingMapAbility.Get())
        if (Ability->SubmitMapTarget(Location) && PendingMapAbility.Get() == Ability)
        {
            MapSelectionsRemaining = FMath::Max(0, MapSelectionsRemaining - 1);
            ClientSetSelectionMode(true, MapSelectionsRemaining);
        }
}
void USRHeroGameplayComponent::ServerCancelMapSelection_Implementation()
{
    if (USRHeroActionAbility* Ability = PendingMapAbility.Get()) Ability->CancelFromTargeting();
    FinishMapSelection();
}
void USRHeroGameplayComponent::RegisterSwapDecoys(const TArray<ASRHeroDecoy*>& Decoys)
{
    for (const auto& Previous : SwapDecoys) if (Previous.IsValid()) Previous->Destroy();
    SwapDecoys.Reset();
    for (ASRHeroDecoy* Decoy : Decoys) SwapDecoys.Add(Decoy);
    bSwapAvailable = SwapDecoys.Num() == 2;
}
void USRHeroGameplayComponent::ServerContextAction_Implementation()
{
    APSBaseCharacter* Character = GetHero();
    if (!Character || !bSwapAvailable || LifeState != ESRHeroLifeState::Alive || Character->IsPSRooted()
        || Character->IsPSTraversing() || Character->IsPSSliding()) return;
    const FVector View = Character->GetPawnViewLocation();
    const FVector Aim = Character->GetControlRotation().Vector();
    float BestDot = FMath::Cos(FMath::DegreesToRadians(SwapConeDegrees));
    ASRHeroDecoy* Selected = nullptr;
    for (const auto& Candidate : SwapDecoys)
        if (ASRHeroDecoy* Decoy = Candidate.Get(); Decoy && FVector::Dist(View, Decoy->GetActorLocation()) <= SwapRange)
        {
            const float Dot = FVector::DotProduct(Aim, (Decoy->GetActorLocation() - View).GetSafeNormal());
            if (Dot > BestDot) { BestDot = Dot; Selected = Decoy; }
        }
    if (!Selected) return;
    FVector HeroDestination, DecoyFloor;
    if (!SRHeroGameplay::SafeDestination(Character, Selected->GetActorLocation(), HeroDestination, Selected)
        || !SRHeroGameplay::Ground(GetWorld(), Character, Character->GetActorLocation(), DecoyFloor)) return;
    const UCapsuleComponent* Capsule = Selected->GetCapsuleComponent();
    const FVector DecoyDestination = DecoyFloor + FVector(0,0,Capsule->GetScaledCapsuleHalfHeight() + 2.f);
    FCollisionQueryParams Params(SCENE_QUERY_STAT(SRDecoySwap), false, Character); Params.AddIgnoredActor(Selected);
    if (GetWorld()->OverlapBlockingTestByChannel(DecoyDestination, Selected->GetActorQuat(), ECC_Pawn,
        FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()), Params)) return;
    const FTransform Previous = Character->GetActorTransform();
    if (!Character->TeleportTo(HeroDestination, Character->GetActorRotation(), false, true)) return;
    if (!Selected->TeleportTo(DecoyDestination, Selected->GetActorRotation(), false, true))
    { Character->TeleportTo(Previous.GetLocation(), Previous.Rotator(), false, true); return; }
    Character->GetCharacterMovement()->StopMovementImmediately();
    bSwapAvailable = false;
}
