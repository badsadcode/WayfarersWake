// The rules of the game (a port of Thornwake's Game/Gameplay: Catalog.cs, Inventory.cs, Needs.cs, PlacedObjects.cs,
// GameRules.cs, and Game/WorldGen/ObjectRemovals.cs). Engine-free, like the world generator, so the headless test
// checks it (Tools/WorldGenTest --rules: Thornwake's own gameplay tests, ported).
// The rules run on the host only: a player's machine asks, the rules check (reach, tools, materials) and apply, and the
// host sends what changed. Content comes from Thornwake's JSON files (Content/Gameplay); rules ask for an item's tags,
// not its id.
// Ported so far (step 1): items, recipes, placeable objects, the start kit, fish; inventories; needs (food, warmth,
// wetness, health); generated objects taken out of the world (felled trees, stones, berries, grass); placed objects;
// chopping a tree and cutting up its trunk, foraging, picking up, making, fuelling a fire, eating, dying, regrowth.
// Not ported yet: animals and hunting, building pieces and blueprints (so a campfire cannot be built by a player yet),
// fishing's catch, saves, and the byte formats the network and saves use.
#pragma once

#include "WorldGen/WwWorld.h"
#include <map>
#include <set>
#include <unordered_map>

namespace wr
{
using ww::FJson;

/// An item kind: rules ask for its tags.
struct FItemDef
{
	std::string Id, Name, Description;
	std::set<std::string> Tags;
	double Weight = 0, Food = 0, Fuel = 0;
	int Stack = 1;
	std::map<std::string, double> Props;
	bool Has(const std::string& tag) const { return Tags.count(tag) > 0; }
	double Prop(const std::string& name, double fallback = 0) const { auto it = Props.find(name); return it != Props.end() ? it->second : fallback; }
};

/// An item or any item with a tag, and how many.
struct FIngredient
{
	std::string Item, Tag; // one of the two is set
	int Count = 1;
	bool Matches(const FItemDef& d) const { return !Item.empty() ? d.Id == Item : !Tag.empty() && d.Has(Tag); }
};

/// Everything made or done: inputs used up, tools needed (tags in the pack), a working station within reach (a lit
/// fire), the time it takes, and items made or an object placed.
struct FRecipeDef
{
	std::string Id, Name, Station, Places, Description;
	std::vector<FIngredient> Inputs, Outputs;
	std::vector<std::string> Tools;
	double Seconds = 1;
};

/// A kind of object players put into the world (a campfire): its tags, fuel (minutes) and warmth (degrees close by).
struct FObjectDef { std::string Id, Name; std::set<std::string> Tags; double Fuel = 0, MaxFuel = 0, Warmth = 0, Reach = 3; };

struct FFishDef { std::string Id, Name; std::set<std::string> Waters; double Stamina = 6, Pull = 1, MinKg = .5, MaxKg = 1; bool bDusk = false; };

/// Items, recipes and placeable objects, from the content files (the caller reads the files). Error is set when the
/// files are damaged or a reference does not resolve: a typo in a content file shows here, not in the middle of a game.
class FCatalog
{
public:
	FCatalog(const std::string& itemsJson, const std::string& recipesJson);
	std::map<std::string, FItemDef> Items;
	std::map<std::string, FRecipeDef> Recipes;
	std::vector<std::string> RecipeOrder; // as in the file
	std::map<std::string, FObjectDef> Objects;
	std::vector<FIngredient> Start;       // what a new character carries
	std::vector<FFishDef> Fish;
	std::string Error; // empty when everything loaded and resolved
	const FItemDef& Item(const std::string& id) const;
};

using FStack = std::pair<std::string, int>;

/// What a character carries: item stacks in order of arrival. No slot limit yet; the weight is there for later.
class FInventory
{
public:
	explicit FInventory(const FCatalog& catalog) : Catalog(&catalog) {}
	const std::vector<FStack>& Stacks() const { return Items; }
	double Weight() const;
	int Count(const std::string& item) const;
	int CountTag(const std::string& tag) const;
	int Count(const FIngredient& x) const;
	bool HasTag(const std::string& tag) const { return CountTag(tag) > 0; }
	void Add(const std::string& item, int count = 1);
	/// Takes the ingredient's count of matching items (newest stacks first), or nothing if there are too few.
	bool Take(const FIngredient& x, std::vector<FStack>* taken = nullptr);
	bool Take(const std::string& item, int count = 1) { return Take(FIngredient{ item, "", count }); }
	void Clear() { Items.clear(); }

private:
	const FCatalog* Catalog;
	std::vector<FStack> Items;
};

/// What the body feels this moment: air temperature (degrees C), wind (m/s), rain 0..1, in water, warmth from fires
/// within reach (degrees), effort 0 resting .. 1 running or chopping.
struct FBodyInput { double Air = 15, Wind = 0, Rain = 0; bool bSwimming = false; double Fire = 0, Effort = 0; };

/// Needs: food, body warmth and wetness, and health that suffers when the others run out. All 0..100; rates per game hour.
class FNeeds
{
public:
	double Food = 80;
	double Warmth = 72; // 70 and up comfortable, under 40 cold, under 15 freezing
	double Wet = 0;     // 0 dry .. 100 soaked
	double Health = 100;
	double HungerRate = 1, ColdRate = 1;

	/// The temperature the body feels: the air, wind chill, wetness and fires.
	double FeelsLike(const FBodyInput& b) const;
	/// Air temperature (a placeholder): the place's yearly mean, the season (0 spring, 1 summer, 2 autumn, 3 winter;
	/// stronger toward the poles), the hour (warmest mid-afternoon) and rain cooling the air.
	static double AirTemperature(double yearlyMean, double latitude, int season, double hour, double precipitation);
	/// Advance by game hours.
	void Step(double hours, const FBodyInput& b);
	void Eat(double food) { Food = ww::Mn(100, Food + food); }
	std::string Describe() const;
};

/// Generated objects taken out of the world: a sparse set keyed by the object's layer and lattice cell (its identity,
/// the same on every machine). The value is when it happened (game hours), for regrowth.
class FObjectRemovals
{
public:
	static constexpr int Trees = 0, Stones = 1, Berries = 2, Grass = 3;
	static constexpr double GrassCell = 1; // pulling long grass bares the 1 m cells around the spot
	struct FKey { int Layer; long long X, Z; bool operator<(const FKey& o) const { return Layer != o.Layer ? Layer < o.Layer : X != o.X ? X < o.X : Z < o.Z; } };

	long long Version = 0;
	bool Contains(int layer, long long x, long long z) const { return Removed.count({ layer, x, z }) > 0; }
	/// Takes the object out (at game hours). False if it was already gone.
	bool Remove(int layer, long long x, long long z, double hours);
	/// Puts it back (regrowth). False if it was there.
	bool Restore(int layer, long long x, long long z);
	/// When it was removed; false if present.
	bool Get(int layer, long long x, long long z, double& hours) const;
	std::vector<FKey> Keys() const;
	std::vector<FKey> TakeChanged();
	int Count() const { return (int)Removed.size(); }

private:
	std::map<FKey, double> Removed;
	std::set<FKey> Changed;
};

/// An object in the world (a campfire, a log pile, a fallen trunk): where, which way, and its state.
struct FPlacedObject
{
	long long Id = 0;
	std::string Kind;
	double X = 0, Y = 0, Z = 0;
	float Yaw = 0;
	float Qx = 0, Qy = 0, Qz = 0, Qw = 1; // full orientation for objects that move (a trunk)
	float Size = 1;      // a trunk: its tree's scale
	int Owner = 0;       // the player whose machine moves it; 0 = nobody
	int Hits = 0;        // axe hits taken (a trunk being cut up)
	int Variant = 0;     // a trunk: its tree's species
	double Fuel = 0;     // minutes of fuel left (fires)
	bool Lit() const { return Fuel > 0; }
	std::vector<FStack> Contents; // items lying in it to pick up
	long long Sequence = 0;
};

/// Objects placed into the world: the host keeps them, steps them (fires burn down) and sends the changed ones.
class FPlacedObjects
{
public:
	const std::map<long long, FPlacedObject>& All() const { return Objects; }
	FPlacedObject* Get(long long id);
	FPlacedObject& Add(const std::string& kind, double x, double y, double z, float yaw, double fuel);
	void Remove(long long id);
	void Touch(long long id) { Changed.insert(id); }
	/// The objects within reach (level distance) of a point, in the order they came.
	std::vector<FPlacedObject*> Near(double x, double z, double reach);
	/// Fires burn their fuel (game minutes). A fire going out counts as a change.
	void Step(double minutes);
	std::vector<long long> TakeChanged();
	/// The owner moved it. False if it is not theirs (or gone).
	bool Move(int player, long long id, double x, double y, double z, float qx, float qy, float qz, float qw);
	std::vector<long long> TakeMoved();

private:
	std::map<long long, FPlacedObject> Objects;
	std::set<long long> Changed, Moved;
	long long Next = 1, SequenceCount = 0;
};

/// What the rules need to know about the world (the game answers from the generated world).
class IWorldQuery
{
public:
	virtual ~IWorldQuery() = default;
	/// The tree of a lattice cell if one grows there (felled or not): its place, size and species.
	virtual bool Tree(long long cx, long long cz, double& x, double& z, double& scale, int& species) = 0;
	virtual FObjectRemovals& Removals() = 0;
	/// Game time in hours since the world began.
	virtual double Hours() = 0;
	/// The wind (the direction it blows toward, speed m/s): it decides where a tree falls.
	virtual void Wind(double& x, double& z, double& speed) = 0;
	virtual double Ground(double x, double z) = 0;
	/// The kind of water ("ocean", "lake", "river", "pond") within a radius of a point, or "".
	virtual std::string WaterNear(double x, double z, double radius) = 0;
	/// Where the gatherable of a layer's lattice cell stands (a loose stone, a berry bush).
	virtual void GatherSpot(int layer, long long cx, long long cz, double& x, double& z) = 0;
};

/// A tree was hit (it shakes; the notch grows) or felled (it falls toward Dir).
struct FTreeEvent { long long Cx, Cz; int HitsLeft, Total; bool bFelled; double DirX, DirZ; };

/// An action's outcome, told to the player who asked.
struct FActionResult { bool bOk = false; std::string Message; double Food = 0; };

class FGameRules
{
public:
	static constexpr double ChopReach = 4.5, UseReach = 3.5, PlaceReach = 6;
	/// Berries grow back on a picked bush after two game days; pulled grass after three.
	static constexpr double BerryRegrowHours = 48, GrassRegrowHours = 72;

	FGameRules(const FCatalog& catalog, IWorldQuery& world) : Catalog(catalog), World(world) {}
	const FCatalog& Catalog;
	FPlacedObjects Objects;

	/// Hits a tree takes: 3 + 4 x its size.
	static int TreeHits(double scale);
	static double TreeHeight(double scale) { return 9 * scale; }
	static double TrunkLength(double scale) { return 7.2 * scale; }
	static double TrunkRadius(double scale) { return .12 + .12 * scale; }
	/// Logs in a trunk: its volume at about 0.3 cubic metres a log.
	static int TrunkLogs(double scale);
	static int TrunkHits(double scale);
	static int CrownBranches(double scale);

	/// A player's inventory; a new character starts with the start kit.
	FInventory& Inventory(int player);
	std::vector<int> TakeInventoryChanges();
	std::vector<FTreeEvent> TakeTreeEvents();

	/// One axe hit on the tree of a lattice cell: the first hit sets the notch on the side the chopper stands; after
	/// TreeHits hits it falls toward the notch, the wind pushing it, leaving a trunk and the crown's branches.
	FActionResult Chop(int player, long long cx, long long cz, double px, double pz);
	/// One axe hit on a fallen trunk; after TrunkHits it is cut into logs lying where it was.
	FActionResult ChopTrunk(int player, long long objectId, double px, double pz);
	/// E on nature: a loose stone picked up (gone for good), berries picked (they grow back), long grass pulled for fibre.
	/// py: the player's feet (NaN: not checked): not from up on a platform.
	FActionResult Forage(int player, const std::string& what, long long cx, long long cz, double x, double z, double px, double pz, double py = ww::NaNValue());
	/// Everything carried drops where the character fell; the player goes on as a new character with the start kit.
	FActionResult Die(int player, double px, double pz);
	bool Claim(int player, long long objectId, double px, double pz);
	void Release(int player);
	/// Pick up what lies in a placed object within reach: into the pack, the object goes.
	FActionResult Gather(int player, long long objectId, double px, double pz);
	/// Make a recipe: its tools in the pack, its inputs, a working station within reach.
	FActionResult Make(int player, const std::string& recipeId, double px, double pz, double x, double y, double z, float yaw);
	/// Why the recipe cannot be made here and now ("" when it can).
	std::string Missing(const FInventory& inv, const FRecipeDef& r, double px, double pz);
	bool StationNear(const std::string& tag, double px, double pz);
	/// Put fuel on a fire (the item with the most fuel in the pack, or the one named): it relights if out.
	FActionResult Fuel(int player, long long objectId, double px, double pz, const std::string& item = "");
	/// Eat one of the item: the food value goes to the player's needs.
	FActionResult Eat(int player, const std::string& item);
	/// Advance the world's objects by game minutes; picked bushes and pulled grass regrow.
	void Step(double minutes);
	/// Warmth (degrees) the lit fires give at a point: full at the fire, nothing at twice its reach.
	static double FireWarmth(const FCatalog& catalog, FPlacedObjects& objects, double x, double z);

private:
	IWorldQuery& World;
	std::map<int, FInventory> Inventories;
	std::set<int> InventoryChanged;
	struct FWork { int Hits; double Nx, Nz; };
	std::map<std::pair<long long, long long>, FWork> TreeWork;
	std::vector<FTreeEvent> TreeEvents;
	double RegrowCheck = 0;
};
}
