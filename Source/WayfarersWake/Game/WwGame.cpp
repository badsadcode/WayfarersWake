#include "Game/WwGame.h"
#include "Rules/WrRules.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Game/WwGameplay.h"
#include "Game/WwWorldSubsystem.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
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
	// The clouds shade the ground: under a storm deck the sun is hidden, not just dimmed.
	Sun->bCastCloudShadows = true;
	Sun->CloudShadowStrength = 1.f;
	Sun->CloudShadowOnSurfaceStrength = 1.f;
	Sun->CloudShadowExtent = 60.f;
	// The moon: a second light of the sky (its own disc), cool and faint; and a short white flash for lightning.
	Moon = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Moon"));
	Moon->SetupAttachment(RootComponent);
	Moon->SetMobility(EComponentMobility::Movable);
	Moon->SetUsingAbsoluteRotation(true);
	Moon->Intensity = 0.2f;
	Moon->LightColor = FColor(170, 195, 255);
	Moon->bAtmosphereSunLight = true;
	Moon->AtmosphereSunLightIndex = 1;
	Moon->ForwardShadingPriority = 0;
	Sun->ForwardShadingPriority = 1;
	Flash = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Flash"));
	Flash->SetupAttachment(RootComponent);
	Flash->SetMobility(EComponentMobility::Movable);
	Flash->SetUsingAbsoluteRotation(true);
	Flash->Intensity = 14.f;
	Flash->LightColor = FColor(215, 225, 255);
	Flash->bAtmosphereSunLight = false;
	Flash->CastShadows = false;
	Flash->SetVisibility(false);
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
	FVector View = GetActorLocation(); FRotator Rotation;
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (PC) PC->GetPlayerViewPoint(View, Rotation);
	UWwWorldSubsystem* WorldSystem = GetGameInstance() ? GetGameInstance()->GetSubsystem<UWwWorldSubsystem>() : nullptr;
	const std::shared_ptr<ww::FTerrainSampler> Sampler = WorldSystem ? WorldSystem->GetSampler() : nullptr;
	if (Sampler) Latitude = Sampler->Plan->Climate->Latitude(View.Y / 100.0);

	// The clock (Thornwake's GameClock): a game day in DayLength real minutes, daylight by the latitude and the season.
	if (!bClockSet)
	{
		bClockSet = true;
		FParse::Value(FCommandLine::Get(), TEXT("WwDayLength="), Clock.DayLengthMinutes); // test switches: real minutes a day (0 stops the clock),
		FParse::Value(FCommandLine::Get(), TEXT("WwSeason="), Clock.Season);              // the season (0 spring .. 3 winter), the day number,
		FParse::Value(FCommandLine::Get(), TEXT("WwDay="), Clock.Day);                    // rain and cloud forced (0..1)
		FParse::Value(FCommandLine::Get(), TEXT("WwRain="), ForcedRain);
		FParse::Value(FCommandLine::Get(), TEXT("WwCloud="), ForcedCloud);
		Clock.SetHour(Hour, Latitude);
	}
	SkyNow = Clock.Advance(DeltaSeconds, Latitude);
	Hour = (float)Clock.Hour;
	const double Hours = Clock.TotalHours();
	HoursStep = LastHours < 0 ? 0 : FMath::Clamp(Hours - LastHours, 0.0, 1.0);
	LastHours = Hours;
	HoursPerMinute = DeltaSeconds > 0 ? (float)(HoursStep / DeltaSeconds * 60.0) : 0.f;

	// Sun and moon: east at +X, south at +Y, up at +Z.
	double East, Up, South;
	ww::FGameClock::Direction(SkyNow.SunElevation, SkyNow.SunAzimuth, East, Up, South);
	const FVector ToSun(East, South, Up);
	Sun->SetWorldRotation((-ToSun).Rotation());
	const float Day = (float)FMath::SmoothStep(-0.08, 0.12, ToSun.Z);
	ww::FGameClock::Direction(SkyNow.MoonElevation, SkyNow.MoonAzimuth, East, Up, South);
	const FVector ToMoon(East, South, Up);
	Moon->SetWorldRotation((-ToMoon).Rotation());
	// (the lit share of the moon's face: none at new moon, all at full)
	const float MoonLit = 0.5f - 0.5f * (float)FMath::Cos(SkyNow.MoonPhase * UE_DOUBLE_TWO_PI);
	const float MoonUp = (float)FMath::SmoothStep(-0.02, 0.15, ToMoon.Z) * (1.f - Day);
	Moon->SetIntensity(0.06f + 0.34f * MoonLit * MoonUp);
	static const bool bNoMoon = FParse::Param(FCommandLine::Get(), TEXT("WwNoMoon")); // (test switch)
	Moon->SetVisibility(MoonUp > 0.01f && !bNoMoon);
	Moon->SetCastShadows(MoonUp > 0.3f && MoonLit > 0.3f);

	UpdateWeather(DeltaSeconds, View, Day);
	// Cloud and rain dim the sun; overcast is a grey day.
	const float Overcast = (float)FMath::SmoothStep(0.55, 1.0, (double)Weather.Cloudiness);
	static const float SunScale = [] { float V = 1; FParse::Value(FCommandLine::Get(), TEXT("WwSun="), V); return V; }(); // test switch: the sun's strength
	Sun->SetIntensity(8.f * Day * SunScale * (1.f - 0.35f * Overcast) * (1.f - 0.2f * Weather.Precipitation));
	static const bool bSunShadows = !FParse::Param(FCommandLine::Get(), TEXT("WwNoSunShadows")); // test switch
	Sun->SetCastShadows(bSunShadows && Day > 0.02f);
	// (under water the whole view is one dim colour: left to itself the exposure lifts it to a bright fog)
	// (by night the eye opens much further: moonlight is a few thousandths of daylight, and a night must be dim, not black)
	Grade->Settings.AutoExposureMinBrightness = -FMath::Lerp(NightEv + 1.5f, CameraDepth > .02f ? SunEv + 1.f : ShadeEv, Day);
	Grade->Settings.AutoExposureMaxBrightness = -FMath::Lerp(NightEv, SunEv, Day);

	// Keep the planet's top (and the fog and clouds with it) under the view: correct horizon anywhere in the world.
	if (PC)
	{
		const FVector Wanted(FMath::GridSnap(View.X, 100000.0), FMath::GridSnap(View.Y, 100000.0), 0.0);
		if (!Wanted.Equals(GetActorLocation(), 1.0)) SetActorLocation(Wanted);
		UpdateUnderwater(View);
	}
	// The stars: a dome round the camera, turning once a day about the pole, lit as the sun goes.
	if (Stars)
	{
		const double Turn = (Clock.Hour - 12.0) * 15.0 + Clock.Day * 0.9856;
		Stars->SetWorldLocationAndRotation(View, FQuat(FVector(1, 0, 0), FMath::DegreesToRadians(90.0 - Latitude)) * FQuat(FVector(0, 0, 1), FMath::DegreesToRadians(Turn)));
		if (StarMaterial) StarMaterial->SetScalarParameterValue(TEXT("Glow"), 0.06f * (1.f - (float)FMath::SmoothStep(-0.12, 0.0, ToSun.Z)) * (1.f - 0.7f * MoonLit * MoonUp));
	}
}

void AWwSky::UpdateWeather(float DeltaSeconds, const FVector& View, float Day)
{
	UWwWorldSubsystem* WorldSystem = GetGameInstance() ? GetGameInstance()->GetSubsystem<UWwWorldSubsystem>() : nullptr;
	const std::shared_ptr<ww::FTerrainSampler> Sampler = WorldSystem ? WorldSystem->GetSampler() : nullptr;
	if (!bWeatherTried)
	{
		bWeatherTried = true;
		// The clouds' material (the engine's own), so the weather can thicken and darken it.
		if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud_Inst.m_SimpleVolumetricCloud_Inst")))
		{
			CloudMaterial = UMaterialInstanceDynamic::Create(Base, this);
			Clouds->SetMaterial(CloudMaterial);
		}
		// Rain and snow: thin shapes round the camera.
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		UMaterialInterface* RainBase = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_WwRain.M_WwRain"));
		if (Cube && RainBase)
		{
			RainMaterial = UMaterialInstanceDynamic::Create(RainBase, this);
			Rain = NewObject<UInstancedStaticMeshComponent>(this);
			Rain->SetStaticMesh(Cube);
			Rain->SetMaterial(0, RainMaterial);
			Rain->SetMobility(EComponentMobility::Movable);
			Rain->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Rain->SetCastShadow(false);
			Rain->SetUsingAbsoluteLocation(true); Rain->SetUsingAbsoluteRotation(true);
			Rain->SetupAttachment(RootComponent);
			Rain->RegisterComponent();
		}
		UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
		UMaterialInterface* StarBase = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_WwStars.M_WwStars"));
		if (Sphere && StarBase && !FParse::Param(FCommandLine::Get(), TEXT("WwNoStars"))) // (test switch)
		{
			StarMaterial = UMaterialInstanceDynamic::Create(StarBase, this);
			Stars = NewObject<UStaticMeshComponent>(this);
			Stars->SetStaticMesh(Sphere);
			Stars->SetMaterial(0, StarMaterial);
			Stars->SetMobility(EComponentMobility::Movable);
			Stars->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Stars->SetCastShadow(false);
			Stars->SetUsingAbsoluteLocation(true); Stars->SetUsingAbsoluteRotation(true); Stars->SetUsingAbsoluteScale(true);
			Stars->SetWorldScale3D(FVector(400000.0)); // (200 km: beyond everything, inside the far plane)
			Stars->SetupAttachment(RootComponent);
			Stars->RegisterComponent();
		}
	}
	// The weather where the camera is: fronts passing over game hours, rain by the place's climate.
	double Temperature = 12, AnnualRain = 700;
	if (Sampler)
	{
		const ww::FWorldPlan& Plan = *Sampler->Plan;
		const double X = View.X / 100.0, Z = View.Y / 100.0;
		Plan.Climate->At(X, Z, Sampler->Height(X, Z, 64) - Plan.Profile().SeaLevel, Temperature, AnnualRain);
		// (the air now: the yearly mean, the season and the hour, as the needs use it)
		Temperature = wr::FNeeds::AirTemperature(Temperature, Latitude, Clock.Season, Clock.Hour, Weather.Precipitation);
	}
	const ww::u64 Seed = WorldSystem ? (ww::u64)WorldSystem->GetSettings().Seed : 1;
	Weather.Update(DeltaSeconds, Clock.TotalHours(), AnnualRain, Temperature, 0, Seed);
	if (ForcedRain >= 0) { Weather.Precipitation = ForcedRain; Weather.Cloudiness = FMath::Max(Weather.Cloudiness, ForcedRain * .95f + .05f); Weather.bThunder = ForcedRain > .78f && Temperature > 6; }
	if (ForcedCloud >= 0) Weather.Cloudiness = FMath::Max(ForcedCloud, Weather.Precipitation > 0 ? Weather.Precipitation * .95f + .05f : 0.f);
	Wind.Update(DeltaSeconds, Clock.TotalHours(), Weather.Storm(), Seed);

	if (CloudMaterial)
	{
		// (the engine's cloud material, measured: coverage -0.9 no cloud, -0.4 scattered small clouds, 0.1 heavy broken
		// cloud, 0.6 a smooth grey deck; storm clouds darken and tower)
		static const float TestCoverage = [] { float V = -99; FParse::Value(FCommandLine::Get(), TEXT("WwCoverage="), V); return V; }(); // (test switch: the material's own number)
		CloudMaterial->SetScalarParameterValue(TEXT("Cloud_GlobalCoverage"), TestCoverage > -90 ? TestCoverage : FMath::Lerp(-0.9f, 0.6f, Weather.Cloudiness));
		CloudMaterial->SetScalarParameterValue(TEXT("Cloud_GlobalDensity"), FMath::Lerp(0.008f, 0.02f, Weather.Precipitation));
		CloudMaterial->SetScalarParameterValue(TEXT("StormClouds"), FMath::Clamp(Weather.Precipitation * 1.4f - 0.3f, 0.f, 1.f));
	}
	// Rain hangs in the air as haze.
	static const float Haze = [this] { return Fog->FogDensity; }();
	Fog->SetFogDensity(Haze * (1.f + 9.f * Weather.Precipitation + 1.5f * (float)FMath::SmoothStep(0.6, 1.0, (double)Weather.Cloudiness)));
	// Lightning: a flash now and then in a thunderstorm.
	FlashSeconds -= DeltaSeconds;
	if (Weather.bThunder && (NextFlash -= DeltaSeconds) <= 0)
	{
		NextFlash = FMath::FRandRange(4.f, 16.f);
		FlashSeconds = FMath::FRandRange(0.08f, 0.22f);
		Flash->SetWorldRotation(FRotator(FMath::FRandRange(-80.f, -40.f), FMath::FRandRange(0.f, 360.f), 0.f));
	}
	Flash->SetVisibility(FlashSeconds > 0);
	UpdateRain(DeltaSeconds, View);
}

void AWwSky::UpdateRain(float DeltaSeconds, const FVector& View)
{
	if (!Rain) return;
	const bool bSnow = Weather.bSnow;
	const int32 Wanted = CameraDepth > .02f ? 0 : FMath::RoundToInt(Weather.Precipitation * (bSnow ? 2500.f : 4000.f));
	const double Reach = 1800.0, Top = 1300.0, Bottom = -500.0; // (cm round the camera)
	if (Drops.Num() != Wanted || bSnow != bSnowFalling)
	{
		bSnowFalling = bSnow;
		Drops.SetNum(Wanted);
		for (FVector& Drop : Drops) Drop = FVector(FMath::FRandRange(-Reach, Reach), FMath::FRandRange(-Reach, Reach), FMath::FRandRange(Bottom, Top));
		Rain->ClearInstances();
		TArray<FTransform> Start;
		Start.Init(FTransform::Identity, Wanted);
		if (Wanted > 0) Rain->AddInstances(Start, false, true, false);
		if (RainMaterial) RainMaterial->SetVectorParameterValue(TEXT("Colour"), bSnow ? FLinearColor(0.2f, 0.21f, 0.22f, 0.8f) : FLinearColor(0.07f, 0.075f, 0.08f, 0.32f));
	}
	Rain->SetVisibility(Wanted > 0);
	if (Wanted == 0) return;
	// Falling with the wind: rain about 9 m/s, snow drifting down at 1.4.
	const FVector Fall(Wind.DirX * Wind.Speed * (bSnow ? 60.0 : 35.0), Wind.DirZ * Wind.Speed * (bSnow ? 60.0 : 35.0), bSnow ? -140.0 : -900.0);
	const FQuat Lean = FRotationMatrix::MakeFromZ(-Fall.GetSafeNormal()).ToQuat();
	const FVector Size = bSnow ? FVector(0.035, 0.035, 0.035) : FVector(0.006, 0.006, 0.3 + 0.35 * Weather.Precipitation);
	TArray<FTransform> Now;
	Now.SetNumUninitialized(Wanted);
	for (int32 i = 0; i < Wanted; i++)
	{
		FVector& Drop = Drops[i];
		Drop += Fall * DeltaSeconds;
		if (bSnow) Drop += FVector(FMath::Sin(i * 1.7 + Wind.Time * 1.3), FMath::Cos(i * 2.3 + Wind.Time * 1.1), 0) * (25.0 * DeltaSeconds); // (flakes wander)
		// (wrapped round the camera: a drop leaving one side comes in at the other)
		Drop.X = FMath::Fmod(Drop.X + Reach * 3, Reach * 2) - Reach;
		Drop.Y = FMath::Fmod(Drop.Y + Reach * 3, Reach * 2) - Reach;
		if (Drop.Z < Bottom) Drop.Z += Top - Bottom;
		// (a drop right in front of the lens would be a pole across the picture)
		Now[i] = FTransform(Lean, View + Drop, Drop.SizeSquared2D() < 150.0 * 150.0 ? FVector::ZeroVector : Size);
	}
	Rain->BatchUpdateInstancesTransforms(0, Now, true, true, false);
}

FString AWwSky::WeatherLine() const
{
	const TCHAR* Sky = Weather.Precipitation > .75f ? (Weather.bSnow ? TEXT("a snowstorm") : Weather.bThunder ? TEXT("a thunderstorm") : TEXT("heavy rain"))
		: Weather.Precipitation > .4f ? (Weather.bSnow ? TEXT("snow") : TEXT("rain")) : Weather.Precipitation > .05f ? (Weather.bSnow ? TEXT("light snow") : TEXT("light rain"))
		: Weather.Cloudiness > .8f ? TEXT("overcast") : Weather.Cloudiness > .5f ? TEXT("cloudy") : Weather.Cloudiness > .25f ? TEXT("fair") : TEXT("clear");
	return FString::Printf(TEXT("Day %d, %02d:%02d, %s, wind %.0f m/s from %s"), Clock.Day, (int32)Clock.Hour, (int32)((Clock.Hour - (int32)Clock.Hour) * 60), Sky, Wind.SteadySpeed, ANSI_TO_TCHAR(Wind.Compass()));
}

void AWwSky::UpdateUnderwater(const FVector& View)
{
	if (!Underwater && !bUnderwaterTried)
	{
		bUnderwaterTried = true;
		if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_WwUnderwater.M_WwUnderwater")))
		{
			Underwater = UMaterialInstanceDynamic::Create(Base, this);
			Grade->Settings.AddBlendable(Underwater, 0.f);
		}
	}
	UWwWorldSubsystem* WorldSystem = GetGameInstance() ? GetGameInstance()->GetSubsystem<UWwWorldSubsystem>() : nullptr;
	const std::shared_ptr<ww::FTerrainSampler> Sampler = WorldSystem ? WorldSystem->GetSampler() : nullptr;
	CameraDepth = 0;
	ww::EWaterKind Kind = ww::EWaterKind::None;
	const double X = View.X / 100.0, Z = View.Y / 100.0;
	if (Sampler)
	{
		// The water surface over the camera: its still level, and in the sea the waves the shader shows there.
		const ww::FTerrainSample T = Sampler->Sample(X, Z, 2);
		Kind = T.Water;
		if (Kind != ww::EWaterKind::None)
		{
			double Level = T.WaterLevel;
			if (Kind == ww::EWaterKind::Ocean) Level += WorldSystem->SeaSurface(X, Z, T.WaterLevel - T.Height, GetWorld()->GetTimeSeconds());
			CameraDepth = (float)(Level - View.Z / 100.0);
		}
	}
	const bool bUnder = CameraDepth > .02f;
	if (!Underwater) return;
	for (FWeightedBlendable& Blendable : Grade->Settings.WeightedBlendables.Array)
		if (Blendable.Object == Underwater) Blendable.Weight = bUnder ? 1.f : 0.f;
	if (!bUnder) return;
	// The water's colour and clarity by its kind (Thornwake Atmosphere.SetUnderwater): the sea clear blue-green, lakes
	// greener, rivers greener and murkier, wetland pools peaty.
	FLinearColor Colour(.04f, .26f, .33f); float Density = .05f;
	if (Kind == ww::EWaterKind::Lake) { Colour = FLinearColor(.06f, .24f, .2f); Density = .09f; }
	else if (Kind == ww::EWaterKind::River) { Colour = FLinearColor(.08f, .2f, .15f); Density = .16f; }
	else if (Kind == ww::EWaterKind::Pond) { Colour = FLinearColor(.07f, .09f, .05f); Density = .45f; }
	Underwater->SetScalarParameterValue(TEXT("SurfaceDepth"), CameraDepth);
	Underwater->SetVectorParameterValue(TEXT("WaterColor"), Colour);
	Underwater->SetScalarParameterValue(TEXT("Density"), Density);
	// The light pattern: from the simulated sea's own slopes in the sea, else drifting noise.
	auto Wrap = [](double V, double L) { return (float)(V - FMath::FloorToDouble(V / L) * L); };
	AWwTerrainStreamer* Streamer = nullptr;
	for (TActorIterator<AWwTerrainStreamer> It(GetWorld()); It; ++It) { Streamer = *It; break; }
	UTexture2D* Slope1 = Streamer ? Streamer->GetOceanTexture(3) : nullptr;
	UTexture2D* Slope2 = Streamer ? Streamer->GetOceanTexture(5) : nullptr;
	const bool bFft = Kind == ww::EWaterKind::Ocean && Slope1 && Slope2;
	Underwater->SetScalarParameterValue(TEXT("UseFft"), bFft ? 1.f : 0.f);
	if (bFft)
	{
		Underwater->SetTextureParameterValue(TEXT("Slope1"), Slope1);
		Underwater->SetTextureParameterValue(TEXT("Slope2"), Slope2);
		Underwater->SetVectorParameterValue(TEXT("CamFft1"), FLinearColor(Wrap(X, ww::FOcean::Sizes[1]), Wrap(Z, ww::FOcean::Sizes[1]), 0, 0));
		Underwater->SetVectorParameterValue(TEXT("CamFft2"), FLinearColor(Wrap(X, ww::FOcean::Sizes[2]), Wrap(Z, ww::FOcean::Sizes[2]), 0, 0));
	}
	Underwater->SetVectorParameterValue(TEXT("CamNoise"), FLinearColor(Wrap(X, 4096.0), Wrap(Z, 4096.0), 0, 0));
}

FString AWwSky::DebugInfo() const
{
	return FString::Printf(TEXT("Time: %s (a game day lasts %.0f real minutes; season %d; latitude %.1f; %.1f h of daylight) · sun %.1f degrees up, intensity %.2f · moon %.1f degrees up, phase %.2f")
		LINE_TERMINATOR TEXT("Weather: cloud %.2f, rain %.2f%s%s · wind toward (%.2f, %.2f), %.1f m/s steady, gust %.2f · exposure limits: shade %.2f, sun %.2f stops · haze (fog density) %.5f"),
		*WeatherLine(), Clock.DayLengthMinutes, Clock.Season, Latitude, SkyNow.DaylightHours, SkyNow.SunElevation, Sun->Intensity, SkyNow.MoonElevation, SkyNow.MoonPhase,
		Weather.Cloudiness, Weather.Precipitation, Weather.bSnow ? TEXT(" (snow)") : TEXT(""), Weather.bThunder ? TEXT(" (thunder)") : TEXT(""),
		Wind.DirX, Wind.DirZ, Wind.SteadySpeed, Wind.Gust, ShadeEv, SunEv, Fog->FogDensity);
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
	UE_LOG(LogWayfarer, Display, TEXT("WwStats[%s] pos=(%.0f, %.0f) m alt=%.0f m travelled=%.1f km tiles=%d trees=%d plants=%d desired=%d missing=%d pending=%d built=%lld avgBuild=%.2f ms maxCommit=%.2f ms fps=%.1f worstFrame=%.1f ms p99=%.1f ms sea=%.1f ms mem=%.0f MB (start %.0f MB)"),
		Why, P.X / 100, P.Y / 100, P.Z / 100, Player->GetDistanceTravelled() / 1000, Streamer->GetTileCount(), Streamer->GetTreeCount(), Streamer->GetPlantCount(), Streamer->GetDesiredCount(), Streamer->GetMissingCount(),
		Streamer->GetPendingCount(), Streamer->GetTilesBuilt(), Streamer->GetAverageBuildMs(), Streamer->GetMaxCommitMs(),
		Average > 0 ? 1.0 / Average : 0.0, WorstFrame * 1000, P99 * 1000, Streamer->GetOceanMs(), FPlatformMemory::GetStats().UsedPhysical / 1048576.0, StartMemoryMb);
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
		: TEXT("WASD walk, Shift sprint, Space jump, I pack, E gather, left mouse axe, T eat, F fly, F9 dump, Esc quit"), 16, Canvas->SizeY - 30, 1.0f);
	for (TActorIterator<AWwSky> It(GetWorld()); It; ++It) { Line(It->WeatherLine(), Canvas->SizeX - 430, 16, 1.0f); break; }
	// The placeholder mechanics: needs, the last thing that happened, and the pack with what can be made.
	if (FWwGameplay* Game = WorldSystem->GetGameplay())
	{
		Line(Game->NeedsLine(), Canvas->SizeX - 430, Canvas->SizeY - 30, 1.0f);
		if (!Player->GetNotice().IsEmpty()) Line(Player->GetNotice(), 16, Canvas->SizeY - 60, 1.1f);
		if (Player->IsPackOpen())
		{
			const FVector At = Player->GetActorLocation();
			const TArray<FString> Lines = Game->PackLines(At.X / 100.0, At.Y / 100.0);
			DrawRect(Shade, Canvas->SizeX - 640, 90, 624, 34 + 20 * Lines.Num());
			float PackY = 100;
			for (const FString& Text : Lines) { Line(Text, Canvas->SizeX - 626, PackY, 1.0f); PackY += 20; }
			Line(TEXT("I closes the pack. T eats. E picks up or pulls grass. Left mouse: the axe."), Canvas->SizeX - 626, PackY + 4, 0.9f);
		}
	}
}
