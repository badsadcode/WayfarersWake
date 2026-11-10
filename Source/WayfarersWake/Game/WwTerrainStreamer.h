#pragma once

#include "WorldGen/WwOcean.h"
#include "Rules/WrRules.h"
#include "Async/Future.h"
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Containers/Queue.h"
#include "WorldGen/WwWorld.h"
#include "WorldGen/WwWaterfalls.h"
#include "Game/WwSpray.h"
#include "InstanceDataTypes.h"
#include <atomic>
#include "WwTerrainStreamer.generated.h"

class UProceduralMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UTexture2D;
class UStaticMesh;
class UInstancedStaticMeshComponent;

/// A terrain tile: Level 0 is BaseTile metres across; each level doubles. X/Y count tiles of that level from the world origin.
struct FWwTileKey
{
	int32 Level = 0;
	int64 X = 0, Y = 0;
	bool operator==(const FWwTileKey& O) const { return Level == O.Level && X == O.X && Y == O.Y; }
	/// True if this tile's square contains (or equals) the other's.
	bool Contains(const FWwTileKey& O) const { return Level >= O.Level && (O.X >> (Level - O.Level)) == X && (O.Y >> (Level - O.Level)) == Y; }
	bool Overlaps(const FWwTileKey& O) const { return Contains(O) || O.Contains(*this); }
};
inline uint32 GetTypeHash(const FWwTileKey& K) { return HashCombine(HashCombine(::GetTypeHash(K.Level), ::GetTypeHash(K.X)), ::GetTypeHash(K.Y)); }

/// Mesh data of one tile, built on a worker thread from the deterministic sampler.
struct FWwTileData
{
	FWwTileKey Key;
	int32 WorldSerial = 0;
	TArray<FVector> Vertices, Normals, WaterVertices, WaterNormals;
	TArray<int32> Triangles, WaterTriangles;
	/// Ground vertex data as in Thornwake: Colors = forest cover, mountain share, beach, submerged bed (0.5 at the
	/// waterline); Uv0 = rock exposure, scree; Uv1 = position within the tile (0..1).
	TArray<FColor> Colors;
	TArray<FVector2D> Uv0, Uv1;
	/// Water sheet (Thornwake's): colour = depth / 20 m, kind code, river-water share, edge fade; UV0 = current (m/s east,
	/// south); UV1 = position within the tile; UV2 = the bed's uphill gradient (toward the shore; length = slope);
	/// UV3 = how open the coast is (0 sheltered .. 1 open), the water's fetch (m: a lake's size, a river's width).
	TArray<FColor> WaterColors;
	TArray<FVector2D> WaterUv0, WaterUv1, WaterUv2, WaterUv3;
	/// Biome ground colours per vertex, three squares side by side (GroundSide texels each): grass + snow,
	/// dry ground + plant cover, rock + wetness. Colours are display (gamma) values, as Thornwake's shader expects.
	TArray<FColor> Ground;
	int32 GroundSide = 0;
	/// Trees standing on this tile (levels near enough to show them).
	struct FTree { int32 Species; FVector Position; float Scale, Yaw; int64 Cx = 0, Cz = 0; };
	TArray<FTree> Trees;
	/// Grass and small plants of a nearest-level tile, ready for instancing: tile-local transforms and, per plant,
	/// four numbers for the material (sprite layer, colour). Upright plants (crossed sprites) and floating leaves (flat).
	TArray<FTransform> Plants, Leaves;
	/// Rocks and props (stones, boulders, cliff slabs; bushes, stumps, logs): mesh (rock kind, or 100 + prop kind), place
	/// in the world, and four numbers for the material (rock colour, rock look).
	struct FScatter { int32 Mesh; FTransform Transform; FLinearColor Data; };
	TArray<FScatter> Scatter;
	TArray<float> PlantData, LeafData;
	double BuildMs = 0;
};

/// A pooled tile: its mesh, the material instance carrying the tile's ground-colour texture, and that texture.
USTRUCT()
struct FWwTileSlot
{
	GENERATED_BODY()
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> Mesh;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> Material;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> Water;
	UPROPERTY() TObjectPtr<UTexture2D> Ground;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Plants;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Leaves;
};

/// Streams the generated terrain around the players as a quadtree of LOD tiles (Thornwake's nested rings: 64 m tiles of
/// 32 cells near the player, each coarser level twice as large). Tiles are built on worker threads and committed on the
/// game thread within a time budget; a coarser tile stays until everything replacing it is ready, so there are no holes.
/// Memory is bounded by the view (a few hundred tiles) whatever the world size: nothing but the plan covers the world.
/// Local to each machine (not replicated): clients rebuild the same terrain from the replicated world settings.
UCLASS()
class WAYFARERSWAKE_API AWwTerrainStreamer : public AActor
{
	GENERATED_BODY()

public:
	AWwTerrainStreamer();
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	/// Metres across a level-0 tile and cells along a tile's side. As in Thornwake, the nearest level is twice as fine
	/// (64 cells: 1 m vertices underfoot), coarser levels have 32 cells (4 m, 8 m, ...).
	UPROPERTY(EditAnywhere, Category = "Terrain") double BaseTile = 64;
	UPROPERTY(EditAnywhere, Category = "Terrain") int32 Cells = 32;
	/// LOD levels: 9 = 16 km root tiles, about 24 km of view.
	UPROPERTY(EditAnywhere, Category = "Terrain") int32 Levels = 9;
	/// A tile splits into four when a viewer is nearer than this many tile sizes.
	UPROPERTY(EditAnywhere, Category = "Terrain") double SplitRange = 1.5;
	/// Finest levels that get collision.
	UPROPERTY(EditAnywhere, Category = "Terrain") int32 CollisionLevels = 2;
	/// Game-thread milliseconds per frame for committing finished tiles.
	UPROPERTY(EditAnywhere, Category = "Terrain") double CommitBudgetMs = 3;
	UPROPERTY(EditAnywhere, Category = "Terrain") int32 MaxJobs = 12;
	/// Trees: full models on the finest levels, the distant models on the next ones, none beyond.
	UPROPERTY(EditAnywhere, Category = "Trees") int32 NearTreeLevels = 2;
	UPROPERTY(EditAnywhere, Category = "Trees") int32 TreeLevels = 4;
	/// Beyond the trees, forests are drawn as tree cards up to this tile level (6: about 12 km; Thornwake as Tom plays it: 7).
	UPROPERTY(EditAnywhere, Category = "Trees") int32 FarTreeLevels = 6;
	/// Grass density as Thornwake's setting (1 = its default; Tom plays with 3; 0 = no plants). Test switch -WwGrass=.
	UPROPERTY(EditAnywhere, Category = "Plants") double GrassAmount = 3;

	/// True once the finest tile under this place is in the scene (with its collision requested).
	bool IsGroundReady(const FVector& Location) const;

	int32 GetTileCount() const { return Tiles.Num(); }
	int32 GetTreeCount() const { return TreesShown; }
	int32 GetPlantCount() const { return PlantsShown; }
	int32 GetPendingCount() const { return Pending.Num(); }
	int32 GetDesiredCount() const { return Desired.Num(); }
	int32 GetMissingCount() const { return Missing; }
	int64 GetTilesBuilt() const { return TilesBuilt; }
	double GetAverageBuildMs() const { return TilesBuilt > 0 ? TotalBuildMs / TilesBuilt : 0; }
	double GetMaxCommitMs() const { return MaxCommitMs; }
	/// How long one update of the simulated sea takes on the worker threads (recent longest, ms).
	double GetOceanMs() const { return OceanMs; }
	/// A tree was felled (its lattice cell): it leaves the picture, and its trunk stops being solid.
	void FellTree(int64 Cx, int64 Cz);
	/// Draws the objects lying in the world (a fallen trunk, piles of logs and branches, a dropped pack) as plain shapes.
	void SyncObjects(const std::map<long long, wr::FPlacedObject>& Objects);
	/// The sea's wave textures: per cascade its displacement (0, 2, 4) and slopes (1, 3, 5); null before they are made.
	UTexture2D* GetOceanTexture(int32 Index) const { return OceanTextures.IsValidIndex(Index) ? OceanTextures[Index].Get() : nullptr; }
	void ResetStats() { MaxCommitMs = 0; }

	static TSharedPtr<FWwTileData> BuildTile(const ww::FTerrainSampler& Sampler, const FWwTileKey& Key, double BaseTile, int32 Cells, bool bTrees, double Grass,
		const ww::FFallSet* Falls = nullptr, bool bRocks = false, bool bCards = false);
	int32 GetRockCount() const { return RocksShown; }
	int32 GetFallCount() const { return FallsShown.Num(); }
	int32 GetPuffCount() const { return PuffsShown; }

private:
	struct FTreeBatch { int32 Component; TArray<FPrimitiveInstanceId> Ids; };
	struct FTile { int32 Slot = -1; int32 Plants = 0; bool bVisible = false; TArray<FWwTileData::FTree> Trees; TArray<FWwTileData::FScatter> Scatter; TArray<FTreeBatch> Batches; };
	/// Tree instances live in components covering 16 km squares (instance positions are single precision).
	struct FTreeKey
	{
		int64 X, Y; int32 Mesh;
		bool bSolid = false; // (instances of the nearest tiles stand in a component with collision)
		bool operator==(const FTreeKey& O) const { return X == O.X && Y == O.Y && Mesh == O.Mesh && bSolid == O.bSolid; }
		friend uint32 GetTypeHash(const FTreeKey& K) { return HashCombine(HashCombine(::GetTypeHash(K.X), ::GetTypeHash(K.Y)), ::GetTypeHash(K.Mesh * 2 + (K.bSolid ? 1 : 0))); }
	};
	struct FShared
	{
		TQueue<TSharedPtr<FWwTileData>, EQueueMode::Mpsc> Done;
		std::atomic<int32> InFlight{ 0 };
		TQueue<TPair<int32, TSharedPtr<ww::FFallModel>>, EQueueMode::Mpsc> FallsDone;
	};

	void ClearAll();
	void GatherViewers(TArray<FVector2D>& Out) const;
	void ComputeDesired(const TArray<FVector2D>& Viewers);
	void Visit(const FWwTileKey& Key, const TArray<FVector2D>& Viewers);
	void LaunchJobs(const TArray<FVector2D>& Viewers);
	void Commit(const TSharedPtr<FWwTileData>& Data);
	void ResolveVisibility();
	void Release(FTile& Tile);
	void ShowTile(const FWwTileKey& Key, FTile& Tile);
	void LoadTreeMeshes();
	void UpdateWaterfalls(const TArray<FVector2D>& Viewers, bool bMoved);
	void UpdateSpray(const TArray<FVector2D>& Viewers, float DeltaSeconds);
	/// The simulated sea (WwOcean): its textures and the foam net are made once; every frame the sea is worked out for the
	/// game's clock on worker threads and the last finished moment is sent to the graphics card.
	void StartOcean();
	void UpdateOcean(float DeltaSeconds);
	void ApplyOcean(UMaterialInstanceDynamic* Water, double CornerX, double CornerZ) const;
	void SetPlants(TObjectPtr<UInstancedStaticMeshComponent>& Component, UStaticMesh* Mesh, const TArray<FTransform>& Transforms, const TArray<float>& Data, const FVector& Origin);
	double TileSize(int32 Level) const { return BaseTile * (double)((int64)1 << Level); }

	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> TerrainMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> WaterMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> FallMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> RockMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> PropMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> FarTreeMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> FallFoamMaterial;
	/// Falling water of the waterfalls within a few kilometres (by index into the fall set).
	UPROPERTY(Transient) TMap<int32, TObjectPtr<UProceduralMeshComponent>> FallsShown;
	UPROPERTY(Transient) TArray<FWwTileSlot> Slots;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMesh>> TreeMeshes;
	UPROPERTY(Transient) TObjectPtr<UStaticMesh> PlantMesh;
	UPROPERTY(Transient) TObjectPtr<UStaticMesh> LeafMesh;
	UPROPERTY(Transient) TObjectPtr<UInstancedStaticMeshComponent> SprayMesh;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> SprayMaterial;
	/// Per cascade: displacement, slopes (three cascades: six textures).
	UPROPERTY(Transient) TArray<TObjectPtr<UTexture2D>> OceanTextures;
	UPROPERTY(Transient) TObjectPtr<UTexture2D> FoamNet;
	UPROPERTY(Transient) TMap<int64, TObjectPtr<UStaticMeshComponent>> ObjectViews;
	UPROPERTY(Transient) TArray<TObjectPtr<UInstancedStaticMeshComponent>> TreeComponents;
	TArray<int32> Free;
	/// Per species and model (near, distant): first index into TreeMeshes and how many parts (bark, foliage, cards).
	struct FTreeModel { int32 First = 0, Count = 0; };
	TArray<FTreeModel> TreeModels;
	TMap<FTreeKey, int32> TreeLookup;
	int32 TreesShown = 0, PlantsShown = 0, RocksShown = 0;
	/// Rock and prop meshes (built in code) follow the tree meshes in TreeMeshes.
	int32 RockMeshFirst = -1, PropMeshFirst = -1;
	/// An unseen cylinder for tree trunks (collision only), after the rock and prop meshes.
	int32 TrunkMesh = -1, CardMesh = -1;
	std::shared_ptr<ww::FFallSet> Falls;
	TFuture<std::shared_ptr<ww::FOceanFrame>> OceanTask;
	double OceanMs = 0;
	TSet<TPair<int64, int64>> FelledTrees;
	/// Spray and mist of the falls shown (by fall index), drawn as one set of camera-facing instances.
	TMap<int32, FWwFallSpray> FallSpray;
	int32 PuffsShown = 0;
	TSet<int32> FallsBuilding;
	bool bPlantShadows = true;
	int32 DrawnSeen[5] = { -1, -1, -1, -1, -1 };
	FLinearColor TouchShown = FLinearColor(0, 0, 0, 0);
	/// The texture set in use (the menu's View page): its three arrays, and the stones' two materials pointed at them.
	void ApplyTextureSet();
	UPROPERTY(Transient) TObjectPtr<class UTexture> GroundSet;
	UPROPERTY(Transient) TObjectPtr<class UTexture> StoneSet;
	UPROPERTY(Transient) TObjectPtr<class UTexture> CliffSet;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> RockLook;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> BigRockLook;
	FString SetShown;
	FLinearColor WeatherShown = FLinearColor(0.496f, 0.868f, 0.69f, 0.5f);

	TMap<FWwTileKey, FTile> Tiles;
	TSet<FWwTileKey> Pending, Desired;
	TSharedPtr<FShared, ESPMode::ThreadSafe> Shared;
	std::shared_ptr<ww::FTerrainSampler> Sampler;
	TArray<FVector2D> LastViewers;
	int32 WorldSerial = -1, Missing = 0;
	bool bDirty = true;
	double WorldHalf = 0, TotalBuildMs = 0, MaxCommitMs = 0;
	int64 TilesBuilt = 0;
};
