#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SREquipmentComponent.generated.h"

class USkeletalMeshComponent;
class UStaticMesh;
class USRWeaponDataAsset;

/**
 * Minimal reusable equipment component: attaches the signature weapon visual
 * pieces (gauntlets) to the character's hand sockets so the fire origin and
 * equipped presentation agree. Placeholder meshes (engine primitives) stand in
 * for real art; attachment, cleanup and owner-ignore collision rules are the
 * production-relevant parts. Never blocks the owner's traces or movement.
 */
UCLASS(ClassGroup=(ProjectS), meta=(BlueprintSpawnableComponent))
class PSGAMEPLAY_API USREquipmentComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    USREquipmentComponent();

    /** Attaches the configured equipment; safe to call again (re-attaches idempotently). */
    void EquipWeapon(const USRWeaponDataAsset* WeaponData);

    /** True when the equipment pieces are currently attached. */
    bool IsEquipped() const { return bEquipped; }

    /** Placeholder mesh overrides for a hero's hand equipment (null = engine box). */
    void SetRightHandPlaceholder(UStaticMesh* Mesh) { RightGauntletMesh = Mesh; }
    void SetLeftHandPlaceholder(UStaticMesh* Mesh) { LeftGauntletMesh = Mesh; }

protected:
    /** Left gauntlet visual (placeholder when empty). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|Equipment")
    TObjectPtr<UStaticMesh> LeftGauntletMesh;

    /** Right gauntlet visual (placeholder when empty). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|Equipment")
    TObjectPtr<UStaticMesh> RightGauntletMesh;

private:
    void AttachPiece(const FName& SocketName, UStaticMesh* Mesh, const TCHAR* PieceName);
    void ClearPieces();

    UPROPERTY(Transient) TObjectPtr<USkeletalMeshComponent> OwnerMesh;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> AttachedPieces;
    bool bEquipped = false;
};
