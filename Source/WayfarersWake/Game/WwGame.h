#pragma once

#include "CoreMinimal.h"
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
	/// Exposure limits by day, in stops of brightening: deep shade and open sun (test switches -WwEv=, -WwEvSun=);
	/// haze is the fog density (-WwFog=).
	float ShadeEv = 3.2f, SunEv = 1.4f;
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
