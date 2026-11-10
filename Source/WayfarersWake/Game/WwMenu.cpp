#include "Game/WwMenu.h"
#include "Styling/CoreStyle.h"
#include "InputCoreTypes.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
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
						.IsEnabled_Lambda([this] { return Search.IsEmpty() && Page != TEXT("World"); })
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
		// A slider one can also type into (double-click). Settings that draw the land again take effect on letting go.
		const bool bOnRelease = Setting.bRebuild;
		return SNew(SSpinBox<double>)
			.Font(MnFont(11))
			.MinValue(Setting.Min).MaxValue(Setting.Max).MinSliderValue(Setting.Min).MaxSliderValue(Setting.Max)
			.Delta(Setting.Step)
			.MinFractionalDigits(MnDigits(Setting.Step)).MaxFractionalDigits(MnDigits(Setting.Step))
			.Value_Lambda([this, Key] { const double* Now = Held.Find(Key); return Now ? *Now : FWwSettings::Get().Num(Key); })
			.OnValueChanged_Lambda([this, Key, bOnRelease](double Value) { if (bOnRelease) Held.Add(Key, Value); else FWwSettings::Get().Set(Key, Value); })
			.OnValueCommitted_Lambda([this, Key](double Value, ETextCommit::Type) { Held.Remove(Key); FWwSettings::Get().Set(Key, Value); });
	}
	}
}
