#include "Game/WwMapScreen.h"
#include "Game/WwSettings.h"
#include "Async/Async.h"
#include "Engine/Texture2D.h"
#include "InputCoreTypes.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "TextureResource.h"

namespace
{
const int32 MsPixels = 1024;
const FLinearColor MsInk(0.23f, 0.15f, 0.09f), MsPaper(0.9f, 0.81f, 0.62f), MsLight(1.f, 0.95f, 0.82f);
FSlateFontInfo MsFont(int32 Size, bool bBold = false) { return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size); }
const FSlateBrush* MsWhite() { return FCoreStyle::Get().GetBrush("WhiteBrush"); }

void MsBox(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2f& At, const FVector2f& Size, const FLinearColor& Colour, const FSlateBrush* Brush = nullptr)
{
	if (Size.X <= 0 || Size.Y <= 0) return;
	FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(Size, FSlateLayoutTransform(At)), Brush ? Brush : MsWhite(), ESlateDrawEffect::None, Colour);
}
void MsLine(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const TArray<FVector2f>& Points, const FLinearColor& Colour, float Thickness)
{
	FSlateDrawElement::MakeLines(Out, Layer, G.ToPaintGeometry(), Points, ESlateDrawEffect::None, Colour, true, Thickness);
}
void MsText(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2f& At, const FString& Text, const FSlateFontInfo& Font, const FLinearColor& Colour, bool bShadow = true)
{
	if (bShadow) FSlateDrawElement::MakeText(Out, Layer, G.ToPaintGeometry(FVector2f(1200, 30), FSlateLayoutTransform(At + FVector2f(1.5f, 1.5f))), Text, Font, ESlateDrawEffect::None, FLinearColor(0, 0, 0, 0.8f));
	FSlateDrawElement::MakeText(Out, Layer + 1, G.ToPaintGeometry(FVector2f(1200, 30), FSlateLayoutTransform(At)), Text, Font, ESlateDrawEffect::None, Colour);
}
/// A round length for the scale bar: 1, 2 or 5 times a power of ten, at most this long.
double MsRound(double Most)
{
	const double Power = FMath::Pow(10.0, FMath::FloorToDouble(FMath::LogX(10.0, Most)));
	return Most >= 5 * Power ? 5 * Power : Most >= 2 * Power ? 2 * Power : Power;
}
}

void SWwMapScreen::Construct(const FArguments& Args)
{
	OnClose = Args._OnClose;
	OnTeleport = Args._OnTeleport;
	OnPlayer = Args._OnPlayer;
	SetCanTick(true);
}

ww::EMapStyle SWwMapScreen::WantedStyle() const
{
	const FWwSettings& Set = FWwSettings::Get();
	return Set.Int(TEXT("map.style")) == 1 ? ww::EMapStyle::Parchment : Set.On(TEXT("map.biomes")) ? ww::EMapStyle::Biomes : ww::EMapStyle::Terrain;
}

void SWwMapScreen::Open(std::shared_ptr<ww::FTerrainSampler> InSampler, double InHomeX, double InHomeZ)
{
	HomeX = InHomeX; HomeZ = InHomeZ;
	if (InSampler != Sampler)
	{
		// A new world: the whole of it.
		Sampler = InSampler;
		bShown = false;
		if (Sampler)
		{
			WorldSize = Sampler->Plan->Profile().WorldSize;
			OriginX = Sampler->Plan->Manifest.OriginX; OriginZ = Sampler->Plan->Manifest.OriginZ;
			Cx = OriginX; Cz = OriginZ; Span = WorldSize;
		}
	}
	// (opened zoomed in on somewhere the player has since left: back to the player)
	double X, Z, Yaw;
	if (Span < WorldSize * 0.9 && OnPlayer.IsBound() && OnPlayer.Execute(X, Z, Yaw) && (FMath::Abs(X - Cx) > Span * 0.5 || FMath::Abs(Z - Cz) > Span * 0.5)) { Cx = X; Cz = Z; Clamp(); }
	bDragging = false;
}

void SWwMapScreen::SetView(double X, double Z, double InSpan)
{
	Cx = X; Cz = Z; Span = InSpan;
	Clamp();
}

void SWwMapScreen::Clamp()
{
	Span = FMath::Clamp(Span, 400.0, WorldSize * 1.1);
	Cx = FMath::Clamp(Cx, OriginX - WorldSize * 0.5, OriginX + WorldSize * 0.5);
	Cz = FMath::Clamp(Cz, OriginZ - WorldSize * 0.5, OriginZ + WorldSize * 0.5);
}

void SWwMapScreen::Zoom(double Factor, double AtX, double AtZ)
{
	const double Old = Span;
	Span = FMath::Clamp(Span * Factor, 400.0, WorldSize * 1.1);
	// (the place under the cursor stays under the cursor)
	Cx = AtX + (Cx - AtX) * Span / Old; Cz = AtZ + (Cz - AtZ) * Span / Old;
	Clamp();
}

FString SWwMapScreen::Describe() const
{
	return FString::Printf(TEXT("centre %.0f, %.0f m, %.2f km across, %s%s, %d pictures drawn"), Cx, Cz, Span / 1000,
		ShownStyle == ww::EMapStyle::Parchment ? TEXT("parchment") : ShownStyle == ww::EMapStyle::Biomes ? TEXT("biomes") : TEXT("terrain"), bShownDetailed ? TEXT(", from the real terrain") : TEXT(", from the plan"), Drawn);
}

void SWwMapScreen::Tick(const FGeometry& Geometry, const double CurrentTime, const float DeltaTime)
{
	if (!Sampler) return;
	// A finished picture goes on screen.
	if (Drawing.IsValid() && Drawing.IsReady())
	{
		const ww::FMapImage Image = Drawing.Get();
		Drawing = TFuture<ww::FMapImage>();
		if (Image.Pixels == MsPixels)
		{
			if (!Texture)
			{
				Texture = TStrongObjectPtr<UTexture2D>(UTexture2D::CreateTransient(MsPixels, MsPixels, PF_B8G8R8A8));
				Texture->SRGB = true;
				Texture->Filter = TF_Bilinear;
				Texture->AddressX = TA_Clamp; Texture->AddressY = TA_Clamp;
				Brush.SetResourceObject(Texture.Get());
				Brush.ImageSize = FVector2D(MsPixels, MsPixels);
				Brush.DrawAs = ESlateBrushDrawType::Image;
			}
			void* Data = Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
			FMemory::Memcpy(Data, Image.Bgra.data(), Image.Bgra.size());
			Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
			Texture->UpdateResource();
			ShownX = Image.CentreX; ShownZ = Image.CentreZ; ShownSpan = Image.Span; ShownStyle = Image.Style; bShownDetailed = Image.bDetailed;
			bShown = true;
			Drawn++;
		}
	}
	// A new one is wanted when the view or the look has changed.
	const ww::EMapStyle Style = WantedStyle();
	const bool bChanged = !bShown || Style != ShownStyle || FMath::Abs(Span - ShownSpan) > Span * 0.002 || FMath::Abs(Cx - ShownX) > Span * 0.002 || FMath::Abs(Cz - ShownZ) > Span * 0.002;
	if (bChanged && !Drawing.IsValid())
	{
		Drawing = Async(EAsyncExecution::Thread, [World = Sampler, Style, X = Cx, Z = Cz, Across = Span]() { return ww::FWorldMap::Render(*World, MsPixels, Style, X, Z, Across); });
	}
}

void SWwMapScreen::MapRect(const FGeometry& Geometry, FVector2f& Corner, float& Side) const
{
	const FVector2f Size = Geometry.GetLocalSize();
	Side = FMath::Max(100.f, FMath::Min(Size.X - 60.f, Size.Y - 110.f));
	Corner = FVector2f((Size.X - Side) * 0.5f, 78.f + (Size.Y - 110.f - Side) * 0.5f);
}

FVector2f SWwMapScreen::ToScreen(const FGeometry& Geometry, double X, double Z) const
{
	FVector2f Corner; float Side;
	MapRect(Geometry, Corner, Side);
	return Corner + FVector2f((float)((X - (Cx - Span * 0.5)) / Span), (float)((Z - (Cz - Span * 0.5)) / Span)) * Side;
}

void SWwMapScreen::ToWorld(const FGeometry& Geometry, const FVector2f& Local, double& X, double& Z) const
{
	FVector2f Corner; float Side;
	MapRect(Geometry, Corner, Side);
	X = Cx - Span * 0.5 + (Local.X - Corner.X) / Side * Span;
	Z = Cz - Span * 0.5 + (Local.Y - Corner.Y) / Side * Span;
}

int32 SWwMapScreen::OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Culling, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bEnabled) const
{
	const FVector2f Size = G.GetLocalSize();
	FVector2f Corner; float Side;
	MapRect(G, Corner, Side);
	const bool bParchment = WantedStyle() == ww::EMapStyle::Parchment;
	const FLinearColor Ink = bParchment ? MsInk : FLinearColor(0.02f, 0.03f, 0.04f), Mark = bParchment ? MsInk : MsLight;

	MsBox(Out, Layer, G, FVector2f::ZeroVector, Size, FLinearColor(0, 0, 0, 0.8f));
	// The sheet, and on it the last picture drawn, moved and stretched to the present view.
	MsBox(Out, Layer + 1, G, Corner, FVector2f(Side, Side), bParchment ? MsPaper : FLinearColor(0.05f, 0.07f, 0.1f));
	Out.PushClip(FSlateClippingZone(G.MakeChild(FVector2f(Side, Side), FSlateLayoutTransform(Corner))));
	if (bShown && Texture)
	{
		const FVector2f At = ToScreen(G, ShownX - ShownSpan * 0.5, ShownZ - ShownSpan * 0.5);
		const float Across = (float)(ShownSpan / Span) * Side;
		MsBox(Out, Layer + 2, G, At, FVector2f(Across, Across), FLinearColor::White, &Brush);
	}
	if (bParchment)
	{
		// Old paper darkens toward its edges.
		const float Strip = Side * 0.012f;
		for (int32 s = 0; s < 8; s++)
		{
			const FLinearColor Dark(0.3f, 0.2f, 0.1f, 0.3f * FMath::Square(1.f - s / 8.f));
			const float In = s * Strip, Long = Side - 2 * In;
			MsBox(Out, Layer + 3, G, Corner + FVector2f(In, In), FVector2f(Long, Strip), Dark);
			MsBox(Out, Layer + 3, G, Corner + FVector2f(In, Side - In - Strip), FVector2f(Long, Strip), Dark);
			MsBox(Out, Layer + 3, G, Corner + FVector2f(In, In + Strip), FVector2f(Strip, Long - 2 * Strip), Dark);
			MsBox(Out, Layer + 3, G, Corner + FVector2f(Side - In - Strip, In + Strip), FVector2f(Strip, Long - 2 * Strip), Dark);
		}
	}

	// Where the world first put you: a ring.
	{
		const FVector2f At = ToScreen(G, HomeX, HomeZ);
		TArray<FVector2f> Ring;
		for (int32 i = 0; i <= 20; i++) Ring.Add(At + FVector2f(FMath::Cos(i * UE_TWO_PI / 20), FMath::Sin(i * UE_TWO_PI / 20)) * 7.f);
		MsLine(Out, Layer + 5, G, Ring, FLinearColor(0, 0, 0, 0.7f), 4.f);
		MsLine(Out, Layer + 6, G, Ring, bParchment ? FLinearColor(0.55f, 0.12f, 0.08f) : FLinearColor(1.f, 0.85f, 0.3f), 2.f);
	}
	// You: an arrow with a notch at the back, so the way it points is plain at a glance (as Thornwake's).
	double Px, Pz, Yaw;
	if (OnPlayer.IsBound() && OnPlayer.Execute(Px, Pz, Yaw))
	{
		const FVector2f At = ToScreen(G, Px, Pz);
		const float A = FMath::DegreesToRadians((float)Yaw);
		const FVector2f Ahead(FMath::Cos(A), FMath::Sin(A)), Right(-Ahead.Y, Ahead.X);
		const FVector2f Tip = At + Ahead * 15.f, BackL = At - Ahead * 11.f - Right * 10.f, BackR = At - Ahead * 11.f + Right * 10.f, Notch = At - Ahead * 5.f;
		const FLinearColor Fill = bParchment ? FLinearColor(0.6f, 0.1f, 0.06f) : FLinearColor(1.f, 0.25f, 0.2f);
		// (filled with a fan of strokes from the tip)
		for (int32 i = 0; i <= 10; i++)
		{
			const float T = i / 10.f;
			MsLine(Out, Layer + 7, G, { Tip, FMath::Lerp(BackL, Notch, T) }, Fill, 2.5f);
			MsLine(Out, Layer + 7, G, { Tip, FMath::Lerp(BackR, Notch, T) }, Fill, 2.5f);
		}
		MsLine(Out, Layer + 8, G, { Tip, BackR, Notch, BackL, Tip }, FLinearColor::Black, 2.f);
	}
	Out.PopClip();

	// The frame: one line, or on the parchment a double line in ink.
	auto Frame = [&](float Out0, float Thickness)
	{
		const FVector2f A = Corner - FVector2f(Out0, Out0), B = Corner + FVector2f(Side + Out0, Side + Out0);
		MsLine(Out, Layer + 9, G, { A, FVector2f(B.X, A.Y), B, FVector2f(A.X, B.Y), A }, Ink, Thickness);
	};
	Frame(0, bParchment ? 3.f : 2.f);
	if (bParchment) Frame(-7, 1.f);

	// North: a compass needle in the top right corner. (North is up: the world's Z runs south, down the sheet.)
	{
		const FVector2f C = Corner + FVector2f(Side - 44.f, 52.f);
		const float R = 26.f;
		if (bParchment)
		{
			TArray<FVector2f> Ring;
			for (int32 i = 0; i <= 32; i++) Ring.Add(C + FVector2f(FMath::Cos(i * UE_TWO_PI / 32), FMath::Sin(i * UE_TWO_PI / 32)) * R);
			MsLine(Out, Layer + 9, G, Ring, Mark, 1.f);
			for (int32 i = 0; i < 8; i++)
			{
				const FVector2f D(FMath::Cos(i * UE_TWO_PI / 8), FMath::Sin(i * UE_TWO_PI / 8));
				MsLine(Out, Layer + 9, G, { C + D * (i % 2 ? R * 0.55f : R * 0.25f), C + D * (R + (i % 2 ? 0.f : 5.f)) }, Mark, 1.f);
			}
		}
		for (int32 i = 0; i <= 6; i++) MsLine(Out, Layer + 9, G, { C + FVector2f(0, -R), C + FVector2f(-6.f + i * 2.f, 0) }, bParchment ? MsInk : FLinearColor(1.f, 0.3f, 0.25f), 2.f);
		for (int32 i = 0; i <= 6; i++) MsLine(Out, Layer + 9, G, { C + FVector2f(0, R), C + FVector2f(-6.f + i * 2.f, 0) }, bParchment ? FLinearColor(0.5f, 0.4f, 0.28f) : FLinearColor(0.8f, 0.8f, 0.8f), 2.f);
		MsText(Out, Layer + 10, G, C + FVector2f(-5.f, -R - 22.f), TEXT("N"), MsFont(12, true), Mark, !bParchment);
	}
	// The scale: a bar of a round length.
	{
		const double Length = MsRound(Span * 0.25);
		const float Pixels = (float)(Length / Span) * Side;
		const FVector2f A = Corner + FVector2f(22.f, Side - 26.f);
		MsLine(Out, Layer + 9, G, { A + FVector2f(0, -6), A, A + FVector2f(Pixels, 0), A + FVector2f(Pixels, -6) }, Mark, 2.f);
		MsLine(Out, Layer + 9, G, { A + FVector2f(Pixels * 0.5f, 0), A + FVector2f(Pixels * 0.5f, -4) }, Mark, 2.f);
		MsText(Out, Layer + 10, G, A + FVector2f(0, -28.f), Length >= 1000 ? FString::Printf(TEXT("%.0f km"), Length / 1000) : FString::Printf(TEXT("%.0f m"), Length), MsFont(11, true), Mark, !bParchment);
	}

	// Words above the sheet: what this is, the place under the cursor, the keys.
	const TCHAR* Look = bParchment ? TEXT("old chart") : WantedStyle() == ww::EMapStyle::Biomes ? TEXT("biomes") : TEXT("terrain");
	MsText(Out, Layer + 10, G, FVector2f(Corner.X, 14.f), FString::Printf(TEXT("WORLD MAP  ·  %s  ·  %s across"), Look, Span >= 10000 ? *FString::Printf(TEXT("%.0f km"), Span / 1000) : *FString::Printf(TEXT("%.1f km"), Span / 1000)), MsFont(15, true), MsLight);
	FString Under = Drawing.IsValid() ? TEXT("drawing...") : FString();
	if (Mouse.X >= Corner.X && Mouse.Y >= Corner.Y && Mouse.X <= Corner.X + Side && Mouse.Y <= Corner.Y + Side)
	{
		double X, Z;
		ToWorld(G, Mouse, X, Z);
		Under = FString::Printf(TEXT("Under the cursor: %.2f km east, %.2f km south%s"), X / 1000, Z / 1000, Drawing.IsValid() ? TEXT("  ·  drawing...") : TEXT(""));
	}
	MsText(Out, Layer + 10, G, FVector2f(Corner.X, 38.f), Under, MsFont(11), MsLight);
	MsText(Out, Layer + 10, G, FVector2f(Corner.X, 57.f), TEXT("Right-click: go there  ·  wheel: zoom  ·  left-drag: move  ·  C: centre on me  ·  P: plain or old chart  ·  B: biomes  ·  M or Esc: close"), MsFont(10), FLinearColor(0.72f, 0.68f, 0.58f));
	return Layer + 11;
}

FReply SWwMapScreen::OnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event)
{
	double X, Z;
	ToWorld(Geometry, FVector2f(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition())), X, Z);
	Zoom(FMath::Pow(0.75, (double)Event.GetWheelDelta()), X, Z);
	return FReply::Handled();
}

FReply SWwMapScreen::OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	const FVector2f Local(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()));
	FVector2f Corner; float Side;
	MapRect(Geometry, Corner, Side);
	const bool bOnSheet = Local.X >= Corner.X && Local.Y >= Corner.Y && Local.X <= Corner.X + Side && Local.Y <= Corner.Y + Side;
	if (Event.GetEffectingButton() == EKeys::LeftMouseButton) { bDragging = true; return FReply::Handled().CaptureMouse(SharedThis(this)); }
	if (Event.GetEffectingButton() == EKeys::RightMouseButton && bOnSheet)
	{
		double X, Z;
		ToWorld(Geometry, Local, X, Z);
		if (FMath::Abs(X - OriginX) < WorldSize * 0.5 && FMath::Abs(Z - OriginZ) < WorldSize * 0.5) OnTeleport.ExecuteIfBound(X, Z);
	}
	return FReply::Handled();
}

FReply SWwMapScreen::OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() == EKeys::LeftMouseButton && bDragging) { bDragging = false; return FReply::Handled().ReleaseMouseCapture(); }
	return FReply::Handled();
}

FReply SWwMapScreen::OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event)
{
	Mouse = FVector2f(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()));
	if (bDragging)
	{
		FVector2f Corner; float Side;
		MapRect(Geometry, Corner, Side);
		const FVector2f Moved = FVector2f(Event.GetCursorDelta()) / FMath::Max(Geometry.Scale, 0.01f);
		Cx -= Moved.X / Side * Span; Cz -= Moved.Y / Side * Span;
		Clamp();
	}
	return FReply::Handled();
}

FReply SWwMapScreen::OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
	FWwSettings& Set = FWwSettings::Get();
	const FKey Key = Event.GetKey();
	if (Key == EKeys::Escape || Key == EKeys::M) OnClose.ExecuteIfBound();
	else if (Key == EKeys::P) Set.Set(TEXT("map.style"), Set.Int(TEXT("map.style")) == 1 ? 0 : 1);
	else if (Key == EKeys::B) { Set.Set(TEXT("map.biomes"), Set.On(TEXT("map.biomes")) ? 0 : 1); Set.Set(TEXT("map.style"), 0); }
	else if (Key == EKeys::C || Key == EKeys::Home)
	{
		double X, Z, Yaw;
		if (OnPlayer.IsBound() && OnPlayer.Execute(X, Z, Yaw)) { Cx = X; Cz = Z; Clamp(); }
	}
	else if (Key == EKeys::Add || Key == EKeys::Equals) Zoom(0.6, Cx, Cz);
	else if (Key == EKeys::Subtract || Key == EKeys::Hyphen) Zoom(1 / 0.6, Cx, Cz);
	else return FReply::Unhandled();
	return FReply::Handled();
}
