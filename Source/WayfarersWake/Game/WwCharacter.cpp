#include "Game/WwCharacter.h"
#include "Game/WwWorldSubsystem.h"
#include "Game/WwTerrainStreamer.h"
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
	Move->JumpZVelocity = 480;
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
	Camera->FieldOfView = 80;

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
	Move->GravityScale = 1;
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
	GetCharacterMovement()->GravityScale = 1;
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
	if (PC->WasInputKeyJustPressed(EKeys::Escape)) PC->ConsoleCommand(TEXT("quit"));

	const float Forward = (PC->IsInputKeyDown(EKeys::W) ? 1.f : 0.f) - (PC->IsInputKeyDown(EKeys::S) ? 1.f : 0.f);
	const float Right = (PC->IsInputKeyDown(EKeys::D) ? 1.f : 0.f) - (PC->IsInputKeyDown(EKeys::A) ? 1.f : 0.f);
	const bool bShift = PC->IsInputKeyDown(EKeys::LeftShift), bCtrl = PC->IsInputKeyDown(EKeys::LeftControl);
	const FRotator Yaw(0, Control.Yaw, 0);
	if (bFly)
	{
		Move->MaxFlySpeed = FlySpeed * (bCtrl ? 40.f : bShift ? 8.f : 1.f);
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
