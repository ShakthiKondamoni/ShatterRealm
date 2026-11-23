#include "Debug/SRDebug.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "GameFramework/Pawn.h"
#include "UObject/Class.h"

namespace
{
// One toggle per debug category (ps.Debug.<Name>); all default off.
TAutoConsoleVariable<int32> CVarDebugInput(TEXT("ps.Debug.Input"), 0, TEXT("Debug: input routing (0/1)."));
TAutoConsoleVariable<int32> CVarDebugCombat(TEXT("ps.Debug.Combat"), 0, TEXT("Debug: combat state, fire requests, charges, recharge."));
TAutoConsoleVariable<int32> CVarDebugHitscan(TEXT("ps.Debug.Hitscan"), 0, TEXT("Debug: hitscan traces and impact geometry."));
TAutoConsoleVariable<int32> CVarDebugDamage(TEXT("ps.Debug.Damage"), 0, TEXT("Debug: damage requests, application and rejection."));
TAutoConsoleVariable<int32> CVarDebugProjectile(TEXT("ps.Debug.Projectile"), 0, TEXT("Debug: projectile spawn, flight, impact, splash."));
TAutoConsoleVariable<int32> CVarDebugAbility(TEXT("ps.Debug.Ability"), 0, TEXT("Debug: ability activation, commitment, completion."));
TAutoConsoleVariable<int32> CVarDebugMovementAbility(TEXT("ps.Debug.MovementAbility"), 0, TEXT("Debug: Static Leap movement and landing."));
TAutoConsoleVariable<int32> CVarDebugGAS(TEXT("ps.Debug.GAS"), 0, TEXT("Debug: GAS attribute/effect events."));
TAutoConsoleVariable<int32> CVarDebugHUD(TEXT("ps.Debug.HUD"), 0, TEXT("Debug: HUD binding and update events."));
TAutoConsoleVariable<int32> CVarDebugMinimap(TEXT("ps.Debug.Minimap"), 0, TEXT("Debug: minimap coordinates, markers, reveals."));
TAutoConsoleVariable<int32> CVarDebugPing(TEXT("ps.Debug.Ping"), 0, TEXT("Debug: ping requests, validation, markers."));
TAutoConsoleVariable<int32> CVarDebugNetworking(TEXT("ps.Debug.Networking"), 0, TEXT("Debug: RPC and replication events."));
TAutoConsoleVariable<int32> CVarDebugMovement(TEXT("ps.Debug.Movement"), 0, TEXT("Debug: sprint/crouch/slide/vault state changes."));
TAutoConsoleVariable<int32> CVarDebugTacticalMap(TEXT("ps.Debug.TacticalMap"), 0, TEXT("Debug: tactical map open/close, zoom, pan, LMB consumption."));
TAutoConsoleVariable<int32> CVarDebugWorldPing(TEXT("ps.Debug.WorldPing"), 0, TEXT("Debug: world ping traces and requests."));
TAutoConsoleVariable<int32> CVarDebugTeamMarkers(TEXT("ps.Debug.TeamMarkers"), 0, TEXT("Debug: teammate marker lifecycle."));
TAutoConsoleVariable<int32> CVarDebugReveal(TEXT("ps.Debug.Reveal"), 0, TEXT("Debug: enemy reveal creation, delivery, expiry."));
TAutoConsoleVariable<int32> CVarDebugUIInput(TEXT("ps.Debug.UIInput"), 0, TEXT("Debug: UI input mode transitions and consumed input."));

int32 GetToggleValue(SRDebug::ECategory Category)
{
	switch (Category)
	{
	case SRDebug::ECategory::Input: return CVarDebugInput.GetValueOnGameThread();
	case SRDebug::ECategory::Combat: return CVarDebugCombat.GetValueOnGameThread();
	case SRDebug::ECategory::Hitscan: return CVarDebugHitscan.GetValueOnGameThread();
	case SRDebug::ECategory::Damage: return CVarDebugDamage.GetValueOnGameThread();
	case SRDebug::ECategory::Projectile: return CVarDebugProjectile.GetValueOnGameThread();
	case SRDebug::ECategory::Ability: return CVarDebugAbility.GetValueOnGameThread();
	case SRDebug::ECategory::MovementAbility: return CVarDebugMovementAbility.GetValueOnGameThread();
	case SRDebug::ECategory::GAS: return CVarDebugGAS.GetValueOnGameThread();
	case SRDebug::ECategory::HUD: return CVarDebugHUD.GetValueOnGameThread();
	case SRDebug::ECategory::Minimap: return CVarDebugMinimap.GetValueOnGameThread();
	case SRDebug::ECategory::Ping: return CVarDebugPing.GetValueOnGameThread();
	case SRDebug::ECategory::Networking: return CVarDebugNetworking.GetValueOnGameThread();
	case SRDebug::ECategory::Movement: return CVarDebugMovement.GetValueOnGameThread();
	case SRDebug::ECategory::TacticalMap: return CVarDebugTacticalMap.GetValueOnGameThread();
	case SRDebug::ECategory::WorldPing: return CVarDebugWorldPing.GetValueOnGameThread();
	case SRDebug::ECategory::TeamMarkers: return CVarDebugTeamMarkers.GetValueOnGameThread();
	case SRDebug::ECategory::Reveal: return CVarDebugReveal.GetValueOnGameThread();
	case SRDebug::ECategory::UIInput: return CVarDebugUIInput.GetValueOnGameThread();
	}
	return 0;
}

FString GetCategoryName(SRDebug::ECategory Category)
{
	switch (Category)
	{
	case SRDebug::ECategory::Input: return TEXT("INPUT");
	case SRDebug::ECategory::Combat: return TEXT("COMBAT");
	case SRDebug::ECategory::Hitscan: return TEXT("HITSCAN");
	case SRDebug::ECategory::Damage: return TEXT("DAMAGE");
	case SRDebug::ECategory::Projectile: return TEXT("PROJECTILE");
	case SRDebug::ECategory::Ability: return TEXT("ABILITY");
	case SRDebug::ECategory::MovementAbility: return TEXT("MOVEMENT");
	case SRDebug::ECategory::GAS: return TEXT("GAS");
	case SRDebug::ECategory::HUD: return TEXT("HUD");
	case SRDebug::ECategory::Minimap: return TEXT("MINIMAP");
	case SRDebug::ECategory::Ping: return TEXT("PING");
	case SRDebug::ECategory::Networking: return TEXT("NET");
	case SRDebug::ECategory::Movement: return TEXT("MOVEMENT");
	case SRDebug::ECategory::TacticalMap: return TEXT("MAP");
	case SRDebug::ECategory::WorldPing: return TEXT("PING");
	case SRDebug::ECategory::TeamMarkers: return TEXT("TEAM");
	case SRDebug::ECategory::Reveal: return TEXT("REVEAL");
	case SRDebug::ECategory::UIInput: return TEXT("UIINPUT");
	}
	return TEXT("DEBUG");
}

// Throttle state: category key -> last on-screen time.
TMap<uint8, double> LastMessageTime;
FCriticalSection MessageLock;
}

namespace SRDebug
{
bool IsEnabled(ECategory Category)
{
	return GetToggleValue(Category) > 0;
}

void Log(ECategory Category, const FString& Label, const FString& Text)
{
#if !UE_BUILD_SHIPPING
	if (!IsEnabled(Category)) return;
	UE_LOG(LogTemp, Log, TEXT("[%s][%s] %s"), *GetCategoryName(Category), *Label, *Text);
#endif
}

void Message(ECategory Category, const FString& Label, const FString& Text, float ThrottleSeconds, const FColor& Color, float DisplaySeconds)
{
#if !UE_BUILD_SHIPPING
	if (!IsEnabled(Category)) return;
	const uint8 Key = static_cast<uint8>(Category);
	FScopeLock Lock(&MessageLock);
	const double Now = FPlatformTime::Seconds();
	double* Last = LastMessageTime.Find(Key);
	if (Last && Now - *Last < ThrottleSeconds) return;
	LastMessageTime.FindOrAdd(Key) = Now;
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(7000 + Key, DisplaySeconds, Color,
			FString::Printf(TEXT("[%s][%s] %s"), *GetCategoryName(Category), *Label, *Text));
	}
#endif
}

	void NetLog(const AActor* Actor, const FString& Message)
	{
#if !UE_BUILD_SHIPPING
		// One consistent prefix + net role (+ local control for pawns) so every
		// lifecycle line identifies which machine and which replication role ran it.
		const ENetRole Role = Actor ? Actor->GetLocalRole() : ROLE_None;
		const FString RoleName = UEnum::GetValueAsString(TEXT("Engine.ENetRole"), Role);
		if (const APawn* Pawn = Cast<APawn>(Actor))
		{
			UE_LOG(LogTemp, Log, TEXT("[ProjectS][Network] [%s] local=%d %s"),
				*RoleName, Pawn->IsLocallyControlled() ? 1 : 0, *Message);
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("[ProjectS][Network] [%s] %s"), *RoleName, *Message);
		}
#endif
	}

	void CombatLog(const AActor* Actor, const FString& Message)
	{
#if !UE_BUILD_SHIPPING
		const ENetRole Role = Actor ? Actor->GetLocalRole() : ROLE_None;
		const FString RoleName = UEnum::GetValueAsString(TEXT("Engine.ENetRole"), Role);
		if (const APawn* Pawn = Cast<APawn>(Actor))
		{
			UE_LOG(LogTemp, Log, TEXT("[ProjectS][Combat] [%s] local=%d %s"),
				*RoleName, Pawn->IsLocallyControlled() ? 1 : 0, *Message);
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("[ProjectS][Combat] [%s] %s"), *RoleName, *Message);
		}
#endif
	}

	void PredictionLog(const AActor* Actor, const FString& Message)
	{
#if !UE_BUILD_SHIPPING
		const ENetRole Role = Actor ? Actor->GetLocalRole() : ROLE_None;
		const FString RoleName = UEnum::GetValueAsString(TEXT("Engine.ENetRole"), Role);
		if (const APawn* Pawn = Cast<APawn>(Actor))
		{
			UE_LOG(LogTemp, Log, TEXT("[ProjectS][FirePrediction] [%s] local=%d %s"),
				*RoleName, Pawn->IsLocallyControlled() ? 1 : 0, *Message);
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("[ProjectS][FirePrediction] [%s] %s"), *RoleName, *Message);
		}
#endif
	}
}
