#include "Combat/SREquipmentComponent.h"
#include "Debug/SRDebug.h"
#include "Combat/SRWeaponDataAsset.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Character.h"

USREquipmentComponent::USREquipmentComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    // Equipment is presentation only: never replicated, never blocking.
    SetIsReplicatedByDefault(false);
}

void USREquipmentComponent::EquipWeapon(const USRWeaponDataAsset* WeaponData)
{
    ACharacter* Character = Cast<ACharacter>(GetOwner());
    OwnerMesh = Character ? Character->GetMesh() : nullptr;
    if (!OwnerMesh || !WeaponData)
    {
        SRDebug::Log(SRDebug::ECategory::Combat, TEXT("Equip"), TEXT("Equip skipped: no mesh or weapon data"));
        return;
    }
    ClearPieces();

    // Placeholder gauntlet visuals: engine primitives stand in for signature art.
    // Production meshes belong in LeftGauntletMesh/RightGauntletMesh on a hero asset.
    AttachPiece(WeaponData->FireSocketName, RightGauntletMesh, TEXT("RightGauntlet"));
    const FName LeftHand(TEXT("hand_l"));
    AttachPiece(LeftHand, LeftGauntletMesh, TEXT("LeftGauntlet"));
    bEquipped = AttachedPieces.Num() > 0;
    SRDebug::Log(SRDebug::ECategory::Combat, TEXT("Equip"), FString::Printf(TEXT("Equipped gauntlets from %s"), *GetNameSafe(WeaponData)));
}

void USREquipmentComponent::AttachPiece(const FName& SocketName, UStaticMesh* Mesh, const TCHAR* PieceName)
{
    if (!OwnerMesh) return;
    const bool bHasAttachmentPoint = OwnerMesh->DoesSocketExist(SocketName) || OwnerMesh->GetBoneIndex(SocketName) != INDEX_NONE;
    if (!bHasAttachmentPoint)
    {
        SRDebug::Log(SRDebug::ECategory::Combat, TEXT("Equip"), FString::Printf(TEXT("Attachment point %s missing; skipping %s"), *SocketName.ToString(), PieceName));
        return;
    }

    UStaticMeshComponent* Piece = NewObject<UStaticMeshComponent>(GetOwner(), FName(PieceName));
    Piece->SetStaticMesh(Mesh);
    // Engine primitive placeholders so the pieces are always visible in testing.
    if (!Mesh)
    {
        Piece->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
    }
    Piece->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Piece->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
    Piece->SetRelativeScale3D(FVector(0.15f, 0.15f, 0.25f));
    Piece->SetupAttachment(OwnerMesh, SocketName);
    Piece->SetVisibility(true);
    Piece->RegisterComponent();
    AttachedPieces.Add(Piece);
}

void USREquipmentComponent::ClearPieces()
{
    for (UStaticMeshComponent* Piece : AttachedPieces)
    {
        if (Piece) Piece->DestroyComponent();
    }
    AttachedPieces.Reset();
    bEquipped = false;
}
