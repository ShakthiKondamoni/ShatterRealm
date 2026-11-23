#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "PSGameMode.generated.h"

UCLASS()
class PSGAMEPLAY_API APSGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    APSGameMode();
    virtual UClass* GetDefaultPawnClassForController_Implementation(AController* InController) override;
    virtual void PostLogin(APlayerController* NewPlayer) override;

    /** Development-only diagnostic: restarts every player through the standard
     *  RestartPlayer path so possession/respawn initialization is testable
     *  (console: SRRespawnPlayers). Never runs in shipping builds. */
    UFUNCTION(Exec, Category="ProjectS|Debug")
    void SRRespawnPlayers();

    /** Development-only diagnostic: sets every OTHER player's health to zero so
     *  death handling can be observed from the local console (SRKillOtherPlayers).
     *  Server-authoritative attribute change only; never runs in shipping builds. */
    UFUNCTION(Exec, Category="ProjectS|Debug")
    void SRKillOtherPlayers();

    /**
     * Development server-console selection from the complete roster.
     * Selects the first remote player, or the host in standalone play.
     */
    UFUNCTION(Exec, Category="ProjectS|Hero")
    void SRSetHero(FName HeroID);
    UFUNCTION(Exec, Category="ProjectS|Hero")
    void SRSetLocalHero(FName HeroID);

    UPROPERTY(EditDefaultsOnly, Category="ProjectS|Character") TSoftClassPtr<APawn> PSCharacterClass;
    UPROPERTY(EditDefaultsOnly, Category="ProjectS|Character") TSoftClassPtr<APawn> PSVeyraPawnClass;
};
