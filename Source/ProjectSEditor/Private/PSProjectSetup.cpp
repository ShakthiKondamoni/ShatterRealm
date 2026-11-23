#include "PSProjectSetup.h"
#include "Character/PSBaseCharacter.h"
#include "Character/PSCharacterMovementComponent.h"
#include "PSGameMode.h"
#include "GAS/PSPlayerState.h"
#include "Abilities/SRAbilities.h"
#include "Data/SRHeroDataAsset.h"
#include "Combat/SRWeaponDataAsset.h"
#include "Character/SRCombatDummy.h"
#include "Data/SRMinimapDataAsset.h"
#include "Character/SRCombatDummy.h"
#include "Components/SphereComponent.h"
#include "Engine/Texture2D.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "BlueprintEditorLibrary.h"
#include "Components/TextRenderComponent.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "Engine/TextRenderActor.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "InputCoreTypes.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"

namespace
{
bool SavePSAsset(UObject* Asset)
{
    UPackage* Package = Asset->GetOutermost();
    const FString File = FPackageName::LongPackageNameToFilename(Package->GetName(), Asset->IsA<UWorld>() ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension());
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    Args.SaveFlags = SAVE_NoError;
    return UPackage::SavePackage(Package, Asset, *File, Args);
}

bool BackupPSAsset(const FString& PackageName, const FString& Extension)
{
    const FString Source = FPackageName::LongPackageNameToFilename(PackageName, Extension);
    const FString Target = FPaths::ProjectSavedDir() / TEXT("ProjectSSetup/OriginalAssets") / FPaths::GetCleanFilename(Source);
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Target), true);
    if (IFileManager::Get().FileExists(*Target)) return true;
    return IFileManager::Get().Copy(*Target, *Source, false) == COPY_OK;
}

template<typename T> T* PSAsset(const FString& Path)
{
    if (T* Existing = LoadObject<T>(nullptr, *Path)) return Existing;
    UPackage* Package = CreatePackage(*Path);
    T* Asset = NewObject<T>(Package, FName(*FPackageName::GetLongPackageAssetName(Path)), RF_Public | RF_Standalone);
    FAssetRegistryModule::AssetCreated(Asset);
    return Asset;
}
}

bool ConfigurePSZyrexCheckpoint()
{
    // Explicit commandlet step: configures the existing hero without adding
    // presentation assets or treating configuration as a passed gameplay test.
    const FString Root = TEXT("/Game/ProjectS/Characters/Shared/Heroes/Zyrex/");
    USRHeroDataAsset* Hero = LoadObject<USRHeroDataAsset>(nullptr, *(Root + TEXT("DA_Zyrex.DA_Zyrex")));
    USRWeaponDataAsset* Weapon = LoadObject<USRWeaponDataAsset>(nullptr, *(Root + TEXT("DA_ZyrexWeapon.DA_ZyrexWeapon")));
    UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *(Root + TEXT("BP_Zyrex.BP_Zyrex")));
    if (!Hero || !Weapon || !Blueprint || !Blueprint->GeneratedClass
        || !Blueprint->GeneratedClass->IsChildOf(APSBaseCharacter::StaticClass()))
    {
        UE_LOG(LogProjectSCharacter, Error, TEXT("Zyrex checkpoint: existing hero/weapon/character assets are missing or invalid."));
        return false;
    }
    for (UObject* Asset : { static_cast<UObject*>(Hero), static_cast<UObject*>(Weapon), static_cast<UObject*>(Blueprint) })
        if (!BackupPSAsset(Asset->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension())) return false;

    Hero->Modify();
    Hero->HeroID = TEXT("Zyrex");
    Hero->DisplayName = NSLOCTEXT("SR", "ZyrexHeroName", "Zyrex");
    Hero->HeroRole = ESRHeroRole::Duelist;
    Hero->HeroElement = ESRHeroElement::Electricity;
    Hero->AbilitySlots.AbilityQ = USRGameplayAbility_ArcBolt::StaticClass();
    Hero->AbilitySlots.AbilityE = USRGameplayAbility_StaticLeap::StaticClass();
    Hero->AbilitySlots.AbilityC = USRGameplayAbility_Overcharge::StaticClass();
    Hero->AbilitySlots.AbilityX = USRGameplayAbility_Stormpiercer::StaticClass();
    Weapon->Modify();
    Weapon->WeaponDisplayName = NSLOCTEXT("SR", "ZyrexPrimaryName", "Spark Shot");
    Weapon->FireMode = ESRFireMode::Automatic;
    Weapon->Damage = 22.f;
    Weapon->Range = 12000.f;
    Weapon->FireRate = 7.f;
    Weapon->MaxCharges = 30;
    Weapon->RechargeDuration = 1.8f;

    Blueprint->Modify();
    APSBaseCharacter* Defaults = Cast<APSBaseCharacter>(Blueprint->GeneratedClass->GetDefaultObject());
    if (!Defaults) return false;
    Defaults->Modify();
    Defaults->HeroData = Hero;
    Defaults->WeaponData = Weapon;
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    if (Blueprint->Status == BS_Error || !Blueprint->GeneratedClass)
    {
        UE_LOG(LogProjectSCharacter, Error, TEXT("Zyrex checkpoint: BP_Zyrex compilation failed; no configured assets saved."));
        return false;
    }
    Hero->MarkPackageDirty();
    Weapon->MarkPackageDirty();
    Blueprint->MarkPackageDirty();
    if (!SavePSAsset(Hero) || !SavePSAsset(Weapon) || !SavePSAsset(Blueprint)) return false;
    UE_LOG(LogProjectSCharacter, Display, TEXT("Zyrex configuration saved and BP compiled. Gameplay/multiplayer verification is still required."));
    return true;
}

bool UpdatePSControlMappings()
{
    auto* Mapping = LoadObject<UInputMappingContext>(nullptr, TEXT("/Game/ProjectS/Input/IMC_PSCharacter.IMC_PSCharacter"));
    auto* Crouch = LoadObject<UInputAction>(nullptr, TEXT("/Game/ProjectS/Input/IA_PSCrouch.IA_PSCrouch"));
    auto* Slide = LoadObject<UInputAction>(nullptr, TEXT("/Game/ProjectS/Input/IA_PSSlide.IA_PSSlide"));
    if (!Mapping || !Crouch || !Slide) return false;
    if (!BackupPSAsset(Mapping->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension())) return false;
    Mapping->Modify();
    Mapping->UnmapKey(Crouch, EKeys::LeftControl);
    Mapping->UnmapAllKeysFromAction(Slide);
    Mapping->MapKey(Slide, EKeys::LeftControl);
    if (!SavePSAsset(Mapping)) return false;
    const FString Marker = FPaths::ProjectSavedDir() / TEXT("ProjectSSetup/controls-v2.complete");
    if (!FFileHelper::SaveStringToFile(TEXT("LeftControl: sprint-slide or held crouch. LeftShift: toggle sprint.\n"), *Marker)) return false;
    UE_LOG(LogProjectSCharacter, Display, TEXT("ProjectS controls updated: LeftControl crouches, or slides from a grounded sprint. C slide binding removed."));
    return true;
}

bool UpdatePSAbilityInput()
{
    const FString InputPath = TEXT("/Game/ProjectS/Input/");
    struct FSlotDefinition { const TCHAR* AssetName; const FKey& Key; };
    const FSlotDefinition Slots[] = {
        { TEXT("IA_PSAbilityQ"), EKeys::Q },
        { TEXT("IA_PSAbilityE"), EKeys::E },
        { TEXT("IA_PSAbilityC"), EKeys::C },
        { TEXT("IA_PSAbilityX"), EKeys::X }
    };

    UInputMappingContext* Mapping = LoadObject<UInputMappingContext>(nullptr, TEXT("/Game/ProjectS/Input/IMC_PSCharacter.IMC_PSCharacter"));
    if (!Mapping) return false;
    if (!BackupPSAsset(Mapping->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension())) return false;

    Mapping->Modify();
    for (const FSlotDefinition& Slot : Slots)
    {
        UInputAction* Action = PSAsset<UInputAction>(InputPath + Slot.AssetName);
        Action->ValueType = EInputActionValueType::Boolean;
        // Idempotent: skip keys that are already mapped to this action.
        bool bAlreadyMapped = false;
        for (const FEnhancedActionKeyMapping& KeyMapping : Mapping->GetMappings())
            bAlreadyMapped |= KeyMapping.Action == Action && KeyMapping.Key == Slot.Key;
        if (!bAlreadyMapped) Mapping->MapKey(Action, Slot.Key);
        if (!SavePSAsset(Action)) return false;
    }
    if (!SavePSAsset(Mapping)) return false;

    const FString Marker = FPaths::ProjectSavedDir() / TEXT("ProjectSSetup/abilities-v1.complete");
    if (!FFileHelper::SaveStringToFile(TEXT("Q/E/C/X map to the hero ability slot actions.\n"), *Marker)) return false;
    UE_LOG(LogProjectSCharacter, Display, TEXT("ProjectS ability input ready: Q/E/C/X are mapped to the hero ability slots."));
    return true;
}

bool UpdatePSCombatInput()
{
    const FString InputPath = TEXT("/Game/ProjectS/Input/");
    struct FCombatInputDefinition { const TCHAR* AssetName; const FKey& Key; };
    const FCombatInputDefinition Inputs[] = {
        { TEXT("IA_PSPrimaryFire"), EKeys::LeftMouseButton },
        { TEXT("IA_PSRecharge"), EKeys::R },
        { TEXT("IA_PSTacticalMap"), EKeys::M },
        { TEXT("IA_PSPing"), EKeys::MiddleMouseButton }
    };

    UInputMappingContext* Mapping = LoadObject<UInputMappingContext>(nullptr, TEXT("/Game/ProjectS/Input/IMC_PSCharacter.IMC_PSCharacter"));
    if (!Mapping) return false;
    Mapping->Modify();
    for (const FCombatInputDefinition& Input : Inputs)
    {
        UInputAction* Action = PSAsset<UInputAction>(InputPath + Input.AssetName);
        Action->ValueType = EInputActionValueType::Boolean;
        bool bAlreadyMapped = false;
        for (const FEnhancedActionKeyMapping& KeyMapping : Mapping->GetMappings())
            bAlreadyMapped |= KeyMapping.Action == Action && KeyMapping.Key == Input.Key;
        if (!bAlreadyMapped) Mapping->MapKey(Action, Input.Key);
        if (!SavePSAsset(Action)) return false;
    }
    if (!SavePSAsset(Mapping)) return false;

    const FString Marker = FPaths::ProjectSavedDir() / TEXT("ProjectSSetup/input-v3.complete");
    if (!FFileHelper::SaveStringToFile(TEXT("LMB primary fire, R recharge, M tactical map, MMB ping.\n"), *Marker)) return false;
    UE_LOG(LogProjectSCharacter, Display, TEXT("ProjectS combat input ready: LMB/R/M/MMB mapped."));
    return true;
}

namespace
{
/** Spawns a scaled cube into the world being built and returns it (nullptr on failure). */
AStaticMeshActor* SpawnTestCube(UWorld* World, const FVector& Center, const FVector& Scale, const FLinearColor& Color, const FName& Name)
{
    if (!World) return nullptr;
    FActorSpawnParameters Params;
    Params.Name = Name;
    AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(Center, FRotator::ZeroRotator, Params);
    if (!Actor) return nullptr;
    UStaticMeshComponent* Mesh = Actor->GetStaticMeshComponent();
    Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
    Mesh->SetWorldScale3D(Scale);
    Mesh->SetCollisionProfileName(TEXT("BlockAll"));
    Actor->SetMobility(EComponentMobility::Static);
    return Actor;
}

/** Spawns a distance/label text marker. */
void SpawnTestLabel(UWorld* World, const FVector& Center, const FString& Text)
{
    ATextRenderActor* Label = World->SpawnActor<ATextRenderActor>(Center, FRotator(0.f, 90.f, 0.f));
    if (Label)
    {
        Label->GetTextRender()->SetText(FText::FromString(Text));
        Label->GetTextRender()->SetTextRenderColor(FColor::White);
        Label->GetTextRender()->SetWorldSize(60.f);
        Label->SetActorScale3D(FVector(4.f));
    }
}
}

namespace
{
/** Draws a filled rectangle (world-space footprint) into the map pixel buffer. */
void FillMapRect(TArray<FColor>& Pixels, int32 TexW, int32 TexH,
    float MinX, float MaxX, float MinY, float MaxY,
    float X0, float Y0, float X1, float Y1, const FColor& Color)
{
    // World -> pixel: north-up, X right, Y up flipped to V down.
    auto ToPxX = [MinX, MaxX, TexW](float X) { return FMath::Clamp(int32((X - MinX) / (MaxX - MinX) * TexW), 0, TexW - 1); };
    auto ToPxY = [MinY, MaxY, TexH](float Y) { return FMath::Clamp(int32((1.f - (Y - MinY) / (MaxY - MinY)) * TexH), 0, TexH - 1); };
    const int32 Px0 = ToPxX(FMath::Min(X0, X1)), Px1 = ToPxX(FMath::Max(X0, X1));
    const int32 Py0 = ToPxY(FMath::Max(Y0, Y1)), Py1 = ToPxY(FMath::Min(Y0, Y1));
    for (int32 Y = Py0; Y <= Py1; ++Y)
    {
        for (int32 X = Px0; X <= Px1; ++X)
        {
            Pixels[Y * TexW + X] = Color;
        }
    }
}

void FillMapCircle(TArray<FColor>& Pixels, int32 TexW, int32 TexH,
    float MinX, float MaxX, float MinY, float MaxY,
    float CX, float CY, float RadiusWorld, const FColor& Color)
{
    const float Cx = (CX - MinX) / (MaxX - MinX) * TexW;
    const float Cy = (1.f - (CY - MinY) / (MaxY - MinY)) * TexH;
    const float Rpx = RadiusWorld / (MaxX - MinX) * TexW;
    const int32 R = FMath::Max(1, int32(Rpx));
    for (int32 Y = FMath::Max(0, int32(Cy) - R); Y <= FMath::Min(TexH - 1, int32(Cy) + R); ++Y)
    {
        for (int32 X = FMath::Max(0, int32(Cx) - R); X <= FMath::Min(TexW - 1, int32(Cx) + R); ++X)
        {
            const float Dx = X - Cx, Dy = Y - Cy;
            if (Dx * Dx + Dy * Dy <= Rpx * Rpx)
            {
                Pixels[Y * TexW + X] = Color;
            }
        }
    }
}
}

bool GeneratePSMapTexture()
{
    if (!UEditorLoadingAndSavingUtils::LoadMap(TEXT("/Game/ProjectS/maps/L_CombatTest")))
    {
        UE_LOG(LogProjectSCharacter, Error, TEXT("Map texture: failed to load L_CombatTest"));
        return false;
    }
    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World) return false;

    constexpr float MinX = -4000.f, MaxX = 4000.f, MinY = -3000.f, MaxY = 3000.f;
    constexpr int32 W = 1024, H = 768;

    // Create a transient texture and fill it with the map representation.
    UTexture2D* Texture = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8);
    if (!Texture) return false;
    Texture->NeverStream = true;

    FColor* Pixels = static_cast<FColor*>(Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE));

    auto ToPxX = [MinX, MaxX, W](float X) { return FMath::Clamp(int32((X - MinX) / (MaxX - MinX) * W), 0, W - 1); };
    auto ToPxY = [MinY, MaxY, H](float Y) { return FMath::Clamp(int32((1.f - (Y - MinY) / (MaxY - MinY)) * H), 0, H - 1); };
    auto FillRect = [&](float X0, float Y0, float X1, float Y1, const FColor& Color)
    {
        const int32 Px0 = ToPxX(FMath::Min(X0, X1)), Px1 = ToPxX(FMath::Max(X0, X1));
        const int32 Py0 = ToPxY(FMath::Max(Y0, Y1)), Py1 = ToPxY(FMath::Min(Y0, Y1));
        for (int32 Y = Py0; Y <= Py1; ++Y)
            for (int32 X = Px0; X <= Px1; ++X)
                Pixels[Y * W + X] = Color;
    };
    auto FillCircle = [&](float CX, float CY, float RadiusWorld, const FColor& Color)
    {
        const float Cx = (CX - MinX) / (MaxX - MinX) * W;
        const float Cy = (1.f - (CY - MinY) / (MaxY - MinY)) * H;
        const float Rpx = RadiusWorld / (MaxX - MinX) * W;
        const int32 R = FMath::Max(1, int32(Rpx));
        for (int32 Y = FMath::Max(0, int32(Cy) - R); Y <= FMath::Min(H - 1, int32(Cy) + R); ++Y)
            for (int32 X = FMath::Max(0, int32(Cx) - R); X <= FMath::Min(W - 1, int32(Cx) + R); ++X)
            {
                const float Dx = X - Cx, Dy = Y - Cy;
                if (Dx * Dx + Dy * Dy <= Rpx * Rpx) Pixels[Y * W + X] = Color;
            }
    };

    // Background.
    for (int32 Y = 0; Y < H; ++Y)
        for (int32 X = 0; X < W; ++X)
            Pixels[Y * W + X] = FColor(12, 18, 25, 255);

    // 10m grid.
    const FColor GridColor(45, 40, 60);
    for (int32 Meters = -30; Meters <= 30; Meters += 10)
    {
        FillRect(Meters * 100 - 25, MinY, Meters * 100 + 25, MaxY, GridColor);
        FillRect(MinX, Meters * 100 - 25, MaxX, Meters * 100 + 25, GridColor);
    }

    // Real geometry footprints.
    const FColor GeometryColor(120, 120, 140);
    for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
    {
        if (It->GetActorLocation().Z < -200.f) continue;
        FBox Bounds = It->GetComponentsBoundingBox(false);
        FillRect(Bounds.Min.X, Bounds.Min.Y, Bounds.Max.X, Bounds.Max.Y, GeometryColor);
    }

    // Dummies (green) and player spawns (blue).
    for (TActorIterator<APSCombatDummy> It(World); It; ++It)
        FillCircle(It->GetActorLocation().X, It->GetActorLocation().Y, 120.f, FColor(40, 200, 60));
    for (TActorIterator<APlayerStart> It(World); It; ++It)
        FillCircle(It->GetActorLocation().X, It->GetActorLocation().Y, 100.f, FColor(60, 120, 255));

    Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
    Texture->UpdateResource();

    // Save the texture to the project content.
    USRMinimapDataAsset* MinimapData = LoadObject<USRMinimapDataAsset>(nullptr, TEXT("/Game/ProjectS/UI/DA_MinimapTest.DA_MinimapTest"));
    if (MinimapData)
    {
        MinimapData->Modify();
        MinimapData->MapTexture = Texture;
        MinimapData->MinWorldX = MinX; MinimapData->MaxWorldX = MaxX;
        MinimapData->MinWorldY = MinY; MinimapData->MaxWorldY = MaxY;
        FAssetRegistryModule::AssetCreated(Texture);
        if (!SavePSAsset(Texture)) return false;
        if (!SavePSAsset(MinimapData)) return false;
    }

    UEditorLoadingAndSavingUtils::LoadMap(TEXT("/Game/Levels/DefaultLevel.DefaultLevel"));
    const FString Marker = FPaths::ProjectSavedDir() / TEXT("ProjectSSetup/map-v2.complete");
    if (!FFileHelper::SaveStringToFile(TEXT("Top-down map texture generated."), *Marker)) return false;
    UE_LOG(LogProjectSCharacter, Display, TEXT("ProjectS map texture generated: /Game/ProjectS/UI/T_Map_CombatTest"));
    return true;
}

bool CreatePSCombatTestMap()
{
    if (!GEditor) return false;
    UE_LOG(LogProjectSCharacter, Display, TEXT("ProjectS setup: building the combat test map L_CombatTest."));

    // Fresh blank map (the editor is idle during setup; the previous level is not saved over).
    if (!UEditorLoadingAndSavingUtils::NewBlankMap(false))
    {
        UE_LOG(LogProjectSCharacter, Error, TEXT("Combat map builder: NewBlankMap failed"));
        return false;
    }
    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World) return false;

    // Ground: 80m x 60m plate with its TOP at z=0 (the 100cm cube scaled 10x on z
    // spans 1000cm: center must sit at -500 so characters stand ON the surface).
    SpawnTestCube(World, FVector(0, 0, -500), FVector(800, 600, 10), FLinearColor::Gray, TEXT("Ground"));

    // --- Firing lane with distance markers (north of spawn, along +Y) ---
    SpawnTestCube(World, FVector(0, 2500, 150), FVector(200, 400, 300), FLinearColor::Red, TEXT("SolidWall"));          // solid cover wall
    SpawnTestCube(World, FVector(1200, 1500, 60), FVector(120, 400, 120), FLinearColor(0.3f, 0.3f, 0.3f), TEXT("LowObstacle")); // vault obstacle
    SpawnTestCube(World, FVector(-1500, 2200, 200), FVector(600, 400, 400), FLinearColor::Blue, TEXT("RaisedPlatform"));
    // Low tunnel: ceiling slab 150cm above the floor with two pillars - the camera-clipping regression case.
    SpawnTestCube(World, FVector(-2500, 1800, 0), FVector(600, 400, 200), FLinearColor::Green, TEXT("TunnelPillarA"));
    SpawnTestCube(World, FVector(-2500, 2600, 0), FVector(600, 400, 200), FLinearColor::Green, TEXT("TunnelPillarB"));
    SpawnTestCube(World, FVector(-2500, 2200, 275), FVector(600, 1600, 50), FLinearColor::Green, TEXT("TunnelCeiling"));
    // Thin obstacle for projectile collision tests.
    SpawnTestCube(World, FVector(2200, 1500, 150), FVector(20, 400, 300), FLinearColor::Yellow, TEXT("ThinObstacle"));

    // Distance markers along the firing lane.
    for (int32 Meters = 10; Meters <= 60; Meters += 10)
    {
        SpawnTestLabel(World, FVector(0, Meters * 100, 30), FString::Printf(TEXT("%dm"), Meters));
    }
    SpawnTestLabel(World, FVector(-2500, 1000, 60), TEXT("CAMERA TUNNEL"));
    SpawnTestLabel(World, FVector(1200, 1000, 60), TEXT("VAULT"));
    SpawnTestLabel(World, FVector(-1500, 1300, 300), TEXT("PLATFORM"));

    // Minimap calibration anchors: center + four boundary markers.
    SpawnTestLabel(World, FVector(0, 0, 60), TEXT("CENTER"));
    SpawnTestLabel(World, FVector(-3800, -2800, 60), TEXT("SW"));
    SpawnTestLabel(World, FVector(3800, -2800, 60), TEXT("SE"));
    SpawnTestLabel(World, FVector(-3800, 2800, 60), TEXT("NW"));
    SpawnTestLabel(World, FVector(3800, 2800, 60), TEXT("NE"));

    // Player spawns (two-player testing) facing the firing lane.
    if (APlayerStart* StartA = World->SpawnActor<APlayerStart>(FVector(0, -1500, 100), FRotator(0.f, 90.f, 0.f)))
    {
        StartA->PlayerStartTag = TEXT("SpawnA");
    }
    if (APlayerStart* StartB = World->SpawnActor<APlayerStart>(FVector(800, -1500, 100), FRotator(0.f, 90.f, 0.f)))
    {
        StartB->PlayerStartTag = TEXT("SpawnB");
    }

    // GAS-enabled damageable dummies (with live health/shield displays).
    World->SpawnActor<APSCombatDummy>(FVector(0, 2000, 100), FRotator::ZeroRotator);
    World->SpawnActor<APSCombatDummy>(FVector(-800, 2600, 100), FRotator::ZeroRotator);
    // Projectile collision targets.
    World->SpawnActor<APSCombatDummy>(FVector(2200, 1900, 100), FRotator::ZeroRotator);
    World->SpawnActor<APSCombatDummy>(FVector(1500, 2500, 100), FRotator::ZeroRotator);

    // This map spawns the combat-enabled hero.
    if (UClass* GameMode = LoadClass<AGameModeBase>(nullptr, TEXT("/Game/ProjectS/Characters/Shared/Heroes/Zyrex/GM_ZyrexTest.GM_ZyrexTest_C")))
    {
        World->GetWorldSettings()->DefaultGameMode = GameMode;
    }

    if (!UEditorLoadingAndSavingUtils::SaveMap(World, TEXT("/Game/ProjectS/maps/L_CombatTest")))
    {
        UE_LOG(LogProjectSCharacter, Error, TEXT("Combat map builder: SaveMapAsset failed"));
        return false;
    }

    // Minimap configuration from the geometry just placed.
    USRMinimapDataAsset* MinimapData = PSAsset<USRMinimapDataAsset>(TEXT("/Game/ProjectS/UI/DA_MinimapTest"));
    if (MinimapData)
    {
        MinimapData->MinWorldX = -4000.f; MinimapData->MaxWorldX = 4000.f;
        MinimapData->MinWorldY = -3000.f; MinimapData->MaxWorldY = 3000.f;
        FAssetRegistryModule::AssetCreated(MinimapData);
        SavePSAsset(MinimapData);
    }

    // Return the editor to the default level.
    UEditorLoadingAndSavingUtils::LoadMap(TEXT("/Game/Levels/DefaultLevel.DefaultLevel"));

    const FString Marker = FPaths::ProjectSavedDir() / TEXT("ProjectSSetup/map-v1.complete");
    if (!FFileHelper::SaveStringToFile(TEXT("Combat test map + minimap data asset created.\n"), *Marker)) return false;
    UE_LOG(LogProjectSCharacter, Display, TEXT("ProjectS combat test map created: /Game/ProjectS/maps/L_CombatTest"));
    return true;
}

bool RunPSProjectSetup()
{
    UE_LOG(LogProjectSCharacter, Display, TEXT("ProjectS setup: creating input assets and preserving the sample animation framework."));
    const FString InputPath = TEXT("/Game/ProjectS/Input/");
    UInputAction* Move = PSAsset<UInputAction>(InputPath + TEXT("IA_PSMove"));
    UInputAction* Look = PSAsset<UInputAction>(InputPath + TEXT("IA_PSLook"));
    UInputAction* Crouch = PSAsset<UInputAction>(InputPath + TEXT("IA_PSCrouch"));
    UInputAction* Sprint = PSAsset<UInputAction>(InputPath + TEXT("IA_PSSprint"));
    UInputAction* Slide = PSAsset<UInputAction>(InputPath + TEXT("IA_PSSlide"));
    UInputAction* Jump = PSAsset<UInputAction>(InputPath + TEXT("IA_PSJump"));
    Move->ValueType = Look->ValueType = EInputActionValueType::Axis2D;
    Move->AccumulationBehavior = EInputActionAccumulationBehavior::Cumulative;
    Crouch->ValueType = Sprint->ValueType = Slide->ValueType = Jump->ValueType = EInputActionValueType::Boolean;
    UInputMappingContext* Mapping = PSAsset<UInputMappingContext>(InputPath + TEXT("IMC_PSCharacter"));
    Mapping->UnmapAll();
    auto MapMove = [Mapping, Move](FKey Key, bool bY, bool bNegative)
    {
        auto& KeyMapping = Mapping->MapKey(Move, Key);
        if (bNegative) KeyMapping.Modifiers.Add(NewObject<UInputModifierNegate>(Mapping));
        if (bY)
        {
            auto* Swizzle = NewObject<UInputModifierSwizzleAxis>(Mapping);
            Swizzle->Order = EInputAxisSwizzle::YXZ;
            KeyMapping.Modifiers.Add(Swizzle);
        }
    };
    MapMove(EKeys::W, true, false); MapMove(EKeys::S, true, true);
    MapMove(EKeys::A, false, true); MapMove(EKeys::D, false, false);
    Mapping->MapKey(Look, EKeys::Mouse2D);
    Mapping->MapKey(Sprint, EKeys::LeftShift);
    Mapping->MapKey(Slide, EKeys::LeftControl);
    Mapping->MapKey(Jump, EKeys::SpaceBar);
    for (UObject* Asset : TArray<UObject*>{Move, Look, Crouch, Sprint, Slide, Jump, Mapping})
        if (!SavePSAsset(Asset)) return false;

    UBlueprint* Sample = LoadObject<UBlueprint>(nullptr, TEXT("/Game/Blueprints/SandboxCharacter_CMC.SandboxCharacter_CMC"));
    if (!Sample || !Sample->GeneratedClass) return false;
    if (!BackupPSAsset(Sample->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension())) return false;
    // Reparenting the existing Blueprint preserves all existing class references,
    // interface implementations, traversal components, mesh and motion matching setup.
    if (Sample->ParentClass != APSBaseCharacter::StaticClass())
    {
        if (Sample->ParentClass != ACharacter::StaticClass())
        {
            UE_LOG(LogProjectSCharacter, Error, TEXT("Sample has a custom parent; refusing to discard it: %s"), *GetNameSafe(Sample->ParentClass));
            return false;
        }
        UBlueprintEditorLibrary::ReparentBlueprint(Sample, APSBaseCharacter::StaticClass());
    }
    FKismetEditorUtilities::CompileBlueprint(Sample);
    if (Sample->Status == BS_Error || !Sample->GeneratedClass->IsChildOf(APSBaseCharacter::StaticClass())) return false;

    const FString CharacterPath = TEXT("/Game/ProjectS/Characters/BP_PSBaseCharacter");
    UBlueprint* CharacterBP = LoadObject<UBlueprint>(nullptr, *CharacterPath);
    if (!CharacterBP)
    {
        UPackage* Package = CreatePackage(*CharacterPath);
        CharacterBP = FKismetEditorUtilities::CreateBlueprint(Sample->GeneratedClass, Package, TEXT("BP_PSBaseCharacter"), BPTYPE_Normal,
            UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
        FAssetRegistryModule::AssetCreated(CharacterBP);
    }
    if (!CharacterBP || CharacterBP->ParentClass != Sample->GeneratedClass) return false;
    FKismetEditorUtilities::CompileBlueprint(CharacterBP);
    if (CharacterBP->Status == BS_Error) return false;
    auto* Defaults = Cast<APSBaseCharacter>(CharacterBP->GeneratedClass->GetDefaultObject());
    if (!Defaults || !Defaults->GetPSMovement()) return false;
    Defaults->PSMappingContext = Mapping;
    Defaults->PSMoveAction = Move; Defaults->PSLookAction = Look;
    Defaults->PSCrouchAction = Crouch; Defaults->PSSprintAction = Sprint;
    Defaults->PSSlideAction = Slide; Defaults->PSJumpAction = Jump;
    Defaults->PSSlideAnimation = LoadObject<UAnimSequence>(nullptr,
        TEXT("/Game/Characters/UEFN_Mannequin/Animations/Slide/M_Neutral_Slide_KneesOut_Loop.M_Neutral_Slide_KneesOut_Loop"));
    if (!Defaults->PSSlideAnimation) return false;
    // Sample BeginPlay installs MappingContext; give it the same new context.
    if (auto* Property = FindFProperty<FObjectPropertyBase>(Defaults->GetClass(), TEXT("MappingContext")))
        Property->SetObjectPropertyValue_InContainer(Defaults, Mapping);
    CharacterBP->MarkPackageDirty();
    if (!SavePSAsset(Sample) || !SavePSAsset(CharacterBP)) return false;

    UWorld* Map = LoadObject<UWorld>(nullptr, TEXT("/Game/Levels/DefaultLevel.DefaultLevel"));
    if (!Map || !BackupPSAsset(Map->GetOutermost()->GetName(), FPackageName::GetMapPackageExtension())) return false;
    Map->GetWorldSettings()->DefaultGameMode = APSGameMode::StaticClass();
    Map->MarkPackageDirty();
    if (!SavePSAsset(Map)) return false;

    const FString Report = TEXT("ProjectS setup complete.\n")
        TEXT("Native base: /Script/PSGameplay.PSBaseCharacter\n")
        TEXT("Animation bridge: /Game/Blueprints/SandboxCharacter_CMC\n")
        TEXT("Playable child: /Game/ProjectS/Characters/BP_PSBaseCharacter\n")
        TEXT("Game mode: /Script/PSGameplay.PSGameMode\n")
        TEXT("Player state: /Script/PSGameplay.PSPlayerState\n")
        TEXT("WASD, Mouse, LeftControl crouch/sprint-slide, LeftShift toggle sprint, Space traversal/jump.\n")
        TEXT("Original sample character and map are backed up under Saved/ProjectSSetup/OriginalAssets.\n");
    IFileManager::Get().MakeDirectory(*(FPaths::ProjectSavedDir() / TEXT("ProjectSSetup")), true);
    if (!FFileHelper::SaveStringToFile(Report, *(FPaths::ProjectSavedDir() / TEXT("ProjectSSetup/setup-v1.complete")))) return false;
    UE_LOG(LogProjectSCharacter, Display, TEXT("%s"), *Report);
    return UpdatePSControlMappings();
}
// Task 04: create /Game/ProjectS/Anim/AM_PrimaryFire from the imported fire
// animation, targeting a slot that the Game Animation Sample AnimBlueprint
// actually evaluates. UpperBody is preferred; the sample's proven DefaultSlot
// is the fallback. Locomotion/matching graph itself is never modified.
// Task 04 follow-up: the fire-montage integration (AM_PrimaryFire + the
// upper-body AnimGraph splice) was REMOVED at the user's request — the project
// uses the Game Animation Sample's pristine animation setup. The playback code
// hooks in PSBaseCharacter/SRCombatComponent remain but resolve to no-ops
// because the montage asset no longer exists. fire1 remains in
// /Game/ProjectS/Anim for a future manual integration.


