#pragma once

#include "CoreMinimal.h"
#include "WorldGen/WwWeather.h"
#include "Components/SkyAtmosphereComponent.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/HUD.h"
#include "Game/WwWorldSubsystem.h"
#include "WwGame.generated.h"

class AWwTerrainStreamer;
class UDirectionalLightComponent;
class USkyLightComponent;
class UExponentialHeightFogComponent;
class UVolumetricCloudComponent;

class UPostProcessComponent;

/// Sun, sky light, atmosphere, clouds and fog with a running time of day. Local to each machine; follows the view.
UCLASS()
class WAYFARERSWAKE_API AWwSky : public AActor
{
	GENERATED_BODY()
public:
	AWwSky();
	virtual void Tick(float DeltaSeconds) override;
	/// Light and air settings in words (debug dump).
	FString DebugInfo() const;
	/// The clock, the weather and the wind of the moment (WwWeather).
	const ww::FGameClock& GetClock() const { return Clock; }
	const ww::FWeather& GetWeather() const { return Weather; }
	const ww::FWind& GetWind() const { return Wind; }
	/// Game hours that passed in the last frame (the clock runs day and night at their own speeds).
	double GetHoursStep() const { return HoursStep; }
	double GetLatitude() const { return Latitude; }
	/// "Day 2, 14:30, light rain, wind 6 m/s from NW".
	FString WeatherLine() const;
	/// How far the camera is below a water surface, m (0 or less: above water).
	float GetCameraDepth() const { return CameraDepth; }

	/// Hour of the day, 0..24.
	UPROPERTY(EditAnywhere, Category = "Sky") float Hour = 9.5f;
	/// Game hours per real minute (0 = time stands still).
	UPROPERTY(EditAnywhere, Category = "Sky") float HoursPerMinute = 0.25f;

private:
	UPROPERTY(VisibleAnywhere, Category = "Sky") TObjectPtr<UDirectionalLightComponent> Sun;
	UPROPERTY(VisibleAnywhere, Category = "Sky") TObjectPtr<USkyLightComponent> SkyLight;
	UPROPERTY(VisibleAnywhere, Category = "Sky") TObjectPtr<USkyAtmosphereComponent> Atmosphere;
	UPROPERTY(VisibleAnywhere, Category = "Sky") TObjectPtr<UExponentialHeightFogComponent> Fog;
	UPROPERTY(VisibleAnywhere, Category = "Sky") TObjectPtr<UVolumetricCloudComponent> Clouds;
	UPROPERTY(VisibleAnywhere, Category = "Sky") TObjectPtr<UPostProcessComponent> Grade;
	/// The underwater view (Thornwake's Underwater.gdshader as a post-process material), on while the camera is below a
	/// water surface.
	UPROPERTY(Transient) TObjectPtr<class UMaterialInstanceDynamic> Underwater;
	/// The moon (a second light with its own disc in the sky), a flash for lightning, the clouds' material, the falling
	/// rain or snow round the camera, and the star dome.
	UPROPERTY(VisibleAnywhere, Category = "Sky") TObjectPtr<UDirectionalLightComponent> Moon;
	UPROPERTY(VisibleAnywhere, Category = "Sky") TObjectPtr<UDirectionalLightComponent> Flash;
	UPROPERTY(Transient) TObjectPtr<class UMaterialInstanceDynamic> CloudMaterial;
	UPROPERTY(Transient) TObjectPtr<class UInstancedStaticMeshComponent> Rain;
	UPROPERTY(Transient) TObjectPtr<class UMaterialInstanceDynamic> RainMaterial;
	UPROPERTY(Transient) TObjectPtr<class UStaticMeshComponent> Stars;
	UPROPERTY(Transient) TObjectPtr<class UMaterialInstanceDynamic> StarMaterial;
	void UpdateWeather(float DeltaSeconds, const FVector& View, float Day);
	void UpdateRain(float DeltaSeconds, const FVector& View);
	ww::FGameClock Clock;
	ww::FWeather Weather;
	ww::FWind Wind;
	ww::FSkyState SkyNow;
	double HoursStep = 0, LastHours = -1, Latitude = 45;
	bool bClockSet = false, bWeatherTried = false, bSnowFalling = false;
	float ForcedRain = -1, ForcedCloud = -1, FlashSeconds = 0, NextFlash = 8;
	TArray<FVector> Drops;
	void UpdateUnderwater(const FVector& View);
	float CameraDepth = 0;
	bool bUnderwaterTried = false;
	/// Exposure limits by day, in stops of brightening: deep shade and open sun (test switches -WwEv=, -WwEvSun=);
	/// haze is the fog density (-WwFog=).
	float ShadeEv = 4.5f, SunEv = 0.6f, NightEv = 3.5f, Saturation = 1.15f, ExposureBias = -0.7f;
};

/// Replicates what defines the world; every machine builds the world from it and streams its own terrain.
UCLASS()
class WAYFARERSWAKE_API AWwGameState : public AGameStateBase
{
	GENERATED_BODY()
public:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(ReplicatedUsing = OnRep_WorldSettings) FWwWorldSettings WorldSettings;
	UFUNCTION() void OnRep_WorldSettings();

	AWwTerrainStreamer* GetStreamer() const { return Streamer; }

private:
	void ApplyWorld();
	UPROPERTY(Transient) TObjectPtr<AWwTerrainStreamer> Streamer;
	UPROPERTY(Transient) TObjectPtr<AWwSky> Sky;
};

/// Server rules: chooses the world (command line: -WwSeed= -WwType= -WwSizeKm= -WwRevision=) and hosts it.
/// Test switches: -WwSelfTest (generator bit-exactness, then quit), -WwAutoShot=<seconds> (screenshot, stats, quit),
/// -WwAutoFly=<m/s> (straight flight), -WwAt=<x>,<z> (start place in metres), -WwStats (log streaming stats).
UCLASS()
class WAYFARERSWAKE_API AWwGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	AWwGameMode();
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void InitGameState() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void LogStats(const TCHAR* Why);
	FWwWorldSettings Settings;
	float AutoShotSeconds = 0;
	double StatsTimer = 0, FrameTimeSum = 0, WorstFrame = 0, StartMemoryMb = 0;
	int32 Frames = 0;
	bool bShotTaken = false, bLogStats = false;
	double ShotAt = 0;
	TArray<float> FrameTimes;
};

UCLASS()
class WAYFARERSWAKE_API AWwHUD : public AHUD
{
	GENERATED_BODY()
public:
	virtual void DrawHUD() override;
private:
	double SmoothedFrame = 0.016;
};
