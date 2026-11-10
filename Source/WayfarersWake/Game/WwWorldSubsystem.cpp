#include "Game/WwWorldSubsystem.h"
#include "WayfarersWake.h"
#include "Async/Async.h"
#include "Async/ParallelFor.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

static void EngineParallelFor(int Count, const std::function<void(int)>& Body)
{
	ParallelFor(Count, [&Body](int32 Index) { Body(Index); });
}

void UWwWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	ww::ParallelForHook() = &EngineParallelFor;
	Stage = MakeShared<std::atomic<int32>, ESPMode::ThreadSafe>(0);
}

static bool ReadText(const FString& Name, std::string& Out, FString& Error)
{
	FString Path = FPaths::ProjectContentDir() / TEXT("WorldData") / Name, Text;
	if (!FFileHelper::LoadFileToString(Text, *Path)) { Error = FString::Printf(TEXT("Cannot read %s"), *Path); return false; }
	Out = TCHAR_TO_UTF8(*Text);
	return true;
}

bool UWwWorldSubsystem::LoadCatalog(std::vector<ww::FWorldProfile>& Profiles, ww::FBiomeRegistry& Biomes, FString& Error)
{
	std::string ProfilesJson, BiomesJson, SetsJson, Problem;
	if (!ReadText(TEXT("world_profiles.json"), ProfilesJson, Error) || !ReadText(TEXT("biomes.json"), BiomesJson, Error) || !ReadText(TEXT("asset_sets.json"), SetsJson, Error)) return false;
	if (!ww::FWorldProfile::ParseCatalog(ProfilesJson, Profiles, Problem) || !ww::FBiomeRegistry::Parse(BiomesJson, SetsJson, Biomes, Problem))
	{
		Error = UTF8_TO_TCHAR(Problem.c_str());
		return false;
	}
	return true;
}

void UWwWorldSubsystem::StartWorld(const FWwWorldSettings& Settings)
{
	if (State != EWwWorldState::Idle && State != EWwWorldState::Failed && Settings == Current) return;
	Current = Settings;
	State = EWwWorldState::Building;
	Sampler.reset();
	Stage->store(0);
	const int32 Request = ++BuildRequest;
	const double Started = FPlatformTime::Seconds();
	UE_LOG(LogWayfarer, Log, TEXT("Building world: type=%s seed=%d size=%.0f km revision=%d"), *Settings.WorldType, Settings.Seed, Settings.SizeKm, Settings.TerrainRevision);

	TWeakObjectPtr<UWwWorldSubsystem> WeakThis(this);
	TSharedPtr<std::atomic<int32>, ESPMode::ThreadSafe> StageRef = Stage;
	Async(EAsyncExecution::Thread, [WeakThis, Settings, Request, Started, StageRef]()
	{
		std::shared_ptr<ww::FTerrainSampler> Built;
		FString Error;
		std::vector<ww::FWorldProfile> Profiles;
		ww::FBiomeRegistry Biomes;
		if (LoadCatalog(Profiles, Biomes, Error))
		{
			std::vector<ww::FWorldRecipe> Recipes = ww::FWorldRecipe::Catalog(Profiles);
			const ww::FWorldRecipe* Recipe = nullptr;
			std::string Wanted = TCHAR_TO_UTF8(*Settings.WorldType);
			for (const ww::FWorldRecipe& R : Recipes) if (R.Id == Wanted) Recipe = &R;
			if (!Recipe) Error = FString::Printf(TEXT("Unknown world type '%s'"), *Settings.WorldType);
			else
			{
				ww::FWorldChoices Choices;
				Choices.SizeKm = Settings.SizeKm;
				ww::FWorldProfile Profile = Recipe->Resolve(Choices);
				std::string Problem;
				if (!Profile.Validate(Problem)) Error = UTF8_TO_TCHAR(Problem.c_str());
				else
				{
					ww::FTerrainStyle Style = ww::FTerrainStyle::For(Profile);
					Style.Revision = FMath::Clamp(Settings.TerrainRevision, 8, ww::FTerrainStyle::PortedRevision);
					auto Plan = std::make_shared<ww::FWorldPlan>(ww::FWorldManifest::Create(Settings.Seed, Profile), Style, Biomes,
						[StageRef](int S) { StageRef->store(S); });
					Built = std::make_shared<ww::FTerrainSampler>(Plan);
				}
			}
		}
		const double Seconds = FPlatformTime::Seconds() - Started;
		AsyncTask(ENamedThreads::GameThread, [WeakThis, Built, Error, Request, Seconds]()
		{
			UWwWorldSubsystem* Self = WeakThis.Get();
			if (!Self || Self->BuildRequest != Request) return; // superseded
			Self->BuildSeconds = Seconds;
			if (Built)
			{
				Self->Sampler = Built;
				Self->State = EWwWorldState::Ready;
				Self->WorldSerial++;
				const ww::FWorldPlan& Plan = *Built->Plan;
				UE_LOG(LogWayfarer, Log, TEXT("World ready in %.2f s: %s, %.0f km, raster %d (cell %.0f m), %d lakes, %d river reaches, %.1f MB, digest %s"),
					Self->BuildSeconds, UTF8_TO_TCHAR(Plan.Profile().Id.c_str()), Plan.Profile().WorldSize / 1000, Plan.Macro.Size, Plan.Macro.Cell,
					(int32)Plan.Hydrology->Lakes.size(), (int32)Plan.Hydrology->Reaches.size(), Plan.MemoryBytes() / 1048576.0,
					UTF8_TO_TCHAR(Plan.Manifest.ProfileDigest.substr(0, 16).c_str()));
			}
			else
			{
				Self->Failure = Error;
				Self->State = EWwWorldState::Failed;
				UE_LOG(LogWayfarer, Error, TEXT("World generation failed: %s"), *Error);
			}
		});
	});
}

FString UWwWorldSubsystem::GetStatusText() const
{
	static const TCHAR* Stages[] = { TEXT("land shape"), TEXT("big landforms"), TEXT("erosion, rivers and lakes"), TEXT("climate"), TEXT("biomes") };
	switch (State)
	{
	case EWwWorldState::Building: return FString::Printf(TEXT("Shaping the world: %s..."), Stages[FMath::Clamp(Stage->load(), 0, 4)]);
	case EWwWorldState::Ready: return TEXT("World ready");
	case EWwWorldState::Failed: return TEXT("World generation failed: ") + Failure;
	default: return TEXT("No world");
	}
}

bool UWwWorldSubsystem::FindSpawn(double& OutX, double& OutZ, double& OutHeight) const
{
	if (!Sampler) return false;
	const ww::FWorldPlan& Plan = *Sampler->Plan;
	const ww::FLandFeature& Land = Plan.Layout.Land[0];
	// Walk outward from the first landmass's centre in a golden-angle spiral: dry lowland with a gentle slope.
	double BestScore = -1e18;
	bool bFound = false;
	for (int32 i = 0; i < 4000; i++)
	{
		double Radius = Land.RadiusX * 0.9 * FMath::Sqrt((i + 0.5) / 4000.0), Angle = i * 2.39996322972865332;
		double X = Land.Center.X + FMath::Cos(Angle) * Radius, Z = Land.Center.Z + FMath::Sin(Angle) * Radius;
		ww::FTerrainSample S = Sampler->Sample(X, Z);
		if (S.Water != ww::EWaterKind::None || S.Land <= 0.05) continue;
		double Hx = Sampler->Height(X + 4, Z) - Sampler->Height(X - 4, Z), Hz = Sampler->Height(X, Z + 4) - Sampler->Height(X, Z - 4);
		double Slope = FMath::Sqrt(Hx * Hx + Hz * Hz) / 8.0;
		if (Slope > 0.25) continue;
		// Prefer low, gentle ground not far from the centre (a view of the land rather than a mountain top).
		double Score = -Slope * 400 - FMath::Abs(S.Height - 60) * 0.5 - Radius / Land.RadiusX * 40 - S.Mountain * 200;
		if (Score > BestScore) { BestScore = Score; OutX = X; OutZ = Z; OutHeight = S.Height; bFound = true; }
	}
	return bFound;
}

FString UWwWorldSubsystem::BiomeNameAt(double X, double Z) const
{
	if (!Sampler || !Sampler->Plan->Biomes) return FString();
	int A, B; double Share;
	Sampler->Plan->Biomes->Sample(X, Z, A, B, Share);
	return UTF8_TO_TCHAR(Sampler->Plan->Biomes->Registry.Pool[A].Name.c_str());
}

bool UWwWorldSubsystem::RunSelfTest(FString& Report)
{
	std::vector<ww::FWorldProfile> Profiles;
	ww::FBiomeRegistry Biomes;
	FString Error;
	if (!LoadCatalog(Profiles, Biomes, Error)) { Report = Error; return false; }
	const ww::FWorldProfile& Profile = Profiles[0]; // balanced
	const double Started = FPlatformTime::Seconds();
	auto Plan = std::make_shared<ww::FWorldPlan>(ww::FWorldManifest::Create(42, Profile), ww::FTerrainStyle::For(Profile), Biomes);
	ww::FTerrainSampler S(Plan);
	// The fingerprint of Thornwake's Tests/WorldGen/Fixtures/terrain-r1.json (terrain revision 44), case "balanced/42".
	ww::FSha256 Sha;
	std::vector<double> Buffer(33 * 33);
	const ww::FLandFeature& F = Plan->Layout.Land[0];
	const double Grids[4][3] = { { Profile.WorldSize * -.5, Profile.WorldSize * -.5, Profile.WorldSize / 32 },
		{ F.Center.X, F.Center.Z, 1.0 }, { F.Center.X + F.RadiusX * .5, F.Center.Z, 97.0 }, { F.Center.X - F.RadiusX * 1.1, F.Center.Z, 13.0 } };
	for (const auto& G : Grids)
	{
		S.HeightGrid(G[0], G[1], G[2], 33, 33, Buffer.data());
		Sha.Append(Buffer.data(), Buffer.size() * sizeof(double));
	}
	for (int C = 0; C < 3; C++) Sha.Append(Plan->Macro.Channel(C).data(), Plan->Macro.Channel(C).size() * sizeof(float));
	const std::string Hash = Sha.FinishHex();
	const bool bMatch = Hash == "a48c43b5bdd43c84a3c37fcac986ebf932d1a20701abf040e2bdf9f753825e6b";
	Report = FString::Printf(TEXT("WorldGen self test (balanced/42, revision 44): %s  hash %s  (%.2f s)"), bMatch ? TEXT("MATCH") : TEXT("MISMATCH"),
		UTF8_TO_TCHAR(Hash.c_str()), FPlatformTime::Seconds() - Started);
	return bMatch;
}
