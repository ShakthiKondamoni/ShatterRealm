#include "Abilities/SRHeroGameplayLibrary.h"
#include "Abilities/SRGameplayTags.h"
#include "Character/PSBaseCharacter.h"
#include "Character/SRHeroGameplayComponent.h"
#include "Combat/SRCombatComponent.h"
#include "Combat/SRHeroGameplayActors.h"
#include "Combat/SRGameplayQueries.h"
#include "GAS/PSPlayerState.h"
#include "GAS/PSAttributeSet.h"
#include "GAS/PSAttributeLibrary.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Components/CapsuleComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/DamageEvents.h"

namespace
{
FSetByCallerFloat Caller(FGameplayTag Tag) { FSetByCallerFloat Value; Value.DataTag = Tag; return Value; }
void Modifier(UGameplayEffect* Effect, FGameplayAttribute Attribute, FGameplayTag Tag)
{
    FGameplayModifierInfo& M = Effect->Modifiers.AddDefaulted_GetRef();
    M.Attribute = Attribute;
    M.ModifierOp = EGameplayModOp::MultiplyCompound;
    M.ModifierMagnitude = FGameplayEffectModifierMagnitude(Caller(Tag));
}
const APSPlayerState* TeamState(const AActor* Actor)
{
    for (int32 Depth = 0; Actor && Depth < 5; ++Depth, Actor = Actor->GetOwner())
    {
        if (const APSPlayerState* State = Cast<APSPlayerState>(Actor)) return State;
        if (const APawn* Pawn = Cast<APawn>(Actor))
            if (const APSPlayerState* State = Pawn->GetPlayerState<APSPlayerState>()) return State;
    }
    return nullptr;
}
}

UGE_SRHeroModifier::UGE_SRHeroModifier()
{
    DurationPolicy = EGameplayEffectDurationType::HasDuration;
    DurationMagnitude = FGameplayEffectModifierMagnitude(Caller(SRGameplayTags::Tag_Data_Cooldown));
    Modifier(this, UPSAttributeSet::GetDamageDealtMultiplierAttribute(), SRGameplayTags::Tag_Data_HeroDamage);
    Modifier(this, UPSAttributeSet::GetFireRateMultiplierAttribute(), SRGameplayTags::Tag_Data_HeroFireRate);
    Modifier(this, UPSAttributeSet::GetRechargeRateMultiplierAttribute(), SRGameplayTags::Tag_Data_HeroRecharge);
    Modifier(this, UPSAttributeSet::GetDamageTakenMultiplierAttribute(), SRGameplayTags::Tag_Data_HeroDamageTaken);
    Modifier(this, UPSAttributeSet::GetMoveSpeedMultiplierAttribute(), SRGameplayTags::Tag_Data_HeroMovement);
    Modifier(this, UPSAttributeSet::GetKnockbackMultiplierAttribute(), SRGameplayTags::Tag_Data_HeroKnockback);
}
UGE_SRHeroStatus::UGE_SRHeroStatus()
{
    DurationPolicy = EGameplayEffectDurationType::HasDuration;
    DurationMagnitude = FGameplayEffectModifierMagnitude(Caller(SRGameplayTags::Tag_Data_Cooldown));
}

UAbilitySystemComponent* SRHeroGameplay::ASC(const AActor* Actor)
{ return Actor ? UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(const_cast<AActor*>(Actor)) : nullptr; }
APSBaseCharacter* SRHeroGameplay::Hero(const AActor* Actor)
{
    if (APSBaseCharacter* Character = Cast<APSBaseCharacter>(const_cast<AActor*>(Actor))) return Character;
    UAbilitySystemComponent* Component = ASC(Actor);
    return Component ? Cast<APSBaseCharacter>(Component->GetAvatarActor()) : nullptr;
}
bool SRHeroGameplay::SameTeam(const AActor* A, const AActor* B)
{
    if (!A || !B) return false;
    if (A == B) return true;
    const APSPlayerState* First = TeamState(A);
    const APSPlayerState* Second = TeamState(B);
    return First && Second && First->TeamId == Second->TeamId;
}
bool SRHeroGameplay::IsAlive(const AActor* Actor)
{
    const UAbilitySystemComponent* Component = ASC(Actor);
    const UPSAttributeSet* Attributes = Component ? Component->GetSet<UPSAttributeSet>() : nullptr;
    const USRHeroGameplayComponent* State = Actor ? Actor->FindComponentByClass<USRHeroGameplayComponent>() : nullptr;
    return Attributes && Attributes->GetHealth() > 0.f && (!State || State->GetLifeState() == ESRHeroLifeState::Alive);
}
bool SRHeroGameplay::IsDamageable(const AActor* Actor)
{
    const UAbilitySystemComponent* Component = ASC(Actor);
    return Actor && ((Component && Component->GetSet<UPSAttributeSet>())
        || Actor->IsA<ASRHeroBarrier>() || Actor->IsA<ASRHeroField>() || Actor->IsA<ASRHeroDecoy>());
}
bool SRHeroGameplay::IsEnemy(const AActor* Source, const AActor* Target)
{ return Source && Target && Source != Target && IsDamageable(Target) && !SameTeam(Source, Target); }
TArray<AActor*> SRHeroGameplay::InRadius(AActor* Source, const FVector& Center, float Radius)
{
    TArray<AActor*> Actors;
    if (!Source || !Source->HasAuthority() || !Source->GetWorld() || Radius <= 0.f) return Actors;
    TArray<FOverlapResult> Results;
    Source->GetWorld()->OverlapMultiByObjectType(Results, Center, FQuat::Identity,
        FCollisionObjectQueryParams::AllObjects, FCollisionShape::MakeSphere(Radius));
    for (const FOverlapResult& Result : Results)
        if (AActor* Actor = Result.GetActor(); Actor && IsDamageable(Actor)) Actors.AddUnique(Actor);
    return Actors;
}
bool SRHeroGameplay::Ground(UWorld* World, const AActor* Ignore, const FVector& Desired, FVector& OutGround)
{
    if (!World || Desired.ContainsNaN()) return false;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(SRHeroGround), false, Ignore);
    for (TActorIterator<APawn> It(World); It; ++It) Params.AddIgnoredActor(*It);
    for (TActorIterator<ASRHeroBarrier> It(World); It; ++It) Params.AddIgnoredActor(*It);
    for (TActorIterator<ASRHeroField> It(World); It; ++It) Params.AddIgnoredActor(*It);
    FHitResult Hit;
    if (!World->LineTraceSingleByChannel(Hit, Desired + FVector(0,0,1200), Desired - FVector(0,0,2500), ECC_Visibility, Params)
        || Hit.ImpactNormal.Z < 0.65f) return false;
    OutGround = Hit.ImpactPoint;
    return true;
}
bool SRHeroGameplay::AimGround(APSBaseCharacter* Caster, float Range, FVector& OutGround)
{
    if (!Caster || !Caster->GetCombatComponent()) return false;
    const FVector View = Caster->GetPawnViewLocation();
    const FVector Aim = Caster->GetCombatComponent()->ResolveAimTarget(View, Range);
    return Ground(Caster->GetWorld(), Caster, Aim, OutGround)
        && FVector::Dist2D(Caster->GetActorLocation(), OutGround) <= Range;
}
bool SRHeroGameplay::SafeDestination(APSBaseCharacter* Caster, const FVector& Desired, FVector& OutLocation, AActor* OtherIgnored)
{
    if (!Caster) return false;
    FVector Floor;
    if (!Ground(Caster->GetWorld(), Caster, Desired, Floor)) return false;
    const UCapsuleComponent* Capsule = Caster->GetCapsuleComponent();
    OutLocation = Floor + FVector(0,0,Capsule->GetScaledCapsuleHalfHeight() + 2.f);
    FCollisionQueryParams Params(SCENE_QUERY_STAT(SRHeroDestination), false, Caster);
    if (OtherIgnored) Params.AddIgnoredActor(OtherIgnored);
    return !Caster->GetWorld()->OverlapBlockingTestByChannel(OutLocation, Caster->GetActorQuat(), ECC_Pawn,
        FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()), Params);
}
bool SRHeroGameplay::Cover(const AActor* Source, AActor* Target, const FVector& Start)
{ return Source && SRGameplayQueries::HasCoverFreePath(Source->GetWorld(), const_cast<AActor*>(Source), Target, Start); }
float SRHeroGameplay::Damage(APSBaseCharacter* Source, AActor* Target, float Amount)
{
    bool bEnemy = false;
    return Source && Source->GetCombatComponent() ? Source->GetCombatComponent()->ApplyCombatDamage(Target, Amount, Source, bEnemy) : 0.f;
}
bool SRHeroGameplay::Heal(APSBaseCharacter* Source, AActor* Target, float Amount)
{ return Source && SameTeam(Source, Target) && IsAlive(Target) && UPSAttributeLibrary::Heal(Target, Amount, Source); }
FActiveGameplayEffectHandle SRHeroGameplay::Buff(AActor* Target, AActor* Source, const FSRHeroActionConfig& Config, float Duration)
{
    UAbilitySystemComponent* TargetASC = ASC(Target);
    UAbilitySystemComponent* SourceASC = ASC(Source);
    if (!Target || !Target->HasAuthority() || !TargetASC || !SourceASC) return {};
    FGameplayEffectSpecHandle Spec = SourceASC->MakeOutgoingSpec(UGE_SRHeroModifier::StaticClass(), 1.f, SourceASC->MakeEffectContext());
    if (!Spec.IsValid()) return {};
    Spec.Data->SetSetByCallerMagnitude(SRGameplayTags::Tag_Data_Cooldown, FMath::Max(0.01f, Duration > 0.f ? Duration : Config.Duration));
    Spec.Data->SetSetByCallerMagnitude(SRGameplayTags::Tag_Data_HeroDamage, Config.DamageScale);
    Spec.Data->SetSetByCallerMagnitude(SRGameplayTags::Tag_Data_HeroFireRate, Config.FireRateScale);
    Spec.Data->SetSetByCallerMagnitude(SRGameplayTags::Tag_Data_HeroRecharge, Config.RechargeScale);
    Spec.Data->SetSetByCallerMagnitude(SRGameplayTags::Tag_Data_HeroDamageTaken, Config.DamageTakenScale);
    Spec.Data->SetSetByCallerMagnitude(SRGameplayTags::Tag_Data_HeroMovement, Config.MovementScale);
    Spec.Data->SetSetByCallerMagnitude(SRGameplayTags::Tag_Data_HeroKnockback, Config.KnockbackScale);
    return TargetASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
}
FActiveGameplayEffectHandle SRHeroGameplay::Status(AActor* Target, AActor* Source, FGameplayTag Tag, float Duration)
{
    UAbilitySystemComponent* TargetASC = ASC(Target);
    if (!Target || !Target->HasAuthority() || !TargetASC || !Tag.IsValid() || Duration <= 0.f) return {};
    if (TargetASC->HasMatchingGameplayTag(SRGameplayTags::Tag_State_ControlImmune)
        && (Tag == SRGameplayTags::Tag_State_Rooted || Tag == SRGameplayTags::Tag_State_Stunned
            || Tag == SRGameplayTags::Tag_State_Blinded || Tag == SRGameplayTags::Tag_State_Nearsighted)) return {};
    FGameplayEffectContextHandle Context = TargetASC->MakeEffectContext();
    Context.AddInstigator(Source, Source);
    FGameplayEffectSpecHandle Spec = TargetASC->MakeOutgoingSpec(UGE_SRHeroStatus::StaticClass(), 1.f, Context);
    if (!Spec.IsValid()) return {};
    Spec.Data->SetSetByCallerMagnitude(SRGameplayTags::Tag_Data_Cooldown, Duration);
    Spec.Data->DynamicGrantedTags.AddTag(Tag);
    return TargetASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
}
void SRHeroGameplay::Push(AActor* Target, const FVector& Direction, float Strength, float Upward)
{
    ACharacter* Character = Cast<ACharacter>(Target);
    if (!Character || !Character->HasAuthority()) return;
    const UAbilitySystemComponent* Component = ASC(Target);
    if (Component && Component->HasMatchingGameplayTag(SRGameplayTags::Tag_State_ControlImmune)) return;
    const UPSAttributeSet* Attributes = Component ? Component->GetSet<UPSAttributeSet>() : nullptr;
    const float Scale = Attributes ? Attributes->GetKnockbackMultiplier() : 1.f;
    Character->LaunchCharacter((Direction.GetSafeNormal2D() * Strength + FVector::UpVector * Upward) * Scale, true, Upward > 0.f);
}
bool SRHeroGameplay::MoveSafely(AActor* Target, const FVector& Delta)
{
    ACharacter* Character = Cast<ACharacter>(Target);
    if (!Character || !Character->HasAuthority() || Delta.ContainsNaN()) return false;
    const UAbilitySystemComponent* Component = ASC(Target);
    if (Component && Component->HasMatchingGameplayTag(SRGameplayTags::Tag_State_ControlImmune)) return false;
    const UPSAttributeSet* Attributes = Component ? Component->GetSet<UPSAttributeSet>() : nullptr;
    FHitResult Hit;
    Character->GetCharacterMovement()->SafeMoveUpdatedComponent(Delta * (Attributes ? Attributes->GetKnockbackMultiplier() : 1.f),
        Character->GetActorQuat(), true, Hit);
    return !Hit.bBlockingHit;
}
FVector SRHeroGameplay::Facing(const APSBaseCharacter* Caster)
{
    if (!Caster) return FVector::ForwardVector;
    const FVector Aim = Caster->GetControlRotation().Vector().GetSafeNormal2D();
    return Aim.IsNearlyZero() ? Caster->GetActorForwardVector().GetSafeNormal2D() : Aim;
}
float SRHeroGameplay::HitscanTransmission(UWorld* World, const FVector& Start, const FVector& End)
{
    float Scale = 1.f;
    if (World) for (TActorIterator<ASRHeroBarrier> It(World); It; ++It)
        if (It->GetAction() == ESRHeroAction::MistVeil && It->Crosses(Start, End)) Scale *= 0.7f;
    return Scale;
}
bool SRHeroGameplay::BarrierFormation(APSBaseCharacter* Caster, const FVector& Center, const FSRHeroActionConfig& Config, TArray<FTransform>& OutTransforms)
{
    OutTransforms.Reset();
    if (!Caster) return false;
    const bool bCage = Config.Action == ESRHeroAction::RootCage;
    const int32 Count = bCage || Config.Action == ESRHeroAction::PrismWall ? FMath::Clamp(Config.Segments, 3, 12) : 1;
    const FVector Forward = Facing(Caster);
    const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
    const float Width = bCage ? 2.f * Config.Radius * FMath::Tan(PI / Count) + 4.f : Config.Width / Count;
    for (int32 Index = 0; Index < Count; ++Index)
    {
        const float Angle = 2.f * PI * Index / Count;
        const FVector Radial(FMath::Cos(Angle), FMath::Sin(Angle), 0.f);
        const FVector Desired = bCage ? Center + Radial * Config.Radius : Center + Right * ((Index - (Count - 1) * 0.5f) * Width);
        FVector GroundPoint;
        const FRotator Rotation = (bCage ? Radial : Forward).Rotation();
        if (!Ground(Caster->GetWorld(), Caster, Desired, GroundPoint)
            || !ASRHeroBarrier::CanPlace(Caster->GetWorld(), Caster, GroundPoint, Rotation, Width, Config.Height))
        { OutTransforms.Reset(); return false; }
        OutTransforms.Add(FTransform(Rotation, GroundPoint + FVector(0,0,Config.Height * 0.5f + 3.f)));
    }
    return true;
}
