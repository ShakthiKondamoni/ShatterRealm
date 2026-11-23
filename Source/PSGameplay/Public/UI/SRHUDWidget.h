#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Input/Reply.h"
#include "GameplayTagContainer.h"
#include "SRHUDWidget.generated.h"

class APSBaseCharacter;
class APSPlayerState;
class UPSCombatComponent;
class USRMinimapDataAsset;
class USRMinimapWidget;
class USRTacticalMapWidget;
class UCanvasPanel;
class UCanvasPanelSlot;
class UProgressBar;
class UTextBlock;
class UImage;
class UBorder;
class USizeBox;
struct FGeometry;
struct FPointerEvent;
struct FKeyEvent;
struct FOnAttributeChangeData;

/**
 * Complete combat HUD (Phase 4): hero/weapon identity, health + shield bars with
 * values, four ability slot widgets driven by the real GAS grants (name, key,
 * cooldown fill, ready/blocked), ultimate charge on X, primary charges +
 * recharge, status effects, compass, crosshair/hit marker, an embedded
 * minimap and an interactive tactical map (M) with zoom/pan and click-to-ping.
 * All data is event-driven from GAS/Combat Component delegates; countdowns
 * refresh on a UI interval, never per-frame.
 */
UCLASS(Blueprintable)
class PSGAMEPLAY_API USRHUDWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    /** Binds the widget to the locally controlled hero; safe on possession changes. */
    void BindToCharacter(APSBaseCharacter* NewCharacter);
    void UnbindFromCharacter();

    /** Minimap configuration; the combat map ships one under /Game/ProjectS/UI. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SR|HUD")
    TObjectPtr<USRMinimapDataAsset> MinimapData;

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeDestruct() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float DeltaSeconds) override;
    virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
        const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
        const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
    virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

    // Tree -----------------------------------------------------------------
    void BuildHudTree();
    void BuildSlots();
    void BuildTacticalMap();
    void BuildAll();
    UTextBlock* MakeText(UPanelWidget* Parent, const FText& Text, const FVector2D& Position, const FVector2D& Size, int32 FontSize, FLinearColor Color);
    UProgressBar* MakeBar(UPanelWidget* Parent, const FVector2D& Position, const FVector2D& Size, FLinearColor FillColor);
    UImage* MakeMarker(UPanelWidget* Parent, FLinearColor Color, const FVector2D& Size, bool bVisible = true);

    UPROPERTY(Transient) TObjectPtr<UCanvasPanel> RootCanvas;
    UPROPERTY(Transient) TObjectPtr<UCanvasPanel> MapPanel;
    UPROPERTY(Transient) TObjectPtr<USRMinimapWidget> MinimapWidget;
    UPROPERTY(Transient) TObjectPtr<USRTacticalMapWidget> TacticalMapWidget;

    UPROPERTY(Transient) TObjectPtr<UTextBlock> HeroNameText;
    UPROPERTY(Transient) TObjectPtr<UProgressBar> HealthBar;
    UPROPERTY(Transient) TObjectPtr<UProgressBar> ShieldBar;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> HealthValuesText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> StatusEffectsText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> ChargesText;
    UPROPERTY(Transient) TObjectPtr<UProgressBar> RechargeBar;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> RechargeText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> WeaponNameText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> CompassText;
    // Screen-centered compass ticks and team-ping bearings. Constructed at runtime.
    UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> CompassTicks;
    UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> CompassPingLabels;
    // On-screen world-anchored team ping markers; derived from delivered ping data.
    UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> WorldPingLabels;
    void RefreshWorldPings(APSBaseCharacter* Character);

    double NextCompassRefresh = 0.0;
    void RefreshCompass(APSBaseCharacter* Character);
    UPROPERTY(Transient) TObjectPtr<UTextBlock> AbilitySlotsText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> BuffText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> DebugIndicatorText;
    UPROPERTY(Transient) TObjectPtr<UImage> HitMarker;
    UPROPERTY(Transient) TObjectPtr<UImage> CrosshairCenter;
    UPROPERTY(Transient) TObjectPtr<UImage> MapImage;
    UPROPERTY(Transient) TObjectPtr<UImage> LocalPlayerMarker;
    TArray<TObjectPtr<UImage>> MapMarkerPool;

    // Ability slot widgets (Q/E/C/X): background, key, name, cooldown fill, status.
    struct FSRSlotWidgets
    {
        TObjectPtr<UBorder> Background = nullptr;
        TObjectPtr<UTextBlock> KeyText = nullptr;
        TObjectPtr<UTextBlock> NameText = nullptr;
        TObjectPtr<UProgressBar> CooldownFill = nullptr;
        TObjectPtr<UTextBlock> StatusText = nullptr;
    };
    FSRSlotWidgets SlotWidgets[4];
    FGameplayTag SlotTags[4];
    bool bSlotsBuilt = false;

    // Data -----------------------------------------------------------------
    void RefreshAllValues();
    void RefreshSlotStates();
    void RefreshStatusEffects();
    void HandleAttributeChanged(const FOnAttributeChangeData& Data);
    void SetTacticalMapVisible(bool bOpen);
    FText FormatSecondsText(float Seconds) const;
    FText FacingToCompass(float Yaw) const;

    UFUNCTION() void HandleChargesChanged(int32 NewCharges, int32 MaxCharges);
    UFUNCTION() void HandleRechargeChanged(bool bIsRecharging, float RechargeStartTime);
    UFUNCTION() void HandleCombatStateChanged();
    UFUNCTION() void HandleHitConfirmed(int32 ShotId, AActor* HitActor, float Distance, float DamageApplied);

    UPROPERTY(Transient) TWeakObjectPtr<APSBaseCharacter> BoundCharacter;
    UPROPERTY(Transient) TWeakObjectPtr<APSPlayerState> BoundPlayerState;
    FDelegateHandle HealthHandle, ShieldHandle, MaxHealthHandle, MaxShieldHandle, UltimateChargeHandle, MaxUltimateChargeHandle;
    double HitMarkerHideTime = 0.0;
    double SlotRefreshTime = 0.0;
    bool bTacticalMapOpen = false;
};
