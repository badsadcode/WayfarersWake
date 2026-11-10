#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Containers/Queue.h"
#include "WorldGen/WwWorld.h"
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
	TArray<FLinearColor> WaterColors;
	/// Biome ground colours per vertex, three squares side by side (GroundSide texels each): grass + snow,
	/// dry ground + plant cover, rock + wetness. Colours are display (gamma) values, as Thornwake's shader expects.
	TArray<FColor> Ground;
	int32 GroundSide = 0;
	/// Trees standing on this tile (levels near enough to show them).
	struct FTree { int32 Species; FVector Position; float Scale, Yaw; };
	TArray<FTree> Trees;
	double BuildMs = 0;
};

/// A pooled tile: its mesh, the material instance carrying the tile's ground-colour texture, and that texture.
USTRUCT()
struct FWwTileSlot
{
	GENERATED_BODY()
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> Mesh;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> Material;
	UPROPERTY() TObjectPtr<UTexture2D> Ground;
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

	/// True once the finest tile under this place is in the scene (with its collision requested).
	bool IsGroundReady(const FVector& Location) const;

	int32 GetTileCount() const { return Tiles.Num(); }
	int32 GetTreeCount() const { return TreesShown; }
	int32 GetPendingCount() const { return Pending.Num(); }
	int32 GetDesiredCount() const { return Desired.Num(); }
	int32 GetMissingCount() const { return Missing; }
	int64 GetTilesBuilt() const { return TilesBuilt; }
	double GetAverageBuildMs() const { return TilesBuilt > 0 ? TotalBuildMs / TilesBuilt : 0; }
	double GetMaxCommitMs() const { return MaxCommitMs; }
	void ResetStats() { MaxCommitMs = 0; }

	static TSharedPtr<FWwTileData> BuildTile(const ww::FTerrainSampler& Sampler, const FWwTileKey& Key, double BaseTile, int32 Cells, bool bTrees);

private:
	struct FTreeBatch { int32 Component; TArray<FPrimitiveInstanceId> Ids; };
	struct FTile { int32 Slot = -1; bool bVisible = false; TArray<FWwTileData::FTree> Trees; TArray<FTreeBatch> Batches; };
	/// Tree instances live in components covering 16 km squares (instance positions are single precision).
	struct FTreeKey
	{
		int64 X, Y; int32 Mesh;
		bool operator==(const FTreeKey& O) const { return X == O.X && Y == O.Y && Mesh == O.Mesh; }
		friend uint32 GetTypeHash(const FTreeKey& K) { return HashCombine(HashCombine(::GetTypeHash(K.X), ::GetTypeHash(K.Y)), ::GetTypeHash(K.Mesh)); }
	};
	struct FShared
	{
		TQueue<TSharedPtr<FWwTileData>, EQueueMode::Mpsc> Done;
		std::atomic<int32> InFlight{ 0 };
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
	double TileSize(int32 Level) const { return BaseTile * (double)((int64)1 << Level); }

	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> TerrainMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> WaterMaterial;
	UPROPERTY(Transient) TArray<FWwTileSlot> Slots;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMesh>> TreeMeshes;
	UPROPERTY(Transient) TArray<TObjectPtr<UInstancedStaticMeshComponent>> TreeComponents;
	TArray<int32> Free;
	/// Per species and model (near, distant): first index into TreeMeshes and how many parts (bark, foliage, cards).
	struct FTreeModel { int32 First = 0, Count = 0; };
	TArray<FTreeModel> TreeModels;
	TMap<FTreeKey, int32> TreeLookup;
	int32 TreesShown = 0;

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
