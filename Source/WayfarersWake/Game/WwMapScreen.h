#pragma once

#include "CoreMinimal.h"
#include "WorldGen/WwMap.h"
#include "Styling/SlateBrush.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SLeafWidget.h"
#include <memory>

class UTexture2D;

/// The world map (M), after Thornwake's: the mouse wheel zooms at the cursor, the left button drags the map, the right
/// button puts you on the ground at that very spot. An arrow shows where you are and which way you look; a ring marks
/// where the world first put you. Two looks (P): the plain map, with terrain or biome colours (B), and an old chart on
/// parchment. Zoomed in, the picture is drawn from the real terrain (WwMap), so the spot under the cursor is the
/// ground you will stand on.
///
/// The picture is drawn off the game thread; while a new one is on its way the last one is shown moved and stretched
/// to the present view, so dragging and zooming answer at once.
class SWwMapScreen : public SLeafWidget
{
public:
	DECLARE_DELEGATE_TwoParams(FOnTeleport, double, double);
	/// Where the player is (metres) and which way they look (degrees; 0 east, 90 south). False if there is no player.
	DECLARE_DELEGATE_RetVal_ThreeParams(bool, FOnPlayer, double&, double&, double&);

	SLATE_BEGIN_ARGS(SWwMapScreen) {}
		SLATE_EVENT(FSimpleDelegate, OnClose)
		SLATE_EVENT(FOnTeleport, OnTeleport)
		SLATE_EVENT(FOnPlayer, OnPlayer)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);
	/// Shows this world, centred on the player if it is a new world or the map was last closed elsewhere.
	void Open(std::shared_ptr<ww::FTerrainSampler> InSampler, double HomeX, double HomeZ);
	/// The view: a square `Span` metres across centred here.
	void SetView(double X, double Z, double InSpan);
	/// As a right click on that place.
	void TeleportTo(double X, double Z) { OnTeleport.ExecuteIfBound(X, Z); }
	/// A line for the log: what is shown.
	FString Describe() const;

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1280, 720); }
	virtual void Tick(const FGeometry& Geometry, const double CurrentTime, const float DeltaTime) override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bEnabled) const override;
	virtual FReply OnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;

private:
	/// The square the map is drawn in (local units): its corner and side.
	void MapRect(const FGeometry& Geometry, FVector2f& Corner, float& Side) const;
	FVector2f ToScreen(const FGeometry& Geometry, double X, double Z) const;
	void ToWorld(const FGeometry& Geometry, const FVector2f& Local, double& X, double& Z) const;
	void Zoom(double Factor, double AtX, double AtZ);
	void Clamp();
	ww::EMapStyle WantedStyle() const;

	FSimpleDelegate OnClose;
	FOnTeleport OnTeleport;
	FOnPlayer OnPlayer;
	std::shared_ptr<ww::FTerrainSampler> Sampler;
	double Cx = 0, Cz = 0, Span = 64000, WorldSize = 64000, OriginX = 0, OriginZ = 0, HomeX = 0, HomeZ = 0;
	bool bDragging = false;
	FVector2f Mouse = FVector2f(-1, -1);

	// The picture now shown (and the square of the world it is of), and the one being drawn.
	TStrongObjectPtr<UTexture2D> Texture;
	FSlateBrush Brush;
	double ShownX = 0, ShownZ = 0, ShownSpan = 0;
	ww::EMapStyle ShownStyle = ww::EMapStyle::Terrain;
	bool bShown = false, bShownDetailed = false;
	TFuture<ww::FMapImage> Drawing;
	int32 Drawn = 0;
};
