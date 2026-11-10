// Ports of Thornwake Game/Gameplay (Catalog, Inventory, Needs, PlacedObjects, GameRules) and WorldGen/ObjectRemovals.
#include "WrRules.h"
#include <cstdarg>
#include <cstdio>

namespace wr
{
namespace
{
const double WrPi = 3.14159265358979323846;
double WrDist(double ax, double az, double bx, double bz) { return std::sqrt((ax - bx) * (ax - bx) + (az - bz) * (az - bz)); }
std::string WrLower(std::string s) { for (char& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a'); return s; }
std::string WrText(const char* format, ...)
{
	char buffer[512];
	va_list args; va_start(args, format);
	std::vsnprintf(buffer, sizeof buffer, format, args);
	va_end(args);
	return buffer;
}
std::set<std::string> WrStrings(const FJson& e, const char* name)
{
	std::set<std::string> out;
	if (const FJson* t = e.Find(name)) for (const FJson& x : t->Items) out.insert(x.Str);
	return out;
}
FIngredient WrIngredient(const FJson& e) { return { e.StringOr("item", ""), e.StringOr("tag", ""), (int)e.NumberOr("count", 1) }; }
FActionResult WrFail(const std::string& message) { return { false, message, 0 }; }
}

// ------------------------------------------------------------------------------------------------------- catalogue
FCatalog::FCatalog(const std::string& itemsJson, const std::string& recipesJson)
{
	// (no exceptions: the engine builds without them. The first problem found is kept in Error.)
	auto Fail = [&](const std::string& why) { if (Error.empty()) Error = why; };
	std::string error;
	FJson items, recipes;
	if (!FJson::Parse(itemsJson, items, error)) { Fail("items: " + error); return; }
	if (!FJson::Parse(recipesJson, recipes, error)) { Fail("recipes: " + error); return; }
	auto Check = [&](const FIngredient& x)
	{
		if (!x.Item.empty() && !Items.count(x.Item)) Fail("unknown item " + x.Item);
		if (x.Item.empty() && x.Tag.empty()) Fail("an ingredient needs an item or a tag");
		if (x.Count < 1) Fail("an ingredient's count must be at least 1");
	};
	const FJson* list = items.Find("items");
	if (!list) { Fail("items: no item list"); return; }
	for (const FJson& e : list->Items)
	{
		FItemDef d;
		d.Id = e.StringOr("id", ""); d.Name = e.StringOr("name", d.Id); d.Tags = WrStrings(e, "tags");
		d.Weight = e.NumberOr("weight", 0); d.Stack = (int)e.NumberOr("stack", 1); d.Food = e.NumberOr("food", 0); d.Fuel = e.NumberOr("fuel", 0);
		d.Description = e.StringOr("description", "");
		if (const FJson* props = e.Find("props")) for (const auto& p : props->Members) d.Props[p.first] = p.second.Num;
		Items[d.Id] = d;
	}
	if (const FJson* start = items.Find("start")) for (const FJson& e : start->Items) Start.push_back(WrIngredient(e));
	if (const FJson* fish = items.Find("fish"))
		for (const FJson& f : fish->Items)
		{
			FFishDef d;
			d.Id = f.StringOr("id", ""); d.Name = f.StringOr("name", d.Id); d.Waters = WrStrings(f, "waters");
			d.Stamina = f.NumberOr("stamina", 6); d.Pull = f.NumberOr("pull", 1); d.bDusk = f.BoolOr("dusk", false);
			if (const FJson* w = f.Find("weight"); w && !w->Items.empty()) { d.MinKg = w->Items.front().Num; d.MaxKg = w->Items.back().Num; }
			Fish.push_back(d);
		}
	if (const FJson* objects = recipes.Find("objects"))
		for (const FJson& e : objects->Items)
		{
			FObjectDef o;
			o.Id = e.StringOr("id", ""); o.Name = e.StringOr("name", o.Id); o.Tags = WrStrings(e, "tags");
			o.Fuel = e.NumberOr("fuel", 0); o.MaxFuel = e.NumberOr("max_fuel", 0); o.Warmth = e.NumberOr("warmth", 0); o.Reach = e.NumberOr("reach", 3);
			Objects[o.Id] = o;
		}
	const FJson* made = recipes.Find("recipes");
	if (!made) { Fail("recipes: no recipe list"); return; }
	for (const FJson& e : made->Items)
	{
		FRecipeDef r;
		r.Id = e.StringOr("id", ""); r.Name = e.StringOr("name", r.Id); r.Station = e.StringOr("station", ""); r.Places = e.StringOr("places", "");
		r.Description = e.StringOr("description", ""); r.Seconds = e.NumberOr("seconds", 1);
		if (const FJson* i = e.Find("inputs")) for (const FJson& x : i->Items) r.Inputs.push_back(WrIngredient(x));
		if (const FJson* o = e.Find("outputs")) for (const FJson& x : o->Items) r.Outputs.push_back(WrIngredient(x));
		if (const FJson* t = e.Find("tools")) for (const FJson& x : t->Items) r.Tools.push_back(x.Str);
		for (const FIngredient& x : r.Inputs) Check(x);
		for (const FIngredient& x : r.Outputs) { Check(x); if (x.Item.empty()) Fail("recipe " + r.Id + ": outputs must name items"); }
		if (!r.Places.empty() && !Objects.count(r.Places)) Fail("recipe " + r.Id + ": unknown object " + r.Places);
		Recipes[r.Id] = r;
		RecipeOrder.push_back(r.Id);
	}
	for (const FIngredient& s : Start) Check(s);
}

const FItemDef& FCatalog::Item(const std::string& id) const
{
	static const FItemDef Unknown;
	auto it = Items.find(id);
	return it != Items.end() ? it->second : Unknown; // (an unknown id: an empty item with no tags)
}

// ------------------------------------------------------------------------------------------------------- inventory
double FInventory::Weight() const { double w = 0; for (const FStack& s : Items) w += Catalog->Item(s.first).Weight * s.second; return w; }
int FInventory::Count(const std::string& item) const { int n = 0; for (const FStack& s : Items) if (s.first == item) n += s.second; return n; }
int FInventory::CountTag(const std::string& tag) const { int n = 0; for (const FStack& s : Items) if (Catalog->Item(s.first).Has(tag)) n += s.second; return n; }
int FInventory::Count(const FIngredient& x) const { int n = 0; for (const FStack& s : Items) if (x.Matches(Catalog->Item(s.first))) n += s.second; return n; }

void FInventory::Add(const std::string& item, int count)
{
	int stack = ww::MxI(1, Catalog->Item(item).Stack);
	for (size_t i = 0; i < Items.size() && count > 0; i++)
	{
		if (Items[i].first != item || Items[i].second >= stack) continue;
		int put = ww::MnI(count, stack - Items[i].second);
		Items[i].second += put;
		count -= put;
	}
	while (count > 0) { int put = ww::MnI(count, stack); Items.push_back({ item, put }); count -= put; }
}

bool FInventory::Take(const FIngredient& x, std::vector<FStack>* taken)
{
	if (Count(x) < x.Count) return false;
	int left = x.Count;
	for (int i = (int)Items.size() - 1; i >= 0 && left > 0; i--)
	{
		if (!x.Matches(Catalog->Item(Items[i].first))) continue;
		int t = ww::MnI(Items[i].second, left);
		left -= t;
		if (taken) taken->push_back({ Items[i].first, t });
		if (t == Items[i].second) Items.erase(Items.begin() + i); else Items[i].second -= t;
	}
	return true;
}

// ----------------------------------------------------------------------------------------------------------- needs
double FNeeds::FeelsLike(const FBodyInput& b) const { return b.Air + b.Fire - ww::Mn(b.Wind, 20) * .55 - Wet / 100 * 7; }

double FNeeds::AirTemperature(double yearlyMean, double latitude, int season, double hour, double precipitation)
{
	// summer - winter: about 6 degrees in the tropics, about 21 at 50 degrees, 24 far north
	double swing = 6 + 18 * ww::DetMath::Clamp(std::fabs(latitude) / 60, 0, 1);
	double seasonal = (season == 1 ? .5 : season == 3 ? -.5 : 0) * swing * (latitude < 0 ? -1 : 1);
	double daily = 4.5 * std::cos((hour - 15) / 24 * 2 * WrPi); // warmest at 15:00
	return yearlyMean + seasonal + daily - 3 * ww::DetMath::Clamp(precipitation, 0, 1);
}

void FNeeds::Step(double hours, const FBodyInput& b)
{
	if (hours <= 0) return;
	hours = ww::Mn(hours, 1); // (a long pause or a jump in time: at most an hour at once)
	// Food: about two game days from full to empty at rest, faster when working hard.
	Food = ww::Mx(0, Food - hours * 2 * (1 + .6 * b.Effort) * HungerRate);
	// Wet: soaked in water, wetter in rain, drying in the air (faster near a fire and when warm).
	double dry = 6 + ww::Mx(0, FeelsLike(b) - 10) * .8 + b.Fire * 4;
	Wet = b.bSwimming ? 100 : ww::DetMath::Clamp(Wet + hours * (b.Rain * 60 - dry), 0, 100);
	// Warmth follows what the body feels: comfortable from about 16 degrees, losing heat below; working warms a little.
	double feels = FeelsLike(b) + 6 * b.Effort;
	double target = ww::DetMath::Clamp(72 + (feels - 16) * 3.2, 0, 85);
	double rate = target < Warmth ? 18 * ColdRate : 25 + b.Fire * 2;
	Warmth = Warmth + ww::DetMath::Clamp(target - Warmth, -rate * hours, rate * hours);
	// Health: starving or freezing hurts; fed and warm, it comes back slowly.
	double harm = (Food <= 0 ? 4 : 0) + (Warmth < 25 ? (25 - Warmth) / 25 * 12 : 0);
	double heal = Food > 40 && Warmth > 50 ? 2.5 : 0;
	Health = ww::DetMath::Clamp(Health + hours * (heal - harm), 0, 100);
}

std::string FNeeds::Describe() const
{
	std::string s = WrText("Food %.0f  Warmth %.0f%s", Food, Warmth, Warmth < 25 ? " (freezing)" : Warmth < 45 ? " (cold)" : "");
	if (Wet > 10) s += WrText("  Wet %.0f", Wet);
	return s + WrText("  Health %.0f", Health);
}

// -------------------------------------------------------------------------------------------------------- removals
bool FObjectRemovals::Remove(int layer, long long x, long long z, double hours)
{
	if (!Removed.insert({ { layer, x, z }, hours }).second) return false;
	Changed.insert({ layer, x, z }); Version++;
	return true;
}
bool FObjectRemovals::Restore(int layer, long long x, long long z)
{
	if (!Removed.erase({ layer, x, z })) return false;
	Changed.insert({ layer, x, z }); Version++;
	return true;
}
bool FObjectRemovals::Get(int layer, long long x, long long z, double& hours) const
{
	auto it = Removed.find({ layer, x, z });
	if (it == Removed.end()) return false;
	hours = it->second;
	return true;
}
std::vector<FObjectRemovals::FKey> FObjectRemovals::Keys() const { std::vector<FKey> k; for (const auto& r : Removed) k.push_back(r.first); return k; }
std::vector<FObjectRemovals::FKey> FObjectRemovals::TakeChanged() { std::vector<FKey> k(Changed.begin(), Changed.end()); Changed.clear(); return k; }

// -------------------------------------------------------------------------------------------------- placed objects
FPlacedObject* FPlacedObjects::Get(long long id) { auto it = Objects.find(id); return it != Objects.end() ? &it->second : nullptr; }

FPlacedObject& FPlacedObjects::Add(const std::string& kind, double x, double y, double z, float yaw, double fuel)
{
	FPlacedObject o;
	o.Id = Next++; o.Kind = kind; o.X = x; o.Y = y; o.Z = z; o.Yaw = yaw; o.Fuel = fuel; o.Sequence = ++SequenceCount;
	Changed.insert(o.Id);
	return Objects[o.Id] = o;
}

void FPlacedObjects::Remove(long long id) { if (Objects.erase(id)) Changed.insert(id); }

std::vector<FPlacedObject*> FPlacedObjects::Near(double x, double z, double reach)
{
	// (Thornwake keeps a 16 m grid for thousands of building pieces; a walk over all is enough until building is ported)
	std::vector<FPlacedObject*> found;
	for (auto& o : Objects) if ((o.second.X - x) * (o.second.X - x) + (o.second.Z - z) * (o.second.Z - z) <= reach * reach) found.push_back(&o.second);
	std::sort(found.begin(), found.end(), [](const FPlacedObject* a, const FPlacedObject* b) { return a->Sequence < b->Sequence; });
	return found;
}

void FPlacedObjects::Step(double minutes)
{
	for (auto& entry : Objects)
	{
		FPlacedObject& o = entry.second;
		if (o.Fuel <= 0) continue;
		o.Fuel = ww::Mx(0, o.Fuel - minutes);
		if (o.Fuel == 0) Changed.insert(o.Id);
	}
}

std::vector<long long> FPlacedObjects::TakeChanged() { std::vector<long long> l(Changed.begin(), Changed.end()); Changed.clear(); return l; }

bool FPlacedObjects::Move(int player, long long id, double x, double y, double z, float qx, float qy, float qz, float qw)
{
	FPlacedObject* o = Get(id);
	if (!o || o->Owner != player) return false;
	o->X = x; o->Y = y; o->Z = z; o->Qx = qx; o->Qy = qy; o->Qz = qz; o->Qw = qw;
	Moved.insert(id);
	return true;
}

std::vector<long long> FPlacedObjects::TakeMoved() { std::vector<long long> l(Moved.begin(), Moved.end()); Moved.clear(); return l; }

// ----------------------------------------------------------------------------------------------------------- rules
int FGameRules::TreeHits(double scale) { return 3 + (int)std::nearbyint(4 * scale); }
int FGameRules::TrunkLogs(double scale) { return ww::MxI(1, (int)std::nearbyint(WrPi * TrunkRadius(scale) * TrunkRadius(scale) * TrunkLength(scale) / .3)); }
int FGameRules::TrunkHits(double scale) { return ww::MxI(2, 2 * (TrunkLogs(scale) - 1)); }
int FGameRules::CrownBranches(double scale) { return 3 + (int)std::nearbyint(3 * scale); }

FInventory& FGameRules::Inventory(int player)
{
	auto it = Inventories.find(player);
	if (it != Inventories.end()) return it->second;
	FInventory inv(Catalog);
	for (const FIngredient& s : Catalog.Start) inv.Add(s.Item, s.Count);
	InventoryChanged.insert(player);
	return Inventories.emplace(player, inv).first->second;
}

std::vector<int> FGameRules::TakeInventoryChanges() { std::vector<int> l(InventoryChanged.begin(), InventoryChanged.end()); InventoryChanged.clear(); return l; }
std::vector<FTreeEvent> FGameRules::TakeTreeEvents() { std::vector<FTreeEvent> l; l.swap(TreeEvents); return l; }

FActionResult FGameRules::Chop(int player, long long cx, long long cz, double px, double pz)
{
	FInventory& inv = Inventory(player);
	if (!inv.HasTag("chopping")) return WrFail("You need something to chop with (an axe).");
	if (World.Removals().Contains(FObjectRemovals::Trees, cx, cz)) return WrFail("That tree is already down.");
	double treeX, treeZ, scale; int species;
	if (!World.Tree(cx, cz, treeX, treeZ, scale, species)) return WrFail("There is no tree there.");
	if (WrDist(px, pz, treeX, treeZ) > ChopReach + 1) return WrFail("Too far from the tree.");
	int total = TreeHits(scale);
	auto key = std::make_pair(cx, cz);
	FWork work;
	auto found = TreeWork.find(key);
	if (found != TreeWork.end()) work = found->second;
	else
	{
		// The notch faces the chopper (from the trunk toward them).
		double ax = px - treeX, az = pz - treeZ, al = std::sqrt(ax * ax + az * az);
		work = al > 1e-6 ? FWork{ 0, ax / al, az / al } : FWork{ 0, 1, 0 };
	}
	int left = total - work.Hits - 1;
	if (left > 0)
	{
		TreeWork[key] = { work.Hits + 1, work.Nx, work.Nz };
		TreeEvents.push_back({ cx, cz, left, total, false, work.Nx, work.Nz });
		return { true, left == 2 ? "It creaks..." : "", 0 };
	}
	TreeWork.erase(key);
	World.Removals().Remove(FObjectRemovals::Trees, cx, cz, World.Hours());
	// Toward the notch; the wind has its say (up to half in a gale).
	double wx, wz, speed;
	World.Wind(wx, wz, speed);
	double k = ww::DetMath::Clamp(speed / 14, 0, .5);
	double dx = work.Nx * (1 - k) + wx * k, dz = work.Nz * (1 - k) + wz * k, dl = ww::Mx(std::sqrt(dx * dx + dz * dz), 1e-6);
	dx /= dl; dz /= dl;
	TreeEvents.push_back({ cx, cz, 0, total, true, dx, dz });
	double half = TrunkLength(scale) / 2, r = TrunkRadius(scale), tx = treeX + dx * half, tz = treeZ + dz * half;
	FPlacedObject& trunk = Objects.Add("trunk", tx, World.Ground(tx, tz) + r, tz, (float)std::atan2(dx, dz), 0);
	// (lying along the fall: the log's long axis, its local up, turned onto the direction)
	double s45 = std::sqrt(.5);
	trunk.Qx = (float)(dz * s45); trunk.Qy = 0; trunk.Qz = (float)(-dx * s45); trunk.Qw = (float)s45;
	trunk.Size = (float)scale; trunk.Variant = species; trunk.Owner = player;
	double crown = TreeHeight(scale) * .8, bx = treeX + dx * crown, bz = treeZ + dz * crown;
	FPlacedObject& branches = Objects.Add("branch_pile", bx, World.Ground(bx, bz), bz, (float)std::atan2(dx, dz), 0);
	branches.Contents.push_back({ "branch", CrownBranches(scale) });
	return { true, WrText("Timber! Cut the trunk up with the axe (%d logs in it); the branches lie where the crown came down (E).", TrunkLogs(scale)), 0 };
}

FActionResult FGameRules::ChopTrunk(int player, long long objectId, double px, double pz)
{
	if (!Inventory(player).HasTag("chopping")) return WrFail("You need something to chop with (an axe).");
	FPlacedObject* t = Objects.Get(objectId);
	if (!t || t->Kind != "trunk") return WrFail("Nothing to cut up there.");
	if (WrDist(px, pz, t->X, t->Z) > TrunkLength(t->Size) / 2 + ChopReach) return WrFail("Too far from the trunk.");
	t->Hits++;
	if (t->Hits < TrunkHits(t->Size))
	{
		Objects.Touch(t->Id);
		return { true, t->Hits % 2 == 0 ? WrText("A cut through (%d of %d).", t->Hits / 2, TrunkLogs(t->Size) - 1) : "", 0 };
	}
	const FPlacedObject copy = *t;
	Objects.Remove(copy.Id);
	int logs = TrunkLogs(copy.Size);
	FPlacedObject& pile = Objects.Add("log_pile", copy.X, copy.Y - TrunkRadius(copy.Size), copy.Z, copy.Yaw, 0);
	pile.Contents.push_back({ "log", logs });
	return { true, WrText("The trunk splits into %d logs (E picks them up).", logs), 0 };
}

FActionResult FGameRules::Forage(int player, const std::string& what, long long cx, long long cz, double x, double z, double px, double pz, double py)
{
	FInventory& inv = Inventory(player);
	// (on the ground's level: not from up on a platform)
	if (!std::isnan(py) && std::fabs(py - World.Ground(x, z)) > 2) return WrFail("You cannot reach the ground from up here.");
	if (what == "stone" || what == "berries")
	{
		int layer = what == "stone" ? FObjectRemovals::Stones : FObjectRemovals::Berries;
		double ax, az;
		World.GatherSpot(layer, cx, cz, ax, az);
		if (WrDist(px, pz, ax, az) > UseReach) return WrFail("Too far away.");
		if (!World.Removals().Remove(layer, cx, cz, World.Hours())) return WrFail(what == "stone" ? "Someone took it already." : "Already picked.");
		if (what == "stone") inv.Add("stone"); else inv.Add("berries", 4);
		InventoryChanged.insert(player);
		return { true, what == "stone" ? "A good stone." : "4 handfuls of berries.", 0 };
	}
	if (what == "grass")
	{
		if (WrDist(px, pz, x, z) > UseReach) return WrFail("Too far away.");
		// The grass is pulled: the cells around the spot are bare (for everyone) until it grows back.
		long long gx = (long long)std::floor(x / FObjectRemovals::GrassCell), gz = (long long)std::floor(z / FObjectRemovals::GrassCell);
		int pulled = 0;
		for (long long dz = -1; dz <= 1; dz++)
			for (long long dx = -1; dx <= 1; dx++)
				if (World.Removals().Remove(FObjectRemovals::Grass, gx + dx, gz + dz, World.Hours())) pulled++;
		if (pulled == 0) return WrFail("No long grass left here.");
		inv.Add("plant_fibre", pulled >= 5 ? 2 : 1);
		InventoryChanged.insert(player);
		return { true, pulled >= 5 ? "Plant fibre x2." : "Plant fibre x1 (little left here).", 0 };
	}
	return WrFail("Nothing to gather there.");
}

FActionResult FGameRules::Die(int player, double px, double pz)
{
	FInventory& inv = Inventory(player);
	std::string dropped;
	if (!inv.Stacks().empty())
	{
		FPlacedObject& pack = Objects.Add("pack", px, World.Ground(px, pz), pz, 0, 0);
		for (const FStack& s : inv.Stacks())
		{
			auto it = std::find_if(pack.Contents.begin(), pack.Contents.end(), [&](const FStack& c) { return c.first == s.first; });
			if (it != pack.Contents.end()) it->second += s.second; else pack.Contents.push_back(s);
		}
		dropped = " Everything you carried lies where you fell.";
	}
	inv.Clear();
	for (const FIngredient& s : Catalog.Start) inv.Add(s.Item, s.Count);
	InventoryChanged.insert(player);
	return { true, "You died." + dropped, 0 };
}

bool FGameRules::Claim(int player, long long objectId, double px, double pz)
{
	FPlacedObject* o = Objects.Get(objectId);
	if (!o || o->Owner == player || (o->Owner == 0 && o->Kind != "trunk")) return false;
	if (WrDist(px, pz, o->X, o->Z) > TrunkLength(o->Size) / 2 + 3) return false;
	o->Owner = player;
	Objects.Touch(o->Id);
	return true;
}

void FGameRules::Release(int player)
{
	std::vector<long long> mine;
	for (const auto& o : Objects.All()) if (o.second.Owner == player) mine.push_back(o.first);
	for (long long id : mine) { Objects.Get(id)->Owner = 0; Objects.Touch(id); }
}

FActionResult FGameRules::Gather(int player, long long objectId, double px, double pz)
{
	FPlacedObject* o = Objects.Get(objectId);
	if (!o || o->Contents.empty()) return WrFail("Nothing to pick up there.");
	if (o->Kind == "carcass") return WrFail("Butcher it first (E with a knife, an axe or a sword in the pack).");
	if (WrDist(px, pz, o->X, o->Z) > UseReach + 1.5) return WrFail("Too far away.");
	FInventory& inv = Inventory(player);
	std::string what;
	for (const FStack& c : o->Contents)
	{
		inv.Add(c.first, c.second);
		what += (what.empty() ? "" : ", ") + WrText("%d x %s", c.second, WrLower(Catalog.Item(c.first).Name).c_str());
	}
	Objects.Remove(o->Id);
	InventoryChanged.insert(player);
	return { true, "Picked up " + what + ".", 0 };
}

FActionResult FGameRules::Make(int player, const std::string& recipeId, double px, double pz, double x, double y, double z, float yaw)
{
	auto it = Catalog.Recipes.find(recipeId);
	if (it == Catalog.Recipes.end()) return WrFail("Unknown recipe.");
	const FRecipeDef& r = it->second;
	FInventory& inv = Inventory(player);
	std::string why = Missing(inv, r, px, pz);
	if (!why.empty()) return WrFail(why);
	if (!r.Places.empty() && WrDist(px, pz, x, z) > PlaceReach) return WrFail("Too far away to put it there.");
	for (const FIngredient& input : r.Inputs) inv.Take(input);
	for (const FIngredient& output : r.Outputs) inv.Add(output.Item, output.Count);
	InventoryChanged.insert(player);
	if (!r.Places.empty())
	{
		const FObjectDef& def = Catalog.Objects.at(r.Places);
		Objects.Add(r.Places, x, y, z, yaw, def.Fuel);
		return { true, def.Name + " built" + (def.Fuel > 0 ? " and lit" : "") + ".", 0 };
	}
	std::string made;
	for (const FIngredient& o : r.Outputs) made += (made.empty() ? "" : ", ") + WrText("%d x %s", o.Count, Catalog.Item(o.Item).Name.c_str());
	return { true, "Made: " + made + ".", 0 };
}

std::string FGameRules::Missing(const FInventory& inv, const FRecipeDef& r, double px, double pz)
{
	for (const std::string& tool : r.Tools)
		if (!inv.HasTag(tool)) return "Needs a tool: " + tool + ".";
	for (const FIngredient& input : r.Inputs)
		if (inv.Count(input) < input.Count)
			return WrText("Needs %d x %s (%d in the pack).", input.Count, (!input.Item.empty() ? Catalog.Item(input.Item).Name : input.Tag).c_str(), inv.Count(input));
	if (!r.Station.empty() && !StationNear(r.Station, px, pz)) return "Needs a " + r.Station + " nearby" + (r.Station == "fire" ? " (a lit campfire)" : "") + ".";
	return "";
}

bool FGameRules::StationNear(const std::string& tag, double px, double pz)
{
	for (FPlacedObject* o : Objects.Near(px, pz, 10))
	{
		auto d = Catalog.Objects.find(o->Kind);
		if (d != Catalog.Objects.end() && d->second.Tags.count(tag) && (!d->second.Tags.count("fire") || o->Lit()) && WrDist(px, pz, o->X, o->Z) <= d->second.Reach) return true;
	}
	return false;
}

FActionResult FGameRules::Fuel(int player, long long objectId, double px, double pz, const std::string& item)
{
	FPlacedObject* o = Objects.Get(objectId);
	auto d = o ? Catalog.Objects.find(o->Kind) : Catalog.Objects.end();
	if (!o || d == Catalog.Objects.end() || d->second.MaxFuel <= 0) return WrFail("Nothing to put fuel on.");
	const FObjectDef& def = d->second;
	if (WrDist(px, pz, o->X, o->Z) > UseReach) return WrFail("Too far from the fire.");
	if (o->Fuel >= def.MaxFuel - 1) return WrFail("The fire has all the wood it can take.");
	FInventory& inv = Inventory(player);
	const FItemDef* fuel = nullptr;
	if (!item.empty()) { if (inv.Count(item) > 0) fuel = &Catalog.Item(item); }
	else
		for (const FStack& s : inv.Stacks())
		{
			const FItemDef& candidate = Catalog.Item(s.first);
			if (candidate.Has("fuel") && (!fuel || candidate.Fuel > fuel->Fuel)) fuel = &candidate;
		}
	if (!fuel || !fuel->Has("fuel")) return WrFail("You have nothing to burn.");
	bool wasOut = !o->Lit();
	inv.Take(fuel->Id);
	o->Fuel = ww::Mn(def.MaxFuel, o->Fuel + fuel->Fuel);
	Objects.Touch(o->Id);
	InventoryChanged.insert(player);
	return { true, fuel->Name + " on the fire" + (wasOut ? ": it catches again" : "") + WrText(" (%.0f min of wood).", o->Fuel), 0 };
}

FActionResult FGameRules::Eat(int player, const std::string& item)
{
	auto d = Catalog.Items.find(item);
	if (d == Catalog.Items.end() || !d->second.Has("food")) return WrFail("You cannot eat that.");
	FInventory& inv = Inventory(player);
	if (!inv.Take(item)) return WrFail("No " + d->second.Name + " left.");
	InventoryChanged.insert(player);
	return { true, "You eat the " + WrLower(d->second.Name) + (d->second.Has("raw") ? " (raw: not very filling)" : "") + ".", d->second.Food };
}

void FGameRules::Step(double minutes)
{
	Objects.Step(minutes);
	if ((RegrowCheck -= minutes) > 0) return;
	RegrowCheck = 10; // (every ten game minutes)
	double now = World.Hours();
	for (const FObjectRemovals::FKey& k : World.Removals().Keys())
	{
		double at;
		if (World.Removals().Get(k.Layer, k.X, k.Z, at)
			&& ((k.Layer == FObjectRemovals::Berries && now - at >= BerryRegrowHours) || (k.Layer == FObjectRemovals::Grass && now - at >= GrassRegrowHours)))
			World.Removals().Restore(k.Layer, k.X, k.Z);
	}
}

double FGameRules::FireWarmth(const FCatalog& catalog, FPlacedObjects& objects, double x, double z)
{
	double sum = 0;
	for (FPlacedObject* o : objects.Near(x, z, 12))
	{
		auto d = catalog.Objects.find(o->Kind);
		if (!o->Lit() || d == catalog.Objects.end() || d->second.Warmth <= 0) continue;
		double k = 1 - WrDist(x, z, o->X, o->Z) / (d->second.Reach * 2);
		sum += d->second.Warmth * ww::DetMath::Clamp(k, 0, 1);
	}
	return sum;
}
}
