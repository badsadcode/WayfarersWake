#include "Game/WwMenu.h"
#include "Styling/CoreStyle.h"
#include "Game/WwWorldSubsystem.h"
#include "WorldGen/WwWorldOptions.h"
#include "Async/Async.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "Widgets/Images/SImage.h"
#include "InputCoreTypes.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
const FLinearColor MnInk(1.f, 0.95f, 0.82f), MnDim(0.72f, 0.68f, 0.58f), MnGold(0.95f, 0.72f, 0.25f), MnPanel(0.025f, 0.035f, 0.035f, 0.93f), MnBar(0.1f, 0.12f, 0.115f, 1.f);
FSlateFontInfo MnFont(int32 Size, bool bBold = false) { return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size); }
int32 MnDigits(double Step) { return Step >= 1 ? 0 : Step >= 0.1 ? 1 : Step >= 0.01 ? 2 : 3; }
}

void SWwMenu::Construct(const FArguments& Args)
{
	OnClose = Args._OnClose;
	OnQuit = Args._OnQuit;
	SetCanTick(true);
	PreviewBrush.DrawAs = ESlateBrushDrawType::NoDrawType;
	PreviewBrush.ImageSize = FVector2D(300, 300);
	FWwSettings& Settings = FWwSettings::Get();
	const TArray<FString> Pages = Settings.Pages();
	Page = Pages.Num() > 0 ? Pages[0] : FString();

	TSharedRef<SHorizontalBox> Tabs = SNew(SHorizontalBox);
	for (const FString& Name : Pages)
		Tabs->AddSlot().AutoWidth().Padding(0, 0, 6, 0)
		[
			SNew(SButton)
			.ContentPadding(FMargin(18, 6))
						.OnClicked_Lambda([this, Name] { Search.Reset(); ShowPage(Name); return FReply::Handled(); })
			[ SNew(STextBlock).Text(FText::FromString(Name)).Font(MnFont(12, true)).ColorAndOpacity_Lambda([this, Name] { return FSlateColor(Search.IsEmpty() && Page == Name ? MnGold : MnDim); }) ]
		];

	ChildSlot.HAlign(HAlign_Center).VAlign(VAlign_Center)
	[
		SNew(SBox).WidthOverride(1100).HeightOverride(800)
		[
			SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(MnPanel).Padding(18).ForegroundColor(MnInk)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight() [ SNew(STextBlock).Text(FText::FromString(TEXT("WAYFARER'S WAKE"))).Font(MnFont(18, true)).ColorAndOpacity(MnInk) ]
						+ SVerticalBox::Slot().AutoHeight() [ SNew(STextBlock).Text(Args._Status).Font(MnFont(10)).ColorAndOpacity(MnDim) ]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SBox).WidthOverride(260)
						[
							SNew(SEditableTextBox)
							.HintText(FText::FromString(TEXT("Search all settings")))
							.Font(MnFont(11))
							.Text_Lambda([this] { return FText::FromString(Search); })
							.OnTextChanged_Lambda([this](const FText& Text) { Search = Text.ToString().TrimStartAndEnd(); Fill(); })
						]
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 14, 0, 10) [ Tabs ]
				+ SVerticalBox::Slot().FillHeight(1)
				[
					SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0, 0, 0, 0.35f)).Padding(FMargin(14, 8))
					[ SAssignNew(List, SScrollBox) ]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 0)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SButton).ContentPadding(FMargin(14, 6))
						.ToolTipText(FText::FromString(TEXT("Puts everything on this page that is kept between sessions back to how it came.")))
						.IsEnabled_Lambda([this] { return Search.IsEmpty(); })
						.OnClicked_Lambda([this] { FWwSettings::Get().ResetPage(Page); return FReply::Handled(); })
						[ SNew(STextBlock).Text(FText::FromString(TEXT("Reset this page"))).Font(MnFont(11)).ColorAndOpacity(MnInk) ]
					]
					+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(14, 0)
					[ SNew(STextBlock).Text(FText::FromString(TEXT("Changes show at once and are kept."))).Font(MnFont(10)).ColorAndOpacity(MnDim) ]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
					[
						SNew(SButton).ContentPadding(FMargin(14, 6)).OnClicked_Lambda([this] { OnQuit.ExecuteIfBound(); return FReply::Handled(); })
						[ SNew(STextBlock).Text(FText::FromString(TEXT("Quit game"))).Font(MnFont(11)).ColorAndOpacity(MnInk) ]
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SButton).ContentPadding(FMargin(14, 6)).OnClicked_Lambda([this] { OnClose.ExecuteIfBound(); return FReply::Handled(); })
						[ SNew(STextBlock).Text(FText::FromString(TEXT("Resume (Esc)"))).Font(MnFont(11, true)).ColorAndOpacity(MnGold) ]
					]
				]
			]
		]
	];
	Fill();
}

FReply SWwMenu::OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
	if (Event.GetKey() == EKeys::Escape) { OnClose.ExecuteIfBound(); return FReply::Handled(); }
	return SCompoundWidget::OnKeyDown(Geometry, Event);
}

void SWwMenu::ShowPage(const FString& InPage)
{
	for (const FString& Name : FWwSettings::Get().Pages())
		if (Name.Equals(InPage, ESearchCase::IgnoreCase)) Page = Name;
	Fill();
}

void SWwMenu::Fill()
{
	if (!List) return;
	List->ClearChildren();
	ChoiceLists.Reset();
	Held.Reset();
	FString Section, Heading;
	int32 Shown = 0;
	if (Search.IsEmpty() && Page == TEXT("World")) List->AddSlot().Padding(0, 4, 0, 6) [ PreviewPanel() ];
	for (const FWwSetting& Setting : FWwSettings::Get().All())
	{
		// A page, or with words in the search box whatever matches them on any page.
		if (Search.IsEmpty() ? Setting.Page != Page
			: !(Setting.Name.Contains(Search) || Setting.Section.Contains(Search) || Setting.Tip.Contains(Search) || Setting.Page.Contains(Search))) continue;
		const FString Wanted = Search.IsEmpty() ? Setting.Section : Setting.Page + TEXT(": ") + Setting.Section;
		if (Wanted != Heading)
		{
			Heading = Wanted;
			List->AddSlot().Padding(0, Shown > 0 ? 16 : 4, 0, 4)
			[ SNew(STextBlock).Text(FText::FromString(Heading.ToUpper())).Font(MnFont(10, true)).ColorAndOpacity(MnGold) ];
		}
		List->AddSlot().Padding(0, 2) [ Row(Setting) ];
		Shown++;
	}
	if (Shown == 0)
		List->AddSlot().Padding(0, 10) [ SNew(STextBlock).Text(FText::FromString(TEXT("Nothing by that name."))).Font(MnFont(11)).ColorAndOpacity(MnDim) ];
	List->ScrollToStart();
}

TSharedRef<SWidget> SWwMenu::Row(const FWwSetting& Setting)
{
	const FName Needs = Setting.Needs;
	const bool bButton = Setting.Kind == EWwSettingKind::Button;
	FString Note = Setting.Unit;
	if (Setting.bRebuild) Note += Note.IsEmpty() ? TEXT("(the land is drawn again)") : TEXT("  (the land is drawn again)");
	return SNew(SHorizontalBox)
		.IsEnabled_Lambda([Needs] { return Needs.IsNone() || FWwSettings::Get().On(Needs); })
		.ToolTipText(FText::FromString(Setting.Tip))
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(280)
			[ SNew(STextBlock).Text(FText::FromString(bButton ? FString() : Setting.Name)).Font(MnFont(11)).ColorAndOpacity(MnInk) ]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[ SNew(SBox).WidthOverride(340).HeightOverride(28) [ Control(Setting) ] ]
		+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(12, 0, 0, 0)
		[ SNew(STextBlock).Text(FText::FromString(bButton ? Setting.Tip : Note)).Font(MnFont(10)).ColorAndOpacity(MnDim) ];
}

TSharedRef<SWidget> SWwMenu::Control(const FWwSetting& Setting)
{
	const FName Key = Setting.Key;
	switch (Setting.Kind)
	{
	case EWwSettingKind::Toggle:
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([Key] { return FWwSettings::Get().On(Key) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([Key](ECheckBoxState State) { FWwSettings::Get().Set(Key, State == ECheckBoxState::Checked ? 1 : 0); })
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)
			[ SNew(STextBlock).Font(MnFont(10)).ColorAndOpacity(MnDim).Text_Lambda([Key] { return FText::FromString(FWwSettings::Get().On(Key) ? TEXT("on") : TEXT("off")); }) ];

	case EWwSettingKind::Choice:
	{
		TSharedPtr<TArray<TSharedPtr<FString>>> Options = MakeShared<TArray<TSharedPtr<FString>>>();
		for (const FString& Option : Setting.Options) Options->Add(MakeShared<FString>(Option));
		ChoiceLists.Add(Options);
		return SNew(SComboBox<TSharedPtr<FString>>)
			.OptionsSource(Options.Get())
			.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) { return SNew(STextBlock).Text(FText::FromString(*Item)).Font(MnFont(11)); })
			.OnSelectionChanged_Lambda([Key, Options](TSharedPtr<FString> Item, ESelectInfo::Type)
			{
				const int32 At = Options->IndexOfByKey(Item);
				if (At >= 0) FWwSettings::Get().Set(Key, At);
			})
			[ SNew(STextBlock).Font(MnFont(11)).Text_Lambda([Key] { return FText::FromString(FWwSettings::Get().Option(Key)); }) ];
	}

	case EWwSettingKind::Button:
		return SNew(SButton).HAlign(HAlign_Center).VAlign(VAlign_Center)
			.OnClicked_Lambda([Key] { FWwSettings::Get().Press(Key); return FReply::Handled(); })
			[ SNew(STextBlock).Text(FText::FromString(Setting.Name)).Font(MnFont(11)).ColorAndOpacity(MnInk) ];

	default:
	{
		// A bar to drag (click anywhere on it and it jumps there) and beside it the number, which can be typed. Settings
		// that draw the land again take effect on letting go. A range too wide to drag (the seed) is typed only.
		const bool bOnRelease = Setting.bRebuild, bTypedOnly = Setting.Max - Setting.Min > 1e6;
		const double Min = Setting.Min, Max = Setting.Max, Step = Setting.Step;
		const int32 Digits = MnDigits(Step);
		auto Snap = [Min, Max, Step](double Value) { return FMath::Clamp(Min + FMath::RoundToDouble((Value - Min) / Step) * Step, Min, Max); };
		TSharedRef<TWeakPtr<SEditableTextBox>> Typed = MakeShared<TWeakPtr<SEditableTextBox>>();
		TSharedRef<SEditableTextBox> Box = SNew(SEditableTextBox)
			.Font(MnFont(11))
			.SelectAllTextWhenFocused(true)
			.Text_Lambda([this, Key, Digits, Typed, AutoAt = Setting.AutoAt]
			{
				// (while it is being typed in, the box keeps what was typed: the hour moves on by itself)
				const TSharedPtr<SEditableTextBox> Self = Typed->Pin();
				if (Self && Self->HasKeyboardFocus()) return Self->GetText();
				const double* Now = Held.Find(Key);
				const double Value = Now ? *Now : FWwSettings::Get().Num(Key);
				if (Value == AutoAt) return FText::FromString(TEXT("auto"));
				return FText::FromString(FString::Printf(TEXT("%.*f"), Digits, Value));
			})
			.OnTextCommitted_Lambda([Key](const FText& Text, ETextCommit::Type How)
			{
				const FString Number = Text.ToString().TrimStartAndEnd().Replace(TEXT(","), TEXT("."));
				if (How != ETextCommit::OnCleared && Number.IsNumeric()) FWwSettings::Get().Set(Key, FCString::Atod(*Number));
			});
		*Typed = Box;
		if (bTypedOnly) return Box;
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
			[
				SNew(SSlider)
				.SliderBarColor(FLinearColor(0.45f, 0.42f, 0.34f)).SliderHandleColor(MnGold)
				.MouseUsesStep(false).StepSize((float)(Step / FMath::Max(Max - Min, 1e-9)))
				.Value_Lambda([this, Key, Min, Max] { const double* Now = Held.Find(Key); return (float)(((Now ? *Now : FWwSettings::Get().Num(Key)) - Min) / FMath::Max(Max - Min, 1e-9)); })
				.OnValueChanged_Lambda([this, Key, Min, Max, Snap, bOnRelease](float Share)
				{
					const double Value = Snap(Min + Share * (Max - Min));
					if (bOnRelease) Held.Add(Key, Value); else FWwSettings::Get().Set(Key, Value);
				})
				.OnMouseCaptureEnd_Lambda([this, Key] { if (const double* Now = Held.Find(Key)) { const double Value = *Now; Held.Remove(Key); FWwSettings::Get().Set(Key, Value); } })
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10, 0, 0, 0)
			[ SNew(SBox).WidthOverride(78) [ Box ] ];
	}
	}
}

// ---------------------------------------------------------------------------------------------------- the preview
TSharedRef<SWidget> SWwMenu::PreviewPanel()
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SBox).WidthOverride(300).HeightOverride(300)
			[
				SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0.05f, 0.07f, 0.1f)).Padding(0)
				[ SNew(SImage).Image(&PreviewBrush) ]
			]
		]
		+ SHorizontalBox::Slot().FillWidth(1).Padding(18, 0, 0, 0).VAlign(VAlign_Top)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight() [ SNew(STextBlock).Text(FText::FromString(TEXT("THE WORLD THESE SETTINGS WOULD MAKE"))).Font(MnFont(10, true)).ColorAndOpacity(MnGold) ]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
			[ SNew(STextBlock).AutoWrapText(true).Font(MnFont(11)).ColorAndOpacity(MnInk).Text_Lambda([this] { return FText::FromString(PreviewText); }) ]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
			[ SNew(STextBlock).AutoWrapText(true).Font(MnFont(10)).ColorAndOpacity(MnDim).Text(FText::FromString(TEXT("The settings below are taken when a world is made; they do not change the world you are in. \"Reset this page\" puts them all back to auto. The map is drawn as the map (M) is: plain, biomes or old chart."))) ]
		];
}

void SWwMenu::StartPreview()
{
	if (Previewing.IsValid()) return;
	const FWwSettings& Set = FWwSettings::Get();
	const FString Type = Set.Option(TEXT("world.type")), Record = Set.WorldRecord();
	const double SizeKm = Set.Int(TEXT("world.size")) == 0 ? 0.0 : FCString::Atod(*Set.Option(TEXT("world.size")));
	const int32 Seed = Set.Int(TEXT("world.seed"));
	const ww::EMapStyle Style = Set.Int(TEXT("map.style")) == 1 ? ww::EMapStyle::Parchment : Set.On(TEXT("map.biomes")) ? ww::EMapStyle::Biomes : ww::EMapStyle::Terrain;
	PreviewText = FString::Printf(TEXT("Making the map of %s, seed %d ..."), *Type, Seed);
	Previewing = Async(EAsyncExecution::Thread, [Type, Record, SizeKm, Seed, Style]()
	{
		FPreview Out;
		const double Started = FPlatformTime::Seconds();
		std::vector<ww::FWorldProfile> Profiles; ww::FBiomeRegistry Biomes; FString Error;
		if (!UWwWorldSubsystem::LoadCatalog(Profiles, Biomes, Error)) { Out.Text = Error; return Out; }
		const std::vector<ww::FWorldRecipe> Recipes = ww::FWorldRecipe::Catalog(Profiles);
		ww::FWorldProfile Profile; ww::FTerrainStyle Terrain; std::string Problem;
		if (!ww::FWorldOptions::Build(Recipes, Biomes, TCHAR_TO_UTF8(*Type), SizeKm, ww::FTerrainStyle::PortedRevision, ww::FWorldOptions::Read(TCHAR_TO_UTF8(*Record)), Profile, Terrain, Problem))
		{
			Out.Text = FString::Printf(TEXT("These settings do not make a world: %s"), UTF8_TO_TCHAR(Problem.c_str()));
			return Out;
		}
		const auto Plan = std::make_shared<ww::FWorldPlan>(ww::FWorldManifest::Create(Seed, Profile), Terrain, Biomes);
		const ww::FTerrainSampler Sampler(Plan);
		Out.Image = ww::FWorldMap::Render(Sampler, 512, Style);
		// How much of it is land (a grid of the plan's own picture of land and sea).
		int32 Land = 0;
		const double Half = Profile.WorldSize / 2;
		for (int32 j = 0; j < 128; j++)
			for (int32 i = 0; i < 128; i++)
				Land += Plan->Macro.Sample(ww::FWorldPlan::LandChannel, Plan->Manifest.OriginX - Half + (i + .5) * Profile.WorldSize / 128, Plan->Manifest.OriginZ - Half + (j + .5) * Profile.WorldSize / 128) > 0;
		Out.Text = FString::Printf(TEXT("%s, seed %d: %.0f km x %.0f km. %d continent%s, %d islands. Land %.0f %%. %d river reaches, %d lakes. Mountains to about %.0f m. Made in %.1f s.%s"),
			*Type, Seed, Profile.WorldSize / 1000, Profile.WorldSize / 1000, Profile.Continents, Profile.Continents == 1 ? TEXT("") : TEXT("s"), Profile.Islands, Land * 100.0 / (128 * 128),
			(int32)Plan->Hydrology->Reaches.size(), (int32)Plan->Hydrology->Lakes.size(), Profile.LowlandHeight + Profile.HillHeight + Profile.MountainHeight, FPlatformTime::Seconds() - Started,
			Record.IsEmpty() ? TEXT("") : *FString::Printf(TEXT("\nChanged from auto: %s"), *Record.Replace(TEXT(";"), TEXT(", ")).Replace(TEXT("gen."), TEXT(""))));
		return Out;
	});
}

void SWwMenu::Tick(const FGeometry& Geometry, const double CurrentTime, const float DeltaTime)
{
	SCompoundWidget::Tick(Geometry, CurrentTime, DeltaTime);
	if (FWwSettings::Get().Changed(TEXT("world.preview"), PreviewSeen)) StartPreview();
	if (!Previewing.IsValid() || !Previewing.IsReady()) return;
	const FPreview Done = Previewing.Get();
	Previewing = TFuture<FPreview>();
	PreviewText = Done.Text;
	UE_LOG(LogTemp, Display, TEXT("Wayfarer menu preview: %s"), *Done.Text.Replace(TEXT("\n"), TEXT(" | ")));
	const int32 N = Done.Image.Pixels;
	if (N <= 0) return;
	if (!PreviewTexture || PreviewTexture->GetSizeX() != N)
	{
		PreviewTexture = TStrongObjectPtr<UTexture2D>(UTexture2D::CreateTransient(N, N, PF_B8G8R8A8));
		PreviewTexture->SRGB = true;
		PreviewBrush.SetResourceObject(PreviewTexture.Get());
		PreviewBrush.DrawAs = ESlateBrushDrawType::Image;
	}
	void* Data = PreviewTexture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Data, Done.Image.Bgra.data(), Done.Image.Bgra.size());
	PreviewTexture->GetPlatformData()->Mips[0].BulkData.Unlock();
	PreviewTexture->UpdateResource();
}
