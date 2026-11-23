#include "Combat/SRHeroGameplayActors.h"
#include "Abilities/SRHeroGameplayLibrary.h"
#include "Abilities/SRGameplayTags.h"
#include "Character/PSBaseCharacter.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "DrawDebugHelpers.h"
#include "Debug/SRDebug.h"

ASRHeroVines::ASRHeroVines()
{
    bReplicates = true; NetUpdateFrequency = 10.f;
    PrimaryActorTick.bCanEverTick = true; PrimaryActorTick.TickInterval = 0.1f;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("VineRoot")));
}
void ASRHeroVines::Initialize(APSBaseCharacter* InCaster, const FSRHeroActionConfig& InConfig)
{
    if (!HasAuthority()) return;
    Caster = InCaster; SetOwner(InCaster); Config = InConfig; Random.Initialize(GetUniqueID());
    for (int32 Index = 0; Index < FMath::Clamp(Config.Branches, 1, 8); ++Index)
    {
        const float Angle = Index * 360.f / FMath::Max(1, Config.Branches) + Random.FRandRange(-18,18);
        Branches.Add({GetActorLocation(), FRotator(0,Angle,0).Vector(), 0.f, false});
    }
    SetLifeSpan(Config.Duration);
}
void ASRHeroVines::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
#if !UE_BUILD_SHIPPING
    if (Config.bDrawDebug || SRDebug::IsEnabled(SRDebug::ECategory::Ability))
        for (const FSRVineSegment& Segment : Segments)
        {
            const FVector Direction = Segment.End - Segment.Start;
            DrawDebugCapsule(GetWorld(), (Segment.Start + Segment.End) * 0.5f, Direction.Size() * 0.5f + 30.f,
                30.f, FQuat::FindBetweenNormals(FVector::UpVector, Direction.GetSafeNormal()), FColor::Green, false, 0.12f);
        }
#endif
    if (!HasAuthority() || !Caster) return;
    const int32 ExistingBranches = Branches.Num();
    for (int32 Index = 0; Index < ExistingBranches && Segments.Num() < 256; ++Index)
    {
        FSRGrowingVine& Branch = Branches[Index];
        if (Branch.Traveled >= Config.Radius) continue;
        const float Distance = FMath::Min(Config.GrowthSpeed * DeltaSeconds, Config.Radius - Branch.Traveled);
        const FVector Direction = Branch.Direction.RotateAngleAxis(Random.FRandRange(-12,12), FVector::UpVector);
        FVector Ground;
        if (!SRHeroGameplay::Ground(GetWorld(), Caster, Branch.Tip + Direction * Distance, Ground)
            || FMath::Abs(Ground.Z + 20.f - Branch.Tip.Z) > 100.f)
        { Branch.Traveled = Config.Radius; continue; }
        const FVector Next = Ground + FVector(0,0,20);
        FCollisionQueryParams Params(SCENE_QUERY_STAT(SRVineGrowth), false, Caster);
        for (TActorIterator<APawn> It(GetWorld()); It; ++It) Params.AddIgnoredActor(*It);
        FHitResult Obstacle;
        if (GetWorld()->LineTraceSingleByChannel(Obstacle, Branch.Tip, Next, ECC_Visibility, Params))
        { Branch.Traveled = Config.Radius; continue; }
        FSRVineSegment Segment; Segment.Start = Branch.Tip; Segment.End = Next; Segments.Add(Segment);
        Branch.Tip = Next; Branch.Direction = Direction; Branch.Traveled += Distance;
        if (!Branch.bForked && Branch.Traveled > Config.Radius * 0.4f && Branches.Num() < 16)
        {
            Branch.bForked = true;
            // Copy before Add: appending can reallocate and invalidate Branch.
            const FSRGrowingVine Fork{Next, Direction.RotateAngleAxis(Random.FRandRange(35,70), FVector::UpVector), Branch.Traveled, true};
            Branches.Add(Fork);
        }
    }
    FCollisionObjectQueryParams Objects; Objects.AddObjectTypesToQuery(ECC_Pawn); Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
    FCollisionQueryParams Params(SCENE_QUERY_STAT(SRVineRoots), false, Caster);
    for (const FSRVineSegment& Segment : Segments)
    {
        TArray<FHitResult> Hits;
        GetWorld()->SweepMultiByObjectType(Hits, Segment.Start, Segment.End, FQuat::Identity, Objects, FCollisionShape::MakeSphere(30.f), Params);
        for (const FHitResult& Hit : Hits)
            if (AActor* Target = Hit.GetActor(); Target && !Rooted.Contains(Target) && SRHeroGameplay::IsEnemy(Caster, Target)
                && SRHeroGameplay::Cover(Caster, Target, Segment.Start + FVector(0,0,20)))
            {
                Rooted.Add(Target);
                SRHeroGameplay::Status(Target, Caster, SRGameplayTags::Tag_State_Rooted, Config.EffectDuration);
            }
    }
}
void ASRHeroVines::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ASRHeroVines, Segments); DOREPLIFETIME(ASRHeroVines, Config); DOREPLIFETIME(ASRHeroVines, Caster);
}

bool USRProjectileTimeSubsystem::IsTickable() const
{ return GetWorld() && GetWorld()->IsGameWorld() && GetWorld()->GetNetMode() != NM_Client && (Bubbles.Num() > 0 || Projectiles.Num() > 0); }
TStatId USRProjectileTimeSubsystem::GetStatId() const
{ RETURN_QUICK_DECLARE_CYCLE_STAT(USRProjectileTimeSubsystem, STATGROUP_Tickables); }
void USRProjectileTimeSubsystem::RegisterBubble(ASRHeroField* Bubble) { Bubbles.AddUnique(Bubble); }
void USRProjectileTimeSubsystem::UnregisterBubble(ASRHeroField* Bubble) { Bubbles.Remove(Bubble); Tick(0.f); }
void USRProjectileTimeSubsystem::Tick(float)
{
    if (!GetWorld() || GetWorld()->GetNetMode() == NM_Client) return;
    Bubbles.RemoveAll([](const auto& Bubble) { return !Bubble.IsValid(); });
    for (TActorIterator<AActor> It(GetWorld()); It; ++It)
    {
        UProjectileMovementComponent* Movement = It->FindComponentByClass<UProjectileMovementComponent>();
        if (!Movement || !It->HasAuthority()) continue;
        float Scale = 1.f;
        for (const auto& Bubble : Bubbles)
            if (Bubble.IsValid() && FVector::DistSquared(It->GetActorLocation(), Bubble->GetActorLocation())
                <= FMath::Square(Bubble->GetConfig().Radius))
                Scale = FMath::Min(Scale, FMath::Clamp(Bubble->GetConfig().MovementScale, 0.1f, 1.f));
        FSRProjectileTimeState* Existing = Projectiles.Find(Movement);
        if (!Existing && Scale == 1.f) continue;
        const float Previous = Existing ? Existing->AppliedScale : 1.f;
        const float Ratio = Scale / Previous;
        Movement->Velocity *= Ratio;
        Movement->InitialSpeed *= Ratio; Movement->MaxSpeed *= Ratio;
        Movement->ProjectileGravityScale *= Ratio * Ratio;
        Movement->HomingAccelerationMagnitude *= Ratio * Ratio;
        if (Scale == 1.f) Projectiles.Remove(Movement);
        else Projectiles.FindOrAdd(Movement).AppliedScale = Scale;
    }
    for (auto It = Projectiles.CreateIterator(); It; ++It) if (!It.Key().IsValid()) It.RemoveCurrent();
}
