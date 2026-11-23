#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SRTacticalMapWidget.generated.h"

class APSBaseCharacter;
class APSPlayerState;
class USRMinimapDataAsset;
class USRMinimapWidget;
class UTextBlock;
class UBorder;
struct FGeometry;
struct FPointerEvent;
struct FKeyEvent;

/**
 * Expanded interactive tactical map (Phase 4): embeds the reusable minimap at a
 * larger size with mouse-wheel zoom and drag pan. LMB inside the map converts
 * the click into a world location and requests a server-validated ping.
 * M/Escape close the map (handled by the owning HUD widget, which also owns the
 * input-mode transition).
 */
UCLASS(Blueprintable)
class PSGAMEPLAY_API USRTacticalMapWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    /** Minimap configuration; the HUD passes the combat map's asset. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|TacticalMap")
    TObjectPtr<USRMinimapDataAsset> MinimapData;

    /** Tactical display size in UI units. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|TacticalMap")
    FVector2D DisplaySize = FVector2D(700.f, 525.f);

    /** Binds the local PlayerState (for team pings) and the map configuration. */
    void BindToPlayerState(APSPlayerState* NewState, USRMinimapDataAsset* InMinimapData);
    /** Sets the tracked hero for the local player marker on the tactical map. */
    void SetLocalCharacter(APSBaseCharacter* NewCharacter);

    /** Zoom multiplier applied by mouse wheel (clamped). */
    UFUNCTION(BlueprintCallable, Category="SR|TacticalMap")
    void SetZoom(float NewZoom);

protected:
    virtual void NativeConstruct() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float DeltaSeconds) override;
    virtual void NativeDestruct() override;
    virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
    virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
    virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
    virtual FReply NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

    UPROPERTY(Transient) TObjectPtr<USRMinimapWidget> MapWidget;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> TitleText;
    UPROPERTY(Transient) TObjectPtr<UBorder> MapBackground;

    UPROPERTY(Transient) TWeakObjectPtr<APSPlayerState> BoundPlayerState;

    FVector2D LastDragPosition = FVector2D::ZeroVector;
    bool bDragging = false;
    float Zoom = 1.f;
};
