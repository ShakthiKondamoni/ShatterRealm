#include "UI/SRTacticalMapWidget.h"
#include "Debug/SRDebug.h"
#include "Data/SRMinimapDataAsset.h"
#include "UI/SRMinimapWidget.h"
#include "Abilities/SRGameplayTags.h"
#include "GAS/PSPlayerState.h"
#include "Character/PSBaseCharacter.h"
#include "Character/SRHeroGameplayComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/CanvasPanelSlot.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

void USRTacticalMapWidget::NativeConstruct()
{
    Super::NativeConstruct();

    USizeBox* SizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("SRTacticalSizeBox"));
    SizeBox->SetWidthOverride(DisplaySize.X);
    SizeBox->SetHeightOverride(DisplaySize.Y);
    WidgetTree->RootWidget = SizeBox;

    UBorder* Border = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SRTacticalBorder"));
    Border->SetBrushColor(FLinearColor(0.f, 0.01f, 0.03f, 0.95f));
    SizeBox->AddChild(Border);

    UOverlay* Content = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("SRTacticalOverlay"));
    Border->SetContent(Content);

    // Embedded minimap (bigger display size, zoom/pan state lives here).
    MapWidget = WidgetTree->ConstructWidget<USRMinimapWidget>(USRMinimapWidget::StaticClass(), TEXT("SRTacticalMinimap"));
    MapWidget->DisplaySize = DisplaySize;
    MapWidget->bFollowLocalPlayer = false;
    Content->AddChildToOverlay(MapWidget);

    TitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SRTacticalTitle"));
    TitleText->SetText(FText::FromString(TEXT("TACTICAL MAP — LMB: ping, Wheel: zoom, Drag: pan, M/Escape: close")));
    TitleText->SetColorAndOpacity(FSlateColor(FLinearColor(0.f, 0.9f, 1.f)));
    if (UOverlaySlot* TitleSlot = Content->AddChildToOverlay(TitleText))
    {
        TitleSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Left);
        TitleSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Top);
        TitleSlot->SetPadding(FMargin(10, 10, 0, 0));
    }

    SRDebug::Message(SRDebug::ECategory::TacticalMap, TEXT("Client"), TEXT("Tactical map widget created"), 0.8f, FColor::Green);
}

void USRTacticalMapWidget::BindToPlayerState(APSPlayerState* NewState, USRMinimapDataAsset* InMinimapData)
{
    BoundPlayerState = NewState;
    if (MapWidget)
    {
        MapWidget->MinimapData = InMinimapData;
        MapWidget->SetPlayerState(NewState);
    }
    SRDebug::Message(SRDebug::ECategory::TacticalMap, TEXT("Client"), TEXT("Tactical map bound"), 0.8f, FColor::Green);
}

void USRTacticalMapWidget::SetLocalCharacter(APSBaseCharacter* NewCharacter)
{
    if (MapWidget) MapWidget->SetLocalCharacter(NewCharacter);
}

void USRTacticalMapWidget::SetZoom(float NewZoom)
{
    Zoom = FMath::Clamp(NewZoom, 1.f, 4.f);
    if (MapWidget) MapWidget->MapScale = Zoom;
    SRDebug::Message(SRDebug::ECategory::TacticalMap, TEXT("Client"),
        FString::Printf(TEXT("Zoom = %.2f"), Zoom), 0.5f, FColor::Cyan);
}

void USRTacticalMapWidget::NativeDestruct()
{
    Super::NativeDestruct();
}
void USRTacticalMapWidget::NativeTick(const FGeometry& MyGeometry, float DeltaSeconds)
{
    Super::NativeTick(MyGeometry, DeltaSeconds);
    const APSBaseCharacter* Character = Cast<APSBaseCharacter>(GetOwningPlayerPawn());
    const USRHeroGameplayComponent* State = Character ? Character->HeroGameplayComponent.Get() : nullptr;
    if (TitleText)
        TitleText->SetText(FText::FromString(State && State->IsAwaitingMapTarget()
            ? FString::Printf(TEXT("SELECT ABILITY LOCATION - %d selection(s) remaining - Escape: cancel"), State->GetMapSelectionsRemaining())
            : TEXT("TACTICAL MAP - LMB: ping, Wheel: zoom, Drag: pan, M/Escape: close")));
}

FReply USRTacticalMapWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    if (InMouseEvent.IsMouseButtonDown(EKeys::RightMouseButton))
    {
        bDragging = true;
        LastDragPosition = InMouseEvent.GetScreenSpacePosition();
        return FReply::Handled();
    }
    if (InMouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton) && MapWidget)
    {
        // Screen -> widget-local (accounting for the minimap child geometry).
        const FGeometry& MapGeometry = MapWidget->TakeWidget()->GetCachedGeometry();
        const FVector2D Local = MapGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
        const FVector2D UV = MapWidget->WidgetLocalToMapUV(Local);
        const FVector WorldXY = MapWidget->MapUVToWorldXY(UV);

        // Floor trace for a valid Z (never assume the world is flat).
        FVector WorldPosition = FVector(WorldXY.X, WorldXY.Y, 500.f);
        FHitResult FloorHit;
        if (GetWorld()->LineTraceSingleByChannel(FloorHit, FVector(WorldXY.X, WorldXY.Y, 2000.f), FVector(WorldXY.X, WorldXY.Y, -1000.f), ECC_Visibility))
        {
            WorldPosition.Z = FloorHit.ImpactPoint.Z + 50.f;
        }
        SRDebug::Message(SRDebug::ECategory::TacticalMap, TEXT("Client"),
            FString::Printf(TEXT("Map click UV=(%.2f, %.2f) world=%s"), UV.X, UV.Y, *WorldPosition.ToString()), 0.5f, FColor::Cyan);

        if (APSBaseCharacter* Character = Cast<APSBaseCharacter>(GetOwningPlayerPawn()))
            if (auto* Gameplay = Character->HeroGameplayComponent.Get(); Gameplay && Gameplay->IsAwaitingMapTarget())
            {
                Gameplay->ServerSubmitMapTarget(WorldPosition);
                return FReply::Handled();
            }
        if (APSPlayerState* State = BoundPlayerState.Get())
        {
            State->RequestPingAtLocation(WorldPosition, SRGameplayTags::Tag_SR_Ping_Location);
        }
        return FReply::Handled();
    }
    return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

FReply USRTacticalMapWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
    {
        bDragging = false;
        return FReply::Handled();
    }
    return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
}

FReply USRTacticalMapWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    if (bDragging && InMouseEvent.IsMouseButtonDown(EKeys::RightMouseButton) && MapWidget)
    {
        const FVector2D Delta = InMouseEvent.GetScreenSpacePosition() - LastDragPosition;
        LastDragPosition = InMouseEvent.GetScreenSpacePosition();
        MapWidget->MapPan += Delta;
        return FReply::Handled();
    }
    return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
}

FReply USRTacticalMapWidget::NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    SetZoom(Zoom + (InMouseEvent.GetWheelDelta() > 0.f ? 0.25f : -0.25f));
    return FReply::Handled();
}
