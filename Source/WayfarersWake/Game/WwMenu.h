#pragma once

#include "CoreMinimal.h"
#include "Game/WwSettings.h"
#include "Widgets/SCompoundWidget.h"

class SScrollBox;
class SHorizontalBox;

/// The Esc menu: the settings (WwSettings) as short pages, with a search across all of them. The world keeps running
/// behind it, so a change shows at once. After Thornwake's Esc menu, with only what does something here; it grows as
/// features arrive (a new setting appears by being added to the list).
class SWwMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SWwMenu) {}
		SLATE_EVENT(FSimpleDelegate, OnClose)
		SLATE_EVENT(FSimpleDelegate, OnQuit)
		/// A line under the title: the world one is in.
		SLATE_ATTRIBUTE(FText, Status)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
	void ShowPage(const FString& InPage);
	const FString& GetPage() const { return Page; }

private:
	void Fill();
	TSharedRef<SWidget> Row(const FWwSetting& Setting);
	TSharedRef<SWidget> Control(const FWwSetting& Setting);

	FSimpleDelegate OnClose, OnQuit;
	FString Page, Search;
	TSharedPtr<SScrollBox> List;
	/// What a slider shows while it is dragged, for settings applied on letting go.
	TMap<FName, double> Held;
	/// The lists the drop-downs choose from (they must outlive their widgets).
	TArray<TSharedPtr<TArray<TSharedPtr<FString>>>> ChoiceLists;
};
