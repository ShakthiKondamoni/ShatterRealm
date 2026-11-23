#include "PSProjectSetup.h"
#include "Data/SRHeroRoster.h"
#include "Data/SRHeroDataAsset.h"
#include "Combat/SRWeaponDataAsset.h"
#include "Character/PSBaseCharacter.h"
#include "PSGameMode.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionTwoSidedSign.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "UObject/SavePackage.h"

namespace
{
bool SaveRosterAsset(UObject* Asset)
{
    if (!Asset) return false;
    UPackage* Package = Asset->GetOutermost();
    const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
    const FString Backup = FPaths::ProjectSavedDir() / TEXT("ProjectSSetup/HeroRosterBackup")
        / Package->GetName().RightChop(6) + FPackageName::GetAssetPackageExtension();
    if (IFileManager::Get().FileExists(*Filename) && !IFileManager::Get().FileExists(*Backup))
    {
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Backup), true);
        if (IFileManager::Get().Copy(*Backup, *Filename, false) != COPY_OK) return false;
    }
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
    FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone; Args.SaveFlags = SAVE_NoError;
    Asset->MarkPackageDirty();
    return UPackage::SavePackage(Package, Asset, *Filename, Args);
}
template<typename T> T* RosterAsset(const FString& Path, bool& bCreated)
{
    if (T* Existing = LoadObject<T>(nullptr, *Path)) { bCreated = false; return Existing; }
    UPackage* Package = CreatePackage(*Path);
    T* Asset = NewObject<T>(Package, FName(*FPackageName::GetLongPackageAssetName(Path)), RF_Public | RF_Standalone);
    if (Asset) FAssetRegistryModule::AssetCreated(Asset);
    bCreated = true; return Asset;
}
UBlueprint* RosterBlueprint(const FString& Path, UClass* Parent)
{
    if (UBlueprint* Existing = LoadObject<UBlueprint>(nullptr, *Path))
    {
        if (!Existing->GeneratedClass || !Existing->GeneratedClass->IsChildOf(Parent))
        { UE_LOG(LogProjectSCharacter, Error, TEXT("Roster: incompatible existing Blueprint %s"), *Path); return nullptr; }
        return Existing;
    }
    UPackage* Package = CreatePackage(*Path);
    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(Parent, Package,
        FName(*FPackageName::GetLongPackageAssetName(Path)), BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
    if (Blueprint) FAssetRegistryModule::AssetCreated(Blueprint);
    return Blueprint;
}
bool CompileAndSave(UBlueprint* Blueprint)
{
    if (!Blueprint) return false;
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    return Blueprint->Status != BS_Error && Blueprint->GeneratedClass && SaveRosterAsset(Blueprint);
}
bool CreateScreenMaterial()
{
    bool bCreated = false;
    UMaterial* Material = RosterAsset<UMaterial>(TEXT("/Game/ProjectS/Gameplay/M_OneWayScreen"), bCreated);
    if (!Material) return false;
    if (!bCreated) return true;
    Material->BlendMode = BLEND_Masked; Material->TwoSided = true;
    UMaterialExpressionTwoSidedSign* Side = NewObject<UMaterialExpressionTwoSidedSign>(Material);
    UMaterialExpressionConstant3Vector* Color = NewObject<UMaterialExpressionConstant3Vector>(Material);
    Color->Constant = FLinearColor(0.12f,0.16f,0.18f);
    Material->GetExpressionCollection().AddExpression(Side);
    Material->GetExpressionCollection().AddExpression(Color);
    Material->GetEditorOnlyData()->OpacityMask.Expression = Side;
    Material->GetEditorOnlyData()->BaseColor.Expression = Color;
    Material->PostEditChange();
    return SaveRosterAsset(Material);
}
bool ConfigureHero(const FSRHeroDefinition& Definition, UClass* SharedCharacterClass, bool bResetExisting)
{
    const FString ID = Definition.ID.ToString(), Root = SRHeroRoster::HeroAssetRoot(Definition.ID);
    bool bNewHero = false, bNewWeapon = false;
    USRHeroDataAsset* Hero = RosterAsset<USRHeroDataAsset>(Root + TEXT("DA_") + ID, bNewHero);
    USRWeaponDataAsset* Weapon = RosterAsset<USRWeaponDataAsset>(Root + TEXT("DA_") + ID + TEXT("Weapon"), bNewWeapon);
    UBlueprint* Character = RosterBlueprint(Root + TEXT("BP_") + ID, SharedCharacterClass);
    if (!Hero || !Weapon || !Character) return false;
    Hero->Modify(); Weapon->Modify(); Character->Modify();
    Hero->HeroID = Definition.ID;
    Hero->DisplayName = FText::FromName(Definition.ID);
    Hero->HeroRole = Definition.Role; Hero->HeroElement = Definition.Element;
    Weapon->WeaponDisplayName = Definition.PrimaryName; Weapon->SignatureWeaponName = Definition.SignatureWeapon;
    if (bNewWeapon || bResetExisting || Definition.ID == TEXT("Zyrex") || Definition.ID == TEXT("Veyra"))
    {
        Weapon->Damage = Definition.Damage; Weapon->Range = Definition.Range; Weapon->FireRate = Definition.FireRate;
        Weapon->MaxCharges = Definition.Charges; Weapon->RechargeDuration = Definition.Recharge;
        Weapon->FireMode = ESRFireMode::Automatic;
    }
    TSubclassOf<UGameplayAbility> Abilities[4];
    const TCHAR* Slots[] = { TEXT("Q"), TEXT("E"), TEXT("C"), TEXT("X") };
    for (int32 Slot = 0; Slot < 4; ++Slot)
    {
        UClass* Parent = Definition.Abilities[Slot].Get();
        UBlueprint* Ability = RosterBlueprint(Root + TEXT("Abilities/GA_") + ID + TEXT("_") + Slots[Slot], Parent);
        if (!Ability || !CompileAndSave(Ability)) return false;
        Abilities[Slot] = Ability->GeneratedClass;
    }
    Hero->AbilitySlots.AbilityQ = Abilities[0]; Hero->AbilitySlots.AbilityE = Abilities[1];
    Hero->AbilitySlots.AbilityC = Abilities[2]; Hero->AbilitySlots.AbilityX = Abilities[3];
    APSBaseCharacter* Defaults = Cast<APSBaseCharacter>(Character->GeneratedClass->GetDefaultObject());
    if (!Defaults) return false;
    Defaults->Modify(); Defaults->HeroData = Hero; Defaults->WeaponData = Weapon;
    if (!SaveRosterAsset(Hero) || !SaveRosterAsset(Weapon) || !CompileAndSave(Character)) return false;
    UBlueprint* Mode = RosterBlueprint(Root + TEXT("GM_") + ID + TEXT("Test"), APSGameMode::StaticClass());
    APSGameMode* ModeDefaults = Mode && Mode->GeneratedClass ? Cast<APSGameMode>(Mode->GeneratedClass->GetDefaultObject()) : nullptr;
    if (!ModeDefaults) return false;
    ModeDefaults->Modify(); ModeDefaults->PSCharacterClass = TSoftClassPtr<APawn>(Character->GeneratedClass);
    if (!CompileAndSave(Mode)) return false;
    UE_LOG(LogProjectSCharacter, Display, TEXT("Roster configured: %s, primary %s, four compiled ability Blueprints"), *ID, *Definition.PrimaryName.ToString());
    return true;
}
}
bool ConfigurePSHeroRoster(bool bResetExisting)
{
    UBlueprint* Shared = LoadObject<UBlueprint>(nullptr, TEXT("/Game/ProjectS/Characters/BP_PSBaseCharacter.BP_PSBaseCharacter"));
    if (!Shared || !Shared->GeneratedClass || !Shared->GeneratedClass->IsChildOf(APSBaseCharacter::StaticClass()))
    { UE_LOG(LogProjectSCharacter, Error, TEXT("Roster setup requires the existing shared sample character.")); return false; }
    if (!CreateScreenMaterial()) return false;
    for (const FSRHeroDefinition& Definition : SRHeroRoster::All())
        if (!ConfigureHero(Definition, Shared->GeneratedClass, bResetExisting)) return false;
    const FString Marker = FPaths::ProjectSavedDir() / TEXT("ProjectSSetup/roster-v1.configured");
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Marker), true);
    return FFileHelper::SaveStringToFile(TEXT("12 hero configurations and their Blueprints saved. This is not gameplay/multiplayer acceptance.\n"), *Marker);
}
bool CreatePSVeyraHero()
{
    UBlueprint* Shared = LoadObject<UBlueprint>(nullptr, TEXT("/Game/ProjectS/Characters/BP_PSBaseCharacter.BP_PSBaseCharacter"));
    const FSRHeroDefinition* Definition = SRHeroRoster::Find(TEXT("Veyra"));
    return Shared && Shared->GeneratedClass && Definition && ConfigureHero(*Definition, Shared->GeneratedClass, false);
}
