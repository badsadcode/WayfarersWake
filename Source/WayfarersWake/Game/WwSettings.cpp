#include "Game/WwSettings.h"
#include "Game/WwWorldSubsystem.h"
#include "WorldGen/WwWorldOptions.h"
#include "WayfarersWake.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Internationalization/Regex.h"

FWwSettings& FWwSettings::Get()
{
	static FWwSettings Settings;
	return Settings;
}

FString FWwSettings::FilePath() { return FPaths::ProjectSavedDir() / TEXT("WwSettings.ini"); }

FWwSetting& FWwSettings::Add(const TCHAR* Key, const TCHAR* Page, const TCHAR* Section, const TCHAR* Name, EWwSettingKind Kind, double Default, const TCHAR* Tip)
{
	FWwSetting S;
	S.Key = Key; S.Page = Page; S.Section = Section; S.Name = Name; S.Kind = Kind; S.Tip = Tip;
	S.Default = S.Value = S.Stored = Default;
	if (Kind == EWwSettingKind::Button) S.bSaved = false;
	Index.Add(S.Key, List.Num());
	return List.Add_GetRef(MoveTemp(S));
}

FWwSetting& FWwSettings::Slider(const TCHAR* Key, const TCHAR* Page, const TCHAR* Section, const TCHAR* Name, double Min, double Max, double Step, double Default, const TCHAR* Unit, const TCHAR* Tip)
{
	FWwSetting& S = Add(Key, Page, Section, Name, EWwSettingKind::Slider, Default, Tip);
	S.Min = Min; S.Max = Max; S.Step = Step; S.Unit = Unit;
	return S;
}

FWwSetting& FWwSettings::Choice(const TCHAR* Key, const TCHAR* Page, const TCHAR* Section, const TCHAR* Name, TArray<FString> Options, int32 Default, const TCHAR* Tip)
{
	FWwSetting& S = Add(Key, Page, Section, Name, EWwSettingKind::Choice, Default, Tip);
	S.Max = Options.Num() - 1; S.Step = 1;
	S.Options = MoveTemp(Options);
	return S;
}

FWwSettings::FWwSettings()
{
	const EWwSettingKind Toggle = EWwSettingKind::Toggle, Button = EWwSettingKind::Button;
	const TCHAR* Body = TEXT("Character");
	const TCHAR* Test = TEXT("Test"); const TCHAR* Sound = TEXT("Sound"); const TCHAR* View = TEXT("View"); const TCHAR* World = TEXT("World");

	// ------------------------------------------------------------------ Character: who you are
	Choice(TEXT("char.body"), Body, TEXT("Body"), TEXT("Body"), { TEXT("Human man"), TEXT("Human woman"), TEXT("Orc man"), TEXT("Orc woman"), TEXT("Dwarf man"), TEXT("Dwarf woman") }, 0,
		TEXT("Tom's base bodies, rigged and animated by Tools/RigCharacters. Changes at once."));
	Slider(TEXT("char.camera"), Body, TEXT("Camera"), TEXT("Camera distance"), 1.5, 8, 0.1, 4.2, TEXT("m"), TEXT("How far behind you the camera sits."));
	// ------------------------------------------------------------------ Test: everything for trying things, in one place
	Slider(TEXT("test.hour"), Test, TEXT("Time"), TEXT("Time of day"), 0, 24, 0.05, 9.5, TEXT("h"), TEXT("The hour now. Drag it to see dawn, noon, dusk or night.")).bSaved = false;
	Slider(TEXT("time.daylength"), Test, TEXT("Time"), TEXT("A day lasts"), 1, 1440, 1, 180, TEXT("real minutes"), TEXT("How long a game day takes. 180 is how Tom plays Thornwake; 10 lets you watch the sun move."));
	Add(TEXT("time.stop"), Test, TEXT("Time"), TEXT("Stop the clock"), Toggle, 0, TEXT("The hour stands still. The weather and the wind stop changing with it.")).bSaved = false;
	Choice(TEXT("time.season"), Test, TEXT("Time"), TEXT("Season"), { TEXT("Spring"), TEXT("Summer"), TEXT("Autumn"), TEXT("Winter") }, 0, TEXT("Sets the length of the day, the height of the sun and how warm the air is."));

	Choice(TEXT("test.weather"), Test, TEXT("Weather"), TEXT("Weather"), { TEXT("As the world makes it"), TEXT("Set by hand") }, 0, TEXT("By hand: the rain and cloud below replace the fronts passing over.")).bSaved = false;
	FWwSetting& Rain = Slider(TEXT("test.rain"), Test, TEXT("Weather"), TEXT("Rain"), 0, 1, 0.01, 0, TEXT(""), TEXT("0 dry, 0.3 light rain, 0.6 heavy, 0.8 and more a thunderstorm."));
	Rain.bSaved = false; Rain.Needs = TEXT("test.weather");
	FWwSetting& Cloud = Slider(TEXT("test.cloud"), Test, TEXT("Weather"), TEXT("Cloud"), 0, 1, 0.01, 0.3, TEXT(""), TEXT("0 a clear sky, 1 overcast. Rain brings its own cloud."));
	Cloud.bSaved = false; Cloud.Needs = TEXT("test.weather");
	FWwSetting& Snow = Add(TEXT("test.snow"), Test, TEXT("Weather"), TEXT("Falls as snow"), Toggle, 0, TEXT("What falls is snow, whatever the temperature."));
	Snow.bSaved = false; Snow.Needs = TEXT("test.weather");
	Add(TEXT("test.windset"), Test, TEXT("Weather"), TEXT("Set the wind by hand"), Toggle, 0, TEXT("The speed and direction below replace the world's wind.")).bSaved = false;
	FWwSetting& WindSpeed = Slider(TEXT("test.windspeed"), Test, TEXT("Weather"), TEXT("Wind speed"), 0, 30, 0.5, 6, TEXT("m/s"), TEXT("5 a breeze, 12 a strong wind, 20 a gale."));
	WindSpeed.bSaved = false; WindSpeed.Needs = TEXT("test.windset");
	FWwSetting& WindDir = Slider(TEXT("test.winddir"), Test, TEXT("Weather"), TEXT("Wind blows from"), 0, 360, 5, 315, TEXT("degrees"), TEXT("0 north, 90 east, 180 south, 270 west."));
	WindDir.bSaved = false; WindDir.Needs = TEXT("test.windset");
	Add(TEXT("test.seaset"), Test, TEXT("Weather"), TEXT("Set the sea by hand"), Toggle, 0, TEXT("The sea state below replaces the one the wind raises.")).bSaved = false;
	FWwSetting& Sea = Slider(TEXT("test.sea"), Test, TEXT("Weather"), TEXT("Sea state"), 0, 1, 0.01, 0.5, TEXT(""), TEXT("0 glassy calm, 0.25 a breeze (waves under a metre), 0.5 rough (3 to 4 m), 0.8 and more a storm (9 m and over)."));
	Sea.bSaved = false; Sea.Needs = TEXT("test.seaset");
	Add(TEXT("test.strike150"), Test, TEXT("Lightning"), TEXT("Strike 150 m away"), Button, 0, TEXT("A flash now, and its thunder after the sound's travel time (under half a second)."));
	Add(TEXT("test.strike1k"), Test, TEXT("Lightning"), TEXT("Strike 1 km away"), Button, 0, TEXT("Thunder after 3 seconds."));
	Add(TEXT("test.strike5k"), Test, TEXT("Lightning"), TEXT("Strike 5 km away"), Button, 0, TEXT("Thunder after 15 seconds, duller."));
	Add(TEXT("test.strike20k"), Test, TEXT("Lightning"), TEXT("Strike 20 km away"), Button, 0, TEXT("A distant rumble after a minute."));

	Add(TEXT("test.fly"), Test, TEXT("Moving"), TEXT("Fly (F)"), Toggle, 0, TEXT("Developer flight: WASD, E and Q up and down, Shift fast, Ctrl very fast.")).bSaved = false;
	Slider(TEXT("test.gox"), Test, TEXT("Moving"), TEXT("Go to: east"), -2048, 2048, 0.01, 0, TEXT("km"), TEXT("Kilometres east of the world's middle, as the top-left text shows them.")).bSaved = false;
	Slider(TEXT("test.goz"), Test, TEXT("Moving"), TEXT("Go to: south"), -2048, 2048, 0.01, 0, TEXT("km"), TEXT("Kilometres south of the world's middle.")).bSaved = false;
	Add(TEXT("test.go"), Test, TEXT("Moving"), TEXT("Go there"), Button, 0, TEXT("Puts you on the ground at that place, once the ground there has been raised."));
	Add(TEXT("test.home"), Test, TEXT("Moving"), TEXT("Back to the start"), Button, 0, TEXT("Where this world first put you."));

	Choice(TEXT("test.waterview"), Test, TEXT("Views"), TEXT("Sea test view"), { TEXT("Off"), TEXT("Foam and breaking"), TEXT("Bed slope, exposure, depth"), TEXT("Foam age and folds") }, 0, TEXT("Colours the water by what the sea shader works with.")).bSaved = false;
	Add(TEXT("view.hud"), Test, TEXT("Views"), TEXT("Text on screen"), Toggle, 1, TEXT("The lines of text over the picture. Off for clean screenshots."));
	Add(TEXT("test.dump"), Test, TEXT("Views"), TEXT("Write a debug dump (F9)"), Button, 0, TEXT("A text file of where you are and what the game is doing, with a screenshot, in Saved/Dumps."));

	// ------------------------------------------------------------------ Debug: what the code can do, in the player's hands
	{
		const TCHAR* Debug = TEXT("Debug");
		// (the items the game knows: read from its content file, so a new item appears here by itself)
		TArray<FString> Items;
		FString Json;
		if (FFileHelper::LoadFileToString(Json, *(FPaths::ProjectContentDir() / TEXT("Gameplay/gameplay_items.json"))))
		{
			FRegexMatcher Ids(FRegexPattern(TEXT("\"id\"\\s*:\\s*\"([^\"]+)\"")), Json);
			while (Ids.FindNext()) Items.AddUnique(Ids.GetCaptureGroup(1));
		}
		if (Items.Num() == 0) Items.Add(TEXT("stone_axe"));
		Choice(TEXT("debug.item"), Debug, TEXT("Items"), TEXT("Item"), Items, 0, TEXT("Any item the game knows.")).bSaved = false;
		Slider(TEXT("debug.amount"), Debug, TEXT("Items"), TEXT("How many"), 1, 50, 1, 5, TEXT(""), TEXT("")).bSaved = false;
		Add(TEXT("debug.give"), Debug, TEXT("Items"), TEXT("Put them in my pack"), Button, 0, TEXT("No crafting needed."));
		Add(TEXT("debug.giveall"), Debug, TEXT("Items"), TEXT("One of everything"), Button, 0, TEXT("One of each item into the pack."));
		Add(TEXT("debug.needs"), Debug, TEXT("Body"), TEXT("Hunger, cold and wet"), Toggle, 1, TEXT("Off: food, warmth, wetness and health stand still while you test.")).bSaved = false;
		Add(TEXT("debug.heal"), Debug, TEXT("Body"), TEXT("Fed, warm, dry and well"), Button, 0, TEXT("Sets every need to its best."));
		Choice(TEXT("debug.creature"), Debug, TEXT("Creatures"), TEXT("Creature"), { TEXT("Deer"), TEXT("Dog"), TEXT("Horse"), TEXT("Chicken"), TEXT("Cat"), TEXT("Penguin"), TEXT("Tiger"),
			TEXT("Blue whale (mystical: 90 m)"), TEXT("Whale calf (30 m)"), TEXT("Megalodon (mystical: 35 m)") }, 0, TEXT("From the asset packs. They stand or swim in place for now: no minds yet.")).bSaved = false;
		Slider(TEXT("debug.size"), Debug, TEXT("Creatures"), TEXT("Size"), 0.1, 5, 0.1, 1, TEXT("x"), TEXT("Times the size given above. The sea creatures are already giants at 1.")).bSaved = false;
		Add(TEXT("debug.spawn"), Debug, TEXT("Creatures"), TEXT("Put one in front of me"), Button, 0, TEXT("On the ground, or in the water if that is where you look. Sea creatures want deep water."));
		Add(TEXT("debug.clear"), Debug, TEXT("Creatures"), TEXT("Take them all away"), Button, 0, TEXT(""));
		Add(TEXT("debug.wildlife"), Debug, TEXT("Wildlife"), TEXT("Wild deer in the world"), Toggle, 1, TEXT("Herds of deer round you that graze, notice you and run. Off: they stand as they are."));
		Add(TEXT("debug.herd"), Debug, TEXT("Wildlife"), TEXT("A herd 50 m ahead"), Button, 0, TEXT("Four deer that behave as wild ones do, to try stalking on."));
	}
	// ------------------------------------------------------------------ Sound (defaults: Tom's Thornwake settings)
	Add(TEXT("sound.on"), Sound, TEXT("Volume"), TEXT("Sound"), Toggle, 1, TEXT("All sound on or off."));
	Slider(TEXT("sound.master"), Sound, TEXT("Volume"), TEXT("Everything"), 0, 2, 0.05, 1.2, TEXT(""), TEXT("The overall volume.")).Needs = TEXT("sound.on");
	Slider(TEXT("sound.weather"), Sound, TEXT("Weather"), TEXT("Weather (all)"), 0, 2, 0.05, 1.05, TEXT(""), TEXT("Rain, wind and thunder together."));
	Slider(TEXT("sound.rain"), Sound, TEXT("Weather"), TEXT("Rain"), 0, 1.5, 0.05, 0.4, TEXT(""), TEXT(""));
	Slider(TEXT("sound.raintone"), Sound, TEXT("Weather"), TEXT("Rain tone"), 0, 1, 0.05, 0, TEXT(""), TEXT("0 a soft patter, 1 a sharp bright hiss."));
	Slider(TEXT("sound.wind"), Sound, TEXT("Weather"), TEXT("Wind"), 0, 1.5, 0.05, 0.6, TEXT(""), TEXT(""));
	Slider(TEXT("sound.thunder"), Sound, TEXT("Weather"), TEXT("Thunder"), 0, 1.5, 0.05, 0.6, TEXT(""), TEXT(""));
	Slider(TEXT("sound.water"), Sound, TEXT("The world"), TEXT("Rivers and surf"), 0, 2, 0.05, 1, TEXT(""), TEXT("Running water and the sea when you stand near them."));
	Slider(TEXT("sound.small"), Sound, TEXT("The world"), TEXT("Steps and the axe"), 0, 2, 0.05, 1, TEXT(""), TEXT("Footsteps and the short sounds of what you do."));

	// ------------------------------------------------------------------ View
	Slider(TEXT("view.grass"), View, TEXT("How much is drawn"), TEXT("Grass and plants"), 0, 3, 0.25, 3, TEXT(""), TEXT("0 none, 1 Thornwake's default, 3 as Tom plays it.")).bRebuild = true;
	Slider(TEXT("view.levels"), View, TEXT("How much is drawn"), TEXT("Land in view"), 6, 9, 1, 9, TEXT("steps"), TEXT("Each step doubles how far the land is drawn: 9 is about 24 km, 8 about 12 km.")).bRebuild = true;
	Slider(TEXT("view.trees"), View, TEXT("How much is drawn"), TEXT("Trees as models"), 2, 5, 1, 4, TEXT("steps"), TEXT("How far trees are real models. Each step doubles the distance; 5 is heavy.")).bRebuild = true;
	Slider(TEXT("view.fartrees"), View, TEXT("How much is drawn"), TEXT("Distant forests"), 0, 8, 1, 6, TEXT("steps"), TEXT("Beyond the models, forests are flat pictures of trees. 6 is about 12 km; 0 turns them off.")).bRebuild = true;
	Add(TEXT("view.plantshadows"), View, TEXT("How much is drawn"), TEXT("Grass casts shadows"), Toggle, 1, TEXT("")).bRebuild = true;
	Add(TEXT("view.sunshadows"), View, TEXT("Light"), TEXT("Sun shadows"), Toggle, 1, TEXT(""));
	Slider(TEXT("view.exposure"), View, TEXT("Light"), TEXT("Brightness"), -2, 1, 0.05, -0.7, TEXT("stops"), TEXT("How bright the picture is exposed. -0.7 was matched to Thornwake's pictures."));
	Slider(TEXT("view.saturation"), View, TEXT("Light"), TEXT("Colour"), 0.5, 1.5, 0.01, 1.15, TEXT(""), TEXT("1 is plain; more is richer colour."));
	Slider(TEXT("view.haze"), View, TEXT("Light"), TEXT("Haze"), 0, 5, 0.1, 1, TEXT("x"), TEXT("How thick the air is in fair weather. Rain thickens it further."));
	Add(TEXT("view.volfog"), View, TEXT("Air"), TEXT("Light in the air"), Toggle, 1, TEXT("Fog and haze that catch the light: rays between the trees, mist that glows at dawn. Costs some speed."));
	Slider(TEXT("view.rays"), View, TEXT("Air"), TEXT("Light rays"), 0, 3, 0.05, 1, TEXT("x"), TEXT("How strongly the sun shows in the air.")).Needs = TEXT("view.volfog");
	Slider(TEXT("view.mist"), View, TEXT("Air"), TEXT("Mist"), 0, 2, 0.05, 1, TEXT("x"), TEXT("Low mist on calm mornings, in the evening and in rain. 0 turns it off."));
	Slider(TEXT("view.bloom"), View, TEXT("Air"), TEXT("Glow of bright things"), 0, 2, 0.05, 1, TEXT("x"), TEXT(""));
	Add(TEXT("view.reflect"), View, TEXT("Water"), TEXT("Water mirrors its surroundings"), Toggle, 1, TEXT("The land, the trees and the clouds reflected in the water. Costs some speed."));
	Add(TEXT("view.splashes"), View, TEXT("Water"), TEXT("Rain splashes"), Toggle, 1, TEXT("Small splashes where raindrops land near you, on the ground and on the water."));
	Add(TEXT("view.stars"), View, TEXT("Light"), TEXT("Stars"), Toggle, 1, TEXT(""));
	Add(TEXT("view.moon"), View, TEXT("Light"), TEXT("Moon"), Toggle, 1, TEXT(""));
	Choice(TEXT("map.style"), View, TEXT("Map (M)"), TEXT("Map drawn as"), { TEXT("A plain map"), TEXT("An old chart on parchment") }, 0, TEXT("P on the map changes it too."));
	Add(TEXT("map.biomes"), View, TEXT("Map (M)"), TEXT("Plain map shows biomes"), Toggle, 0, TEXT("Each biome in its own colour instead of the height of the land. B on the map."));
	Slider(TEXT("view.fov"), View, TEXT("Camera"), TEXT("Field of view"), 60, 110, 1, 90, TEXT("degrees"), TEXT("Across the screen. 90 is Thornwake's view at 16:9."));
	Slider(TEXT("view.sens"), View, TEXT("Camera"), TEXT("Mouse speed"), 0.03, 0.4, 0.01, 0.12, TEXT(""), TEXT(""));

	// ------------------------------------------------------------------ World (takes effect with "Make this world")
	{
		TArray<FString> Types;
		std::vector<ww::FWorldProfile> Profiles; ww::FBiomeRegistry Biomes; FString Error;
		if (UWwWorldSubsystem::LoadCatalog(Profiles, Biomes, Error))
			for (const ww::FWorldRecipe& Recipe : ww::FWorldRecipe::Catalog(Profiles)) Types.Add(UTF8_TO_TCHAR(Recipe.Id.c_str()));
		if (Types.Num() == 0) Types.Add(TEXT("balanced"));
		Choice(TEXT("world.type"), World, TEXT("A new world"), TEXT("World type"), Types, FMath::Max(0, Types.IndexOfByKey(FString(TEXT("balanced")))), TEXT("The kind of world: how land and sea are laid out."));
		Slider(TEXT("world.seed"), World, TEXT("A new world"), TEXT("Seed"), 0, 2147483647, 1, 7319, TEXT(""), TEXT("The number the world grows from. The same type, size and seed always give the same world."));
		Add(TEXT("world.random"), World, TEXT("A new world"), TEXT("Random seed"), Button, 0, TEXT(""));
		TArray<FString> Sizes;
		Sizes.Add(TEXT("The type's own size"));
		for (double Km : ww::FWorldRecipe::Sizes()) Sizes.Add(FString::Printf(TEXT("%.0f"), Km));
		Choice(TEXT("world.size"), World, TEXT("A new world"), TEXT("Size (km across)"), Sizes, FMath::Max(0, Sizes.IndexOfByKey(FString(TEXT("64")))), TEXT("Width and length. Large worlds take longer to make."));
		Add(TEXT("world.preview"), World, TEXT("A new world"), TEXT("Preview the map"), Button, 0, TEXT("Draws the map of the world these settings would make, without leaving the one you are in."));
		Add(TEXT("world.make"), World, TEXT("A new world"), TEXT("Make this world"), Button, 0, TEXT("Leaves the world you are in and makes the one chosen here. Nothing of the present world is kept."));
		// How the world is generated (Thornwake's New world settings; WwWorldOptions): taken when the world is made.
		for (const ww::FGenSetting& G : ww::FWorldOptions::Catalog(ww::FWorldRecipe::Catalog(Profiles), Biomes))
		{
			const FString Key = UTF8_TO_TCHAR(G.Key.c_str()), Section = UTF8_TO_TCHAR(G.Section.c_str()), Name = UTF8_TO_TCHAR(G.Name.c_str()), Tip = UTF8_TO_TCHAR(G.Tip.c_str());
			if (!G.Options.empty())
			{
				TArray<FString> Options;
				for (const std::string& O : G.Options) Options.Add(UTF8_TO_TCHAR(O.c_str()));
				Choice(*Key, World, *Section, *Name, Options, (int32)G.Default, *Tip);
			}
			else if (G.bToggle) Add(*Key, World, *Section, *Name, Toggle, G.Default, *Tip);
			else Slider(*Key, World, *Section, *Name, G.Min, G.Max, G.Step, G.Default, UTF8_TO_TCHAR(G.Unit.c_str()), *Tip).AutoAt = G.AutoAt;
		}
	}

	bUseFile = !FParse::Param(FCommandLine::Get(), TEXT("unattended")) && !FParse::Param(FCommandLine::Get(), TEXT("WwNoSettingsFile"));
	Load();
	ReadSwitches();
}

void FWwSettings::Load()
{
	if (!bUseFile) return;
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *FilePath())) return;
	for (const FString& Line : Lines)
	{
		FString Key, Text;
		if (!Line.Split(TEXT("="), &Key, &Text)) continue;
		FWwSetting* S = FindMutable(FName(*Key.TrimStartAndEnd()));
		if (!S || !S->bSaved) continue;
		Text.TrimStartAndEndInline();
		if (S->Kind == EWwSettingKind::Choice)
		{
			// (choices are kept by their words, so a list that grows does not shift what was chosen)
			const int32 At = S->Options.IndexOfByKey(Text);
			if (At >= 0) S->Value = S->Stored = At;
		}
		else if (Text.IsNumeric()) S->Value = S->Stored = FMath::Clamp(FCString::Atod(*Text), S->Min, S->Max);
	}
}

void FWwSettings::ReadSwitches()
{
	// The test switches of the command line, where a setting now holds what the switch used to set directly. They win
	// over the file but are not written to it.
	const TCHAR* Cmd = FCommandLine::Get();
	auto Number = [&](const TCHAR* Switch, const TCHAR* Key) { double V; if (!FParse::Value(Cmd, Switch, V)) return false; if (FWwSetting* S = FindMutable(Key)) S->Value = V; return true; };
	auto Off = [&](const TCHAR* Switch, const TCHAR* Key) { if (FParse::Param(Cmd, Switch)) if (FWwSetting* S = FindMutable(Key)) S->Value = 0; };
	auto Word = [&](const TCHAR* Switch, const TCHAR* Key)
	{
		FString Text;
		if (!FParse::Value(Cmd, Switch, Text)) return;
		FWwSetting* S = FindMutable(Key);
		if (!S) return;
		int32 At = S->Options.IndexOfByPredicate([&](const FString& O) { return O.Equals(Text, ESearchCase::IgnoreCase) || (Text.IsNumeric() && O.IsNumeric() && FCString::Atod(*O) == FCString::Atod(*Text)); });
		if (At < 0 && Text.IsNumeric() && FCString::Atod(*Text) == 0) At = 0; // (-WwSizeKm=0: the type's own size)
		if (At >= 0) S->Value = At;
		else UE_LOG(LogWayfarer, Warning, TEXT("%s%s is not one of the choices; keeping %s"), Switch, *Text, *S->Options[FMath::RoundToInt32(S->Value)]);
	};
	Number(TEXT("WwHour="), TEXT("test.hour"));
	Number(TEXT("WwDayLength="), TEXT("time.daylength"));
	Number(TEXT("WwSeason="), TEXT("time.season"));
	const bool bRain = Number(TEXT("WwRain="), TEXT("test.rain")), bCloud = Number(TEXT("WwCloud="), TEXT("test.cloud"));
	if (bRain || bCloud)
	{
		FindMutable(TEXT("test.weather"))->Value = 1;
		if (!bRain) FindMutable(TEXT("test.rain"))->Value = 0;
		if (!bCloud) FindMutable(TEXT("test.cloud"))->Value = 0;
	}
	Number(TEXT("WwGrass="), TEXT("view.grass"));
	Number(TEXT("WwLevels="), TEXT("view.levels"));
	Number(TEXT("WwTreeLevels="), TEXT("view.trees"));
	Number(TEXT("WwFarTreeLevels="), TEXT("view.fartrees"));
	Off(TEXT("WwNoPlantShadows"), TEXT("view.plantshadows"));
	Off(TEXT("WwNoSunShadows"), TEXT("view.sunshadows"));
	Off(TEXT("WwNoStars"), TEXT("view.stars"));
	Off(TEXT("WwNoMoon"), TEXT("view.moon"));
	Off(TEXT("WwNoSound"), TEXT("sound.on"));
	Number(TEXT("WwBias="), TEXT("view.exposure"));
	Number(TEXT("WwSaturation="), TEXT("view.saturation"));
	Number(TEXT("WwWaterDebug="), TEXT("test.waterview"));
	Number(TEXT("WwSeed="), TEXT("world.seed"));
	Word(TEXT("WwType="), TEXT("world.type"));
	Word(TEXT("WwSizeKm="), TEXT("world.size"));
	// -WwGen=key=value;key=value: world generation settings (gen.rivers=2;gen.biome.desert=0 ...).
	FString Gen;
	if (FParse::Value(Cmd, TEXT("WwGen="), Gen, false))
		for (const auto& Entry : ww::FWorldOptions::Read(TCHAR_TO_UTF8(*Gen)))
			if (FWwSetting* S = FindMutable(FName(UTF8_TO_TCHAR(Entry.first.c_str())))) S->Value = Entry.second;
			else UE_LOG(LogWayfarer, Warning, TEXT("-WwGen: no setting '%s'"), UTF8_TO_TCHAR(Entry.first.c_str()));
}

FWwSetting* FWwSettings::FindMutable(FName Key)
{
	const int32* At = Index.Find(Key);
	return At ? &List[*At] : nullptr;
}

const FWwSetting* FWwSettings::Find(FName Key) const
{
	const int32* At = Index.Find(Key);
	return At ? &List[*At] : nullptr;
}

double FWwSettings::Num(FName Key) const
{
	const FWwSetting* S = Find(Key);
	ensureMsgf(S, TEXT("No setting called %s"), *Key.ToString());
	return S ? S->Value : 0.0;
}

FString FWwSettings::Option(FName Key) const
{
	const FWwSetting* S = Find(Key);
	return S && S->Options.IsValidIndex(FMath::RoundToInt32(S->Value)) ? S->Options[FMath::RoundToInt32(S->Value)] : FString();
}

void FWwSettings::Set(FName Key, double Value)
{
	FWwSetting* S = FindMutable(Key);
	if (!S) return;
	Value = FMath::Clamp(Value, S->Min, S->Max);
	if (S->Kind != EWwSettingKind::Slider) Value = FMath::RoundToDouble(Value);
	if (Value == S->Value && Value == S->Stored) return;
	S->Value = S->Stored = Value;
	S->Serial++;
	if (S->bSaved) { bDirty = true; DirtySince = FPlatformTime::Seconds(); }
}

void FWwSettings::Show(FName Key, double Value)
{
	if (FWwSetting* S = FindMutable(Key)) S->Value = Value;
}

void FWwSettings::Press(FName Key)
{
	if (FWwSetting* S = FindMutable(Key)) S->Serial++;
}

bool FWwSettings::Changed(FName Key, int32& Seen) const
{
	const FWwSetting* S = Find(Key);
	if (!S) return false;
	// (someone new, a fresh level's game mode or player, starts from now: what was pressed before is not theirs to act on)
	if (Seen < 0) { Seen = S->Serial; return false; }
	if (S->Serial == Seen) return false;
	Seen = S->Serial;
	return true;
}

void FWwSettings::ResetPage(const FString& Page)
{
	for (FWwSetting& S : List)
		if (S.Page == Page && S.bSaved && S.Kind != EWwSettingKind::Button && !S.Key.ToString().StartsWith(TEXT("world."))) Set(S.Key, S.Default);
}

FString FWwSettings::WorldRecord() const
{
	std::map<std::string, double> Values;
	for (const FWwSetting& S : List)
		if (S.Key.ToString().StartsWith(TEXT("gen.")) && S.Value != S.Default) Values[TCHAR_TO_UTF8(*S.Key.ToString())] = S.Value;
	return UTF8_TO_TCHAR(ww::FWorldOptions::Write(Values).c_str());
}

TArray<FString> FWwSettings::Pages() const
{
	TArray<FString> Out;
	for (const FWwSetting& S : List) Out.AddUnique(S.Page);
	return Out;
}

void FWwSettings::SaveIfDue()
{
	if (bDirty && FPlatformTime::Seconds() - DirtySince > 0.5) Save();
}

void FWwSettings::Save()
{
	bDirty = false;
	if (!bUseFile) return;
	FString Text = TEXT("; Wayfarer's Wake: what was chosen in the Esc menu. Delete this file to go back to the defaults.\r\n");
	for (const FWwSetting& S : List)
	{
		if (!S.bSaved || S.Stored == S.Default) continue;
		if (S.Kind == EWwSettingKind::Choice) Text += FString::Printf(TEXT("%s=%s\r\n"), *S.Key.ToString(), *S.Options[FMath::Clamp(FMath::RoundToInt32(S.Stored), 0, S.Options.Num() - 1)]);
		else Text += FString::Printf(TEXT("%s=%s\r\n"), *S.Key.ToString(), *FString::SanitizeFloat(S.Stored));
	}
	if (!FFileHelper::SaveStringToFile(Text, *FilePath())) UE_LOG(LogWayfarer, Warning, TEXT("Could not write %s"), *FilePath());
}
