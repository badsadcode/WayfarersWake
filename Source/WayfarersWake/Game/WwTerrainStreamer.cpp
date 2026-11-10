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
	for (FWwTileSlot& Slot : Slots) if (Slot.Mesh) Slot.Mesh->DestroyComponent();
	for (UInstancedStaticMeshComponent* Component : TreeComponents) if (Component) Component->DestroyComponent();
	Tiles.Empty(); Pending.Empty(); Desired.Empty(); Slots.Empty(); Free.Empty(); TreeComponents.Empty(); TreeLookup.Empty();
	TreesShown = 0;
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

TSharedPtr<FWwTileData> AWwTerrainStreamer::BuildTile(const ww::FTerrainSampler& Sampler, const FWwTileKey& Key, double BaseTile, int32 Cells, bool bTrees)
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
			const double Beach = S.Water != ww::EWaterKind::None ? 0.0 : (1 - ww::DetMath::Smooth(S.Land / .0025)) * (1 - ww::DetMath::Smooth((H - Sea) / 2.4));
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
			Data->Uv0.Add(FVector2D(A.RockExposure * Share + B.RockExposure * Sb, S.Scree));
			Data->Uv1.Add(FVector2D((double)i / Cells, (double)j / Cells));
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

	// Water surface: sea, lakes, rivers and ponds at their own levels wherever a cell touches water.
	static const FLinearColor WaterColour[5] = { FLinearColor(0, 0, 0, 1), FLinearColor(0.012f, 0.07f, 0.13f, 1), FLinearColor(0.02f, 0.10f, 0.14f, 1),
		FLinearColor(0.03f, 0.12f, 0.15f, 1), FLinearColor(0.05f, 0.10f, 0.07f, 1) };
	for (int32 j = 0; j < Cells; j++)
		for (int32 i = 0; i < Cells; i++)
		{
			const ww::FTerrainSample* Corner[4] = { &At(i, j), &At(i + 1, j), &At(i, j + 1), &At(i + 1, j + 1) };
			double LevelSum = 0; int32 Wet = 0; ww::EWaterKind Kind = ww::EWaterKind::None;
			for (const ww::FTerrainSample* S : Corner)
				if (S->Water != ww::EWaterKind::None) { LevelSum += S->WaterLevel; Wet++; if (Kind == ww::EWaterKind::None) Kind = S->Water; }
			if (Wet == 0) continue;
			const double Mean = LevelSum / Wet;
			const int32 First = Data->WaterVertices.Num();
			for (int32 c = 0; c < 4; c++)
			{
				const double Level = Corner[c]->Water != ww::EWaterKind::None ? Corner[c]->WaterLevel : Mean;
				Data->WaterVertices.Add(FVector((i + (c & 1)) * Spacing * 100.0, (j + (c >> 1)) * Spacing * 100.0, Level * 100.0));
				Data->WaterNormals.Add(FVector::UpVector);
				Data->WaterColors.Add(WaterColour[(int32)Kind]);
			}
			Data->WaterTriangles.Append({ First, First + 2, First + 3, First, First + 3, First + 1 });
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
			bTrees = Key.Level < TreeLevels && TreeModels.Num() > 0]()
		{
			TSharedPtr<FWwTileData> Data = BuildTile(*World, Key, Base, CellCount, bTrees);
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
		Mesh->CreateMeshSection_LinearColor(1, Data->WaterVertices, Data->WaterTriangles, Data->WaterNormals, NoUvs, Data->WaterColors, NoTangents, false, false);
		Mesh->SetMaterial(1, WaterMaterial);
	}
	Mesh->SetVisibility(false);
	FTile Tile; Tile.Slot = Index;
	Tile.Trees = MoveTemp(Data->Trees);
	Tiles.Add(Data->Key, MoveTemp(Tile));
	TilesBuilt++; TotalBuildMs += Data->BuildMs;
}

void AWwTerrainStreamer::ShowTile(const FWwTileKey& Key, FTile& Tile)
{
	Tile.bVisible = true;
	Slots[Tile.Slot].Mesh->SetVisibility(true);
	if (Tile.Trees.Num() == 0 || TreeModels.Num() == 0) return;
	// Trees appear and go with their tile, so each stands exactly once whatever the mix of levels on screen.
	const int32 Model = Key.Level < NearTreeLevels ? 0 : 1;
	const double Region = 16384.0 * 100.0;
	TMap<FTreeKey, TArray<FTransform>> Groups;
	for (const FWwTileData::FTree& T : Tile.Trees)
	{
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
			Component->SetupAttachment(RootComponent);
			Component->RegisterComponent();
			Component->SetWorldLocation(FVector(Group.Key.X * Region, Group.Key.Y * Region, 0.0));
			Found = &TreeLookup.Add(Group.Key, TreeComponents.Add(Component));
		}
		FTreeBatch Batch;
		Batch.Component = *Found;
		Batch.Ids = TreeComponents[*Found]->AddInstancesById(Group.Value, false, false);
		Tile.Batches.Add(MoveTemp(Batch));
	}
}

void AWwTerrainStreamer::Release(FTile& Tile)
{
	UProceduralMeshComponent* Mesh = Slots[Tile.Slot].Mesh;
	Mesh->SetVisibility(false);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->ClearAllMeshSections();
	Free.Add(Tile.Slot);
	for (const FTreeBatch& Batch : Tile.Batches) TreeComponents[Batch.Component]->RemoveInstancesById(Batch.Ids, false);
	if (Tile.Batches.Num() > 0) TreesShown -= Tile.Trees.Num();
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
		WorldHalf = Sampler->Plan->Profile().WorldSize * 0.5;
		if (!TerrainMaterial) TerrainMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_WwTerrain.M_WwTerrain"));
		if (!WaterMaterial) WaterMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_WwWater.M_WwWater"));
		if (!TerrainMaterial) { TerrainMaterial = UMaterial::GetDefaultMaterial(MD_Surface); UE_LOG(LogWayfarer, Warning, TEXT("M_WwTerrain missing: run Tools/CreateAssets.cmd")); }
		if (!WaterMaterial) WaterMaterial = TerrainMaterial;
		LoadTreeMeshes();
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
}

bool AWwTerrainStreamer::IsGroundReady(const FVector& Location) const
{
	const FWwTileKey Key{ 0, (int64)FMath::FloorToDouble(Location.X / 100.0 / BaseTile), (int64)FMath::FloorToDouble(Location.Y / 100.0 / BaseTile) };
	const FTile* Tile = Tiles.Find(Key);
	return Tile && Tile->bVisible;
}
