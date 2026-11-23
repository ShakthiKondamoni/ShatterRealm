#pragma once

#include "CoreMinimal.h"

/**
 * Development-only debug framework. Every gameplay subsystem exposes a toggle
 * console variable (ps.Debug.*) that controls log verbosity, throttled
 * on-screen messages and debug geometry. All drawing and messaging is opt-in;
 * shipping builds compile these calls down to no-ops via the !UE_BUILD_SHIPPING
 * guards in the implementation.
 */
namespace SRDebug
{
	enum class ECategory : uint8
	{
		Input, Combat, Hitscan, Damage, Projectile, Ability, MovementAbility,
		GAS, HUD, Minimap, Ping, Networking, Movement,
		TacticalMap, WorldPing, TeamMarkers, Reveal, UIInput
	};

	/** True when the given category is enabled (ps.Debug.<Name> != 0). */
	PSGAMEPLAY_API bool IsEnabled(ECategory Category);

	/** Throttled on-screen message with a stable key per category; label like [COMBAT][Server]. */
	PSGAMEPLAY_API void Message(ECategory Category, const FString& Label, const FString& Text, float ThrottleSeconds = 0.25f, const FColor& Color = FColor::White, float DisplaySeconds = 4.f);

	/** Log through the shared combat debug category. */
	PSGAMEPLAY_API void Log(ECategory Category, const FString& Label, const FString& Text);

	/**
	 * Multiplayer lifecycle trace with the consistent [ProjectS][Network] prefix
	 * (development-only; compiled out of shipping). Always includes the actor's
	 * net role and — for pawns — local-control state, so server, autonomous-proxy
	 * (owning client) and simulated-proxy (remote pawn) paths stay distinguishable
	 * in the shared PIE log.
	 */
	PSGAMEPLAY_API void NetLog(const AActor* Actor, const FString& Message);

	/**
	 * Combat trace with the consistent [ProjectS][Combat] prefix (development-only;
	 * compiled out of shipping). Includes the actor's net role so shot acceptance,
	 * rejection reasons, hit regions and damage breakdowns identify which machine
	 * produced them.
	 */
	PSGAMEPLAY_API void CombatLog(const AActor* Actor, const FString& Message);

	/**
	 * Fire-prediction trace with the consistent [ProjectS][FirePrediction] prefix
	 * (development-only; compiled out of shipping): local shot sequence ids,
	 * acknowledgments, predicted vs authoritative ammunition, pattern index.
	 */
	PSGAMEPLAY_API void PredictionLog(const AActor* Actor, const FString& Message);
}
