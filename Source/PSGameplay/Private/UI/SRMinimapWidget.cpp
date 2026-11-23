#include "UI/SRMinimapWidget.h"
#include "Debug/SRDebug.h"
#include "Data/SRMinimapDataAsset.h"
#include "Character/PSBaseCharacter.h"
#include "GAS/PSPlayerState.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/SizeBox.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Abilities/SRGameplayTags.h"

void USRMinimapWidget::NativeConstruct()
{
    Super::NativeConstruct();
    BuildTree();
    SRDebug::Message(SRDebug::ECategory::Minimap, TEXT("Client"), TEXT("Widget Created"), 0.8f, FColor::Green);
}

void USRMinimapWidget::NativeDestruct()
{
    UnbindFromCharacter();
    Super::NativeDestruct();
}

void USRMinimapWidget::BuildTree()
{
    // SizeBox (fixed DPI-scaled size) -> Border -> Overlay -> [MapImage, MarkerCanvas]
    SizeBoxRoot = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("SRMinimapSizeBox"));
    SizeBoxRoot->SetWidthOverride(DisplaySize.X);
    SizeBoxRoot->SetHeightOverride(DisplaySize.Y);
    SizeBoxRoot->SetClipping(EWidgetClipping::ClipToBounds);
    WidgetTree->RootWidget = SizeBoxRoot;

    MapBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SRMinimapBorder"));
    MapBorder->SetBrushColor(FLinearColor(0.02f, 0.03f, 0.06f, 0.9f));
    SizeBoxRoot->AddChild(MapBorder);

    MapOverlay = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("SRMinimapOverlay"));
    MapBorder->SetContent(MapOverlay);

    MapImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("SRMapImage"));
    MapImage->SetVisibility(ESlateVisibility::HitTestInvisible);
    MapOverlay->AddChildToOverlay(MapImage);

    MarkerCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("SRMarkerCanvas"));
    MarkerCanvas->SetVisibility(ESlateVisibility::HitTestInvisible);
    MapOverlay->AddChildToOverlay(MarkerCanvas);

    // Local player direction arrow (rotated to facing yaw).
    LocalPlayerArrow = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("SRLocalArrow"));
    LocalPlayerArrow->SetColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 0.95f));
    LocalPlayerArrow->SetVisibility(ESlateVisibility::HitTestInvisible);
    if (UCanvasPanelSlot* ArrowSlot = MarkerCanvas->AddChildToCanvas(LocalPlayerArrow))
    {
        ArrowSlot->SetAnchors(FAnchors(0.f, 0.f));
        ArrowSlot->SetPosition(FVector2D(DisplaySize.X * 0.5f - 6.f, DisplaySize.Y * 0.5f - 6.f));
        ArrowSlot->SetSize(FVector2D(12, 12));
        ArrowSlot->SetZOrder(20);
    }

    // Wire the map texture from the data asset.
    if (MinimapData && MinimapData->MapTexture)
    {
        MapImage->SetBrushFromTexture(MinimapData->MapTexture, true);
        SRDebug::Log(SRDebug::ECategory::Minimap, TEXT("Client"), TEXT("Map texture assigned"));
    }
    else
    {
        SRDebug::Log(SRDebug::ECategory::Minimap, TEXT("Client"), TEXT("No map texture; grid placeholder background"));
    }
    SRDebug::Message(SRDebug::ECategory::Minimap, TEXT("Client"),
        FString::Printf(TEXT("Bounds X %.0f..%.0f Y %.0f..%.0f"), MinimapData ? MinimapData->MinWorldX : 0.f,
            MinimapData ? MinimapData->MaxWorldX : 0.f, MinimapData ? MinimapData->MinWorldY : 0.f, MinimapData ? MinimapData->MaxWorldY : 0.f),
        0.8f, FColor::Green);
}

void USRMinimapWidget::BindToCharacter(APSBaseCharacter* NewCharacter)
{
    UnbindFromCharacter();
    SetLocalCharacter(NewCharacter);
    APSPlayerState* State = NewCharacter ? NewCharacter->GetPlayerState<APSPlayerState>() : nullptr;
    SetPlayerState(State);
    UpdateMapTransform();
    if (!bReadyAnnounced)
    {
        bReadyAnnounced = true;
        SRDebug::Message(SRDebug::ECategory::Minimap, TEXT("Client"), TEXT("Minimap ready"), 0.8f, FColor::Green);
        OnMinimapReady.Broadcast();
    }
}

void USRMinimapWidget::SetLocalCharacter(APSBaseCharacter* NewCharacter)
{
    BoundCharacter = NewCharacter;
}

void USRMinimapWidget::SetPlayerState(APSPlayerState* NewState)
{
    if (APSPlayerState* Old = BoundPlayerState.Get()) Old->OnTeamPingsChanged.RemoveAll(this);
    BoundPlayerState = NewState;
    if (NewState)
    {
        if (MinimapData && MinimapData->MapTexture && MapImage)
            MapImage->SetBrushFromTexture(MinimapData->MapTexture, true);
        NewState->OnTeamPingsChanged.AddDynamic(this, &USRMinimapWidget::RefreshPingMarkers);
        RefreshPingMarkers(NewState->GetTeamPings());
    }
}

void USRMinimapWidget::BindToPlayerState(APSPlayerState* NewState, USRMinimapDataAsset* InMinimapData)
{
    UnbindFromCharacter();
    MinimapData = InMinimapData;
    SetPlayerState(NewState);
    UpdateMapTransform();
    if (!bReadyAnnounced)
    {
        bReadyAnnounced = true;
        SRDebug::Message(SRDebug::ECategory::Minimap, TEXT("Client"), TEXT("Minimap ready"), 0.8f, FColor::Green);
        OnMinimapReady.Broadcast();
    }
}

void USRMinimapWidget::UnbindFromCharacter()
{
    if (APSPlayerState* State = BoundPlayerState.Get())
    {
        State->OnTeamPingsChanged.RemoveAll(this);
    }
    BoundCharacter.Reset();
    BoundPlayerState.Reset();
}

bool USRMinimapWidget::IsMapReady() const
{
    return MinimapData != nullptr && MarkerCanvas != nullptr;
}

FVector2D USRMinimapWidget::GetMapCenterUV() const
{
    if (bFollowLocalPlayer && BoundCharacter.IsValid() && MinimapData)
        return MinimapData->WorldToNormalized(BoundCharacter->GetActorLocation());
    return FVector2D(0.5f, 0.5f);
}

FVector2D USRMinimapWidget::WorldToMapPixel(const FVector& WorldLocation) const
{
    if (!MinimapData) return FVector2D(-10000.f, -10000.f);
    const FVector2D UV = MinimapData->WorldToNormalized(WorldLocation);
    const float Zoom = bFollowLocalPlayer ? FollowZoom : FMath::Max(MapScale, 1.f);
    return (UV - GetMapCenterUV()) * DisplaySize * Zoom + DisplaySize * 0.5f +
        (bFollowLocalPlayer ? FVector2D::ZeroVector : MapPan);
}

bool USRMinimapWidget::IsPixelOnMap(const FVector2D& Pixel, float Margin) const
{
    return Pixel.X >= Margin && Pixel.Y >= Margin &&
        Pixel.X <= DisplaySize.X - Margin && Pixel.Y <= DisplaySize.Y - Margin;
}

void USRMinimapWidget::UpdateMapTransform()
{
    if (!MapImage || !MinimapData) return;
    const float Zoom = bFollowLocalPlayer ? FMath::Max(1.f, FollowZoom) : FMath::Max(1.f, MapScale);
    const FVector2D Delta = (FVector2D(0.5f, 0.5f) - GetMapCenterUV()) * DisplaySize * Zoom +
        (bFollowLocalPlayer ? FVector2D::ZeroVector : MapPan);
    MapImage->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
    MapImage->SetRenderScale(FVector2D(Zoom, Zoom));
    MapImage->SetRenderTranslation(Delta);
}

FVector2D USRMinimapWidget::WidgetLocalToMapUV(const FVector2D& LocalPoint) const
{
    const float Zoom = bFollowLocalPlayer ? FMath::Max(1.f, FollowZoom) : FMath::Max(1.f, MapScale);
    const FVector2D Offset = bFollowLocalPlayer ? FVector2D::ZeroVector : MapPan;
    const FVector2D UV = (LocalPoint - DisplaySize * 0.5f - Offset) / (DisplaySize * Zoom) + GetMapCenterUV();
    return FVector2D(FMath::Clamp(UV.X, 0.f, 1.f), FMath::Clamp(UV.Y, 0.f, 1.f));
}

FVector USRMinimapWidget::MapUVToWorldXY(const FVector2D& UV) const
{
    if (!MinimapData) return FVector::ZeroVector;
    return MinimapData->NormalizedToWorld(UV);
}

void USRMinimapWidget::NativeTick(const FGeometry& MyGeometry, float DeltaSeconds)
{
    Super::NativeTick(MyGeometry, DeltaSeconds);
    const double Now = FPlatformTime::Seconds();
    if (Now < MarkerRefreshTime) return;
    MarkerRefreshTime = Now + UpdateInterval;

    UpdateMapTransform();
    RefreshLocalMarker();
    RefreshTeammateMarkers();
    if (APSPlayerState* State = BoundPlayerState.Get())
        RefreshPingMarkers(State->GetTeamPings());
}

void USRMinimapWidget::RefreshLocalMarker()
{
    APSBaseCharacter* Character = BoundCharacter.Get();
    if (!Character || !LocalPlayerArrow || !MinimapData) return;

    // Local marker at the tracked world position.
    const FVector2D Pixel = WorldToMapPixel(Character->GetActorLocation());
    LocalPlayerArrow->SetVisibility(IsPixelOnMap(Pixel) ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    if (UCanvasPanelSlot* ArrowSlot = Cast<UCanvasPanelSlot>(LocalPlayerArrow->Slot))
    {
        const FVector2D Half(6.f, 6.f);
        ArrowSlot->SetPosition(Pixel - Half);
        // North-up map: rotate the arrow to the controller facing yaw.
        LocalPlayerArrow->SetRenderTransformAngle(-Character->GetControlRotation().Yaw);
    }
    // Position is refreshed at a fixed interval, avoiding per-frame debug spam.
}

void USRMinimapWidget::RefreshTeammateMarkers()
{
    APSPlayerState* LocalState = BoundPlayerState.Get();
    if (!LocalState || !MinimapData) return;

    // Teammate set from replicated PlayerStates on the SAME team only.
    TArray<APSPlayerState*> Teammates;
    for (TActorIterator<APSPlayerState> It(GetWorld()); It; ++It)
    {
        APSPlayerState* Other = *It;
        if (!Other || Other == LocalState || Other->TeamId != LocalState->TeamId) continue;
        Teammates.Add(Other);
    }

    // Rebuild the marker pool when the teammate count changes (spawn/leave/team change).
    if (Teammates.Num() != LastTeammateCount)
    {
        while (TeammateMarkers.Num() < Teammates.Num())
        {
            UImage* Marker = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), NAME_None);
            Marker->SetColorAndOpacity(FLinearColor(0.2f, 0.7f, 1.f, 0.95f));
            Marker->SetVisibility(ESlateVisibility::HitTestInvisible);
            if (UCanvasPanelSlot* MarkerSlot = MarkerCanvas->AddChildToCanvas(Marker))
            {
                MarkerSlot->SetAnchors(FAnchors(0.f, 0.f));
                MarkerSlot->SetSize(FVector2D(8, 8));
                MarkerSlot->SetZOrder(10);
            }
            TeammateMarkers.Add(Marker);
        }
        for (int32 Index = Teammates.Num(); Index < TeammateMarkers.Num(); ++Index)
        {
            if (IsValid(TeammateMarkers[Index])) TeammateMarkers[Index]->SetVisibility(ESlateVisibility::Collapsed);
        }
        SRDebug::Message(SRDebug::ECategory::TeamMarkers, TEXT("Client"),
            FString::Printf(TEXT("Teammate markers: %d"), Teammates.Num()), 0.8f, FColor::Cyan);
        LastTeammateCount = Teammates.Num();
    }

    for (int32 Index = 0; Index < Teammates.Num() && Index < TeammateMarkers.Num(); ++Index)
    {
        UImage* Marker = TeammateMarkers[Index];
        APSPlayerState* Teammate = Teammates[Index];
        const APawn* TeammatePawn = Teammate ? Teammate->GetPawn() : nullptr;
        if (!TeammatePawn || !Marker) { Marker->SetVisibility(ESlateVisibility::Collapsed); continue; }
        const FVector2D Pixel = WorldToMapPixel(TeammatePawn->GetActorLocation());
        Marker->SetVisibility(IsPixelOnMap(Pixel) ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
        if (UCanvasPanelSlot* MarkerSlot = Cast<UCanvasPanelSlot>(Marker->Slot))
        {
            MarkerSlot->SetPosition(Pixel - FVector2D(4.f, 4.f));
        }

    }
}

void USRMinimapWidget::RefreshPingMarkers(const TArray<FSRPingInfo>& TeamPings)
{
    // Ping + reveal markers from the delivered (team-private) list.
    if (!MarkerCanvas || !MinimapData) return;

    const int32 Needed = TeamPings.Num();
    while (PingMarkers.Num() < Needed)
    {
        UImage* Marker = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), NAME_None);
        Marker->SetVisibility(ESlateVisibility::HitTestInvisible);
        if (UCanvasPanelSlot* MarkerSlot = MarkerCanvas->AddChildToCanvas(Marker))
        {
            MarkerSlot->SetAnchors(FAnchors(0.f, 0.f));
            MarkerSlot->SetSize(FVector2D(7, 7));
            MarkerSlot->SetZOrder(12);
        }
        PingMarkers.Add(Marker);
    }
    for (int32 Index = 0; Index < PingMarkers.Num(); ++Index)
    {
        UImage* Marker = PingMarkers[Index];
        if (Index < Needed)
        {
            const FSRPingInfo& Ping = TeamPings[Index];
            const FVector2D Pixel = WorldToMapPixel(Ping.Location);
            Marker->SetVisibility(IsPixelOnMap(Pixel) ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
            Marker->SetColorAndOpacity(Ping.Category == SRGameplayTags::Tag_SR_Ping_Danger ? FLinearColor::Red
                : Ping.Category == SRGameplayTags::Tag_SR_Ping_Reveal ? FLinearColor(1.f, 0.25f, 0.1f, 1.f) : FLinearColor::Yellow);
            if (UCanvasPanelSlot* MarkerSlot = Cast<UCanvasPanelSlot>(Marker->Slot))
            {
                MarkerSlot->SetPosition(Pixel - FVector2D(3.5f, 3.5f));
            }

        }
        else
        {
            Marker->SetVisibility(ESlateVisibility::Collapsed);
            SRDebug::Log(SRDebug::ECategory::Ping, TEXT("Client"), TEXT("Marker expired/removed"));
        }
    }
}
