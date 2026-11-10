// Checks of the game rules (Source/WayfarersWake/Rules): Thornwake's own gameplay tests (Tests/WorldGen/GameplayTests.cs:
// content, inventory, the felling-to-eating slice, needs, gathering, death), ported.   WorldGenTest <repo root> --rules
// Left out with what they test: the network copy of an inventory, blueprints (a campfire is put down directly here), the
// lean-to, fishing, saves, hunting, building.
#include "../../Source/WayfarersWake/Rules/WrRules.h"
#include <cstdio>
#include <fstream>
#include <sstream>

using namespace wr;

namespace
{
int Failed = 0, Checks = 0;
void Check(bool ok, const std::string& what) { Checks++; if (!ok) { Failed++; std::printf("  FAIL %s\n", what.c_str()); } }
std::string Read(const std::string& path) { std::ifstream f(path, std::ios::binary); std::stringstream s; s << f.rdbuf(); return s.str(); }

struct FWorld : IWorldQuery
{
	FObjectRemovals Gone;
	double Now = 10, WindX = 1, WindZ = 0, WindSpeed = 0;
	bool Tree(long long cx, long long cz, double& x, double& z, double& scale, int& species) override
	{
		if (cx != 3 || cz != 4) return false;
		x = 21.5; z = 29.0; scale = 1.1; species = 5;
		return true;
	}
	FObjectRemovals& Removals() override { return Gone; }
	double Hours() override { return Now; }
	void Wind(double& x, double& z, double& speed) override { x = WindX; z = WindZ; speed = WindSpeed; }
	double Ground(double, double) override { return 5; }
	std::string WaterNear(double x, double, double) override { return x > 1000 ? "river" : ""; }
	void GatherSpot(int, long long cx, long long cz, double& x, double& z) override { x = cx * 3.5 + 1; z = cz * 3.5 + 1; }
};

const FPlacedObject* Only(FGameRules& rules, const char* kind)
{
	const FPlacedObject* found = nullptr; int n = 0;
	for (const auto& o : rules.Objects.All()) if (o.second.Kind == kind) { found = &o.second; n++; }
	return n == 1 ? found : nullptr;
}
}

int RulesTest(const std::string& root)
{
	FCatalog c(Read(root + "/Content/Gameplay/gameplay_items.json"), Read(root + "/Content/Gameplay/gameplay_recipes.json"));

	std::printf("Content\n");
	Check(c.Items.count("stone_axe") && c.Item("stone_axe").Has("chopping"), "the axe chops");
	Check(c.Objects.count("campfire") && c.Objects.at("campfire").Tags.count("fire"), "a campfire is a fire");
	Check(!c.Start.empty(), "a start kit");
	Check(c.Fish.size() >= 6 && c.Recipes.size() >= 10, "fish and recipes load");
	Check(c.Error.empty(), "the content files load and every reference resolves: " + c.Error);
	FCatalog bad("{\"items\":[]}", "{\"recipes\":[{\"id\":\"x\",\"inputs\":[{\"item\":\"nothing\"}]}]}");
	Check(!bad.Error.empty(), "a recipe naming an unknown item is refused when loading");

	std::printf("Inventory\n");
	{
		FInventory inv(c);
		inv.Add("branch", 45); // stacks of 30
		Check(inv.Stacks().size() == 2 && inv.Count("branch") == 45, "45 branches in two stacks");
		inv.Add("log", 3);
		Check(inv.CountTag("fuel") == 48 && inv.CountTag("heavy") == 3, "counted by tag");
		Check(std::fabs(inv.Weight() - (45 * .8 + 3 * 8)) < 1e-9, "weight");
		std::vector<FStack> taken; int sum = 0;
		bool ok = inv.Take(FIngredient{ "", "wood", 47 }, &taken);
		for (const FStack& t : taken) sum += t.second;
		Check(ok && sum == 47, "47 wood taken (any kind)");
		Check(inv.CountTag("wood") == 1, "one left");
		Check(!inv.Take(FIngredient{ "log", "", 5 }) && inv.CountTag("wood") == 1, "too few: nothing taken");
	}

	std::printf("Felling, a fire, cooking, eating\n");
	{
		FWorld world;
		FGameRules rules(c, world);
		const int me = 7, other = 9;
		FInventory& inv = rules.Inventory(me);
		Check(inv.Count("stone_axe") == 1, "starts with an axe");
		Check(!rules.Chop(me, 3, 4, 40, 40).bOk, "too far from the tree");
		Check(!rules.Chop(me, 5, 5, 21, 29).bOk, "no tree there");
		Check(rules.Inventory(other).HasTag("chopping"), "every new character has an axe");
		int hits = FGameRules::TreeHits(1.1);
		bool standing = true;
		for (int i = 1; i < hits; i++) standing = standing && rules.Chop(me, 3, 4, 21, 28).bOk && !world.Gone.Contains(FObjectRemovals::Trees, 3, 4);
		Check(standing, "the tree stands until the last hit");
		std::vector<FTreeEvent> events = rules.TakeTreeEvents();
		bool allStanding = true;
		for (const FTreeEvent& e : events) allStanding = allStanding && !e.bFelled && e.Total == hits;
		Check((int)events.size() == hits - 1 && allStanding && events.back().HitsLeft == 1, "a hit event each, the last with one hit left");
		Check(events[0].DirZ < -.8 && events[0].DirX < 0, "the notch faces me");
		FActionResult chop = rules.Chop(me, 3, 4, 22, 30); // the last hit from the other side
		Check(chop.bOk && world.Gone.Contains(FObjectRemovals::Trees, 3, 4), "felled: " + chop.Message);
		double when = 0;
		Check(world.Gone.Get(FObjectRemovals::Trees, 3, 4, when) && when == 10, "with the time it fell");
		std::vector<FTreeEvent> fall = rules.TakeTreeEvents();
		Check(fall.size() == 1 && fall[0].bFelled && std::fabs(fall[0].DirX - events[0].DirX) < 1e-9 && std::fabs(fall[0].DirZ - events[0].DirZ) < 1e-9, "it falls toward the notch");
		Check(!rules.Chop(me, 3, 4, 21, 28).bOk, "only once");
		const FPlacedObject* trunk = Only(rules, "trunk");
		Check(trunk && trunk->Owner == me && trunk->Z < 29 && trunk->Size == 1.1f && trunk->Variant == 5, "a trunk lies toward the fall, mine to move");
		const FPlacedObject* crown = Only(rules, "branch_pile");
		Check(crown && crown->Contents.size() == 1 && crown->Contents[0].second == FGameRules::CrownBranches(1.1), "branches where the crown came down");
		Check(inv.Count("log") == 0, "nothing in the pack yet");
		Check(FGameRules::TrunkLogs(.8) < FGameRules::TrunkLogs(1.1) && FGameRules::TrunkLogs(1.1) < FGameRules::TrunkLogs(1.6), "logs by size");
		Check(FGameRules::TreeHits(.8) < FGameRules::TreeHits(1.6), "and the effort");
		long long trunkId = trunk->Id, crownId = crown->Id;
		double x0 = trunk->X;
		bool rolled = rules.Objects.Move(me, trunkId, x0 + 1, trunk->Y, trunk->Z, 0, 0, 0, 1) && trunk->X == x0 + 1;
		std::vector<long long> moved = rules.Objects.TakeMoved();
		Check(rolled && std::find(moved.begin(), moved.end(), trunkId) != moved.end(), "it rolled a metre (my machine moves it; passed on)");
		Check(!rules.Objects.Move(other, trunkId, 0, 0, 0, 0, 0, 0, 1), "a report from another is ignored");
		Check(!rules.Claim(other, trunkId, 200, 200), "not from afar");
		Check(rules.Claim(other, trunkId, trunk->X + 1, trunk->Z) && trunk->Owner == other, "taken over by the player pushing it");
		rules.Release(other);
		Check(trunk->Owner == 0, "a player leaving lets it go");
		int cuts = FGameRules::TrunkHits(1.1);
		bool cutting = true;
		for (int i = 1; i < cuts; i++) cutting = cutting && rules.ChopTrunk(me, trunkId, trunk->X, trunk->Z - 1).bOk && rules.Objects.Get(trunkId) != nullptr;
		Check(cutting, "cut hits");
		Check(!rules.ChopTrunk(me, trunkId, 200, 200).bOk, "not from afar");
		double tx = trunk->X, tz = trunk->Z;
		FActionResult split = rules.ChopTrunk(me, trunkId, tx, tz - 1);
		const FPlacedObject* pile = Only(rules, "log_pile");
		Check(split.bOk && rules.Objects.Get(trunkId) == nullptr && pile && pile->Contents.size() == 1 && pile->Contents[0] == FStack("log", FGameRules::TrunkLogs(1.1)), "split: " + split.Message);
		long long pileId = pile ? pile->Id : 0; double pileX = pile ? pile->X : 0, pileZ = pile ? pile->Z : 0;
		Check(!rules.Gather(me, pileId, 21, 100).bOk, "too far to pick it up");
		FActionResult gather = rules.Gather(me, pileId, pileX, pileZ - 2);
		Check(gather.bOk && inv.Count("log") == FGameRules::TrunkLogs(1.1) && rules.Objects.Get(pileId) == nullptr, "picked up: " + gather.Message);
		const FPlacedObject* crownNow = rules.Objects.Get(crownId);
		Check(crownNow && rules.Gather(me, crownId, crownNow->X, crownNow->Z - 1).bOk && inv.Count("branch") >= 4, "and the branches");
		std::vector<int> changed = rules.TakeInventoryChanges();
		Check(std::find(changed.begin(), changed.end(), me) != changed.end(), "my inventory is sent to me");
		rules.Objects.TakeChanged();
		// A strong wind has its say: notched from the south, in a 10 m/s west wind it falls north-east.
		FWorld windy; windy.WindSpeed = 10;
		FGameRules rules2(c, windy);
		for (int i = 0; i < hits; i++) rules2.Chop(me, 3, 4, 21.5, 26);
		std::vector<FTreeEvent> windFall = rules2.TakeTreeEvents();
		Check(!windFall.empty() && windFall.back().bFelled && windFall.back().DirX > .3 && windFall.back().DirZ < -.5, "in the wind it falls partly downwind");

		Check(!rules.Make(me, "cook_fish", 21, 28, 0, 0, 0, 0).bOk, "no fish, no fire: cannot cook");
		// (Thornwake builds the campfire as a blueprint piece; building is not ported, so it is put down directly)
		long long fireId = rules.Objects.Add("campfire", 22, 5, 29.5, 0, c.Objects.at("campfire").Fuel).Id;
		Check(rules.Objects.Get(fireId)->Lit(), "a lit campfire");
		Check(FGameRules::FireWarmth(c, rules.Objects, 22, 29.5) > 10 && FGameRules::FireWarmth(c, rules.Objects, 40, 40) == 0, "warm beside it, not far away");
		inv.Add("raw_fish", 2);
		Check(rules.StationNear("fire", 21, 28), "the fire is a working station");
		FActionResult cook = rules.Make(me, "cook_fish", 21, 28, 0, 0, 0, 0);
		Check(cook.bOk && inv.Count("cooked_fish") == 1 && inv.Count("raw_fish") == 1, "cooked: " + cook.Message);
		Check(!rules.Make(me, "cook_fish", 35, 35, 0, 0, 0, 0).bOk, "not away from the fire");
		rules.Objects.TakeChanged();
		rules.Step(rules.Objects.Get(fireId)->Fuel + 1);
		std::vector<long long> out = rules.Objects.TakeChanged();
		Check(!rules.Objects.Get(fireId)->Lit() && std::find(out.begin(), out.end(), fireId) != out.end(), "it burns out (and everyone sees it)");
		Check(!rules.Make(me, "cook_fish", 21, 28, 0, 0, 0, 0).bOk, "a cold fire cooks nothing");
		inv.Add("log", 1);
		FActionResult fuel = rules.Fuel(me, fireId, 21, 28);
		Check(fuel.bOk && rules.Objects.Get(fireId)->Lit() && rules.Objects.Get(fireId)->Fuel == c.Item("log").Fuel, "a log relights it: " + fuel.Message);
		Check(!rules.Fuel(me, fireId, 40, 40).bOk, "not from afar");
		FActionResult eat = rules.Eat(me, "cooked_fish");
		Check(eat.bOk && eat.Food == c.Item("cooked_fish").Food && inv.Count("cooked_fish") == 0, "eaten: " + eat.Message);
		Check(!rules.Eat(me, "log").bOk, "logs are not food");
	}

	std::printf("Gathering\n");
	{
		FWorld world;
		FGameRules rules(c, world);
		const int me = 7, other = 9;
		FInventory& inv = rules.Inventory(me);
		Check(!rules.Forage(me, "stone", 10, 10, 0, 0, 0, 0).bOk, "a stone 50 m away: too far");
		Check(rules.Forage(me, "stone", 10, 10, 0, 0, 36, 36).bOk && inv.Count("stone") == 1, "picked up a stone");
		Check(world.Gone.Contains(FObjectRemovals::Stones, 10, 10), "it is gone from the world (for everyone)");
		Check(!rules.Forage(other, "stone", 10, 10, 0, 0, 36, 36).bOk, "nobody picks it up twice");
		Check(rules.Forage(me, "berries", 2, 3, 0, 0, 8, 11).bOk && inv.Count("berries") == 4, "picked berries");
		Check(!rules.Forage(me, "berries", 2, 3, 0, 0, 8, 11).bOk, "the bush is picked");
		world.Now += 47; rules.Step(30);
		Check(world.Gone.Contains(FObjectRemovals::Berries, 2, 3), "47 hours later: still bare");
		world.Now += 2; rules.Step(30);
		Check(!world.Gone.Contains(FObjectRemovals::Berries, 2, 3) && world.Gone.Contains(FObjectRemovals::Stones, 10, 10), "two days later the berries are back (the stone is not)");
		Check(rules.Eat(me, "berries").bOk && inv.Count("berries") == 3, "berries are food");
		Check(!rules.Forage(me, "grass", 0, 0, 50, 50, 0, 0).bOk, "grass out of reach");
		Check(!rules.Forage(me, "grass", 0, 0, 6, 6, 6, 6, 8.5).bOk, "not from a platform 3.5 m above the ground");
		for (int i = 0; i < 2; i++) Check(rules.Forage(me, "grass", 0, 0, 1 + 3 * i, 1, 1 + 3 * i, 1).bOk, "grass pulled");
		Check(inv.Count("plant_fibre") == 4, "4 plant fibre");
		Check(world.Gone.Contains(FObjectRemovals::Grass, 1, 1) && world.Gone.Contains(FObjectRemovals::Grass, 0, 0) && world.Gone.Contains(FObjectRemovals::Grass, 2, 2), "the grass around the spot is gone (a 3 x 3 m patch)");
		Check(!rules.Forage(me, "grass", 0, 0, 1, 1, 1, 1).bOk, "nothing left to pull there");
		world.Now += 73; rules.Step(30);
		Check(!world.Gone.Contains(FObjectRemovals::Grass, 1, 1), "three days later it has grown back");
		inv.Add("branch", 1);
		FActionResult rod = rules.Make(me, "fishing_rod", 0, 0, 0, 0, 0, 0);
		Check(rod.bOk && inv.HasTag("fishing") && inv.Count("plant_fibre") == 0, "a fishing rod: " + rod.Message);
	}

	std::printf("Death\n");
	{
		FWorld world;
		FGameRules rules(c, world);
		FInventory& inv = rules.Inventory(7);
		inv.Add("log", 3); inv.Add("berries", 5);
		int axes = inv.Count("stone_axe");
		FActionResult died = rules.Die(7, 40, 41);
		const FPlacedObject* pack = Only(rules, "pack");
		bool logs = false, berries = false;
		if (pack) for (const FStack& s : pack->Contents) { logs = logs || s == FStack("log", 3); berries = berries || s == FStack("berries", 5); }
		Check(died.bOk && pack && pack->X == 40 && pack->Z == 41 && logs && berries, "the pack lies where I fell: " + died.Message);
		Check(inv.Count("log") == 0 && inv.Count("stone_axe") == axes, "a new character with the start kit");
		Check(pack && rules.Gather(9, pack->Id, 40, 42).bOk && rules.Inventory(9).Count("log") == 3, "anyone can pick it up");
		FBodyInput rainyNight{ 2, 7, .9, false, 0, 0 }, under{ 2, 7 * .3, 0, false, 9, 0 };
		FNeeds open, snug;
		open.Warmth = 70; snug.Warmth = 70;
		for (int i = 0; i < 16; i++) { open.Step(.5, rainyNight); snug.Step(.5, under); }
		Check(open.Health < 100 && snug.Health >= 99 && snug.Warmth > 45, "eight hours of cold rain: out in it it hurts; sheltered by a fire it does not");
	}

	std::printf("Needs\n");
	{
		FBodyInput mild{ 18, 1, 0, false, 0, 0 };
		FNeeds n;
		for (int i = 0; i < 24; i++) n.Step(1, mild);
		Check(n.Food < 80 - 40 && n.Food > 80 - 60, "a day at rest: food between 20 and 40");
		Check(n.Warmth > 70 && n.Health >= 99, "mild air: warm, healthy");
		FBodyInput night{ 1, 6, .8, false, 0, 0 };
		FNeeds cold;
		for (int i = 0; i < 8; i++) cold.Step(.5, night);
		Check(cold.Warmth < 25 && cold.Wet > 80, "four hours of cold rain and wind: cold and wet");
		Check(cold.Health < 100, "and it hurts");
		FBodyInput fire = night; fire.Rain = 0; fire.Fire = 14;
		double before = cold.Warmth;
		for (int i = 0; i < 4; i++) cold.Step(.5, fire);
		Check(cold.Warmth > before + 20 && cold.Wet < 80, "two hours by a fire: warmer, drying");
		FNeeds hungry; hungry.Food = 0;
		hungry.Step(1, mild);
		Check(hungry.Health < 100, "starving hurts");
		hungry.Eat(35);
		Check(hungry.Food == 35, "eating fills");
		double winterNight = FNeeds::AirTemperature(9, 50, 3, 4, 0), summerDay = FNeeds::AirTemperature(9, 50, 1, 15, 0);
		Check(winterNight < 0 && summerDay > 20, "air: a winter night below zero, a summer afternoon above 20 (yearly mean 9, 50 degrees north)");
		Check(FNeeds::AirTemperature(26, 5, 3, 15, 0) > 25, "the tropics stay warm in winter");
		std::printf("     %s\n", n.Describe().c_str());
	}
	std::printf("%d checks, %d failed\n", Checks, Failed);
	return Failed;
}
