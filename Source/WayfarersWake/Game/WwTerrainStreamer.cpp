#include "Game/WwTerrainStreamer.h"
#include "Game/WwWorldSubsystem.h"
#include "WayfarersWake.h"
#include "ProceduralMeshComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
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
	for (UProceduralMeshComponent* Mesh : Owned) if (Mesh) Mesh->DestroyComponent();
	Tiles.Empty(); Pending.Empty(); Desired.Empty(); Owned.Empty(); Free.Empty();
	// Jobs still running hold the old shared state and sampler; their results are never read.
	Shared = MakeShared<FShared, ESPMode::ThreadSafe>();
	bDirty = true;
}

// ---------------------------------------------------------------------------------------------------- tile build
static FLinearColor ToLinear(const ww::FColor3& C)
{
	return FLinearColor(FMath::Pow(C.R, 2.2f), FMath::Pow(C.G, 2.2f), FMath::Pow(C.B, 2.2f), 1.f);
}

TSharedPtr<FWwTileData> AWwTerrainStreamer::BuildTile(const ww::FTerrainSampler& Sampler, const FWwTileKey& Key, double BaseTile, int32 Cells)
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

	const std::vector<ww::FBiome>& Pool = Plan.Biomes->Registry.Pool;
	TArray<FLinearColor> Grass, Rock, Soil;
	for (const ww::FBiome& B : Pool) { Grass.Add(ToLinear(B.Grass)); Rock.Add(ToLinear(B.Rock)); Soil.Add(ToLinear(B.Soil)); }
	const FLinearColor Sand(0.52f, 0.44f, 0.27f), Snow(0.86f, 0.88f, 0.92f), DeepBed(0.05f, 0.07f, 0.08f);

	const int32 VertexCount = Side * Side;
	Data->Vertices.Reserve(VertexCount + Side * 4); Data->Normals.Reserve(VertexCount + Side * 4); Data->Colors.Reserve(VertexCount + Side * 4);
	for (int32 j = 0; j < Side; j++)
		for (int32 i = 0; i < Side; i++)
		{
			const ww::FTerrainSample& S = At(i, j);
			const double X = X0 + i * Spacing, Z = Z0 + j * Spacing;
			const double Dx = (At(i + 1, j).Height - At(i - 1, j).Height) / (2 * Spacing), Dz = (At(i, j + 1).Height - At(i, j - 1).Height) / (2 * Spacing);
			FVector Normal(-Dx, -Dz, 1.0);
			Normal.Normalize();
			Data->Vertices.Add(FVector(i * Spacing * 100.0, j * Spacing * 100.0, S.Height * 100.0));
			Data->Normals.Add(Normal);

			// Ground colour (placeholder look until real ground materials): biome palette, rock on steep and mountain
			// ground, sand at the shore and under shallow water, snow by temperature.
			int Ids[12]; double Weights[12];
			const int Count = Plan.Biomes->SampleAll(X, Z, Ids, Weights);
			FLinearColor G(0, 0, 0, 1), R(0, 0, 0, 1), So(0, 0, 0, 1);
			double RockExposure = 0, GroundSnow = 0;
			for (int q = 0; q < Count; q++)
			{
				const float W = (float)Weights[q];
				G += Grass[Ids[q]] * W; R += Rock[Ids[q]] * W; So += Soil[Ids[q]] * W;
				RockExposure += Pool[Ids[q]].RockExposure * Weights[q]; GroundSnow += Pool[Ids[q]].Snow * Weights[q];
			}
			const double Slope = FMath::Sqrt(Dx * Dx + Dz * Dz);
			const double Above = S.Height - Sea;
			float Rocky = (float)FMath::Clamp(FMath::SmoothStep(0.45, 0.95, Slope) + FMath::SmoothStep(0.25, 0.8, S.Mountain) * 0.6 + RockExposure * 0.35, 0.0, 1.0);
			FLinearColor Colour = FMath::Lerp(G, So, (float)FMath::SmoothStep(0.25, 0.5, Slope) * 0.6f);
			Colour = FMath::Lerp(Colour, R, Rocky);
			if (S.Land < 0.06) Colour = FMath::Lerp(Sand, Colour, (float)FMath::SmoothStep(1.5, 5.0, Above)); // beaches
			double Temperature, Rain;
			Plan.Climate->At(X, Z, FMath::Max(0.0, Above), Temperature, Rain);
			Colour = FMath::Lerp(Colour, Snow, (float)(Plan.Climate->Snow(GroundSnow, Temperature, Above) * (1.0 - 0.5 * FMath::SmoothStep(0.8, 1.4, Slope))));
			if (S.Water != ww::EWaterKind::None)
			{
				const double Depth = S.WaterLevel - S.Height;
				Colour = FMath::Lerp(FMath::Lerp(Colour, Sand, 0.7f), DeepBed, (float)FMath::SmoothStep(0.5, 12.0, Depth));
			}
			Colour.A = 1.f;
			Data->Colors.Add(Colour);
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
			const FLinearColor Colour = Data->Colors[Source];
			Data->Vertices.Add(Position);
			Data->Normals.Add(Normal);
			Data->Colors.Add(Colour);
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
		UE::Tasks::Launch(UE_SOURCE_LOCATION, [State = Shared, World = Sampler, Key, Base = BaseTile, CellCount = Cells, Serial = WorldSerial]()
		{
			TSharedPtr<FWwTileData> Data = BuildTile(*World, Key, Base, CellCount);
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
	UProceduralMeshComponent* Mesh = nullptr;
	const bool bReused = Free.Num() > 0;
	if (bReused) Mesh = Free.Pop(EAllowShrinking::No);
	else
	{
		Mesh = NewObject<UProceduralMeshComponent>(this);
		Mesh->bUseAsyncCooking = true;
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetCanEverAffectNavigation(false);
		Mesh->SetupAttachment(RootComponent);
		Mesh->RegisterComponent();
		Owned.Add(Mesh);
	}
	const bool bCollide = Data->Key.Level < CollisionLevels;
	Mesh->SetCollisionEnabled(bCollide ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	if (bCollide) Mesh->SetCollisionProfileName(TEXT("BlockAll"));
	const double Size = TileSize(Data->Key.Level);
	Mesh->SetWorldLocation(FVector(Data->Key.X * Size * 100.0, Data->Key.Y * Size * 100.0, 0.0));
	static const TArray<FVector2D> NoUvs;
	static const TArray<FProcMeshTangent> NoTangents;
	Mesh->CreateMeshSection_LinearColor(0, Data->Vertices, Data->Triangles, Data->Normals, NoUvs, Data->Colors, NoTangents, bCollide, false);
	Mesh->SetMaterial(0, TerrainMaterial);
	if (Data->WaterTriangles.Num() > 0)
	{
		Mesh->CreateMeshSection_LinearColor(1, Data->WaterVertices, Data->WaterTriangles, Data->WaterNormals, NoUvs, Data->WaterColors, NoTangents, false, false);
		Mesh->SetMaterial(1, WaterMaterial);
	}
	Mesh->SetVisibility(false);
	FTile Tile; Tile.Mesh = Mesh;
	Tiles.Add(Data->Key, Tile);
	TilesBuilt++; TotalBuildMs += Data->BuildMs;
}

void AWwTerrainStreamer::Release(UProceduralMeshComponent* Mesh)
{
	Mesh->SetVisibility(false);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->ClearAllMeshSections();
	Free.Add(Mesh);
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
		if (Tile.Mesh) Release(Tile.Mesh);
	}
	for (auto& Pair : Tiles)
	{
		if (Pair.Value.bVisible || !Desired.Contains(Pair.Key)) continue;
		bool bBlocked = false;
		for (const FWwTileKey& Old : Remaining) if (Old.Overlaps(Pair.Key)) { bBlocked = true; break; }
		if (bBlocked) continue;
		Pair.Value.bVisible = true;
		Pair.Value.Mesh->SetVisibility(true);
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
