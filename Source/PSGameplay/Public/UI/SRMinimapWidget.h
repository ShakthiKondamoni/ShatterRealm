#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "Data/SRPingTypes.h"
#include "SRMinimapWidget.generated.h"

class APSBaseCharacter;
class APSPlayerState;
class USRMinimapDataAsset;
class UBorder;
class UCanvasPanel;
class UCanvasPanelSlot;
class UImage;
class UOverlay;
class USizeBox;
class UTexture2D;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSRMinimapReady);

/**
 * Self-contained, reusable minimap (Phase 4): a 220x220 DPI-scaled top-down map
 * built from the level's real geometry texture, with a local-player direction
 * arrow, teammate markers, team pings and enemy reveal markers. North-up with a
 * fixed map orientation; positions update on a configurable interval rather
 * than scanning actors every frame.
 *
 * Also drives the expanded tactical map (larger scale, zoom/pan) by setting
 * MapScale and MapPan on the embedded instance.
 */
UCLASS(Blueprintable)
class PSGAMEPLAY_API USRMinimapWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    /** Binds the map to the locally controlled hero and its PlayerState. */
    void BindToCharacter(APSBaseCharacter* NewCharacter);
    void UnbindFromCharacter();
    /** PlayerState-only binding (tactical map: pings without a tracked hero). */
    void SetPlayerState(APSPlayerState* NewState);
    /** Full binding for the tactical map use case. */
    void BindToPlayerState(APSPlayerState* NewState, USRMinimapDataAsset* InMinimapData);
    /** Sets the tracked hero for the local marker (tactical map use). */
    void SetLocalCharacter(APSBaseCharacter* NewCharacter);

    /** Minimap configuration (bounds/texture). Assigned by the owning HUD. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|Minimap")
    TObjectPtr<USRMinimapDataAsset> MinimapData;

    /** Display size in UI units (default 220x220 for the corner minimap; the
     *  tactical map sets its own larger size on the embedded instance). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|Minimap")
    FVector2D DisplaySize = FVector2D(220.f, 220.f);

    /** Marker position refresh interval (seconds). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|Minimap", meta=(ClampMin="0.02", ClampMax="1.0"))
    float UpdateInterval = 0.1f;

    /** Corner minimap follows the hero; tactical map should set false. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SR|Minimap")
    bool bFollowLocalPlayer = true;

    /** Corner map zoom, in addition to tactical-map zoom. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SR|Minimap", meta=(ClampMin="1.0", ClampMax="6.0"))
    float FollowZoom = 2.5f;

    /** Zoom multiplier for the tactical-map use case (1 = minimap scale). */
    UPROPERTY(BlueprintReadOnly, Category="SR|Minimap")
    float MapScale = 1.f;

    /** Pan offset in UI units (tactical map). */
    UPROPERTY(BlueprintReadOnly, Category="SR|Minimap")
    FVector2D MapPan = FVector2D::ZeroVector;

    /** Broadcast once the widget tree, texture and bounds are all valid. */
    UPROPERTY(BlueprintAssignable, Category="SR|Minimap")
    FSRMinimapReady OnMinimapReady;

    /** Converts a widget-local point (inside this widget) into normalized map UV. */
    FVector2D WidgetLocalToMapUV(const FVector2D& LocalPoint) const;
    /** Normalized map UV into world X/Y (Z resolved by the caller). */
    FVector MapUVToWorldXY(const FVector2D& UV) const;

    /** True when the tree, texture and bounds are all valid. */
    UFUNCTION(BlueprintPure, Category="SR|Minimap")
    bool IsMapReady() const;

protected:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float DeltaSeconds) override;

    void BuildTree();
    FVector2D GetMapCenterUV() const;
    FVector2D WorldToMapPixel(const FVector& WorldLocation) const;
    bool IsPixelOnMap(const FVector2D& Pixel, float Margin = 5.f) const;
    void UpdateMapTransform();
    void RefreshLocalMarker();
    void RefreshTeammateMarkers();
    UFUNCTION() void RefreshPingMarkers(UPARAM(ref) const TArray<FSRPingInfo>& TeamPings);

    UPROPERTY(Transient) TWeakObjectPtr<APSBaseCharacter> BoundCharacter;
    UPROPERTY(Transient) TWeakObjectPtr<APSPlayerState> BoundPlayerState;
    FDelegateHandle TeamPingsHandle;

    UPROPERTY(Transient) TObjectPtr<USizeBox> SizeBoxRoot;
    UPROPERTY(Transient) TObjectPtr<UBorder> MapBorder;
    UPROPERTY(Transient) TObjectPtr<UOverlay> MapOverlay;
    UPROPERTY(Transient) TObjectPtr<UImage> MapImage;
    /** Public for the tactical map title overlay. */
    UPROPERTY(Transient) TObjectPtr<UCanvasPanel> MarkerCanvas;
    UPROPERTY(Transient) TObjectPtr<UImage> LocalPlayerArrow;

    TArray<TObjectPtr<UImage>> TeammateMarkers;
    TArray<TObjectPtr<UImage>> PingMarkers;
    UImage* RevealMarker = nullptr;

    double MarkerRefreshTime = 0.0;
    int32 LastTeammateCount = -1;
    FVector CachedRevealPosition = FVector::ZeroVector;
    bool bHasReveal = false;
    bool bReadyAnnounced = false;
};
