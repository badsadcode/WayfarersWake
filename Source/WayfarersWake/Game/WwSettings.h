#pragma once

#include "CoreMinimal.h"
#include <limits>

/// What the player can set and try from the Esc menu (after Thornwake's GameSettings, with only what does something
/// here). One list: each setting has a page and a section, a plain name, a range and a default. The game's parts ask
/// for values by key every frame; the menu draws its rows from the same list, so a new setting needs one line here and
/// one read where it acts.
///
/// Kept in Saved/WwSettings.ini (key=value lines), written a moment after a change. Things that are the state of the
/// moment (the time of day, forced weather, flying) are never kept. A test switch on the command line (-WwGrass=0 ...)
/// wins over the file until the setting is changed in the menu. Scripted runs (-unattended) neither read nor write the
/// file, so tests do not depend on what was last chosen in the menu.
enum class EWwSettingKind : uint8 { Slider, Toggle, Choice, Button };

struct FWwSetting
{
	FName Key;
	FString Page, Section, Name, Tip, Unit;
	EWwSettingKind Kind = EWwSettingKind::Slider;
	double Min = 0, Max = 1, Step = 0.01, Default = 0, Value = 0;
	/// What the file holds (differs from Value while a command-line switch overrides it).
	double Stored = 0;
	TArray<FString> Options;
	/// Kept in the file; false for the state of the moment.
	bool bSaved = true;
	/// Takes a moment to show (the ground is drawn again): applied when the slider is let go.
	bool bRebuild = false;
	/// Greyed out unless this other setting is on (a toggle, or a choice not at its first option).
	FName Needs;
	/// Shown as "auto" at this value (NaN: never).
	double AutoAt = std::numeric_limits<double>::quiet_NaN();
	/// Bumped on every change from the menu and every press of a button.
	int32 Serial = 0;
};

class FWwSettings
{
public:
	static FWwSettings& Get();

	double Num(FName Key) const;
	bool On(FName Key) const { return Num(Key) > 0.5; }
	int32 Int(FName Key) const { return FMath::RoundToInt32(Num(Key)); }
	/// The chosen option's text (choices).
	FString Option(FName Key) const;

	/// A change by the player: takes effect, is kept, and ends a command-line override.
	void Set(FName Key, double Value);
	/// The game showing its own state in the menu (the hour as it passes): no notice to anyone.
	void Show(FName Key, double Value);
	/// A button pressed.
	void Press(FName Key);
	/// True once after each change or press since Seen was last brought up to date. Seen starts at -1: not looked yet.
	bool Changed(FName Key, int32& Seen) const;
	/// Back to the defaults for one page (what is kept only).
	void ResetPage(const FString& Page);

	const TArray<FWwSetting>& All() const { return List; }
	const FWwSetting* Find(FName Key) const;
	TArray<FString> Pages() const;
	/// Writes the file if something changed more than half a second ago (called every frame).
	void SaveIfDue();
	void Save();
	/// The world generation settings that differ from their defaults, as a world records them ("key=value;...").
	FString WorldRecord() const;
	bool UsesFile() const { return bUseFile; }
	static FString FilePath();

private:
	FWwSettings();
	FWwSetting& Add(const TCHAR* Key, const TCHAR* Page, const TCHAR* Section, const TCHAR* Name, EWwSettingKind Kind, double Default, const TCHAR* Tip);
	FWwSetting& Slider(const TCHAR* Key, const TCHAR* Page, const TCHAR* Section, const TCHAR* Name, double Min, double Max, double Step, double Default, const TCHAR* Unit, const TCHAR* Tip);
	FWwSetting& Choice(const TCHAR* Key, const TCHAR* Page, const TCHAR* Section, const TCHAR* Name, TArray<FString> Options, int32 Default, const TCHAR* Tip);
	void Load();
	void ReadSwitches();
	FWwSetting* FindMutable(FName Key);

	TArray<FWwSetting> List;
	TMap<FName, int32> Index;
	bool bUseFile = true, bDirty = false;
	double DirtySince = 0;
};
