#include "Game/WwCharacter.h"
#include "Game/WwGameplay.h"
#include "Game/WwWorldSubsystem.h"
#include "Game/WwTerrainStreamer.h"
#include "Game/WwGame.h"
#include "WorldGen/WwVegetation.h"
#include "Engine/Engine.h"
#include "Engine/OverlapResult.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/GameViewportClient.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMemory.h"
#include "Misc/App.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RHI.h"
#include "UnrealClient.h"
#include "WayfarersWake.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "UObject/ConstructorHelpers.h"

AWwCharacter::AWwCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	GetCapsuleComponent()->InitCapsuleSize(34.f, 90.f);
	bUseControllerRotationYaw = false;

	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->bOrientRotationToMovement = true;
	Move->RotationRate = FRotator(0, 540, 0);
	Move->MaxWalkSpeed = WalkSpeed;
	Move->JumpZVelocity = 650; // (Thornwake: 6.5 m/s up, gravity 20 m/s2)
	Move->AirControl = 0.35f;
	Move->SetWalkableFloorAngle(50.f);
	Move->MaxStepHeight = 50.f;
	Move->BrakingDecelerationFlying = 6000;
	Move->GravityScale = 0; // frozen until placed on the ground

	Boom = CreateDefaultSubobject<USpringArmComponent>(TEXT("Boom"));
	Boom->SetupAttachment(RootComponent);
	Boom->TargetArmLength = 520;
	Boom->SocketOffset = FVector(0, 0, 90);
	Boom->bUsePawnControlRotation = true;
	Boom->bEnableCameraLag = true;
	Boom->CameraLagSpeed = 14;
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Boom);
	Camera->FieldOfView = 90; // Thornwake: 59 degrees vertical, which is 90 across at 16:9

	// Alden Wayfarer in blocks (feet at -90 cm, facing +X).
	const FLinearColor Teal(0.045f, 0.19f, 0.19f), Trousers(0.09f, 0.05f, 0.025f), Boots(0.05f, 0.028f, 0.014f), Skin(0.60f, 0.33f, 0.19f);
	const FLinearColor Hair(0.06f, 0.03f, 0.015f), Pack(0.16f, 0.085f, 0.035f), Roll(0.40f, 0.29f, 0.13f), Gold(0.75f, 0.50f, 0.08f), Wood(0.14f, 0.08f, 0.035f);
	AddPart(TEXT("LegL"), FVector(0, -11, -52), FVector(18, 18, 56), Trousers);
	AddPart(TEXT("LegR"), FVector(0, 11, -52), FVector(18, 18, 56), Trousers);
	AddPart(TEXT("BootL"), FVector(2, -11, -82), FVector(24, 20, 16), Boots);
	AddPart(TEXT("BootR"), FVector(2, 11, -82), FVector(24, 20, 16), Boots);
	AddPart(TEXT("Torso"), FVector(0, 0, 6), FVector(24, 44, 56), Teal);
	AddPart(TEXT("Belt"), FVector(0, 0, -19), FVector(26, 46, 7), Boots);
	AddPart(TEXT("Buckle"), FVector(13, 0, -19), FVector(3, 9, 8), Gold);
	AddPart(TEXT("ArmL"), FVector(0, -30, 14), FVector(15, 14, 40), Teal);
	AddPart(TEXT("ArmR"), FVector(0, 30, 14), FVector(15, 14, 40), Teal);
	AddPart(TEXT("HandL"), FVector(0, -30, -12), FVector(14, 13, 14), Skin);
	AddPart(TEXT("HandR"), FVector(6, 30, -12), FVector(14, 13, 14), Skin);
	AddPart(TEXT("Head"), FVector(0, 0, 52), FVector(34, 34, 34), Skin);
	AddPart(TEXT("Hair"), FVector(-2, 0, 69), FVector(36, 37, 12), Hair);
	AddPart(TEXT("Beard"), FVector(13, 0, 40), FVector(10, 30, 12), Hair);
	AddPart(TEXT("Pack"), FVector(-23, 0, 8), FVector(22, 36, 44), Pack);
	AddPart(TEXT("Bedroll"), FVector(-23, 0, 39), FVector(18, 46, 18), Roll);
	AddPart(TEXT("Staff"), FVector(12, 40, -8), FVector(6, 6, 164), Wood);
}

void AWwCharacter::AddPart(const TCHAR* Name, const FVector& Centre, const FVector& Size, const FLinearColor& Colour)
{
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Solid(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Name);
	Part->SetupAttachment(RootComponent);
	if (Cube.Succeeded()) Part->SetStaticMesh(Cube.Object);
	if (Solid.Succeeded()) Part->SetMaterial(0, Solid.Object);
	Part->SetRelativeLocation(Centre);
	Part->SetRelativeScale3D(Size / 100.0);
	Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Part->SetGenerateOverlapEvents(false);
	Parts.Add(Part);
	PartColours.Add(Colour);
}

void AWwCharacter::BeginPlay()
{
	Super::BeginPlay();
	for (int32 i = 0; i < Parts.Num() && i < PartColours.Num(); i++)
		if (UMaterialInstanceDynamic* Mid = Parts[i]->CreateAndSetMaterialInstanceDynamic(0)) Mid->SetVectorParameterValue(TEXT("Color"), PartColours[i]);
	FParse::Value(FCommandLine::Get(), TEXT("WwAutoFly="), AutoFlySpeed);
	GetCharacterMovement()->SetMovementMode(MOVE_None);
}

void AWwCharacter::SetFly(bool bEnable)
{
	bFly = bEnable;
	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->SetMovementMode(bFly ? MOVE_Flying : MOVE_Falling);
	Move->GravityScale = 2.04f;
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
}

void AWwCharacter::TryPlace()
{
	UWwWorldSubsystem* WorldSystem = GetGameInstance() ? GetGameInstance()->GetSubsystem<UWwWorldSubsystem>() : nullptr;
	if (!WorldSystem || WorldSystem->GetState() != EWwWorldState::Ready) return;
	if (!bHaveSpawn)
	{
		double X = 0, Z = 0, Height = 0;
		FString Override;
		if (FParse::Value(FCommandLine::Get(), TEXT("WwAt="), Override, false)) // -WwAt=x,z in metres
		{
			FString Left, Right;
			if (Override.Split(TEXT(","), &Left, &Right)) { X = FCString::Atod(*Left); Z = FCString::Atod(*Right); Height = WorldSystem->GetSampler()->Height(X, Z); }
		}
		else if (!WorldSystem->FindSpawn(X, Z, Height)) { X = 0; Z = 0; Height = WorldSystem->GetSampler()->Height(0, 0); }
		if (FParse::Param(FCommandLine::Get(), TEXT("WwCoast"))) // test switch: start over the nearest open, shelving sea shore
		{
			const ww::FTerrainSampler& Sampler = *WorldSystem->GetSampler();
			bool bFound = false;
			for (double Radius = 250; Radius < 200000 && !bFound; Radius += 250)
				for (int32 Dir = 0; Dir < 48 && !bFound; Dir++)
				{
					const double Px = X + FMath::Cos(Dir * UE_DOUBLE_PI / 24) * Radius, Pz = Z + FMath::Sin(Dir * UE_DOUBLE_PI / 24) * Radius;
					const ww::FTerrainSample T = Sampler.Sample(Px, Pz, 8);
					const double Depth = T.WaterLevel - T.Height;
					if (T.Water != ww::EWaterKind::Ocean || Depth < 4 || Depth > 8) continue;
					float Gx, Gz, Exposure;
					ww::FShore::CoastAt(Sampler, Px, Pz, Depth, 1, Gx, Gz, Exposure);
					const double Slope = FMath::Sqrt(Gx * Gx + Gz * Gz);
					if (Exposure < .9f || Slope < .015 || Slope > .12) continue;
					bFound = true; X = Px; Z = Pz; Height = T.Height;
					UE_LOG(LogWayfarer, Display, TEXT("Coast: %.1f, %.1f (water %.1f m deep, bed slope %.3f, exposure %.2f); the shore lies toward yaw %.0f"),
						Px, Pz, Depth, Slope, Exposure, FMath::RadiansToDegrees(FMath::Atan2(Gz, Gx)));
				}
		}
		SpawnPoint = UWwWorldSubsystem::ToUnreal(X, Z, Height);
		SetActorLocation(SpawnPoint + FVector(0, 0, 300), false, nullptr, ETeleportType::TeleportPhysics);
		bHaveSpawn = true;
		if (FParse::Param(FCommandLine::Get(), TEXT("WwProfile"))) // test switch: generated heights around the start place
			for (int32 Dir = 0; Dir < 4; Dir++)
			{
				FString Line;
				for (int32 Step = 0; Step <= 20; Step++)
					Line += FString::Printf(TEXT(" %.0f"), WorldSystem->GetSampler()->Height(X + (Dir == 0 ? 25 : Dir == 1 ? -25 : 0) * Step, Z + (Dir == 2 ? 25 : Dir == 3 ? -25 : 0) * Step));
				UE_LOG(LogWayfarer, Display, TEXT("Heights every 25 m to the %s:%s"), Dir == 0 ? TEXT("east") : Dir == 1 ? TEXT("west") : Dir == 2 ? TEXT("south") : TEXT("north"), *Line);
			}
		UE_LOG(LogWayfarer, Log, TEXT("Spawn at %.1f, %.1f (height %.1f m), biome %s"), X, Z, Height, *WorldSystem->BiomeNameAt(X, Z));
	}
	// Stand on the ground once its collision is in the scene.
	AWwTerrainStreamer* Streamer = nullptr;
	for (TActorIterator<AWwTerrainStreamer> It(GetWorld()); It; ++It) { Streamer = *It; break; }
	if (!Streamer || !Streamer->IsGroundReady(SpawnPoint)) return;
	FHitResult Hit;
	FCollisionQueryParams Params(FName(TEXT("WwPlace")), true, this);
	if (!GetWorld()->LineTraceSingleByChannel(Hit, SpawnPoint + FVector(0, 0, 5000), SpawnPoint - FVector(0, 0, 5000), ECC_WorldStatic, Params)) return;
	SetActorLocation(Hit.ImpactPoint + FVector(0, 0, GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5), false, nullptr, ETeleportType::TeleportPhysics);
	bPlaced = true;
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
		if (PC->IsLocalController()) { PC->SetInputMode(FInputModeGameOnly()); PC->bShowMouseCursor = false; PC->SetControlRotation(FRotator(-12, 35, 0));
			FString Look, Pitch, Yaw; // test switch -WwLook=pitch,yaw (degrees)
			if (FParse::Value(FCommandLine::Get(), TEXT("WwLook="), Look, false) && Look.Split(TEXT(","), &Pitch, &Yaw)) PC->SetControlRotation(FRotator(FCString::Atod(*Pitch), FCString::Atod(*Yaw), 0)); }
	LastPlace = GetActorLocation();
	GetCharacterMovement()->GravityScale = 2.04f;
	GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	if (AutoFlySpeed > 0)
	{
		double Up = 200; // test switch -WwUp=<metres above the ground to fly at>
		FParse::Value(FCommandLine::Get(), TEXT("WwUp="), Up);
		SetFly(true); AddActorWorldOffset(FVector(0, 0, Up * 100.0));
	}
	UE_LOG(LogWayfarer, Log, TEXT("Player placed on the ground at Z %.1f m (generated height %.1f m)"), Hit.ImpactPoint.Z / 100.0, SpawnPoint.Z / 100.0);
}

void AWwCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority() && !IsLocallyControlled()) return;
	if (!bPlaced) { if (HasAuthority()) TryPlace(); return; }
	SecondsPlaced += DeltaSeconds;
	Travelled += FVector::Dist2D(GetActorLocation(), LastPlace) / 100.0;
	LastPlace = GetActorLocation();

	UCharacterMovementComponent* Move = GetCharacterMovement();
	// Never under the generated ground (collision of a tile can arrive a moment after a fast landing).
	if (UWwWorldSubsystem* WorldSystem = GetGameInstance()->GetSubsystem<UWwWorldSubsystem>())
		if (std::shared_ptr<ww::FTerrainSampler> Sampler = WorldSystem->GetSampler())
		{
			const FVector Here = GetActorLocation();
			const double Ground = Sampler->Height(Here.X / 100.0, Here.Y / 100.0) * 100.0, Feet = Here.Z - GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			if (Feet < Ground - 150.0)
			{
				SetActorLocation(FVector(Here.X, Here.Y, Ground + GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 10), false, nullptr, ETeleportType::TeleportPhysics);
				Move->Velocity.Z = FMath::Max(Move->Velocity.Z, 0.0);
			}
		}
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !PC->IsLocalController()) return;

	if (AutoFlySpeed > 0)
	{
		Move->MaxFlySpeed = AutoFlySpeed * 100;
		Move->MaxAcceleration = 1e7f;
		AddMovementInput(FVector(1, 0.35, 0).GetSafeNormal(), 1);
		return;
	}

	// Look.
	float MouseX = 0, MouseY = 0;
	PC->GetInputMouseDelta(MouseX, MouseY);
	FRotator Control = PC->GetControlRotation();
	Control.Yaw += MouseX * MouseSensitivity * 20;
	Control.Pitch = FMath::Clamp(FRotator::NormalizeAxis(Control.Pitch) + MouseY * MouseSensitivity * 20, -85.0, 85.0);
	PC->SetControlRotation(Control);

	if (PC->WasInputKeyJustPressed(EKeys::F)) SetFly(!bFly);
	if (PC->WasInputKeyJustPressed(EKeys::F9)) DebugDump();
	UpdateGameplay(DeltaSeconds, PC, PC->IsInputKeyDown(EKeys::LeftShift) && (PC->IsInputKeyDown(EKeys::W) || PC->IsInputKeyDown(EKeys::S) || PC->IsInputKeyDown(EKeys::A) || PC->IsInputKeyDown(EKeys::D)));
	if (PC->WasInputKeyJustPressed(EKeys::Escape)) PC->ConsoleCommand(TEXT("quit"));

	const float Forward = (PC->IsInputKeyDown(EKeys::W) ? 1.f : 0.f) - (PC->IsInputKeyDown(EKeys::S) ? 1.f : 0.f);
	const float Right = (PC->IsInputKeyDown(EKeys::D) ? 1.f : 0.f) - (PC->IsInputKeyDown(EKeys::A) ? 1.f : 0.f);
	const bool bShift = PC->IsInputKeyDown(EKeys::LeftShift), bCtrl = PC->IsInputKeyDown(EKeys::LeftControl);
	const FRotator Yaw(0, Control.Yaw, 0);
	// Swimming (Thornwake's rules): in water over 1.25 m deep at the feet the player swims, head above the surface,
	// riding the sea's waves.
	bool bWasSwimming = bSwimming;
	bSwimming = false;
	double WaterLevel = 0;
	if (!bFly)
		if (UWwWorldSubsystem* WorldSystem = GetGameInstance()->GetSubsystem<UWwWorldSubsystem>())
			if (std::shared_ptr<ww::FTerrainSampler> Sampler = WorldSystem->GetSampler())
			{
				const FVector Here = GetActorLocation();
				const ww::FTerrainSample T = Sampler->Sample(Here.X / 100.0, Here.Y / 100.0, 2);
				const double Feet = (Here.Z - GetCapsuleComponent()->GetScaledCapsuleHalfHeight()) / 100.0;
				// (the sea's surface is its still level plus the waves the water shader shows here)
				double Level = T.WaterLevel;
				if (T.Water == ww::EWaterKind::Ocean) Level += WorldSystem->SeaSurface(Here.X / 100.0, Here.Y / 100.0, T.WaterLevel - T.Height, GetWorld()->GetTimeSeconds());
				if (T.Water != ww::EWaterKind::None && Level - Feet > 1.25) { bSwimming = true; WaterLevel = Level; }
			}
	if (bSwimming)
	{
		if (Move->MovementMode != MOVE_Flying) Move->SetMovementMode(MOVE_Flying);
		Move->BrakingDecelerationFlying = 0; // (the swim sets the velocity itself each frame; the engine's braking cancelled it)
		const float Speed = bShift ? 3.2f : 1.8f; // m/s
		const FVector Flat = (FRotationMatrix(Yaw).GetUnitAxis(EAxis::X) * Forward + FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y) * Right).GetClampedToMaxSize(1.0);
		const bool bDive = PC->IsInputKeyDown(EKeys::C) || PC->IsInputKeyDown(EKeys::Q), bUp = PC->IsInputKeyDown(EKeys::SpaceBar);
		const double Feet = (GetActorLocation().Z - GetCapsuleComponent()->GetScaledCapsuleHalfHeight()) / 100.0, Target = WaterLevel - 1.3;
		const bool bSubmerged = Feet < Target - 0.4;
		double Vertical;
		if (bDive) Vertical = bShift ? -3.0 : -1.8;      // C/Q: dive down to the bottom
		else if (bUp) Vertical = bShift ? 3.0 : 1.8;     // Space: swim up
		// Under water: swimming forward follows the view (look down to go deeper); idle, you drift up slowly.
		else if (bSubmerged) Vertical = Forward > 0 ? FMath::Sin(FMath::DegreesToRadians(FRotator::NormalizeAxis(Control.Pitch)) + 0.32) * Speed * Forward + 0.15 : 0.5;
		else Vertical = FMath::Clamp((Target - Feet) * 3.0, -2.0, 2.0); // buoyancy: head above the surface
		UE_LOG(LogWayfarer, Verbose, TEXT("Swim: feet %.2f target %.2f vertical %.2f mode %d velocity before %s"), Feet, Target, Vertical, (int32)Move->MovementMode.GetValue(), *Move->Velocity.ToString());
		Move->Velocity = FVector(Flat.X * Speed * 100.0, Flat.Y * Speed * 100.0, Vertical * 100.0);
		return;
	}
	if (bWasSwimming && !bFly) Move->SetMovementMode(MOVE_Falling);
	if (bFly)
	{
		// (Thornwake: 45 m/s, Shift 220, Ctrl six times that)
		Move->MaxFlySpeed = (bShift ? 22000.f : FlySpeed) * (bCtrl ? 6.f : 1.f);
		Move->BrakingDecelerationFlying = 6000;
		Move->MaxAcceleration = Move->MaxFlySpeed * 4;
		const float Up = (PC->IsInputKeyDown(EKeys::E) || PC->IsInputKeyDown(EKeys::SpaceBar) ? 1.f : 0.f) - (PC->IsInputKeyDown(EKeys::Q) ? 1.f : 0.f);
		AddMovementInput(Control.Vector(), Forward);
		AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y), Right);
		AddMovementInput(FVector::UpVector, Up);
	}
	else
	{
		Move->MaxWalkSpeed = bShift ? SprintSpeed : WalkSpeed;
		Move->MaxAcceleration = 2048;
		AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::X), Forward);
		AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y), Right);
		if (PC->WasInputKeyJustPressed(EKeys::SpaceBar)) Jump();
		if (PC->WasInputKeyJustReleased(EKeys::SpaceBar)) StopJumping();
	}
}

// ---------------------------------------------------------------------------------------------------- debug dump
void AWwCharacter::DebugDump()
{
	UWwWorldSubsystem* WorldSystem = GetGameInstance() ? GetGameInstance()->GetSubsystem<UWwWorldSubsystem>() : nullptr;
	std::shared_ptr<ww::FTerrainSampler> Sampler = WorldSystem ? WorldSystem->GetSampler() : nullptr;
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!Sampler || !PC) return;
	const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Dumps"));
	IFileManager::Get().MakeDirectory(*Dir, true);
	const FDateTime Now = FDateTime::Now();
	const FString Stamp = Now.ToString(TEXT("%Y%m%d-%H%M%S")), Path = Dir / FString::Printf(TEXT("dump-%s.txt"), *Stamp);

	const ww::FWorldPlan& Plan = *Sampler->Plan;
	const FWwWorldSettings& S = WorldSystem->GetSettings();
	const FVector Here = GetActorLocation();
	const double X = Here.X / 100.0, Z = Here.Y / 100.0, Feet = (Here.Z - GetCapsuleComponent()->GetScaledCapsuleHalfHeight()) / 100.0;
	const ww::FTerrainSample T = Sampler->Sample(X, Z, 2);
	const bool bWater = T.Water != ww::EWaterKind::None;
	const double Surface = bWater ? FMath::Max(T.Height, T.WaterLevel) : T.Height, Above = Feet - Surface, Sea = Plan.Profile().SeaLevel;
	int A, B; double Share;
	Plan.Biomes->Sample(X, Z, A, B, Share);
	double Temperature, Rain;
	Plan.Climate->At(X, Z, FMath::Max(0.0, T.Height - Sea), Temperature, Rain);
	const std::vector<ww::FBiome>& Pool = Plan.Biomes->Registry.Pool;
	static const TCHAR* WaterNames[] = { TEXT("None"), TEXT("Sea"), TEXT("Lake"), TEXT("River"), TEXT("Pond") };
	const FRotator Look = PC->GetControlRotation();
	FVector CameraAt; FRotator CameraRotation;
	PC->GetPlayerViewPoint(CameraAt, CameraRotation);
	AWwSky* Sky = nullptr;
	for (TActorIterator<AWwSky> It(GetWorld()); It; ++It) { Sky = *It; break; }
	AWwTerrainStreamer* Streamer = nullptr;
	for (TActorIterator<AWwTerrainStreamer> It(GetWorld()); It; ++It) { Streamer = *It; break; }
	const FVector2D Window = GEngine && GEngine->GameViewport ? [&] { FVector2D V; GEngine->GameViewport->GetViewportSize(V); return V; }() : FVector2D::ZeroVector;
	const double Pitch = FRotator::NormalizeAxis(Look.Pitch), Yaw = FRotator::NormalizeAxis(Look.Yaw);

	FString Out;
	Out += FString::Printf(TEXT("Wayfarer's Wake debug dump %s\n"), *Now.ToString(TEXT("%Y-%m-%d %H:%M:%S")));
	Out += FString::Printf(TEXT("Version 0.1 · Unreal Engine %s · %s · %s build\n"), *FEngineVersion::Current().ToString(), *FPlatformMisc::GetOSVersion(), LexToString(FApp::GetBuildConfiguration()));
	Out += FString::Printf(TEXT("GPU %s · window (%.0f, %.0f) · FPS %.0f\n\n"), *GRHIAdapterName, Window.X, Window.Y, 1.0 / FMath::Max(FApp::GetDeltaTime(), 1e-4));
	Out += FString::Printf(TEXT("World: %s (type %s), seed %d, size %.0f km, sea level %.3f m, terrain revision %d\n"), UTF8_TO_TCHAR(Plan.Profile().Name.c_str()), *S.WorldType, S.Seed,
		Plan.Profile().WorldSize / 1000.0, Sea, S.TerrainRevision);
	Out += FString::Printf(TEXT("Player: x %.3f, z %.3f, y %.3f (%.3f m above ground/water) · %s\n"), X, Z, Feet, Above, bFly ? TEXT("fly") : bSwimming ? TEXT("swimming") : TEXT("walking"));
	Out += FString::Printf(TEXT("Ground %.3f m · water %s"), T.Height, WaterNames[FMath::Clamp((int32)T.Water, 0, 4)]);
	if (bWater) Out += FString::Printf(TEXT(" surface %.3f m, depth %.3f m"), T.WaterLevel, T.WaterLevel - T.Height);
	if (T.Water == ww::EWaterKind::Ocean)
	{
		float Gx = 0, Gz = 0, Exposure = 1;
		ww::FShore::CoastAt(*Sampler, X, Z, T.WaterLevel - T.Height, 1, Gx, Gz, Exposure);
		Out += FString::Printf(TEXT(", shore: bed rises toward (%.4f, %.4f), exposure %.3f"), Gx, Gz, Exposure);
	}
	if (T.Water == ww::EWaterKind::Ocean)
		if (UWwWorldSubsystem* Waves = GetGameInstance()->GetSubsystem<UWwWorldSubsystem>())
			Out += FString::Printf(TEXT(", waves now %+.3f m (sea state %.2f, significant wave height %.2f m)"),
				Waves->SeaSurface(X, Z, T.WaterLevel - T.Height, GetWorld()->GetTimeSeconds()), Waves->GetOcean()->SeaState(), Waves->GetOcean()->Hs());
	Out += FString::Printf(TEXT(" · mountain %.3f · land %.4f · scree %.3f · lava %.3f\n"), T.Mountain, T.Land, T.Scree, T.Lava);
	Out += FString::Printf(TEXT("Biome %s"), UTF8_TO_TCHAR(Pool[A].Name.c_str()));
	if (A != B) Out += FString::Printf(TEXT(" (%.3f %%, -> %s)"), Share * 100, UTF8_TO_TCHAR(Pool[B].Name.c_str()));
	Out += FString::Printf(TEXT(" · %.3f °C, %.3f mm/yr · forest cover %.3f\n"), Temperature, Rain, ww::FVegetation::ForestDensity(*Sampler, X, Z, T));
	Out += FString::Printf(TEXT("Camera: pitch %.2f, yaw %.2f (degrees; yaw 0 looks east, 90 south) · field of view %.0f · at (%.2f, %.2f, %.2f) m\n"), Pitch, Yaw, Camera->FieldOfView,
		CameraAt.X / 100, CameraAt.Y / 100, CameraAt.Z / 100);
	if (Sky) Out += Sky->DebugInfo() + TEXT("\n");
	if (FWwGameplay* Game = GetGameInstance()->GetSubsystem<UWwWorldSubsystem>()->GetGameplay())
	{
		Out += FString::Printf(TEXT("Needs: %s (game time %.2f h)") LINE_TERMINATOR, *Game->NeedsLine(), Game->GetHours());
		for (const FString& Text : Game->PackLines(X, Z)) Out += TEXT("  ") + Text + LINE_TERMINATOR;
		Out += FString::Printf(TEXT("Objects lying in the world: %d") LINE_TERMINATOR, (int32)Game->Objects().size());
	}
	if (Sky && Sky->GetCameraDepth() > .02f) Out += FString::Printf(TEXT("Camera under water: %.3f m below the surface\n"), Sky->GetCameraDepth());
	if (Streamer)
		Out += FString::Printf(TEXT("Terrain: %d tiles (%d wanted, %d missing, %d building), %d trees, %d plants, %d waterfalls and %d spray puffs shown, tile build %.2f ms average\n"), Streamer->GetTileCount(), Streamer->GetDesiredCount(),
			Streamer->GetMissingCount(), Streamer->GetPendingCount(), Streamer->GetTreeCount(), Streamer->GetPlantCount(), Streamer->GetFallCount(), Streamer->GetPuffCount(), Streamer->GetAverageBuildMs());
	{
		// Solid things near by (tree trunks, rocks, bushes, stumps, logs): what the player bumps into besides the ground.
		TArray<FOverlapResult> Hits;
		GetWorld()->OverlapMultiByChannel(Hits, Here, FQuat::Identity, ECC_WorldStatic, FCollisionShape::MakeSphere(3000.f), FCollisionQueryParams(FName(TEXT("WwDump")), false, this));
		int32 Solid = 0;
		for (const FOverlapResult& Hit : Hits) if (Cast<UInstancedStaticMeshComponent>(Hit.GetComponent())) Solid++;
		Out += FString::Printf(TEXT("Solid objects within 30 m: %d\n"), Solid);
	}
	Out += FString::Printf(TEXT("Memory: %.0f MB\n"), FPlatformMemory::GetStats().UsedPhysical / 1048576.0);
	Out += FString::Printf(TEXT("Command line: %s\n\n"), FCommandLine::Get());
	// The same scene again (for the developer's screenshot runs).
	Out += TEXT("Reproduce:\n");
	Out += FString::Printf(TEXT("  -WwType=%s -WwSeed=%d -WwSizeKm=%.0f -WwRevision=%d -WwAt=%.3f,%.3f -WwLook=%.2f,%.2f"), *S.WorldType, S.Seed, S.SizeKm, S.TerrainRevision, X, Z, Pitch, Yaw);
	if (Sky) Out += FString::Printf(TEXT(" -WwHour=%.3f"), Sky->Hour);
	if (bFly) Out += FString::Printf(TEXT(" -WwAutoFly=0.01 -WwUp=%.3f"), FMath::Max(0.0, Feet - T.Height));
	Out += TEXT("\n");
	FFileHelper::SaveStringToFile(Out, *Path, FFileHelper::EEncodingOptions::ForceUTF8);
	FScreenshotRequest::RequestScreenshot(Dir / FString::Printf(TEXT("dump-%s.png"), *Stamp), true, false);
	Status = FString::Printf(TEXT("Debug dump saved: %s (send me the .txt and .png)"), *Path);
	StatusUntil = GetWorld()->GetTimeSeconds() + 12.0;
	UE_LOG(LogWayfarer, Display, TEXT("DUMPED %s"), *Path);
}

// ---------------------------------------------------------------------------------------------------- placeholder mechanics
void AWwCharacter::UpdateGameplay(float DeltaSeconds, APlayerController* PC, bool bSprinting)
{
	UWwWorldSubsystem* WorldSystem = GetGameInstance() ? GetGameInstance()->GetSubsystem<UWwWorldSubsystem>() : nullptr;
	FWwGameplay* Game = WorldSystem ? WorldSystem->GetGameplay() : nullptr;
	if (!Game || !bPlaced) return;
	AWwSky* Sky = nullptr;
	for (TActorIterator<AWwSky> It(GetWorld()); It; ++It) { Sky = *It; break; }
	AWwTerrainStreamer* Streamer = nullptr;
	for (TActorIterator<AWwTerrainStreamer> It(GetWorld()); It; ++It) { Streamer = *It; break; }
	const FVector Here = GetActorLocation();
	const double X = Here.X / 100.0, Z = Here.Y / 100.0, Feet = (Here.Z - GetCapsuleComponent()->GetScaledCapsuleHalfHeight()) / 100.0;
	// (developer flight is outside the game: no hunger or cold up there)
	if (!bFly) Game->Tick(Sky ? Sky->HoursPerMinute * DeltaSeconds / 60.0 : 0.0, Sky ? Sky->Hour : 12.0, X, Z, Feet, bSwimming, bSprinting ? 1.0 : 0.0);
	FString Said;
	if (PC->WasInputKeyJustPressed(EKeys::I)) bPackOpen = !bPackOpen;
	if (!bFly && PC->WasInputKeyJustPressed(EKeys::E)) Said = Game->Use(X, Z, Feet);
	if (!bFly && !bPackOpen && PC->WasInputKeyJustPressed(EKeys::LeftMouseButton))
	{
		const FVector Look = FRotationMatrix(FRotator(0, PC->GetControlRotation().Yaw, 0)).GetUnitAxis(EAxis::X);
		Said = Game->Strike(X, Z, Look.X, Look.Y);
	}
	if (PC->WasInputKeyJustPressed(EKeys::T)) Said = Game->Eat();
	// Test switch -WwPlayTest: the game plays itself for a moment (the axe swung all round, E, two recipes, a bite to
	// eat), so a scripted run can check the mechanics end to end; the log and the F9 dump show what came of it.
	static const bool bPlayTest = FParse::Param(FCommandLine::Get(), TEXT("WwPlayTest"));
	if (bPlayTest && !bFly)
	{
		static float Clock = 0; static int32 Step = 0;
		if ((Clock += DeltaSeconds) > 0.15f)
		{
			Clock = 0; Step++;
			if (Step == 1)
			{
				// Stand two metres from the nearest tree.
				const ww::FTerrainSampler& Sampler = *WorldSystem->GetSampler();
				const double Cell = ww::FVegetation::LatticeCell(Sampler);
				double Best = 1e9; ww::FLatticeTree Nearest;
				for (int64 cz = (int64)FMath::FloorToDouble((Z - 60) / Cell); cz <= (int64)FMath::FloorToDouble((Z + 60) / Cell); cz++)
					for (int64 cx = (int64)FMath::FloorToDouble((X - 60) / Cell); cx <= (int64)FMath::FloorToDouble((X + 60) / Cell); cx++)
					{
						ww::FLatticeTree T;
						if (!ww::FVegetation::TreeAt(Sampler, cx, cz, T)) continue;
						const double D = FMath::Square(T.X - X) + FMath::Square(T.Z - Z);
						if (D < Best) { Best = D; Nearest = T; }
					}
				if (Best < 1e9)
				{
					UE_LOG(LogWayfarer, Display, TEXT("Game: play test walks to the tree at %.1f, %.1f (size %.2f)"), Nearest.X, Nearest.Z, Nearest.Scale);
					SetActorLocation(FVector((Nearest.X + 2.0) * 100.0, Nearest.Z * 100.0, (Nearest.Y + 3.0) * 100.0), false, nullptr, ETeleportType::TeleportPhysics);
				}
				return;
			}
			const double Angle = Step * 0.7;
			if (Step % 3 == 0) Said = Game->Use(X, Z, Feet);
			else if (Step == 200) Said = Game->Eat();
			else if (Step == 205) Said = Game->Make(0, X, Z);
			else if (Step == 208) Said = Game->Make(5, X, Z);
			else Said = Game->Strike(X, Z, FMath::Cos(Angle), FMath::Sin(Angle));
		}
	}
	if (bPackOpen)
	{
		static const FKey Numbers[10] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine, EKeys::Zero };
		for (int32 i = 0; i < 10; i++) if (PC->WasInputKeyJustPressed(Numbers[i])) Said = Game->Make(i, X, Z);
	}
	if (Streamer)
	{
		for (const TPair<int64, int64>& Cell : Game->TakeFelled()) Streamer->FellTree(Cell.Key, Cell.Value);
		if (Game->TakeObjectsChanged()) Streamer->SyncObjects(Game->Objects());
	}
	if (Game->TakeDied())
	{
		Said = TEXT("You died. Everything you carried lies where you fell.");
		SetActorLocation(SpawnPoint + FVector(0, 0, 300), false, nullptr, ETeleportType::TeleportPhysics);
		bPlaced = false; // (stand on the ground at the start place again)
	}
	if (!Said.IsEmpty()) { Notice = Said; NoticeSeconds = 5.f; UE_LOG(LogWayfarer, Display, TEXT("Game: %s"), *Said); }
	else if ((NoticeSeconds -= DeltaSeconds) <= 0) Notice.Reset();
}
