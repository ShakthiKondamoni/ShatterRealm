#include "PSSetupCommandlet.h"
#include "PSProjectSetup.h"
#include "Misc/Parse.h"

UPSSetupCommandlet::UPSSetupCommandlet()
{
    IsClient = false;
    IsServer = false;
    IsEditor = true;
    LogToConsole = true;
}
int32 UPSSetupCommandlet::Main(const FString& Params)
{
    if (FParse::Param(*Params, TEXT("HeroRoster"))) return ConfigurePSHeroRoster(FParse::Param(*Params, TEXT("ResetHeroDefaults"))) ? 0 : 1;
    if (FParse::Param(*Params, TEXT("ZyrexCheckpoint"))) return ConfigurePSZyrexCheckpoint() ? 0 : 1;
    return RunPSProjectSetup() ? 0 : 1;
}
