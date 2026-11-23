#include "Modules/ModuleManager.h"
#include "Containers/Ticker.h"
#include "Editor.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/World.h"
#include "PSProjectSetup.h"

class FProjectSEditorModule final : public IModuleInterface
{
    FTSTicker::FDelegateHandle SetupTicker;
public:
    virtual void StartupModule() override
    {
        if (IsRunningCommandlet()) return;
        // New native classes must exist before the animation Blueprint can be reparented.
        // Wait for the editor/asset registry rather than modifying assets during module loading.
        SetupTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
        {
            // The editor world package must be fully loaded ("LogLoad: Done") before any
        // setup stage touches assets: saving during engine init deadlocks the editor.
        if (!GEditor) return true;
        if (FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get().IsLoadingAssets()) return true;
        UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
        if (!EditorWorld || !EditorWorld->GetPackage() || !EditorWorld->GetPackage()->IsFullyLoaded()) return true;
            const FString Marker = FPaths::ProjectSavedDir() / TEXT("ProjectSSetup/setup-v1.complete");
            if (!IFileManager::Get().FileExists(*Marker))
            {
                if (!RunPSProjectSetup()) UE_LOG(LogTemp, Error, TEXT("ProjectS character setup failed; see Output Log."));
            }
            else if (!IFileManager::Get().FileExists(*(FPaths::ProjectSavedDir() / TEXT("ProjectSSetup/controls-v2.complete"))))
            {
                if (!UpdatePSControlMappings()) UE_LOG(LogTemp, Error, TEXT("ProjectS control update failed; see Output Log."));
            }
            else if (!IFileManager::Get().FileExists(*(FPaths::ProjectSavedDir() / TEXT("ProjectSSetup/abilities-v1.complete"))))
            {
                if (!UpdatePSAbilityInput()) UE_LOG(LogTemp, Error, TEXT("ProjectS ability input update failed; see Output Log."));
            }
            else if (!IFileManager::Get().FileExists(*(FPaths::ProjectSavedDir() / TEXT("ProjectSSetup/input-v3.complete"))))
            {
                if (!UpdatePSCombatInput()) UE_LOG(LogTemp, Error, TEXT("ProjectS combat input update failed; see Output Log."));
            }
            else if (!IFileManager::Get().FileExists(*(FPaths::ProjectSavedDir() / TEXT("ProjectSSetup/map-v1.complete"))))
            {
                if (!CreatePSCombatTestMap()) UE_LOG(LogTemp, Error, TEXT("ProjectS combat test map creation failed; see Output Log."));
            }
            else if (!IFileManager::Get().FileExists(*(FPaths::ProjectSavedDir() / TEXT("ProjectSSetup/map-v2.complete"))))
            {
                if (!GeneratePSMapTexture()) UE_LOG(LogTemp, Error, TEXT("ProjectS map texture generation failed; see Output Log."));
            }
            else if (!IFileManager::Get().FileExists(*(FPaths::ProjectSavedDir() / TEXT("ProjectSSetup/roster-v1.configured"))))
            {
                if (!ConfigurePSHeroRoster()) UE_LOG(LogTemp, Error, TEXT("ProjectS hero roster setup failed; see Output Log."));
            }
            return false;
        }), 1.f);
    }
    virtual void ShutdownModule() override
    {
        FTSTicker::GetCoreTicker().RemoveTicker(SetupTicker);
    }
};

IMPLEMENT_MODULE(FProjectSEditorModule, ProjectSEditor);
