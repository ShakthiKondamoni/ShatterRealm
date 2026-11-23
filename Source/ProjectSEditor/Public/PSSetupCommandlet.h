#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "PSSetupCommandlet.generated.h"

// Also usable headlessly: UnrealEditor-Cmd ProjectS.uproject -run=PSSetup -nullrhi -unattended.
UCLASS()
class UPSSetupCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UPSSetupCommandlet();
    virtual int32 Main(const FString& Params) override;
};
