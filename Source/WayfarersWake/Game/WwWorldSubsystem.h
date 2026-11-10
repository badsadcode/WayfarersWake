#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "WorldGen/WwWorld.h"
#include "WorldGen/WwOcean.h"
#include "Game/WwGameplay.h"
#include <atomic>
#include "WwWorldSubsystem.generated.h"

/// What defines a generated world. Replicated to clients, who rebuild the same world locally (the terrain itself is
/// never sent): the same settings always give bit-identical terrain.
USTRUCT(BlueprintType)
struct FWwWorldSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World") int32 Seed = 7319;
	/// World type id (FWorldRecipe::Catalog): balanced, continental, archipelago, mountainous, many-continents, islands-mainland, vast-world.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World") FString WorldType = TEXT("balanced");
	/// Width and length in km: 32, 64, 128, 256, 512, 768, 1024, 2048 or 4096. 0 = the world type's native size.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World") float SizeKm = 64;
	/// Terrain revision (versioned generation: a world keeps the revision it was created with).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World") int32 TerrainRevision = 44;

	bool operator==(const FWwWorldSettings& O) const { return Seed == O.Seed && WorldType == O.WorldType && SizeKm == O.SizeKm && TerrainRevision == O.TerrainRevision; }
	bool operator!=(const FWwWorldSettings& O) const { return !(*this == O); }
};

UENUM()
enum class EWwWorldState : uint8 { Idle, Building, Ready, Failed };

/// Owns the generated world of this game instance: builds the plan off the game thread and hands out the sampler.
/// Coordinates: the generator works in metres (X east, Z south, height up); Unreal X = east, Y = south, Z = up, cm.
UCLASS()
class WAYFARERSWAKE_API UWwWorldSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/// Starts building the world for these settings (no-op if it is already this world).
	void StartWorld(const FWwWorldSettings& Settings);

	EWwWorldState GetState() const { return State; }
	const FWwWorldSettings& GetSettings() const { return Current; }
	FString GetStatusText() const;
	double GetBuildSeconds() const { return BuildSeconds; }
	/// Bumped every time a new world becomes ready (streamers rebuild when it changes).
	int32 GetWorldSerial() const { return WorldSerial; }

	/// The terrain authority; null until Ready. Thread-safe to sample.
	std::shared_ptr<ww::FTerrainSampler> GetSampler() const { return Sampler; }
	/// The sea's waves: one simulated sea for the whole game (the sea state and the wind are fixed until the weather is
	/// ported: Thornwake's defaults, sea state 0.5, wind from the north-west).
	std::shared_ptr<ww::FOcean> GetOcean();
	/// The placeholder mechanics of this world (null until the world is ready, or if the content files are damaged).
	FWwGameplay* GetGameplay();
	/// The sea's surface above its still level (m) at a world position (m) where the still water is Depth deep, at the
	/// game's clock: what the water shader shows, for swimming.
	double SeaSurface(double X, double Z, double Depth, double Time);
	/// The wind (fixed): the direction it blows to (east, south) and its speed, m/s.
	static constexpr double WindX = 0.496;
	static constexpr double WindZ = 0.868;
	static constexpr double WindSpeed = 5.512;

	/// A dry, gently sloping place on land to start at (metres), near the first landmass's centre.
	bool FindSpawn(double& OutX, double& OutZ, double& OutHeight) const;
	/// Name of the strongest biome at a place.
	FString BiomeNameAt(double X, double Z) const;

	static FVector ToUnreal(double X, double Z, double Height) { return FVector(X * 100.0, Z * 100.0, Height * 100.0); }
	static void ToWorld(const FVector& P, double& X, double& Z) { X = P.X / 100.0; Z = P.Y / 100.0; }

	/// Reads the content data (profiles, biomes). False with an error if damaged.
	static bool LoadCatalog(std::vector<ww::FWorldProfile>& Profiles, ww::FBiomeRegistry& Biomes, FString& Error);
	/// Bit-exactness check of this build of the generator against a Thornwake fingerprint (balanced, seed 42).
	static bool RunSelfTest(FString& Report);

private:
	FWwWorldSettings Current;
	EWwWorldState State = EWwWorldState::Idle;
	std::shared_ptr<ww::FTerrainSampler> Sampler;
	std::shared_ptr<ww::FOcean> Ocean;
	std::unique_ptr<FWwGameplay> Gameplay;
	std::shared_ptr<ww::FTerrainSampler> GameplayWorld; // the world the gameplay object was made for
	TSharedPtr<std::atomic<int32>, ESPMode::ThreadSafe> Stage;
	FString Failure;
	double BuildSeconds = 0;
	int32 WorldSerial = 0, BuildRequest = 0;
};
