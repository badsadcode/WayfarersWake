#include "Game/WwTerrainStreamer.h"
#include "Game/WwWorldSubsystem.h"
#include "WayfarersWake.h"
#include "ProceduralMeshComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "Misc/PackageName.h"
#include "WorldGen/WwVegetation.h"
#include "WorldGen/WwFlora.h"
#include "WorldGen/WwWaterSheet.h"
#include "WorldGen/WwRocks.h"
#include "Game/WwPropMeshes.h"
#include "Misc/CommandLine.h"
#include "Tasks/Task.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

AWwTerrainStreamer::AWwTerrainStreamer()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Shared = MakeShared<FShared, ESPMode::ThreadSafe>();
}

void AWwTerrainStreamer::EndPlay(const EEndPlayReason::Type Reason)
{
	ClearAll();
	Super::EndPlay(Reason);
}

void AWwTerrainStreamer::ClearAll()
{
	for (FWwTileSlot& Slot : Slots)
	{
		if (Slot.Mesh) Slot.Mesh->DestroyComponent();
		if (Slot.Plants) Slot.Plants->DestroyComponent();
		if (Slot.Leaves) Slot.Leaves->DestroyComponent();
	}
	PlantsShown = 0;
	for (auto& Shown : FallsShown) if (Shown.Value) Shown.Value->DestroyComponent();
	FallsShown.Empty(); FallsBuilding.Empty(); FallSpray.Empty();
	if (SprayMesh) SprayMesh->ClearInstances();
	for (UInstancedStaticMeshComponent* Component : TreeComponents) if (Component) Component->DestroyComponent();
	Tiles.Empty(); Pending.Empty(); Desired.Empty(); Slots.Empty(); Free.Empty(); TreeComponents.Empty(); TreeLookup.Empty();
	TreesShown = 0; RocksShown = 0;
	// Jobs still running hold the old shared state and sampler; their results are never read.
	Shared = MakeShared<FShared, ESPMode::ThreadSafe>();
	bDirty = true;
}

// ---------------------------------------------------------------------------------------------------- tile build
static FColor Bytes(double R, double G, double B, double A)
{
	auto Q = [](double V) { return (uint8)FMath::Clamp(FMath::RoundToInt(V * 255.0), 0, 255); };
	return FColor(Q(R), Q(G), Q(B), Q(A));
}

TSharedPtr<FWwTileData> AWwTerrainStreamer::BuildTile(const ww::FTerrainSampler& Sampler, const FWwTileKey& Key, double BaseTile, int32 Cells, bool bTrees, double Grass, const ww::FFallSet* Falls, bool bRocks)
{
	const double Started = FPlatformTime::Seconds();
	TSharedPtr<FWwTileData> Data = MakeShared<FWwTileData>();
	Data->Key = Key;
	const ww::FWorldPlan& Plan = *Sampler.Plan;
	const double Size = BaseTile * (double)((int64)1 << Key.Level), Spacing = Size / Cells;
	const double X0 = Key.X * Size, Z0 = Key.Y * Size, Sea = Plan.Profile().SeaLevel;
	const int32 Side = Cells + 1, Grid = Cells + 3; // one extra ring of samples for normals (exact across tile edges)
	TArray<ww::FTerrainSample> Samples;
	Samples.SetNumUninitialized(Grid * Grid);
	for (int32 j = 0; j < Grid; j++)
		for (int32 i = 0; i < Grid; i++)
			Samples[j * Grid + i] = Sampler.Sample(X0 + (i - 1) * Spacing, Z0 + (j - 1) * Spacing, Spacing * 2);
	auto At = [&](int32 i, int32 j) -> const ww::FTerrainSample& { return Samples[(j + 1) * Grid + (i + 1)]; };

	// The vertex data follows Thornwake's TileBuilder: the material (M_WwTerrain) is a port of its ground shader.
	const std::vector<ww::FBiome>& Pool = Plan.Biomes->Registry.Pool;
	const bool bBiomes = Plan.Climate->Settings.Biomes;
	const int32 VertexCount = Side * Side, Total = VertexCount + Side * 4;
	Data->Vertices.Reserve(Total); Data->Normals.Reserve(Total); Data->Colors.Reserve(Total); Data->Uv0.Reserve(Total); Data->Uv1.Reserve(Total);
	Data->GroundSide = Side;
	Data->Ground.SetNumUninitialized(Side * 3 * Side);
	// What the plant rules read (nearest-level tiles only), at full precision as in Thornwake.
	const bool bFlora = Key.Level == 0 && Grass > 0;
	// (rocks reach two levels further out; they read the same grid and a little more)
	const bool bScatter = bRocks && Key.Level <= 2, bCover = bFlora || bScatter;
	TArray<float> FloraHeight, FloraUp, FloraShore, FloraPlants, FloraWetness, FloraGrass, CoverNormal, CoverRock, CoverPropRocks, CoverPropLogs, CoverLook, CoverCold;
	TArray<double> FloraForest;
	TArray<ww::FTerrainSample> FloraSamples;
	if (bScatter)
	{
		CoverNormal.SetNumUninitialized(VertexCount * 3); CoverRock.SetNumUninitialized(VertexCount * 3); CoverPropRocks.SetNumUninitialized(VertexCount);
		CoverPropLogs.SetNumUninitialized(VertexCount); CoverLook.SetNumUninitialized(VertexCount); CoverCold.SetNumUninitialized(VertexCount);
	}
	if (bCover)
	{
		FloraHeight.SetNumUninitialized(VertexCount); FloraUp.SetNumUninitialized(VertexCount); FloraShore.SetNumUninitialized(VertexCount);
		FloraPlants.SetNumUninitialized(VertexCount); FloraWetness.SetNumUninitialized(VertexCount); FloraGrass.SetNumUninitialized(VertexCount * 3);
		FloraForest.SetNumUninitialized(VertexCount); FloraSamples.SetNumUninitialized(VertexCount);
	}
	for (int32 j = 0; j < Side; j++)
		for (int32 i = 0; i < Side; i++)
		{
			const ww::FTerrainSample& S = At(i, j);
			const double X = X0 + i * Spacing, Z = Z0 + j * Spacing, H = S.Height;
			const double Dx = (At(i + 1, j).Height - At(i - 1, j).Height) / (2 * Spacing), Dz = (At(i, j + 1).Height - At(i, j - 1).Height) / (2 * Spacing);
			FVector Normal(-Dx, -Dz, 1.0);
			Normal.Normalize();
			Data->Vertices.Add(FVector(i * Spacing * 100.0, j * Spacing * 100.0, H * 100.0));
			Data->Normals.Add(Normal);

			// Submerged-bed tint from the signed depth below the nearest water surface (dry vertices beside water use
			// the neighbouring level): exactly 0.5 at the waterline, so the tint follows the real shoreline.
			bool bLevel = S.Water != ww::EWaterKind::None;
			double Level = S.WaterLevel;
			if (!bLevel)
				for (int32 b = -1; b <= 1; b++)
					for (int32 a = -1; a <= 1; a++)
					{
						const ww::FTerrainSample& N = At(i + a, j + b);
						if (N.Water == ww::EWaterKind::None) continue;
						if (!bLevel || N.WaterLevel > Level) Level = N.WaterLevel;
						bLevel = true;
					}
			const double Submerged = bLevel ? FMath::Clamp(0.5 + (Level - H) * 0.5, 0.0, 1.0) : 0.0;
			// Beach: a band along the sea (low land influence) and only a few metres above it.
			double Beach = S.Water != ww::EWaterKind::None ? 0.0 : (1 - ww::DetMath::Smooth(S.Land / .0025)) * (1 - ww::DetMath::Smooth((H - Sea) / 2.4));
			// Rocky shore patches: bare rock instead of sand (and blocky rocks there).
			const double Rocky = ww::FRocks::RockyShore(X, Z, H - Sea);
			Beach *= 1 - Rocky;
			const double Forest = ww::FVegetation::ForestDensity(Sampler, X, Z, S);
			Data->Colors.Add(Bytes(Forest, S.Mountain, Beach, Submerged));

			// Biome colours, blended between the two best-fitting biomes; snow from the biome and the cold.
			int Ia, Ib; double Share;
			Plan.Biomes->Sample(X, Z, Ia, Ib, Share);
			const ww::FBiome& A = Pool[Ia]; const ww::FBiome& B = Pool[Ib];
			const double Sb = 1 - Share, Dark = 1 - .35 * (A.Darkness * Share + B.Darkness * Sb);
			double Temperature, Rain;
			Plan.Climate->At(X, Z, H - Sea, Temperature, Rain);
			const double Snow = Plan.Climate->Snow(A.Snow * Share + B.Snow * Sb, Temperature, H - Sea);
			double Plants = A.GrassAmount * Share + B.GrassAmount * Sb;
			if (bBiomes) Plants *= ww::DetMath::Smooth((Temperature + 6) / 6); // alpine meadows thin out from 0 C to -6 C
			const double LavaFree = 1 - ww::DetMath::Smooth(S.Lava / .1);
			FColor* Texel = &Data->Ground[j * Side * 3 + i];
			Texel[0] = Bytes((A.Grass.R * Share + B.Grass.R * Sb) * Dark, (A.Grass.G * Share + B.Grass.G * Sb) * Dark, (A.Grass.B * Share + B.Grass.B * Sb) * Dark, Snow);
			Texel[Side] = Bytes((A.Dry.R * Share + B.Dry.R * Sb) * Dark, (A.Dry.G * Share + B.Dry.G * Sb) * Dark, (A.Dry.B * Share + B.Dry.B * Sb) * Dark,
				FMath::Min(1.0, Plants) * LavaFree * (1 - .8 * S.Scree));
			Texel[Side * 2] = Bytes(A.Rock.R * Share + B.Rock.R * Sb, A.Rock.G * Share + B.Rock.G * Sb, A.Rock.B * Share + B.Rock.B * Sb, A.Wetness * Share + B.Wetness * Sb);
			Data->Uv0.Add(FVector2D(FMath::Max(A.RockExposure * Share + B.RockExposure * Sb, Rocky * 1.6), S.Scree));
			if (bScatter)
			{
				const int32 K = j * Side + i;
				CoverNormal[K * 3] = (float)Normal.X; CoverNormal[K * 3 + 1] = (float)Normal.Z; CoverNormal[K * 3 + 2] = (float)Normal.Y;
				CoverRock[K * 3] = (float)(A.Rock.R * Share + B.Rock.R * Sb); CoverRock[K * 3 + 1] = (float)(A.Rock.G * Share + B.Rock.G * Sb); CoverRock[K * 3 + 2] = (float)(A.Rock.B * Share + B.Rock.B * Sb);
				CoverPropRocks[K] = (float)((A.PropRocks * Share + B.PropRocks * Sb) * LavaFree);
				CoverPropLogs[K] = (float)((A.PropLogs * Share + B.PropLogs * Sb) * LavaFree);
				CoverLook[K] = (float)(Share >= .5 ? A.RockLook : B.RockLook);
				CoverCold[K] = (float)((A.bCold ? Share : 0) + (B.bCold ? Sb : 0));
			}
			if (bCover)
			{
				const int32 K = j * Side + i;
				FloraHeight[K] = (float)H; FloraUp[K] = (float)Normal.Z; FloraShore[K] = (float)Submerged; FloraForest[K] = Forest; FloraSamples[K] = S;
				FloraPlants[K] = (float)(FMath::Min(1.0, Plants) * LavaFree * (1 - .8 * S.Scree));
				FloraWetness[K] = (float)(A.Wetness * Share + B.Wetness * Sb);
				FloraGrass[K * 3] = (float)((A.Grass.R * Share + B.Grass.R * Sb) * Dark);
				FloraGrass[K * 3 + 1] = (float)((A.Grass.G * Share + B.Grass.G * Sb) * Dark);
				FloraGrass[K * 3 + 2] = (float)((A.Grass.B * Share + B.Grass.B * Sb) * Dark);
			}
			Data->Uv1.Add(FVector2D((double)i / Cells, (double)j / Cells));
		}

	ww::FFloraGrid FloraGrid;
	if (bCover)
	{
		FloraGrid.Side = Side; FloraGrid.Spacing = Spacing; FloraGrid.X0 = X0; FloraGrid.Z0 = Z0;
		FloraGrid.Height = FloraHeight.GetData(); FloraGrid.Up = FloraUp.GetData(); FloraGrid.Forest = FloraForest.GetData(); FloraGrid.Samples = FloraSamples.GetData();
		FloraGrid.Shore = FloraShore.GetData(); FloraGrid.Plants = FloraPlants.GetData(); FloraGrid.Wetness = FloraWetness.GetData(); FloraGrid.Grass = FloraGrass.GetData();
		FloraGrid.bClimateLine = bBiomes;
	}
	if (bScatter)
	{
		ww::FRockGrid RockGrid;
		RockGrid.Base = FloraGrid;
		RockGrid.Normal = CoverNormal.GetData(); RockGrid.Rock = CoverRock.GetData(); RockGrid.PropRocks = CoverPropRocks.GetData(); RockGrid.PropLogs = CoverPropLogs.GetData();
		RockGrid.RockLook = CoverLook.GetData(); RockGrid.Cold = CoverCold.GetData();
		const ww::u64 CoverSeed = ww::FFlora::CoverSeed(Sampler);
		std::vector<ww::FRockInstance> Rocks, Props;
		ww::FRocks::Scatter(RockGrid, Key.Level, Size, Sea, CoverSeed, Plan.Style.RockAmount, Rocks);
		if (Key.Level <= 1) ww::FRocks::Props(RockGrid, Size, Sea, CoverSeed, Plan.Style.PropAmount, Props);
		Data->Scatter.Reserve((int32)(Rocks.size() + Props.size()));
		auto Add = [&](const ww::FRockInstance& R, int32 Mesh)
		{
			// (the instance's axes from the generator's x, up, z to Unreal's x, y, up, on both sides: the mesh was turned the same way)
			FMatrix M = FMatrix::Identity;
			M.SetAxis(0, FVector(R.Ax[0], R.Ax[2], R.Ax[1]));
			M.SetAxis(1, FVector(R.Az[0], R.Az[2], R.Az[1]));
			M.SetAxis(2, FVector(R.Ay[0], R.Ay[2], R.Ay[1]));
			M.SetOrigin(FVector((X0 + R.X) * 100.0, (Z0 + R.Z) * 100.0, R.Y * 100.0));
			Data->Scatter.Add({ Mesh, FTransform(M), FLinearColor(R.R, R.G, R.B, R.Look) });
		};
		for (const ww::FRockInstance& R : Rocks) Add(R, R.Kind);
		for (const ww::FRockInstance& R : Props) Add(R, 100 + R.Kind);
	}
	if (bFlora)
	{
		const ww::u64 CoverSeed = ww::FFlora::CoverSeed(Sampler);
		std::vector<ww::FFloraInstance> Flora;
		ww::FFlora::Land(FloraGrid, Size, Sea, CoverSeed, Grass, Flora);
		ww::FFlora::Water(FloraGrid, Size, Sea, CoverSeed, *Plan.Biomes, FMath::Min(1.5, Grass), Flora);
		Data->Plants.Reserve((int32)Flora.size()); Data->PlantData.Reserve((int32)Flora.size() * 4);
		for (const ww::FFloraInstance& P : Flora)
		{
			const bool bFlat = ww::FFlora::Species(P.Species).Form == ww::EFloraForm::Floating;
			// (Thornwake: turn about the vertical, then lean about the plant's own sideways axis; the sprite is 1 m)
			const FQuat Turn = FQuat(FVector::UpVector, P.Yaw) * FQuat(FVector::ForwardVector, P.Tilt);
			const FTransform Transform(Turn, FVector(P.X * 100.0, P.Z * 100.0, P.Y * 100.0), FVector(P.Size));
			(bFlat ? Data->Leaves : Data->Plants).Add(Transform);
			(bFlat ? Data->LeafData : Data->PlantData).Append({ (float)P.Species, P.R, P.G, P.B });
		}
	}

	if (bTrees)
	{
		std::vector<ww::FLatticeTree> Trees;
		ww::FVegetation::TreesIn(Sampler, X0, Z0, Size, Trees);
		Data->Trees.Reserve((int32)Trees.size());
		for (const ww::FLatticeTree& T : Trees)
			if (T.Species < ww::FVegetation::SpeciesWithArt) // (the placeholder plants of Thornwake have no models yet)
				Data->Trees.Add({ T.Species, FVector(T.X * 100.0, T.Z * 100.0, T.Y * 100.0), T.Scale, T.Yaw });
	}

	Data->Triangles.Reserve(Cells * Cells * 6 + Cells * 4 * 12);
	for (int32 j = 0; j < Cells; j++)
		for (int32 i = 0; i < Cells; i++)
		{
			const int32 A = j * Side + i, B = A + 1, C = A + Side, D = C + 1;
			// Split along the diagonal with the smaller height difference (fewer saw-teeth on ridges and banks).
			const bool bFlip = FMath::Abs(Data->Vertices[A].Z - Data->Vertices[D].Z) > FMath::Abs(Data->Vertices[B].Z - Data->Vertices[C].Z);
			if (bFlip) Data->Triangles.Append({ A, C, B, B, C, D });
			else Data->Triangles.Append({ A, C, D, A, D, B });
		}

	// Skirts: a strip hanging down from every edge hides the cracks between tiles of different LOD.
	const double SkirtDrop = FMath::Max(400.0, Spacing * 100.0 * 1.5);
	auto Skirt = [&](int32 StartIndex, int32 Step)
	{
		const int32 First = Data->Vertices.Num();
		for (int32 k = 0; k < Side; k++)
		{
			const int32 Source = StartIndex + k * Step;
			const FVector Position = Data->Vertices[Source] - FVector(0, 0, SkirtDrop), Normal = Data->Normals[Source]; // (copies: Add may reallocate)
			const FColor Colour = Data->Colors[Source];
			const FVector2D A = Data->Uv0[Source], B = Data->Uv1[Source];
			Data->Vertices.Add(Position);
			Data->Normals.Add(Normal);
			Data->Colors.Add(Colour);
			Data->Uv0.Add(A);
			Data->Uv1.Add(B);
		}
		for (int32 k = 0; k < Cells; k++)
		{
			const int32 A = StartIndex + k * Step, B = StartIndex + (k + 1) * Step, C = First + k, D = First + k + 1;
			Data->Triangles.Append({ A, B, C, B, D, C, A, C, B, B, C, D }); // both sides
		}
	};
	Skirt(0, 1); Skirt(Cells * Side, 1); Skirt(0, Side); Skirt(Cells, Side);

	// Water surface: Thornwake's sheet on the tile's vertex grid (banks carried under the ground, an edge fade, currents).
	{
		TArray<ww::FTerrainSample> SheetSamples;
		TArray<double> Heights;
		SheetSamples.SetNumUninitialized(VertexCount); Heights.SetNumUninitialized(VertexCount);
		for (int32 j = 0; j < Side; j++)
			for (int32 i = 0; i < Side; i++) { SheetSamples[j * Side + i] = At(i, j); Heights[j * Side + i] = At(i, j).Height; }
		ww::FWaterSheet Sheet;
		Sheet.Build(SheetSamples.GetData(), Heights.GetData(), Side, Spacing, Sea, *Plan.Hydrology, X0, Z0, Falls);
		if (Sheet.bAny && !Sheet.Indices.empty())
		{
			Data->WaterVertices.Reserve(VertexCount); Data->WaterNormals.Reserve(VertexCount); Data->WaterColors.Reserve(VertexCount);
			Data->WaterUv0.Reserve(VertexCount); Data->WaterUv1.Reserve(VertexCount);
			for (int32 j = 0; j < Side; j++)
				for (int32 i = 0; i < Side; i++)
				{
					const int32 K = j * Side + i;
					Data->WaterVertices.Add(FVector(i * Spacing * 100.0, j * Spacing * 100.0, Sheet.Level[K] * 100.0));
					Data->WaterNormals.Add(FVector::UpVector);
					Data->WaterColors.Add(Bytes(Sheet.Depth[K], Sheet.Kind[K], Sheet.Plume[K], Sheet.Edge[K]));
					Data->WaterUv0.Add(FVector2D(Sheet.FlowX[K], Sheet.FlowZ[K]));
					Data->WaterUv1.Add(FVector2D((double)i / Cells, (double)j / Cells));
				}
			Data->WaterTriangles.Reserve((int32)Sheet.Indices.size());
			for (size_t T = 0; T + 2 < Sheet.Indices.size(); T += 3) // (Thornwake's a, b, c is clockwise seen from above here)
				Data->WaterTriangles.Append({ Sheet.Indices[T], Sheet.Indices[T + 2], Sheet.Indices[T + 1] });
		}
	}
	Data->BuildMs = (FPlatformTime::Seconds() - Started) * 1000.0;
	return Data;
}

// ---------------------------------------------------------------------------------------------------- streaming
void AWwTerrainStreamer::GatherViewers(TArray<FVector2D>& Out) const
{
	UWorld* World = GetWorld();
	if (!World) return;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		if (!PC) continue;
		FVector Location; FRotator Rotation;
		if (const APawn* Pawn = PC->GetPawn()) Location = Pawn->GetActorLocation();
		else PC->GetPlayerViewPoint(Location, Rotation);
		Out.Add(FVector2D(Location.X / 100.0, Location.Y / 100.0));
	}
}

void AWwTerrainStreamer::Visit(const FWwTileKey& Key, const TArray<FVector2D>& Viewers)
{
	const double Size = TileSize(Key.Level), MinX = Key.X * Size, MinY = Key.Y * Size;
	if (MinX > WorldHalf || MinY > WorldHalf || MinX + Size < -WorldHalf || MinY + Size < -WorldHalf) return; // beyond the world's edge
	double Nearest = TNumericLimits<double>::Max();
	for (const FVector2D& V : Viewers)
	{
		const double Dx = FMath::Max3(MinX - V.X, 0.0, V.X - (MinX + Size)), Dy = FMath::Max3(MinY - V.Y, 0.0, V.Y - (MinY + Size));
		Nearest = FMath::Min(Nearest, FMath::Sqrt(Dx * Dx + Dy * Dy));
	}
	if (Key.Level > 0 && Nearest < Size * SplitRange)
	{
		for (int32 c = 0; c < 4; c++) Visit(FWwTileKey{ Key.Level - 1, Key.X * 2 + (c & 1), Key.Y * 2 + (c >> 1) }, Viewers);
		return;
	}
	Desired.Add(Key);
}

void AWwTerrainStreamer::ComputeDesired(const TArray<FVector2D>& Viewers)
{
	Desired.Reset();
	const int32 Top = Levels - 1;
	const double Root = TileSize(Top);
	TSet<FWwTileKey> Roots;
	for (const FVector2D& V : Viewers)
	{
		const int64 Cx = (int64)FMath::FloorToDouble(V.X / Root), Cy = (int64)FMath::FloorToDouble(V.Y / Root);
		for (int64 Y = Cy - 1; Y <= Cy + 1; Y++)
			for (int64 X = Cx - 1; X <= Cx + 1; X++) Roots.Add(FWwTileKey{ Top, X, Y });
	}
	for (const FWwTileKey& Key : Roots) Visit(Key, Viewers);
}

void AWwTerrainStreamer::LaunchJobs(const TArray<FVector2D>& Viewers)
{
	Missing = 0;
	TArray<TPair<double, FWwTileKey>> Wanted;
	for (const FWwTileKey& Key : Desired)
	{
		if (Tiles.Contains(Key)) continue;
		Missing++;
		if (Pending.Contains(Key)) continue;
		const double Size = TileSize(Key.Level);
		double Nearest = TNumericLimits<double>::Max();
		for (const FVector2D& V : Viewers) Nearest = FMath::Min(Nearest, FVector2D::Distance(V, FVector2D((Key.X + 0.5) * Size, (Key.Y + 0.5) * Size)));
		Wanted.Emplace(Nearest, Key);
	}
	Wanted.Sort([](const TPair<double, FWwTileKey>& A, const TPair<double, FWwTileKey>& B) { return A.Key < B.Key; });
	for (const auto& Entry : Wanted)
	{
		if (Shared->InFlight.load() >= MaxJobs) break;
		const FWwTileKey Key = Entry.Value;
		Pending.Add(Key);
		Shared->InFlight++;
		UE::Tasks::Launch(UE_SOURCE_LOCATION, [State = Shared, World = Sampler, Key, Base = BaseTile, CellCount = Key.Level == 0 ? Cells * 2 : Cells, Serial = WorldSerial,
			bTrees = Key.Level < TreeLevels && TreeModels.Num() > 0, Grass = PlantMesh ? GrassAmount : 0.0, FallSet = Falls, bRocks = RockMeshFirst >= 0]()
		{
			TSharedPtr<FWwTileData> Data = BuildTile(*World, Key, Base, CellCount, bTrees, Grass, FallSet.get(), bRocks);
			Data->WorldSerial = Serial;
			State->Done.Enqueue(Data);
			State->InFlight--;
		}, UE::Tasks::ETaskPriority::BackgroundNormal);
	}
}

void AWwTerrainStreamer::Commit(const TSharedPtr<FWwTileData>& Data)
{
	// Components are pooled: creating and destroying one per tile left hundreds of MB of garbage between collections
	// during fast travel (measured: +4 GB over 24 km at 400 m/s).
	int32 Index;
	if (Free.Num() > 0) Index = Free.Pop(EAllowShrinking::No);
	else
	{
		UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(this);
		Mesh->bUseAsyncCooking = true;
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetCanEverAffectNavigation(false);
		Mesh->SetupAttachment(RootComponent);
		Mesh->RegisterComponent();
		Index = Slots.AddDefaulted();
		Slots[Index].Mesh = Mesh;
		Slots[Index].Material = UMaterialInstanceDynamic::Create(TerrainMaterial, this);
	}
	FWwTileSlot& Slot = Slots[Index];
	UProceduralMeshComponent* Mesh = Slot.Mesh;

	// The ground colours of the tile go to the material as a small texture (three squares side by side).
	const int32 Width = Data->GroundSide * 3, Height = Data->GroundSide;
	if (!Slot.Ground || Slot.Ground->GetSizeX() != Width || Slot.Ground->GetSizeY() != Height)
	{
		UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
		Texture->SRGB = false;
		Texture->Filter = TF_Bilinear;
		Texture->AddressX = Texture->AddressY = TA_Clamp;
		Texture->NeverStream = true;
		void* Pixels = Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(Pixels, Data->Ground.GetData(), Data->Ground.Num() * sizeof(FColor));
		Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
		Texture->UpdateResource();
		Slot.Ground = Texture;
		Slot.Material->SetTextureParameterValue(TEXT("TileData"), Texture);
	}
	else
	{
		// (the copy and the region are freed once the render thread has taken them)
		uint8* Copy = (uint8*)FMemory::Malloc(Data->Ground.Num() * sizeof(FColor));
		FMemory::Memcpy(Copy, Data->Ground.GetData(), Data->Ground.Num() * sizeof(FColor));
		FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, Width, Height);
		Slot.Ground->UpdateTextureRegions(0, 1, Region, Width * 4, 4, Copy, [](uint8* Source, const FUpdateTextureRegion2D* Regions) { FMemory::Free(Source); delete Regions; });
	}
	const double Size = TileSize(Data->Key.Level);
	const int32 TileCells = Data->GroundSide - 1;
	// Noise coordinates wrap every 65,536 m (as in Thornwake), so the shader keeps its precision far from the origin.
	auto Fold = [](double V) { return (float)(V - 65536.0 * FMath::FloorToDouble(V / 65536.0)); };
	Slot.Material->SetVectorParameterValue(TEXT("Tile"), FLinearColor(Fold(Data->Key.X * Size), Fold(Data->Key.Y * Size), (float)Size, (float)TileCells));
	Slot.Material->SetScalarParameterValue(TEXT("SeaLevel"), (float)Sampler->Plan->Profile().SeaLevel);

	const bool bCollide = Data->Key.Level < CollisionLevels;
	Mesh->SetCollisionEnabled(bCollide ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	if (bCollide) Mesh->SetCollisionProfileName(TEXT("BlockAll"));
	Mesh->SetWorldLocation(FVector(Data->Key.X * Size * 100.0, Data->Key.Y * Size * 100.0, 0.0));
	static const TArray<FVector2D> NoUvs;
	static const TArray<FProcMeshTangent> NoTangents;
	Mesh->CreateMeshSection(0, Data->Vertices, Data->Triangles, Data->Normals, Data->Uv0, Data->Uv1, NoUvs, NoUvs, Data->Colors, NoTangents, bCollide);
	Mesh->SetMaterial(0, Slot.Material);
	if (Data->WaterTriangles.Num() > 0)
	{
		Mesh->CreateMeshSection(1, Data->WaterVertices, Data->WaterTriangles, Data->WaterNormals, Data->WaterUv0, Data->WaterUv1, NoUvs, NoUvs, Data->WaterColors, NoTangents, false);
		if (!Slot.Water) Slot.Water = UMaterialInstanceDynamic::Create(WaterMaterial, this);
		Slot.Water->SetVectorParameterValue(TEXT("Tile"), FLinearColor(Fold(Data->Key.X * Size), Fold(Data->Key.Y * Size), (float)Size, (float)TileCells));
		Mesh->SetMaterial(1, Slot.Water);
	}
	Mesh->SetVisibility(false);
	const FVector Origin(Data->Key.X * Size * 100.0, Data->Key.Y * Size * 100.0, 0.0);
	SetPlants(Slot.Plants, PlantMesh, Data->Plants, Data->PlantData, Origin);
	SetPlants(Slot.Leaves, LeafMesh, Data->Leaves, Data->LeafData, Origin);
	FTile Tile; Tile.Slot = Index;
	Tile.Plants = Data->Plants.Num() + Data->Leaves.Num();
	Tile.Trees = MoveTemp(Data->Trees);
	Tile.Scatter = MoveTemp(Data->Scatter);
	Tiles.Add(Data->Key, MoveTemp(Tile));
	TilesBuilt++; TotalBuildMs += Data->BuildMs;
}

void AWwTerrainStreamer::SetPlants(TObjectPtr<UInstancedStaticMeshComponent>& Component, UStaticMesh* Mesh, const TArray<FTransform>& Transforms, const TArray<float>& Data, const FVector& Origin)
{
	if (Component && Component->GetInstanceCount() > 0) Component->ClearInstances();
	if (Transforms.Num() == 0 || !Mesh) return;
	if (!Component)
	{
		Component = NewObject<UInstancedStaticMeshComponent>(this);
		Component->SetStaticMesh(Mesh);
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetCanEverAffectNavigation(false);
		Component->SetCastShadow(bPlantShadows);
		Component->SetNumCustomDataFloats(4);
		Component->SetupAttachment(RootComponent);
		Component->RegisterComponent();
	}
	Component->SetVisibility(false);
	Component->SetWorldLocation(Origin);
	Component->AddInstances(Transforms, false, false, false);
	// (per plant: sprite layer and colour; set in one block, far cheaper than one call per plant)
	if (Component->PerInstanceSMCustomData.Num() == Data.Num())
	{
		FMemory::Memcpy(Component->PerInstanceSMCustomData.GetData(), Data.GetData(), Data.Num() * sizeof(float));
		Component->MarkRenderStateDirty();
	}
}

void AWwTerrainStreamer::ShowTile(const FWwTileKey& Key, FTile& Tile)
{
	Tile.bVisible = true;
	Slots[Tile.Slot].Mesh->SetVisibility(true);
	if (Slots[Tile.Slot].Plants) Slots[Tile.Slot].Plants->SetVisibility(true);
	if (Slots[Tile.Slot].Leaves) Slots[Tile.Slot].Leaves->SetVisibility(true);
	PlantsShown += Tile.Plants;
	if (Tile.Trees.Num() == 0 && Tile.Scatter.Num() == 0) return;
	// Trees, rocks and props appear and go with their tile, so each stands exactly once whatever the mix of levels on screen.
	const int32 Model = Key.Level < NearTreeLevels ? 0 : 1;
	const double Region = 16384.0 * 100.0;
	TMap<FTreeKey, TArray<FTransform>> Groups;
	TMap<FTreeKey, TArray<float>> GroupData;
	for (const FWwTileData::FScatter& S : Tile.Scatter)
	{
		const int32 Mesh = S.Mesh >= 100 ? PropMeshFirst + S.Mesh - 100 : RockMeshFirst + S.Mesh;
		if (RockMeshFirst < 0 || !TreeMeshes.IsValidIndex(Mesh)) continue;
		const FVector At = S.Transform.GetLocation();
		const int64 Rx = (int64)FMath::FloorToDouble(At.X / Region), Ry = (int64)FMath::FloorToDouble(At.Y / Region);
		FTransform Local = S.Transform;
		Local.SetLocation(At - FVector(Rx * Region, Ry * Region, 0.0));
		const FTreeKey GroupKey{ Rx, Ry, Mesh };
		Groups.FindOrAdd(GroupKey).Add(Local);
		GroupData.FindOrAdd(GroupKey).Append({ S.Data.R, S.Data.G, S.Data.B, S.Data.A });
	}
	RocksShown += Tile.Scatter.Num();
	for (const FWwTileData::FTree& T : Tile.Trees)
	{
		if (TreeModels.Num() == 0) break;
		const FTreeModel& Parts = TreeModels[T.Species * 2 + Model];
		if (Parts.Count == 0) continue;
		const int64 Rx = (int64)FMath::FloorToDouble(T.Position.X / Region), Ry = (int64)FMath::FloorToDouble(T.Position.Y / Region);
		const FTransform Local(FRotator(0.0, FMath::RadiansToDegrees(T.Yaw), 0.0), T.Position - FVector(Rx * Region, Ry * Region, 0.0), FVector(T.Scale));
		for (int32 Part = 0; Part < Parts.Count; Part++) Groups.FindOrAdd(FTreeKey{ Rx, Ry, Parts.First + Part }).Add(Local);
	}
	TreesShown += Tile.Trees.Num();
	for (auto& Group : Groups)
	{
		int32* Found = TreeLookup.Find(Group.Key);
		if (!Found)
		{
			UInstancedStaticMeshComponent* Component = NewObject<UInstancedStaticMeshComponent>(this);
			Component->SetStaticMesh(TreeMeshes[Group.Key.Mesh]);
			Component->SetMobility(EComponentMobility::Movable);
			Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Component->SetCanEverAffectNavigation(false);
			if (RockMeshFirst >= 0 && Group.Key.Mesh >= RockMeshFirst) Component->SetNumCustomDataFloats(4); // (rock colour and look)
			Component->SetupAttachment(RootComponent);
			Component->RegisterComponent();
			Component->SetWorldLocation(FVector(Group.Key.X * Region, Group.Key.Y * Region, 0.0));
			Found = &TreeLookup.Add(Group.Key, TreeComponents.Add(Component));
		}
		FTreeBatch Batch;
		Batch.Component = *Found;
		Batch.Ids = TreeComponents[*Found]->AddInstancesById(Group.Value, false, false);
		if (const TArray<float>* Numbers = GroupData.Find(Group.Key)) TreeComponents[*Found]->SetCustomDataById(Batch.Ids, *Numbers);
		Tile.Batches.Add(MoveTemp(Batch));
	}
}

void AWwTerrainStreamer::Release(FTile& Tile)
{
	UProceduralMeshComponent* Mesh = Slots[Tile.Slot].Mesh;
	Mesh->SetVisibility(false);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->ClearAllMeshSections();
	for (UInstancedStaticMeshComponent* Plants : { Slots[Tile.Slot].Plants.Get(), Slots[Tile.Slot].Leaves.Get() })
		if (Plants) { Plants->SetVisibility(false); Plants->ClearInstances(); }
	if (Tile.bVisible) PlantsShown -= Tile.Plants;
	Free.Add(Tile.Slot);
	for (const FTreeBatch& Batch : Tile.Batches) TreeComponents[Batch.Component]->RemoveInstancesById(Batch.Ids, false);
	if (Tile.Batches.Num() > 0) { TreesShown -= Tile.Trees.Num(); RocksShown -= Tile.Scatter.Num(); }
	Tile.Batches.Empty();
}

void AWwTerrainStreamer::LoadTreeMeshes()
{
	if (TreeModels.Num() > 0) return;
	const std::vector<std::string>& Names = ww::FVegetation::Species();
	static const TCHAR* Parts[] = { TEXT("Bark"), TEXT("Foliage"), TEXT("Card") };
	int32 Absent = 0;
	for (int32 Species = 0; Species < ww::FVegetation::SpeciesWithArt; Species++)
		for (int32 Model = 0; Model < 2; Model++)
		{
			const FString Name = UTF8_TO_TCHAR(Names[Species].c_str());
			const FString Asset = Model == 0 ? FString::Printf(TEXT("TREE_%s"), *Name) : FString::Printf(TEXT("TREE_%s_LOD2"), *Name);
			FTreeModel Entry; Entry.First = TreeMeshes.Num();
			for (const TCHAR* Part : Parts)
			{
				const FString Package = FString::Printf(TEXT("/Game/Trees/%s_L%d/%s/StaticMeshes/%s__%s"), *Name, Model == 0 ? 0 : 2, *Asset, *Asset, Part);
				if (!FPackageName::DoesPackageExist(Package)) continue; // (distant models have no cards)
				const FString Path = FString::Printf(TEXT("%s.%s__%s"), *Package, *Asset, Part);
				if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Path))
				{
					if (TreeMeshes.Num() < 3) UE_LOG(LogWayfarer, Display, TEXT("Tree part %s bounds %s"), *Path, *Mesh->GetBounds().ToString());
					TreeMeshes.Add(Mesh); Entry.Count++;
				}
			}
			if (Entry.Count == 0) Absent++;
			TreeModels.Add(Entry);
		}
	if (Absent > 0) UE_LOG(LogWayfarer, Warning, TEXT("%d tree models missing: run Tools/ImportAssets.cmd"), Absent);
	// Rocks and props: shapes made in code, as in Thornwake (no model files).
	if (!RockMaterial) RockMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_WwRock.M_WwRock"));
	if (!PropMaterial) PropMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_WwProp.M_WwProp"));
	if (RockMaterial && PropMaterial)
	{
		RockMeshFirst = TreeMeshes.Num();
		UMaterialInterface* Big = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_WwRockBig.M_WwRockBig"));
		if (!Big) Big = RockMaterial;
		for (int32 Kind = 0; Kind < ww::FRocks::CliffRock; Kind++)
			TreeMeshes.Add(WwBuildStaticMesh(this, ww::FPropShapes::Rock(Kind), RockMaterial, *FString::Printf(TEXT("WwRock%d"), Kind)));
		// Worn cliff blocks: six shapes near, the same coarse for the middle distance, the near ones again for a cliff's lip.
		for (int32 Shape = 0; Shape < ww::FRocks::CliffShapes; Shape++)
			TreeMeshes.Add(WwBuildStaticMesh(this, ww::FPropShapes::CliffBlock(Shape, false), Big, *FString::Printf(TEXT("WwCliff%d"), Shape)));
		for (int32 Shape = 0; Shape < ww::FRocks::CliffShapes; Shape++)
			TreeMeshes.Add(WwBuildStaticMesh(this, ww::FPropShapes::CliffBlock(Shape, true), Big, *FString::Printf(TEXT("WwCliffCoarse%d"), Shape)));
		for (int32 Shape = 0; Shape < ww::FRocks::CliffShapes; Shape++) { UStaticMesh* Near = TreeMeshes[RockMeshFirst + ww::FRocks::CliffRock + Shape]; TreeMeshes.Add(Near); } // (a copy first: Add may move the array)
		// The rock kit's formations: columns, slab stacks, tiered piles, tors; four variants each.
		for (int32 Kind = 0; Kind < 4; Kind++)
			for (int32 Variant = 0; Variant < 4; Variant++)
				TreeMeshes.Add(WwBuildStaticMesh(this, ww::FPropShapes::Formation(Kind, Variant), Big, *FString::Printf(TEXT("WwFormation%d_%d"), Kind, Variant)));
		check(TreeMeshes.Num() - RockMeshFirst == ww::FRocks::RockKinds);
		PropMeshFirst = TreeMeshes.Num();
		for (int32 Kind = 0; Kind < ww::FRocks::PropKinds; Kind++)
			TreeMeshes.Add(WwBuildStaticMesh(this, ww::FPropShapes::Prop(Kind), PropMaterial, *FString::Printf(TEXT("WwProp%d"), Kind)));
	}
	else UE_LOG(LogWayfarer, Warning, TEXT("M_WwRock or M_WwProp missing: run Tools/CreateAssets.cmd"));
	UE_LOG(LogWayfarer, Display, TEXT("Trees: %d mesh parts for %d species"), TreeMeshes.Num(), (int32)ww::FVegetation::SpeciesWithArt);
}

void AWwTerrainStreamer::ResolveVisibility()
{
	// A tile no longer wanted goes once every wanted tile over its ground is in the scene; wanted tiles show once
	// nothing old lies over them. So the ground is always drawn exactly once, and never missing.
	TArray<FWwTileKey> Stale;
	for (const auto& Pair : Tiles) if (!Desired.Contains(Pair.Key)) Stale.Add(Pair.Key);
	TArray<FWwTileKey> Remaining;
	for (const FWwTileKey& Old : Stale)
	{
		bool bCovered = true;
		for (const FWwTileKey& Want : Desired)
			if (Want.Overlaps(Old) && !Tiles.Contains(Want)) { bCovered = false; break; }
		if (!bCovered) { Remaining.Add(Old); continue; }
		FTile Tile = Tiles.FindAndRemoveChecked(Old);
		Release(Tile);
	}
	for (auto& Pair : Tiles)
	{
		if (Pair.Value.bVisible || !Desired.Contains(Pair.Key)) continue;
		bool bBlocked = false;
		for (const FWwTileKey& Old : Remaining) if (Old.Overlaps(Pair.Key)) { bBlocked = true; break; }
		if (bBlocked) continue;
		ShowTile(Pair.Key, Pair.Value);
	}
}

void AWwTerrainStreamer::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UGameInstance* GameInstance = GetGameInstance();
	UWwWorldSubsystem* WorldSystem = GameInstance ? GameInstance->GetSubsystem<UWwWorldSubsystem>() : nullptr;
	if (!WorldSystem || WorldSystem->GetState() != EWwWorldState::Ready) return;
	if (WorldSystem->GetWorldSerial() != WorldSerial)
	{
		ClearAll();
		WorldSerial = WorldSystem->GetWorldSerial();
		Sampler = WorldSystem->GetSampler();
		Falls = std::make_shared<ww::FFallSet>(*Sampler->Plan->Hydrology);
		UE_LOG(LogWayfarer, Display, TEXT("Waterfalls in this world: %d"), (int32)Falls->All().size());
		WorldHalf = Sampler->Plan->Profile().WorldSize * 0.5;
		if (!TerrainMaterial) TerrainMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_WwTerrain.M_WwTerrain"));
		if (!WaterMaterial) WaterMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_WwWater.M_WwWater"));
		if (!TerrainMaterial) { TerrainMaterial = UMaterial::GetDefaultMaterial(MD_Surface); UE_LOG(LogWayfarer, Warning, TEXT("M_WwTerrain missing: run Tools/CreateAssets.cmd")); }
		if (!WaterMaterial) WaterMaterial = TerrainMaterial;
		LoadTreeMeshes();
		if (!FallMaterial) FallMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_WwWaterfall.M_WwWaterfall"));
		if (!SprayMaterial) SprayMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_WwSpray.M_WwSpray"));
		if (!FallFoamMaterial) FallFoamMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_WwWaterfallFoam.M_WwWaterfallFoam"));
		if (!PlantMesh)
		{
			PlantMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Plants/SM_WwPlantUpright.SM_WwPlantUpright"));
			LeafMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Plants/SM_WwPlantFlat.SM_WwPlantFlat"));
			if (!PlantMesh) UE_LOG(LogWayfarer, Warning, TEXT("Plant meshes missing: run Tools/ImportAssets.cmd"));
			FParse::Value(FCommandLine::Get(), TEXT("WwGrass="), GrassAmount);
			bPlantShadows = !FParse::Param(FCommandLine::Get(), TEXT("WwNoPlantShadows"));
		}
	}

	TArray<FVector2D> Viewers;
	GatherViewers(Viewers);
	if (Viewers.Num() == 0) return;
	bool bMoved = Viewers.Num() != LastViewers.Num();
	for (int32 i = 0; !bMoved && i < Viewers.Num(); i++) bMoved = FVector2D::DistSquared(Viewers[i], LastViewers[i]) > FMath::Square(BaseTile * 0.125);
	if (bMoved || bDirty)
	{
		ComputeDesired(Viewers);
		LastViewers = Viewers;
		bDirty = false;
	}

	// Commit finished tiles within the frame budget.
	const double Started = FPlatformTime::Seconds();
	bool bChanged = false;
	TSharedPtr<FWwTileData> Data;
	while ((FPlatformTime::Seconds() - Started) * 1000.0 < CommitBudgetMs && Shared->Done.Dequeue(Data))
	{
		Pending.Remove(Data->Key);
		if (Data->WorldSerial != WorldSerial || !Desired.Contains(Data->Key) || Tiles.Contains(Data->Key)) continue; // no longer wanted
		Commit(Data);
		bChanged = true;
	}
	MaxCommitMs = FMath::Max(MaxCommitMs, (FPlatformTime::Seconds() - Started) * 1000.0);
	if (bChanged || bMoved) ResolveVisibility();
	LaunchJobs(Viewers);
	UpdateWaterfalls(Viewers, bMoved || bChanged);
	UpdateSpray(Viewers, DeltaSeconds);
}

bool AWwTerrainStreamer::IsGroundReady(const FVector& Location) const
{
	const FWwTileKey Key{ 0, (int64)FMath::FloorToDouble(Location.X / 100.0 / BaseTile), (int64)FMath::FloorToDouble(Location.Y / 100.0 / BaseTile) };
	const FTile* Tile = Tiles.Find(Key);
	return Tile && Tile->bVisible;
}

// ---------------------------------------------------------------------------------------------------- waterfalls
void AWwTerrainStreamer::UpdateWaterfalls(const TArray<FVector2D>& Viewers, bool bMoved)
{
	if (!Falls || !FallMaterial) return;
	const double DrawRange = 4000; // (Thornwake's)
	const std::vector<ww::FFall>& All = Falls->All();
	auto Near = [&](const ww::FFall& F, double Range)
	{
		for (const FVector2D& V : Viewers) if (FMath::Abs(F.LipX - V.X) < Range && FMath::Abs(F.LipZ - V.Y) < Range) return true;
		return false;
	};
	if (bMoved || FallsBuilding.Num() > 0)
	{
		for (auto It = FallsShown.CreateIterator(); It; ++It)
			if (!Near(All[It.Key()], DrawRange * 1.1)) { if (It.Value()) It.Value()->DestroyComponent(); FallSpray.Remove(It.Key()); It.RemoveCurrent(); }
		// Built on worker threads, at most four at a time.
		for (int32 Index = 0; Index < (int32)All.size() && FallsBuilding.Num() < 4; Index++)
		{
			if (!Near(All[Index], DrawRange) || FallsShown.Contains(Index) || FallsBuilding.Contains(Index)) continue;
			FallsBuilding.Add(Index);
			UE::Tasks::Launch(UE_SOURCE_LOCATION, [State = Shared, World = Sampler, Set = Falls, Index]()
			{
				TSharedPtr<ww::FFallModel> Model = MakeShared<ww::FFallModel>(ww::FFallModel::Build(*World, Set->All()[Index]));
				State->FallsDone.Enqueue(TPair<int32, TSharedPtr<ww::FFallModel>>(Index, Model));
			}, UE::Tasks::ETaskPriority::BackgroundNormal);
		}
	}
	TPair<int32, TSharedPtr<ww::FFallModel>> Done;
	if (!Shared->FallsDone.Dequeue(Done)) return; // (one a frame)
	FallsBuilding.Remove(Done.Key);
	if (Done.Key >= (int32)All.size() || FallsShown.Contains(Done.Key) || !Near(All[Done.Key], DrawRange * 1.1)) return;
	const ww::FFall& Fall = All[Done.Key];
	UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(this);
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCanEverAffectNavigation(false);
	Mesh->SetCastShadow(false);
	Mesh->SetTranslucentSortPriority(2); // (drawn after the water: it is see-through too)
	Mesh->SetupAttachment(RootComponent);
	Mesh->RegisterComponent();
	Mesh->SetWorldLocation(FVector(Fall.LipX * 100.0, Fall.LipZ * 100.0, 0.0));
	static const TArray<FVector2D> NoUvs;
	static const TArray<FProcMeshTangent> NoTangents;
	auto Section = [&](int32 Id, const std::vector<ww::FFallModel::FVertex>& Vertices, const std::vector<int>& Indices, UMaterialInterface* Material)
	{
		if (Vertices.empty() || Indices.empty()) return;
		TArray<FVector> Positions, Normals; TArray<FVector2D> Uv0, Uv1; TArray<FColor> Colours; TArray<int32> Triangles;
		Positions.Reserve((int32)Vertices.size()); Uv0.Reserve((int32)Vertices.size()); Uv1.Reserve((int32)Vertices.size()); Colours.Reserve((int32)Vertices.size());
		for (const ww::FFallModel::FVertex& V : Vertices)
		{
			Positions.Add(FVector(V.X * 100.0, V.Z * 100.0, V.Y * 100.0));
			Uv0.Add(FVector2D(V.U, V.V)); Uv1.Add(FVector2D(V.Travel, 0));
			Colours.Add(Bytes(V.Speed, V.Foot, V.OnRock, V.Edge));
		}
		Normals.SetNumZeroed(Positions.Num());
		Triangles.Reserve((int32)Indices.size());
		for (size_t T = 0; T + 2 < Indices.size(); T += 3)
		{
			const int32 A = Indices[T], B = Indices[T + 1], C = Indices[T + 2];
			Triangles.Append({ A, B, C });
			// (the face normal by Thornwake's winding, in its axes: x, up, z; written back in Unreal's)
			const ww::FFallModel::FVertex& Va = Vertices[A]; const ww::FFallModel::FVertex& Vb = Vertices[B]; const ww::FFallModel::FVertex& Vc = Vertices[C];
			const FVector E1(Vb.X - Va.X, Vb.Y - Va.Y, Vb.Z - Va.Z), E2(Vc.X - Va.X, Vc.Y - Va.Y, Vc.Z - Va.Z);
			const FVector N = FVector::CrossProduct(E1, E2);
			const FVector Unreal(N.X, N.Z, N.Y);
			Normals[A] += Unreal; Normals[B] += Unreal; Normals[C] += Unreal;
		}
		for (FVector& N : Normals) N = N.SizeSquared() > 1e-12 ? N.GetSafeNormal() : FVector::UpVector;
		Mesh->CreateMeshSection(Id, Positions, Triangles, Normals, Uv0, Uv1, NoUvs, NoUvs, Colours, NoTangents, false);
		Mesh->SetMaterial(Id, Material);
	};
	Section(0, Done.Value->Curtain, Done.Value->CurtainIndices, FallMaterial);
	Section(1, Done.Value->Foam, Done.Value->FoamIndices, FallFoamMaterial ? FallFoamMaterial.Get() : FallMaterial.Get());
	FallsShown.Add(Done.Key, Mesh);
	const ww::FFallModel& M = *Done.Value;
	FallSpray.Add(Done.Key).Build(Fall.LipX, Fall.LipZ, Fall.Dx, Fall.Dz, (float)Fall.Height(), (float)Fall.Width, FVector(M.FootX, M.FootY, M.FootZ), FVector(M.FaceX, M.FaceY, M.FaceZ),
		M.LipLevel, M.Left, M.Right, Done.Key);
}

// ---------------------------------------------------------------------------------------------------- spray and mist
void AWwTerrainStreamer::UpdateSpray(const TArray<FVector2D>& Viewers, float DeltaSeconds)
{
	PuffsShown = 0;
	if (!Falls || !LeafMesh || !SprayMaterial || Viewers.Num() == 0) return;
	const double MistRange = 1500; // (Thornwake's)
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC) return;
	FVector View; FRotator ViewRotation;
	PC->GetPlayerViewPoint(View, ViewRotation);
	if (!SprayMesh)
	{
		SprayMesh = NewObject<UInstancedStaticMeshComponent>(this);
		SprayMesh->SetStaticMesh(LeafMesh); // (a flat 1 m square)
		SprayMesh->SetMaterial(0, SprayMaterial);
		SprayMesh->SetMobility(EComponentMobility::Movable);
		SprayMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		SprayMesh->SetCanEverAffectNavigation(false);
		SprayMesh->SetCastShadow(false);
		SprayMesh->SetNumCustomDataFloats(3);
		SprayMesh->SetTranslucentSortPriority(3); // (after the water and the falling water: they are see-through too)
		SprayMesh->SetupAttachment(RootComponent);
		SprayMesh->RegisterComponent();
	}
	const FVector Anchor(FMath::GridSnap(View.X, 100000.0), FMath::GridSnap(View.Y, 100000.0), 0.0);
	TArray<FTransform> Transforms; TArray<float> Data;
	// Each puff faces the camera (its square's normal toward it), turned by its own spin.
	const FQuat Facing = FRotationMatrix::MakeFromZX(-ViewRotation.Vector(), FRotationMatrix(ViewRotation).GetUnitAxis(EAxis::Y)).ToQuat();
	const std::vector<ww::FFall>& All = Falls->All();
	for (auto& Pair : FallSpray)
	{
		const ww::FFall& F = All[Pair.Key];
		bool bNear = false;
		for (const FVector2D& V : Viewers) bNear |= FMath::Abs(F.LipX - V.X) + FMath::Abs(F.LipZ - V.Y) < MistRange;
		if (!bNear) continue;
		for (FWwSprayEmitter& E : Pair.Value.Emitters)
		{
			E.Tick(DeltaSeconds);
			for (const FWwSprayEmitter::FPuff& P : E.Puffs)
			{
				const float Density = E.DensityOf(P);
				if (Density <= 0.002f) continue;
				Transforms.Add(FTransform(Facing * FQuat(FVector::UpVector, FMath::DegreesToRadians(P.Angle)), P.Position - Anchor, FVector(E.SizeOf(P))));
				Data.Append({ P.Age / E.Lifetime, P.Seed, Density });
			}
		}
	}
	SprayMesh->ClearInstances();
	PuffsShown = Transforms.Num();
	if (Transforms.Num() == 0) return;
	SprayMesh->SetWorldLocation(Anchor);
	SprayMesh->AddInstances(Transforms, false, false, false);
	if (SprayMesh->PerInstanceSMCustomData.Num() == Data.Num())
	{
		FMemory::Memcpy(SprayMesh->PerInstanceSMCustomData.GetData(), Data.GetData(), Data.Num() * sizeof(float));
		SprayMesh->MarkRenderStateDirty();
	}
}
