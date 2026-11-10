#include "Game/WwGame.h"
#include "Game/WwCharacter.h"
#include "Game/WwTerrainStreamer.h"
#include "WayfarersWake.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Components/PostProcessComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMemory.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Net/UnrealNetwork.h"
#include "UnrealClient.h"

// ---------------------------------------------------------------------------------------------------- sky
AWwSky::AWwSky()
{
	PrimaryActorTick.bCanEverTick = true;
	Atmosphere = CreateDefaultSubobject<USkyAtmosphereComponent>(TEXT("Atmosphere"));
	// The planet's top sits at the component (not the world origin), so the horizon is right thousands of km away.
	// (the property has no public setter and the class cannot be subclassed from a game module)
	if (FProperty* Mode = USkyAtmosphereComponent::StaticClass()->FindPropertyByName(TEXT("TransformMode")))
		*Mode->ContainerPtrToValuePtr<uint8>(Atmosphere.Get()) = (uint8)ESkyAtmosphereTransformMode::PlanetTopAtComponentTransform;
	RootComponent = Atmosphere;
	Sun = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Sun"));
	Sun->SetupAttachment(RootComponent);
	Sun->SetMobility(EComponentMobility::Movable);
	Sun->SetUsingAbsoluteRotation(true);
	Sun->Intensity = 8.f;
	Sun->bAtmosphereSunLight = true;
	Sun->DynamicShadowDistanceMovableLight = 60000.f;
	Sun->DynamicShadowCascades = 4;
	SkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLight"));
	SkyLight->SetupAttachment(RootComponent);
	SkyLight->SetMobility(EComponentMobility::Movable);
	SkyLight->bRealTimeCapture = true;
	SkyLight->Intensity = 1.3f; // (shade was black beside Thornwake's)
	Fog = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("Fog"));
	Fog->SetupAttachment(RootComponent);
	// Light haze only: the atmosphere already fades the distance (0.004 washed out everything beyond a kilometre).
	Fog->FogDensity = 0.0002f;
	Fog->FogHeightFalloff = 0.02f;
	FParse::Value(FCommandLine::Get(), TEXT("WwFog="), Fog->FogDensity);
	Clouds = CreateDefaultSubobject<UVolumetricCloudComponent>(TEXT("Clouds"));
	Clouds->SetupAttachment(RootComponent);
	// Exposure adapts between open sun and deep shade (as Thornwake's does), within limits, so bright ground and snow
	// do not grey out and a forest floor is not black; no motion blur.
	Grade = CreateDefaultSubobject<UPostProcessComponent>(TEXT("Grade"));
	Grade->SetupAttachment(RootComponent);
	Grade->bUnbound = true;
	FPostProcessSettings& G = Grade->Settings;
	G.bOverride_MotionBlurAmount = true; G.MotionBlurAmount = 0.f;
	G.bOverride_AutoExposureMethod = true; G.AutoExposureMethod = AEM_Histogram;
	// (the automatic exposure aims brighter than Thornwake's picture: measured against Tom's Godot dumps)
	FParse::Value(FCommandLine::Get(), TEXT("WwBias="), ExposureBias);
	G.bOverride_AutoExposureBias = true; G.AutoExposureBias = ExposureBias;
	G.bOverride_AutoExposureMinBrightness = true; G.bOverride_AutoExposureMaxBrightness = true;
	// A first stand-in for the grading Tom plays Thornwake with ("Fantasy" at 0.6): a little more colour and contrast.
	G.bOverride_ColorSaturation = true; G.ColorSaturation = FVector4(1.15, 1.15, 1.15, 1.0);
	G.bOverride_ColorContrast = true; G.ColorContrast = FVector4(1.04, 1.04, 1.04, 1.0);
	FParse::Value(FCommandLine::Get(), TEXT("WwSaturation="), Saturation);
	G.ColorSaturation = FVector4(Saturation, Saturation, Saturation, 1.0);
	G.bOverride_AutoExposureSpeedUp = true; G.AutoExposureSpeedUp = 2.f;
	G.bOverride_AutoExposureSpeedDown = true; G.AutoExposureSpeedDown = 1.5f;
	FParse::Value(FCommandLine::Get(), TEXT("WwEv="), ShadeEv);
	FParse::Value(FCommandLine::Get(), TEXT("WwEvSun="), SunEv);
	FParse::Value(FCommandLine::Get(), TEXT("WwHour="), Hour);
}

void AWwSky::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Hour = FMath::Fmod(Hour + HoursPerMinute * DeltaSeconds / 60.f + 24.f, 24.f);
	// East at +X, south at +Y: the sun rises in the east, stands in the south at noon, sets in the west.
	const double Angle = (Hour - 6.0) / 12.0 * UE_DOUBLE_PI, Tilt = FMath::DegreesToRadians(32.0);
	FVector ToSun(FMath::Cos(Angle), FMath::Sin(Angle) * FMath::Sin(Tilt), FMath::Sin(Angle) * FMath::Cos(Tilt));
	Sun->SetWorldRotation((-ToSun).Rotation());
	const float Day = (float)FMath::SmoothStep(-0.08, 0.12, ToSun.Z);
	Sun->SetIntensity(8.f * Day);
	Sun->SetCastShadows(Day > 0.02f);
	Grade->Settings.AutoExposureMinBrightness = -FMath::Lerp(SunEv - 3.f, ShadeEv, Day);
	Grade->Settings.AutoExposureMaxBrightness = -FMath::Lerp(SunEv - 3.f, SunEv, Day);

	// Keep the planet's top (and the fog and clouds with it) under the view: correct horizon anywhere in the world.
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		FVector View; FRotator Rotation;
		PC->GetPlayerViewPoint(View, Rotation);
		const FVector Wanted(FMath::GridSnap(View.X, 100000.0), FMath::GridSnap(View.Y, 100000.0), 0.0);
		if (!Wanted.Equals(GetActorLocation(), 1.0)) SetActorLocation(Wanted);
	}
}

FString AWwSky::DebugInfo() const
{
	return FString::Printf(TEXT("Time: hour %.3f (%.3f game hours per real minute) · sun intensity %.2f · exposure limits: shade %.2f, sun %.2f stops · haze (fog density) %.5f"),
		Hour, HoursPerMinute, Sun->Intensity, ShadeEv, SunEv, Fog->FogDensity);
}

// ---------------------------------------------------------------------------------------------------- game state
void AWwGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AWwGameState, WorldSettings);
}

void AWwGameState::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority()) ApplyWorld();
}

void AWwGameState::OnRep_WorldSettings() { ApplyWorld(); }

void AWwGameState::ApplyWorld()
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld()) return;
	if (UWwWorldSubsystem* WorldSystem = World->GetGameInstance()->GetSubsystem<UWwWorldSubsystem>()) WorldSystem->StartWorld(WorldSettings);
	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;
	if (!Streamer) Streamer = World->SpawnActor<AWwTerrainStreamer>(AWwTerrainStreamer::StaticClass(), FTransform::Identity, Params);
	if (!Sky && World->GetNetMode() != NM_DedicatedServer) Sky = World->SpawnActor<AWwSky>(AWwSky::StaticClass(), FTransform::Identity, Params);
}

// ---------------------------------------------------------------------------------------------------- game mode
AWwGameMode::AWwGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	DefaultPawnClass = AWwCharacter::StaticClass();
	GameStateClass = AWwGameState::StaticClass();
	HUDClass = AWwHUD::StaticClass();
}

void AWwGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	const TCHAR* Cmd = FCommandLine::Get();
	FParse::Value(Cmd, TEXT("WwSeed="), Settings.Seed);
	FParse::Value(Cmd, TEXT("WwType="), Settings.WorldType);
	FParse::Value(Cmd, TEXT("WwSizeKm="), Settings.SizeKm);
	FParse::Value(Cmd, TEXT("WwRevision="), Settings.TerrainRevision);
	FParse::Value(Cmd, TEXT("WwAutoShot="), AutoShotSeconds);
	bLogStats = FParse::Param(Cmd, TEXT("WwStats"));
	if (FParse::Param(Cmd, TEXT("WwSelfTest")))
	{
		FString Report;
		const bool bOk = UWwWorldSubsystem::RunSelfTest(Report);
		if (bOk) { UE_LOG(LogWayfarer, Display, TEXT("%s"), *Report); }
		else { UE_LOG(LogWayfarer, Error, TEXT("%s"), *Report); }
		FPlatformMisc::RequestExitWithStatus(false, bOk ? 0 : 3);
	}
	StartMemoryMb = FPlatformMemory::GetStats().UsedPhysical / 1048576.0;
}

void AWwGameMode::InitGameState()
{
	Super::InitGameState();
	if (AWwGameState* State = GetGameState<AWwGameState>()) State->WorldSettings = Settings;
}

void AWwGameMode::LogStats(const TCHAR* Why)
{
	AWwGameState* State = GetGameState<AWwGameState>();
	AWwTerrainStreamer* Streamer = State ? State->GetStreamer() : nullptr;
	AWwCharacter* Player = Cast<AWwCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
	if (!Streamer || !Player) return;
	FrameTimes.Sort();
	const double Average = Frames > 0 ? FrameTimeSum / Frames : 0;
	const double P99 = FrameTimes.Num() > 0 ? FrameTimes[FMath::Min(FrameTimes.Num() - 1, (int32)(FrameTimes.Num() * 0.99))] : 0;
	const FVector P = Player->GetActorLocation();
	UE_LOG(LogWayfarer, Display, TEXT("WwStats[%s] pos=(%.0f, %.0f) m alt=%.0f m travelled=%.1f km tiles=%d trees=%d plants=%d desired=%d missing=%d pending=%d built=%lld avgBuild=%.2f ms maxCommit=%.2f ms fps=%.1f worstFrame=%.1f ms p99=%.1f ms mem=%.0f MB (start %.0f MB)"),
		Why, P.X / 100, P.Y / 100, P.Z / 100, Player->GetDistanceTravelled() / 1000, Streamer->GetTileCount(), Streamer->GetTreeCount(), Streamer->GetPlantCount(), Streamer->GetDesiredCount(), Streamer->GetMissingCount(),
		Streamer->GetPendingCount(), Streamer->GetTilesBuilt(), Streamer->GetAverageBuildMs(), Streamer->GetMaxCommitMs(),
		Average > 0 ? 1.0 / Average : 0.0, WorstFrame * 1000, P99 * 1000, FPlatformMemory::GetStats().UsedPhysical / 1048576.0, StartMemoryMb);
	Frames = 0; FrameTimeSum = 0; WorstFrame = 0; FrameTimes.Reset();
	Streamer->ResetStats();
}

void AWwGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	AWwCharacter* Player = Cast<AWwCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
	if (!Player || !Player->IsPlaced()) return;
	// (the first second after placement is start-up: shader and collision warm-up, not streaming)
	if (Player->GetSecondsPlaced() > 1.0) { Frames++; FrameTimeSum += DeltaSeconds; WorstFrame = FMath::Max<double>(WorstFrame, DeltaSeconds); FrameTimes.Add(DeltaSeconds); }
	StatsTimer += DeltaSeconds;
	if (bLogStats && StatsTimer >= 5.0) { StatsTimer = 0; LogStats(TEXT("run")); }
	if (AutoShotSeconds > 0 && !bShotTaken && Player->GetSecondsPlaced() >= AutoShotSeconds)
	{
		bShotTaken = true;
		ShotAt = Player->GetSecondsPlaced();
		const FString Path = FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("WwAutoShot.png");
		FScreenshotRequest::RequestScreenshot(Path, true, false);
		if (FParse::Param(FCommandLine::Get(), TEXT("WwDump"))) Player->DebugDump(); // test switch: the F9 dump too
		LogStats(TEXT("shot"));
		UE_LOG(LogWayfarer, Display, TEXT("Screenshot requested: %s"), *Path);
	}
	if (bShotTaken && Player->GetSecondsPlaced() > ShotAt + 2.0) FPlatformMisc::RequestExit(false);
}

// ---------------------------------------------------------------------------------------------------- HUD
void AWwHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas || !GEngine) return;
	UWwWorldSubsystem* WorldSystem = GetGameInstance() ? GetGameInstance()->GetSubsystem<UWwWorldSubsystem>() : nullptr;
	if (!WorldSystem) return;
	UFont* Font = GEngine->GetMediumFont();
	const FLinearColor Ink(1.f, 0.95f, 0.82f), Shade(0.f, 0.f, 0.f, 0.55f);
	auto Line = [&](const FString& Text, float X, float Y, float Scale)
	{
		DrawText(Text, FLinearColor::Black, X + 1.5f, Y + 1.5f, Font, Scale);
		DrawText(Text, Ink, X, Y, Font, Scale);
	};
	AWwCharacter* Player = Cast<AWwCharacter>(GetOwningPawn());
	const FWwWorldSettings& S = WorldSystem->GetSettings();
	if (WorldSystem->GetState() != EWwWorldState::Ready || !Player || !Player->IsPlaced())
	{
		DrawRect(Shade, 0, 0, Canvas->SizeX, Canvas->SizeY);
		Line(TEXT("WAYFARER'S WAKE"), Canvas->SizeX * 0.5f - 190, Canvas->SizeY * 0.42f, 2.6f);
		const FString Status = WorldSystem->GetState() == EWwWorldState::Ready ? TEXT("Raising the ground under your feet...") : WorldSystem->GetStatusText();
		Line(Status, Canvas->SizeX * 0.5f - 190, Canvas->SizeY * 0.42f + 60, 1.3f);
		Line(FString::Printf(TEXT("%s, %.0f km x %.0f km, seed %d"), *S.WorldType, S.SizeKm, S.SizeKm, S.Seed), Canvas->SizeX * 0.5f - 190, Canvas->SizeY * 0.42f + 90, 1.1f);
		return;
	}
	SmoothedFrame = FMath::Lerp(SmoothedFrame, (double)GetWorld()->GetDeltaSeconds(), 0.05);
	const FVector P = Player->GetActorLocation();
	const double X = P.X / 100.0, Z = P.Y / 100.0;
	std::shared_ptr<ww::FTerrainSampler> Sampler = WorldSystem->GetSampler();
	const double WorldKm = Sampler->Plan->Profile().WorldSize / 1000.0;
	float Y = 16;
	Line(FString::Printf(TEXT("Wayfarer's Wake 0.1  |  %s  %.0f km  seed %d  terrain rev %d"), *S.WorldType, WorldKm, S.Seed, S.TerrainRevision), 16, Y, 1.0f); Y += 20;
	Line(FString::Printf(TEXT("%.2f km east, %.2f km south   height %.0f m   %s"), X / 1000.0, Z / 1000.0, P.Z / 100.0, *WorldSystem->BiomeNameAt(X, Z)), 16, Y, 1.0f); Y += 20;
	if (const AWwGameState* State = GetWorld()->GetGameState<AWwGameState>())
		if (const AWwTerrainStreamer* Streamer = State->GetStreamer())
		{
			Line(FString::Printf(TEXT("%.0f fps   tiles %d  trees %d  plants %d  rocks %d  building %d  missing %d   tile build %.1f ms"), 1.0 / FMath::Max(SmoothedFrame, 1e-4), Streamer->GetTileCount(),
				Streamer->GetTreeCount(), Streamer->GetPlantCount(), Streamer->GetRockCount(), Streamer->GetPendingCount(), Streamer->GetMissingCount(), Streamer->GetAverageBuildMs()), 16, Y, 1.0f);
			Y += 20;
		}
	if (const FString Status = Player->GetStatus(); !Status.IsEmpty()) { Line(Status, 16, Y, 1.0f); Y += 20; }
	Line(Player->IsFlying() ? TEXT("FLYING  WASD move, E/Q up/down, Shift fast, Ctrl very fast, F walk, F9 debug dump, Esc quit")
		: Player->IsSwimming() ? TEXT("SWIMMING  WASD swim, Shift faster, Space up, C dive, F fly, F9 debug dump, Esc quit")
		: TEXT("WASD walk, Shift sprint, Space jump, F fly, F9 debug dump, Esc quit"), 16, Canvas->SizeY - 30, 1.0f);
}
