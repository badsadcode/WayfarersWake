#include "Game/WwGameplay.h"
#include "Game/WwWorldSubsystem.h"
#include "WayfarersWake.h"
#include "WorldGen/WwVegetation.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
FString WwText(const std::string& Text) { return FString(UTF8_TO_TCHAR(Text.c_str())); }
}

FWwGameplay::FWwGameplay(std::shared_ptr<ww::FTerrainSampler> InSampler, const FString& ContentFolder) : Sampler(InSampler)
{
	FString Items, Recipes;
	if (!FFileHelper::LoadFileToString(Items, *(ContentFolder / TEXT("gameplay_items.json"))) || !FFileHelper::LoadFileToString(Recipes, *(ContentFolder / TEXT("gameplay_recipes.json"))))
	{
		Error = FString::Printf(TEXT("The game's item and recipe files are missing from %s"), *ContentFolder);
		UE_LOG(LogWayfarer, Error, TEXT("%s"), *Error);
		return;
	}
	Catalog = std::make_unique<wr::FCatalog>(std::string(TCHAR_TO_UTF8(*Items)), std::string(TCHAR_TO_UTF8(*Recipes)));
	if (!Catalog->Error.empty())
	{
		Error = WwText(Catalog->Error);
		UE_LOG(LogWayfarer, Error, TEXT("Game content: %s"), *Error);
		return;
	}
	Rules = std::make_unique<wr::FGameRules>(*Catalog, *this);
	Rules->Inventory(Player); // (the start kit)
	UE_LOG(LogWayfarer, Display, TEXT("Game rules ready: %d items, %d recipes"), (int32)Catalog->Items.size(), (int32)Catalog->Recipes.size());
}

// ---------------------------------------------------------------------------------------------------- world queries
bool FWwGameplay::Tree(long long cx, long long cz, double& x, double& z, double& scale, int& species)
{
	ww::FLatticeTree T;
	if (!Sampler || !ww::FVegetation::TreeAt(*Sampler, cx, cz, T)) return false;
	x = T.X; z = T.Z; scale = T.Scale; species = T.Species;
	return true;
}

void FWwGameplay::Wind(double& x, double& z, double& speed) { x = UWwWorldSubsystem::WindX; z = UWwWorldSubsystem::WindZ; speed = UWwWorldSubsystem::WindSpeed; }
double FWwGameplay::Ground(double x, double z) { return Sampler ? Sampler->Height(x, z) : 0; }

std::string FWwGameplay::WaterNear(double x, double z, double radius)
{
	// The nearest water on rings around the point (every 3 m or so).
	for (double r = 0; Sampler && r <= radius; r += 3)
		for (int32 k = 0; k < (r == 0 ? 1 : 12); k++)
		{
			const double a = k * UE_DOUBLE_TWO_PI / 12;
			switch (Sampler->Sample(x + FMath::Cos(a) * r, z + FMath::Sin(a) * r).Water)
			{
			case ww::EWaterKind::Ocean: return "ocean";
			case ww::EWaterKind::Lake: return "lake";
			case ww::EWaterKind::River: return "river";
			case ww::EWaterKind::Pond: return "pond";
			default: break;
			}
		}
	return "";
}

void FWwGameplay::GatherSpot(int, long long cx, long long cz, double& x, double& z)
{
	// (loose stones and berry bushes are not marked in the ground cover yet: nothing asks for them)
	x = cx * 3.5; z = cz * 3.5;
}

// ---------------------------------------------------------------------------------------------------- every frame
void FWwGameplay::Tick(double GameHours, double HourOfDay, double X, double Z, double FeetY, bool bSwimming, double Effort)
{
	if (!IsValid() || GameHours <= 0) return;
	GameTime += GameHours;
	Rules->Step(GameHours * 60);
	// What the body feels: the air (the place's climate and the hour), the wind, water, fires near by. No rain yet.
	wr::FBodyInput Body;
	double Temperature = 12, Rain = 0;
	const ww::FWorldPlan& Plan = *Sampler->Plan;
	Plan.Climate->At(X, Z, Ground(X, Z) - Plan.Profile().SeaLevel, Temperature, Rain);
	Body.Air = wr::FNeeds::AirTemperature(Temperature, 45, 0, HourOfDay, 0);
	Body.Wind = UWwWorldSubsystem::WindSpeed;
	Body.bSwimming = bSwimming;
	Body.Fire = wr::FGameRules::FireWarmth(*Catalog, Rules->Objects, X, Z);
	Body.Effort = Effort;
	Needs.Step(GameHours, Body);
	if (Needs.Health <= 0)
	{
		Rules->Die(Player, X, Z);
		Needs = wr::FNeeds();
		bDied = true;
	}
}

// --------------------------------------------------------------------------------------------------------- actions
FString FWwGameplay::Tell(const wr::FActionResult& Result) { return WwText(Result.Message); }

FString FWwGameplay::Use(double X, double Z, double FeetY)
{
	if (!IsValid()) return FString();
	// What lies nearest within reach (a pile of logs or branches, a dropped pack).
	const wr::FPlacedObject* Nearest = nullptr;
	double Best = wr::FGameRules::UseReach + 1.5;
	for (const auto& Entry : Rules->Objects.All())
	{
		const wr::FPlacedObject& O = Entry.second;
		const double D = FMath::Sqrt(FMath::Square(O.X - X) + FMath::Square(O.Z - Z));
		if (!O.Contents.empty() && D <= Best) { Best = D; Nearest = &O; }
	}
	if (Nearest) return Tell(Rules->Gather(Player, Nearest->Id, X, Z));
	if (Sampler->Sample(X, Z).Water != ww::EWaterKind::None) return TEXT("Nothing to gather in the water.");
	return Tell(Rules->Forage(Player, "grass", 0, 0, X, Z, X, Z, FeetY));
}

FString FWwGameplay::Strike(double X, double Z, double DirX, double DirZ)
{
	if (!IsValid()) return FString();
	// A fallen trunk within reach comes first (it is what the axe is for once the tree is down).
	for (const auto& Entry : Rules->Objects.All())
	{
		const wr::FPlacedObject& O = Entry.second;
		if (O.Kind == "trunk" && FMath::Sqrt(FMath::Square(O.X - X) + FMath::Square(O.Z - Z)) <= wr::FGameRules::TrunkLength(O.Size) / 2 + 2.5)
			return Tell(Rules->ChopTrunk(Player, O.Id, X, Z));
	}
	// The nearest standing tree in front, within the axe's reach.
	const double Cell = ww::FVegetation::LatticeCell(*Sampler), Around = wr::FGameRules::ChopReach + Cell;
	bool bFound = false; long long BestX = 0, BestZ = 0; double Best = wr::FGameRules::ChopReach;
	for (long long cz = (long long)FMath::FloorToDouble((Z - Around) / Cell); cz <= (long long)FMath::FloorToDouble((Z + Around) / Cell); cz++)
		for (long long cx = (long long)FMath::FloorToDouble((X - Around) / Cell); cx <= (long long)FMath::FloorToDouble((X + Around) / Cell); cx++)
		{
			double Tx, Tz, Scale; int Species;
			if (Gone.Contains(wr::FObjectRemovals::Trees, cx, cz) || !Tree(cx, cz, Tx, Tz, Scale, Species)) continue;
			const double Dx = Tx - X, Dz = Tz - Z, D = FMath::Sqrt(Dx * Dx + Dz * Dz);
			if (D > Best || (D > 0.8 && (Dx * DirX + Dz * DirZ) / D < 0.3)) continue; // (out of reach, or not in front)
			Best = D; BestX = cx; BestZ = cz; bFound = true;
		}
	if (!bFound) return TEXT("No tree within reach (stand close and face it).");
	const wr::FActionResult Result = Rules->Chop(Player, BestX, BestZ, X, Z);
	FString Message = Tell(Result);
	for (const wr::FTreeEvent& Event : Rules->TakeTreeEvents())
	{
		if (Event.bFelled) Felled.Add(TPair<int64, int64>(Event.Cx, Event.Cz));
		else if (Message.IsEmpty()) Message = FString::Printf(TEXT("Chop (%d of %d)"), Event.Total - Event.HitsLeft, Event.Total);
	}
	return Message;
}

FString FWwGameplay::Make(int32 Index, double X, double Z)
{
	if (!IsValid() || Index < 0 || Index >= (int32)Catalog->RecipeOrder.size()) return FString();
	return Tell(Rules->Make(Player, Catalog->RecipeOrder[Index], X, Z, X, Ground(X, Z), Z, 0));
}

FString FWwGameplay::Eat()
{
	if (!IsValid()) return FString();
	const wr::FItemDef* Best = nullptr;
	for (const wr::FStack& S : Rules->Inventory(Player).Stacks())
	{
		const wr::FItemDef& D = Catalog->Item(S.first);
		if (D.Has("food") && (!Best || D.Food > Best->Food)) Best = &D;
	}
	if (!Best) return TEXT("Nothing to eat in the pack.");
	const wr::FActionResult Result = Rules->Eat(Player, Best->Id);
	if (Result.bOk) Needs.Eat(Result.Food);
	return Tell(Result);
}

bool FWwGameplay::TakeObjectsChanged() { return IsValid() && !Rules->Objects.TakeChanged().empty(); }

FString FWwGameplay::NeedsLine() const { return IsValid() ? WwText(Needs.Describe()) : FString(); }

TArray<FString> FWwGameplay::PackLines(double X, double Z)
{
	TArray<FString> Lines;
	if (!IsValid()) { Lines.Add(Error); return Lines; }
	wr::FInventory& Inv = Rules->Inventory(Player);
	Lines.Add(FString::Printf(TEXT("PACK  (%.1f kg)"), Inv.Weight()));
	for (const wr::FStack& S : Inv.Stacks()) Lines.Add(FString::Printf(TEXT("  %d x %s"), S.second, *WwText(Catalog->Item(S.first).Name)));
	if (Inv.Stacks().empty()) Lines.Add(TEXT("  (empty)"));
	Lines.Add(TEXT(""));
	Lines.Add(TEXT("MAKE  (press the number)"));
	for (int32 i = 0; i < (int32)Catalog->RecipeOrder.size() && i < 10; i++)
	{
		const wr::FRecipeDef& R = Catalog->Recipes.at(Catalog->RecipeOrder[i]);
		const std::string Why = Rules->Missing(Inv, R, X, Z);
		Lines.Add(FString::Printf(TEXT("  %d  %s%s"), (i + 1) % 10, *WwText(R.Name), Why.empty() ? TEXT("") : *(TEXT("   - ") + WwText(Why))));
	}
	return Lines;
}
