#include "GAS/PSPlayerState.h"
#include "Debug/SRDebug.h"
#include "Abilities/SRGameplayTags.h"
#include "GAS/PSAttributeSet.h"
#include "Abilities/SRAbilitySystemComponent.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerStart.h"
#include "Net/UnrealNetwork.h"

namespace
{
constexpr double SRPingRateLimitSeconds = 1.5;
constexpr int32 SRMaxActivePings = 5;
constexpr float SRPingMaxRange = 60000.f;
constexpr float SRPingMaxLifetime = 8.f;
}

APSPlayerState::APSPlayerState()
{
    SetNetUpdateFrequency(30.f);
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = 0.5f;
    PSAbilitySystem = CreateDefaultSubobject<UPSAbilitySystemComponent>(TEXT("PSAbilitySystem"));
    PSAbilitySystem->SetIsReplicated(true);
    PSAbilitySystem->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);
    PSAttributes = CreateDefaultSubobject<UPSAttributeSet>(TEXT("PSAttributes"));
}

void APSPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(APSPlayerState, TeamId);
    DOREPLIFETIME(APSPlayerState, SelectedHeroID);
}

void APSPlayerState::SetTeamFromSpawnSpot()
{
    // Controlled test teams: SpawnB (or any tag containing "B") -> team 1, else team 0.
    AController* OwningController = nullptr;
    for (FConstControllerIterator It = GetWorld()->GetControllerIterator(); It; ++It)
    {
        AController* Candidate = It->Get();
        if (Candidate && Candidate->GetPawn() && Candidate->GetPawn()->GetPlayerState() == this)
        {
            OwningController = Candidate;
            break;
        }
    }
    if (OwningController)
    {
        const APlayerStart* StartSpot = Cast<APlayerStart>(OwningController->StartSpot.Get());
        const FString Tag = StartSpot ? StartSpot->PlayerStartTag.ToString() : FString();
        // In untagged test maps, alternate teams by stable replicated PlayerId.
        // Production matchmaking should explicitly assign TeamId in GameMode.
        const int32 DerivedTeam = Tag.Contains(TEXT("B")) ? 1 :
            (Tag.Contains(TEXT("A")) ? 0 : FMath::Abs(GetPlayerId()) % 2);
        if (TeamId != DerivedTeam)
        {
            TeamId = DerivedTeam;
            SRDebug::Message(SRDebug::ECategory::TeamMarkers, HasAuthority() ? TEXT("Server") : TEXT("Client"),
                FString::Printf(TEXT("Team set to %d from spawn tag '%s'"), TeamId, *Tag), 0.8f, FColor::Cyan);
        }
    }
}

void APSPlayerState::SRTeamId(int32 NewTeamId)
{
#if UE_BUILD_SHIPPING
    return;
#else
    if (!HasAuthority()) return;
    TeamId = FMath::Max(0, NewTeamId);
    SRDebug::Message(SRDebug::ECategory::TeamMarkers, TEXT("Debug"),
        FString::Printf(TEXT("%s team set to %d"), *GetName(), TeamId), 0.8f, FColor::Cyan);
    DeliverPingsToTeam();
#endif
}

UAbilitySystemComponent* APSPlayerState::GetAbilitySystemComponent() const { return PSAbilitySystem; }

void APSPlayerState::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (HasAuthority() && ServerTeamPings.Num() > 0) PruneExpiredPings();
}

void APSPlayerState::RequestPingFromAim(FGameplayTag Category)
{
    APlayerController* PC = Cast<APlayerController>(GetOwner());
    APawn* ViewPawn = PC ? PC->GetPawn() : nullptr;
    if (!PC || !ViewPawn)
    {
        SRDebug::Message(SRDebug::ECategory::Ping, TEXT("Client"), TEXT("Ping rejected: no pawn"), 0.6f, FColor::Orange);
        return;
    }

    // Client-side aiming trace: the server re-validates location and range.
    FVector CameraLoc; FRotator CameraRot;
    PC->GetPlayerViewPoint(CameraLoc, CameraRot);
    const FVector End = CameraLoc + CameraRot.Vector() * SRPingMaxRange;
    FHitResult Hit;
    GetWorld()->LineTraceSingleByChannel(Hit, CameraLoc, End, ECC_Visibility, FCollisionQueryParams(SCENE_QUERY_STAT(SRPingAim), false, ViewPawn));
    const FVector Proposed = Hit.bBlockingHit ? Hit.ImpactPoint : End;

    SRDebug::Message(SRDebug::ECategory::Ping, TEXT("Client"), FString::Printf(TEXT("Ping requested at %s"), *Proposed.ToString()), 0.5f, FColor::Yellow);
    SRDebug::Log(SRDebug::ECategory::Ping, TEXT("Client"), FString::Printf(TEXT("Proposed %s cat=%s"), *Proposed.ToString(), *Category.ToString()));

    int32 OutId;
    if (HasAuthority()) { AddTeamPing(Proposed, Category, SRPingMaxLifetime, OutId); }
    else { ServerRequestPing(Proposed, Category); }
}

void APSPlayerState::RequestPingAtLocation(const FVector& Location, FGameplayTag Category)
{
    if (Category != SRGameplayTags::Tag_SR_Ping_Location && Category != SRGameplayTags::Tag_SR_Ping_Danger) return;
    int32 OutId;
    if (HasAuthority()) { AddTeamPing(Location, Category, SRPingMaxLifetime, OutId); }
    else { ServerRequestPing(Location, Category); }
}

bool APSPlayerState::AddTeamPing(const FVector& Location, FGameplayTag Category, float Lifetime, int32& OutPingId)
{
    OutPingId = INDEX_NONE;
    if (!HasAuthority()) return false;
    if (!Category.IsValid()) return false;
    if (Category != SRGameplayTags::Tag_SR_Ping_Location &&
        Category != SRGameplayTags::Tag_SR_Ping_Danger &&
        Category != SRGameplayTags::Tag_SR_Ping_Reveal) return false;
    if (Location.ContainsNaN()) return false;
    // Player-created markers are validated relative to the player's pawn, not world origin.
    // Reveal markers are server-generated by abilities and may be elsewhere in the world.
    if (Category != SRGameplayTags::Tag_SR_Ping_Reveal)
    {
        const APlayerController* PC = Cast<APlayerController>(GetOwner());
        const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
        if (!Pawn || FVector::Dist(Pawn->GetActorLocation(), Location) > SRPingMaxRange)
        {
            SRDebug::Log(SRDebug::ECategory::Ping, TEXT("Server"), TEXT("Rejected: ping outside pawn range"));
            return false;
        }
    }

    // Rate limit and per-player marker cap for interactive pings only.
    const double Now = FPlatformTime::Seconds();
    if (Category != SRGameplayTags::Tag_SR_Ping_Reveal)
    {
        if (Now - LastPingRealTime < SRPingRateLimitSeconds)
        {
            SRDebug::Message(SRDebug::ECategory::Ping, TEXT("Server"), TEXT("Ping rejected: rate limit"), 0.6f, FColor::Orange);
            SRDebug::Log(SRDebug::ECategory::Ping, TEXT("Server"), TEXT("Rejected: rate limit"));
            return false;
        }
        int32 ActiveCount = 0;
        for (const FSRPingInfo& Ping : ServerTeamPings) ActiveCount += (Ping.Category != SRGameplayTags::Tag_SR_Ping_Reveal) ? 1 : 0;
        if (ActiveCount >= SRMaxActivePings)
        {
            // Replace this player's oldest manual ping instead of permanently blocking MMB.
            int32 OldestIndex = INDEX_NONE;
            double OldestCreated = TNumericLimits<double>::Max();
            for (int32 Index = 0; Index < ServerTeamPings.Num(); ++Index)
            {
                if (ServerTeamPings[Index].Category == SRGameplayTags::Tag_SR_Ping_Reveal) continue;
                const double* Created = PingCreationTimes.Find(ServerTeamPings[Index].PingId);
                const double CreatedAt = Created ? *Created : 0.0;
                if (CreatedAt < OldestCreated) { OldestCreated = CreatedAt; OldestIndex = Index; }
            }
            if (OldestIndex != INDEX_NONE)
            {
                PingCreationTimes.Remove(ServerTeamPings[OldestIndex].PingId);
                ServerTeamPings.RemoveAt(OldestIndex);
            }
        }
        LastPingRealTime = Now;
    }

    FSRPingInfo Ping;
    // IDs must be unique within a team, even when multiple PlayerStates submit pings.
    bool bIdInUse = false;
    do
    {
        bIdInUse = false;
        for (TActorIterator<APSPlayerState> It(GetWorld()); It && !bIdInUse; ++It)
        {
            if (It->TeamId != TeamId) continue;
            for (const FSRPingInfo& Existing : It->ServerTeamPings)
            {
                if (Existing.PingId == NextPingId) { bIdInUse = true; ++NextPingId; break; }
            }
        }
    } while (bIdInUse);
    Ping.PingId = NextPingId++;
    Ping.TeamId = TeamId;
    Ping.Location = Location;
    Ping.Category = Category;
    Ping.ExpirySeconds = FMath::Min(Lifetime, SRPingMaxLifetime);
    ServerTeamPings.Add(Ping);
    PingCreationTimes.Add(Ping.PingId, FPlatformTime::Seconds());
    OutPingId = Ping.PingId;
    DeliverPingsToTeam();
    SRDebug::Message(SRDebug::ECategory::Ping, TEXT("Server"), FString::Printf(TEXT("Ping accepted #%d at %s"), Ping.PingId, *Location.ToString()), 0.5f, FColor::Green);
    SRDebug::Log(SRDebug::ECategory::Ping, TEXT("Server"), FString::Printf(TEXT("Added #%d cat=%s expires in %.1fs"), Ping.PingId, *Category.ToString(), Ping.ExpirySeconds));
    return true;
}

bool APSPlayerState::ServerRequestPing_Validate(FVector_NetQuantize100 Location, FGameplayTag Category)
{
    return Category == SRGameplayTags::Tag_SR_Ping_Location
        || Category == SRGameplayTags::Tag_SR_Ping_Danger;
}

void APSPlayerState::ServerRequestPing_Implementation(FVector_NetQuantize100 Location, FGameplayTag Category)
{
    int32 OutId;
    AddTeamPing(FVector(Location), Category, SRPingMaxLifetime, OutId);
}

void APSPlayerState::PruneExpiredPings()
{
    const double Now = FPlatformTime::Seconds();
    bool bExpiredAny = false;
    for (int32 Index = ServerTeamPings.Num() - 1; Index >= 0; --Index)
    {
        const double* Created = PingCreationTimes.Find(ServerTeamPings[Index].PingId);
        if (Created && Now - *Created >= ServerTeamPings[Index].ExpirySeconds)
        {
            SRDebug::Log(SRDebug::ECategory::Ping, TEXT("Server"), FString::Printf(TEXT("Marker #%d expired"), ServerTeamPings[Index].PingId));
            SRDebug::Log(SRDebug::ECategory::Reveal, TEXT("Server"), FString::Printf(TEXT("Marker #%d expired"), ServerTeamPings[Index].PingId));
            PingCreationTimes.Remove(ServerTeamPings[Index].PingId);
            ServerTeamPings.RemoveAt(Index);
            bExpiredAny = true;
        }
    }
    if (bExpiredAny) DeliverPingsToTeam();
}

void APSPlayerState::DeliverPingsToTeam()
{
    if (!HasAuthority() || !GetWorld()) return;
    TArray<FSRPingInfo> Combined;
    for (TActorIterator<APSPlayerState> It(GetWorld()); It; ++It)
    {
        if (It->TeamId == TeamId)
            Combined.Append(It->ServerTeamPings);
    }
    // Deliver only to team owners; do not replicate enemy markers globally.
    for (TActorIterator<APSPlayerState> It(GetWorld()); It; ++It)
    {
        if (It->TeamId != TeamId) continue;
        It->AggregatedTeamPings = Combined;
        It->OnTeamPingsChanged.Broadcast(It->AggregatedTeamPings);
        It->ClientDeliverTeamPings(Combined);
    }
}

void APSPlayerState::ClientDeliverTeamPings_Implementation(const TArray<FSRPingInfo>& Pings)
{
    DeliveredPings = Pings;
    OnTeamPingsChanged.Broadcast(DeliveredPings);
    SRDebug::Log(SRDebug::ECategory::Ping, TEXT("Client"), FString::Printf(TEXT("Delivered %d team markers"), DeliveredPings.Num()));
    SRDebug::Log(SRDebug::ECategory::Reveal, TEXT("Client"), FString::Printf(TEXT("Authorized client updated: %d markers"), DeliveredPings.Num()));
}

void APSPlayerState::PostInitializeComponents()
{
    Super::PostInitializeComponents();
    PSAbilitySystem->AddAttributeSetSubobject(PSAttributes.Get());
    const TArray<TPair<FName, FGameplayAttribute>> Attributes = {
        {TEXT("Health"), UPSAttributeSet::GetHealthAttribute()},
        {TEXT("MaxHealth"), UPSAttributeSet::GetMaxHealthAttribute()},
        {TEXT("Shield"), UPSAttributeSet::GetShieldAttribute()},
        {TEXT("MaxShield"), UPSAttributeSet::GetMaxShieldAttribute()},
        {TEXT("UltimateCharge"), UPSAttributeSet::GetUltimateChargeAttribute()},
        {TEXT("MaxUltimateCharge"), UPSAttributeSet::GetMaxUltimateChargeAttribute()}
    };
    SRDebug::NetLog(this, FString::Printf(TEXT("PostInitializeComponents: ASC + AttributeSet ready (attributes replicated REPNOTIFY_Always)")));
    for (const auto& Attribute : Attributes)
    {
        PSAbilitySystem->GetGameplayAttributeValueChangeDelegate(Attribute.Value).AddWeakLambda(this,
            [this, Name=Attribute.Key](const FOnAttributeChangeData& Data)
            {
                // Throttled replication trace: attribute updates reach clients via
                // RepNotify; server-side changes log through the same path.
                static TMap<FName, double> LastLogTime;
                const double Now = FPlatformTime::Seconds();
                double* Last = LastLogTime.Find(Name);
                if (!Last || Now - *Last > 1.0)
                {
                    LastLogTime.FindOrAdd(Name) = Now;
                    SRDebug::NetLog(this, FString::Printf(TEXT("attribute %s -> %.1f"), *Name.ToString(), Data.NewValue));
                }
                OnPSAttributeChanged.Broadcast(Name, Data.NewValue, Data.OldValue);
            });
    }
}
