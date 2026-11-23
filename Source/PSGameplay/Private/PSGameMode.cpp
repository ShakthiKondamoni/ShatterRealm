#include "PSGameMode.h"
#include "Character/PSBaseCharacter.h"
#include "GAS/PSPlayerState.h"
#include "GAS/PSAttributeSet.h"
#include "Debug/SRDebug.h"
#include "Data/SRHeroRoster.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"

APSGameMode::APSGameMode()
{
    PlayerStateClass = APSPlayerState::StaticClass();
    DefaultPawnClass = APSBaseCharacter::StaticClass();
    PSCharacterClass = TSoftClassPtr<APawn>(FSoftObjectPath(TEXT("/Game/ProjectS/Characters/Shared/Heroes/Zyrex/BP_Zyrex.BP_Zyrex_C")));
    PSVeyraPawnClass = TSoftClassPtr<APawn>(FSoftObjectPath(TEXT("/Game/ProjectS/Characters/Shared/Heroes/Veyra/BP_Veyra.BP_Veyra_C")));
}

UClass* APSGameMode::GetDefaultPawnClassForController_Implementation(AController* InController)
{
    const APlayerController* PC = Cast<APlayerController>(InController);
    if (PC)
    {
        if (const APSPlayerState* PS = PC->GetPlayerState<APSPlayerState>())
        {
            if (!PS->SelectedHeroID.IsNone())
            {
                if (UClass* Selected = SRHeroRoster::LoadPawnClass(PS->SelectedHeroID)) return Selected;
                UE_LOG(LogProjectSCharacter, Error, TEXT("Selected hero asset is unavailable: %s"), *PS->SelectedHeroID.ToString());
            }
        }
    }
    if (UClass* Class = PSCharacterClass.LoadSynchronous()) return Class;
    UE_LOG(LogProjectSCharacter, Error, TEXT("BP_PSBaseCharacter is missing. Build and open the editor once to run ProjectS setup."));
    return Super::GetDefaultPawnClassForController_Implementation(InController);
}

void APSGameMode::SRSetHero(FName HeroID)
{
#if UE_BUILD_SHIPPING
    return;
#else
    if (!HasAuthority())
    {
        UE_LOG(LogProjectSCharacter, Warning, TEXT("SRSetHero requires server authority"));
        return;
    }
    if (!SRHeroRoster::LoadPawnClass(HeroID))
    {
        UE_LOG(LogProjectSCharacter, Error, TEXT("Unknown or unconfigured hero '%s'; run PSSetup -HeroRoster."), *HeroID.ToString());
        return;
    }
    // Dev hero selection: applies to the FIRST non-local controller when run
    // from the listen-host console (Player 2 in a two-player session), else to
    // the host. The chosen pawn spawns on the next RestartPlayer/respawn.
    APlayerController* Target = nullptr;
    for (FConstControllerIterator It = GetWorld()->GetControllerIterator(); It; ++It)
    {
        APlayerController* PC = Cast<APlayerController>(It->Get());
        if (PC && PC->IsLocalController()) { Target = Target ? Target : PC; continue; } // remember host as fallback
        if (PC && !PC->IsLocalController()) { Target = PC; break; }                       // prefer the remote player
    }
    if (!Target) return;
    if (APSPlayerState* PS = Target->GetPlayerState<APSPlayerState>())
    {
        PS->SelectedHeroID = HeroID;
        UE_LOG(LogProjectSCharacter, Display, TEXT("[ProjectS][Hero] %s selected hero '%s'"), *GetNameSafe(Target), *HeroID.ToString());
        if (APawn* Previous = Target->GetPawn()) { Target->UnPossess(); Previous->Destroy(); }
        RestartPlayer(Target);
    }
#endif
}

void APSGameMode::SRSetLocalHero(FName HeroID)
{
#if !UE_BUILD_SHIPPING
    if (!HasAuthority() || !SRHeroRoster::LoadPawnClass(HeroID)) return;
    for (FConstControllerIterator It = GetWorld()->GetControllerIterator(); It; ++It)
        if (APlayerController* PC = Cast<APlayerController>(It->Get()); PC && PC->IsLocalController())
            if (APSPlayerState* State = PC->GetPlayerState<APSPlayerState>())
            {
                State->SelectedHeroID = HeroID;
                if (APawn* Previous = PC->GetPawn()) { PC->UnPossess(); Previous->Destroy(); }
                RestartPlayer(PC); return;
            }
#endif
}

void APSGameMode::PostLogin(APlayerController* NewPlayer)
{
    Super::PostLogin(NewPlayer);
    // Lifecycle trace: PostLogin runs before the pawn spawn/possess chain, so
    // this line anchors the per-player initialization sequence in the log.
    SRDebug::NetLog(this, FString::Printf(TEXT("PostLogin: controller=%s PlayerState=%s players=%d"),
        *GetNameSafe(NewPlayer), *GetNameSafe(NewPlayer ? NewPlayer->PlayerState : nullptr), GetNumPlayers()));
}

void APSGameMode::SRRespawnPlayers()
{
#if UE_BUILD_SHIPPING
    return;
#else
    if (!HasAuthority())
    {
        UE_LOG(LogProjectSCharacter, Warning, TEXT("SRRespawnPlayers requires server authority"));
        return;
    }
    // Standard engine respawn path: destroys the current pawn and spawns/possesses
    // a fresh one, re-running the full initialization lifecycle for each player.
    for (FConstControllerIterator It = GetWorld()->GetControllerIterator(); It; ++It)
    {
        APlayerController* PC = Cast<APlayerController>(It->Get());
        if (!PC) continue;
        SRDebug::NetLog(this, FString::Printf(TEXT("SRRespawnPlayers: restarting %s"), *GetNameSafe(PC)));
        if (APawn* Previous = PC->GetPawn()) { PC->UnPossess(); Previous->Destroy(); }
        RestartPlayer(PC);
    }
#endif
}

void APSGameMode::SRKillOtherPlayers()
{
#if UE_BUILD_SHIPPING
    return;
#else
    if (!HasAuthority())
    {
        UE_LOG(LogProjectSCharacter, Warning, TEXT("SRKillOtherPlayers requires server authority"));
        return;
    }
    // Diagnostic for the death/respawn lifecycle: zero health via the authoritative
    // attribute base; death gates and respawn react exactly as in gameplay.
    for (FConstControllerIterator It = GetWorld()->GetControllerIterator(); It; ++It)
    {
        APlayerController* PC = Cast<APlayerController>(It->Get());
        if (!PC || PC->IsLocalController()) continue; // keep the issuing (local) player alive
        APSBaseCharacter* Victim = Cast<APSBaseCharacter>(PC->GetPawn());
        UAbilitySystemComponent* ASC = Victim ? Victim->GetAbilitySystemComponent() : nullptr;
        if (!ASC) continue;
        ASC->SetNumericAttributeBase(UPSAttributeSet::GetHealthAttribute(), 0.f);
        SRDebug::NetLog(this, FString::Printf(TEXT("SRKillOtherPlayers: zeroed health of %s"), *GetNameSafe(Victim)));
    }
#endif
}
