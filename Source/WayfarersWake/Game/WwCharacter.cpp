#include "Game/WwCharacter.h"
#include "Game/WwGameplay.h"
#include "Game/WwWorldSubsystem.h"
#include "Game/WwTerrainStreamer.h"
#include "Game/WwGame.h"
#include "Game/WwSettings.h"
#include "Game/WwWildlife.h"
#include "Animation/AnimSequence.h"
#include "Animation/SkeletalMeshActor.h"
#include "Engine/StaticMeshActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
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
	LoadBody();
}

void AWwCharacter::LoadBody()
{
	// The body chosen in the menu (Character page). If its assets are missing (Tools/ImportCharacters.cmd not run), the
	// old figure of blocks stands in.
	static const TCHAR* Names[] = { TEXT("HumanMale"), TEXT("HumanFemale"), TEXT("OrcMale"), TEXT("OrcFemale"), TEXT("DwarfMale"), TEXT("DwarfFemale") };
	static const float Heights[] = { 180, 170, 205, 192, 138, 130 };
	static const TCHAR* ClipNames[] = { TEXT("Idle"), TEXT("Walk"), TEXT("Run"), TEXT("Swim"), TEXT("Attack"), TEXT("Crouch"), TEXT("Climb") };
	const int32 Which = FMath::Clamp(FWwSettings::Get().Int(TEXT("char.body")), 0, 5);
	const FString Name = Names[Which], Folder = FString::Printf(TEXT("/Game/Characters/%s/"), *Name);
	USkeletalMesh* BodyMesh = LoadObject<USkeletalMesh>(nullptr, *(Folder + TEXT("SK_") + Name + TEXT(".SK_") + Name));
	Clips.Reset();
	for (const TCHAR* Clip : ClipNames)
	{
		const FString Asset = FString::Printf(TEXT("A_%s_%s"), *Name, Clip);
		Clips.Add(LoadObject<UAnimSequence>(nullptr, *(Folder + Asset + TEXT(".") + Asset)));
	}
	bHasBody = BodyMesh != nullptr;
	for (UStaticMeshComponent* Part : Parts) if (Part) Part->SetVisibility(!bHasBody);
	USkeletalMeshComponent* Body = GetMesh();
	Body->SetVisibility(bHasBody);
	Pose = EPose::None;
	if (!bHasBody) { UE_LOG(LogWayfarer, Warning, TEXT("Body %s is not in the project (run Tools/ImportCharacters.cmd): the figure of blocks stands in"), *Name); return; }
	BodyHeight = Heights[Which];
	Body->SetSkeletalMesh(BodyMesh);
	Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	// (its skin: the texture Tom's body came with, as a plain matte material)
	if (UMaterialInterface* Skin = LoadObject<UMaterialInterface>(nullptr, *(Folder + TEXT("M_") + Name + TEXT(".M_") + Name))) Body->SetMaterial(0, Skin);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// Whatever units the file came in with, the body stands as tall as its race; feet at the capsule's foot; facing
	// ahead (the bodies face -Y in Blender, which arrives as +Y here).
	const float Tall = FMath::Max(1.f, BodyMesh->GetBounds().BoxExtent.Z * 2.f);
	const float Half = BodyHeight * 0.5f;
	GetCapsuleComponent()->SetCapsuleSize(FMath::Min(34.f, BodyHeight * 0.19f), Half);
	Body->SetRelativeScale3D(FVector(BodyHeight / Tall));
	Body->SetRelativeLocationAndRotation(FVector(0, 0, -Half), FRotator(0, -90, 0));
	UE_LOG(LogWayfarer, Display, TEXT("Body: %s, %.0f cm tall (the file's %.0f), %d of 7 animations"), *Name, BodyHeight, Tall, Clips.FilterByPredicate([](const TObjectPtr<UAnimSequence>& A) { return A != nullptr; }).Num());
}

FString AWwCharacter::UpdateBow(float DeltaSeconds, APlayerController* PC, FWwGameplay* Game)
{
	// The bow (Thornwake has one among its items; this is its first use here). Hold the right mouse button to draw: it
	// takes about a second to come to full draw. Let go to loose: the arrow flies where the middle of the screen
	// points, faster and harder the fuller the draw, and falls as it goes. A full draw kills a deer; a half one wounds.
	FString Said;
	// Test switch -WwBowTest (with -WwDeerTest for a herd): eight seconds in, a bow and arrows, a full draw aimed at the
	// nearest deer with the rise the distance needs, and the arrow loosed; again two seconds later.
	static const bool bBowTest = FParse::Param(FCommandLine::Get(), TEXT("WwBowTest"));
	static int32 TestShots = 0;
	FVector Mark;
	if (bBowTest && Wildlife && TestShots < 2 && SecondsPlaced > 8.0 + 2.5 * TestShots && DrawSeconds <= 0 && Wildlife->NearestAlive(GetActorLocation(), Mark))
	{
		TestShots++;
		Game->Give(TEXT("bow"), 1); Game->Give(TEXT("arrow"), 3);
		const FVector To = Mark - (GetActorLocation() + FVector(0, 0, 50));
		const double Flat = To.Size2D(), Rise = 0.5 * FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(980.0 * Flat / FMath::Square(5500.0), 0.0, 1.0)));
		PC->SetControlRotation(FRotator(FMath::RadiansToDegrees(FMath::Atan2(To.Z, Flat)) + Rise, FMath::RadiansToDegrees(FMath::Atan2(To.Y, To.X)), 0));
		DrawSeconds = 1.2f;
		UE_LOG(LogWayfarer, Display, TEXT("Bow test: a deer %.0f m off, aiming %.1f degrees over it"), Flat / 100, Rise);
	}
	const bool bHold = !bFly && !bSwimming && !bPackOpen && KeyDown(EKeys::RightMouseButton);
	if (bHold && !bBowRefused)
	{
		if (DrawSeconds <= 0)
		{
			if (Game->Count(TEXT("bow")) < 1) { Said = TEXT("You have no bow."); bBowRefused = true; }
			else if (Game->Count(TEXT("arrow")) < 1) { Said = TEXT("You have no arrows."); bBowRefused = true; }
		}
		if (!bBowRefused)
		{
			DrawSeconds += DeltaSeconds;
			SetActorRotation(FRotator(0, PC->GetControlRotation().Yaw, 0)); // (the archer faces the shot)
		}
	}
	else if (!bHold)
	{
		if (DrawSeconds > 0.25f && Game->Take(TEXT("arrow"), 1))
		{
			const float Power = FMath::Clamp(DrawSeconds / 1.1f, 0.3f, 1.f);
			FVector Eye; FRotator Look;
			PC->GetPlayerViewPoint(Eye, Look);
			const FVector Right = FRotationMatrix(FRotator(0, Look.Yaw, 0)).GetUnitAxis(EAxis::Y);
			const FVector Start = GetActorLocation() + FVector(0, 0, 50) + Right * 25.0;
			const FVector Dir = (Eye + Look.Vector() * 6000.0 - Start).GetSafeNormal();
			FActorSpawnParameters Params;
			Params.ObjectFlags |= RF_Transient;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			if (AStaticMeshActor* Arrow = GetWorld()->SpawnActor<AStaticMeshActor>(Start, Dir.Rotation(), Params))
			{
				// (a thin shaft, 75 cm long: there is no arrow model yet)
				UStaticMeshComponent* Shaft = Arrow->GetStaticMeshComponent();
				Shaft->SetMobility(EComponentMobility::Movable);
				Shaft->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
				Shaft->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Arrow->SetActorScale3D(FVector(0.75, 0.018, 0.018));
				Arrows.Add(Arrow); ArrowAt.Add(Start); ArrowVelocity.Add(Dir * 5500.0 * Power); ArrowLife.Add(0); ArrowDamage.Add(20.f + 45.f * Power); ArrowStuck.Add(false);
			}
			Said = FString::Printf(TEXT("Loosed (%d arrows left)."), Game->Count(TEXT("arrow")));
			UE_LOG(LogWayfarer, Display, TEXT("Bow: an arrow at %.0f %% draw, %.0f m/s"), Power * 100, 55.0 * Power);
		}
		DrawSeconds = 0;
		bBowRefused = false;
	}
	// The arrows in the air fall as they fly; one that meets an animal wounds or kills it, one that meets the ground stays there a while.
	UWwWorldSubsystem* WorldSystem = GetGameInstance()->GetSubsystem<UWwWorldSubsystem>();
	const std::shared_ptr<ww::FTerrainSampler> Sampler = WorldSystem ? WorldSystem->GetSampler() : nullptr;
	for (int32 i = Arrows.Num() - 1; i >= 0; i--)
	{
		ArrowLife[i] += DeltaSeconds;
		bool bGone = !Arrows[i] || ArrowLife[i] > (ArrowStuck[i] ? 40.f : 8.f);
		if (!bGone && !ArrowStuck[i])
		{
			ArrowVelocity[i].Z -= 980.0 * DeltaSeconds;
			const FVector Next = ArrowAt[i] + ArrowVelocity[i] * DeltaSeconds;
			FString Hit;
			if (Wildlife && Wildlife->Shot(ArrowAt[i], Next, ArrowDamage[i], Hit)) { Said = Hit; bGone = true; UE_LOG(LogWayfarer, Display, TEXT("Bow: %s"), *Hit); }
			else if (Sampler && Sampler->Height(Next.X / 100.0, Next.Y / 100.0, 2) * 100.0 > Next.Z) { ArrowStuck[i] = true; ArrowLife[i] = 0; }
			ArrowAt[i] = Next;
			if (!bGone) Arrows[i]->SetActorLocationAndRotation(Next, ArrowVelocity[i].Rotation());
		}
		if (bGone)
		{
			if (Arrows[i]) Arrows[i]->Destroy();
			Arrows.RemoveAt(i); ArrowAt.RemoveAt(i); ArrowVelocity.RemoveAt(i); ArrowLife.RemoveAt(i); ArrowDamage.RemoveAt(i); ArrowStuck.RemoveAt(i);
		}
	}
	return Said;
}

FString AWwCharacter::UpdateDebug(FWwGameplay* Game)
{
	FWwSettings& Set = FWwSettings::Get();
	FString Said;
	Game->SetNeedsRun(Set.On(TEXT("debug.needs")));
	if (Set.Changed(TEXT("debug.give"), GiveSeen)) Said = Game->Give(Set.Option(TEXT("debug.item")), Set.Int(TEXT("debug.amount")));
	if (Set.Changed(TEXT("debug.giveall"), GiveAllSeen)) Said = Game->GiveAll();
	if (Set.Changed(TEXT("debug.heal"), HealSeen)) { Game->Restore(); Said = TEXT("Fed, warm, dry and well (debug)."); }
	if (Set.Changed(TEXT("debug.spawn"), SpawnSeen)) SpawnCreature(Set.Int(TEXT("debug.creature")), (float)Set.Num(TEXT("debug.size")));
	if (Set.Changed(TEXT("debug.herd"), HerdSeen) && Wildlife)
	{
		const APlayerController* Looking = Cast<APlayerController>(GetController());
		Wildlife->SpawnHerd(GetActorLocation(), Looking ? Looking->GetControlRotation().Yaw : 0.f, 5000.f, 4);
		Said = TEXT("A herd of deer, 50 m ahead (debug).");
	}
	if (Set.Changed(TEXT("debug.clear"), ClearSeen))
	{
		for (AActor* Creature : Spawned) if (Creature) Creature->Destroy();
		Said = FString::Printf(TEXT("Took %d creatures away (debug)."), Spawned.Num());
		Spawned.Reset();
	}
	return Said;
}

void AWwCharacter::SpawnCreature(int32 Which, float Size)
{
	// The creatures of the asset packs (Animals_Free, the blue whale, the megalodon): mesh, the animation it is shown
	// with, how many times its own size it is at "size 1" (the whale and the shark are mystical giants: Tom, 2026-11-10),
	// and whether it lives in water.
	struct FCreature { const TCHAR* Name; const TCHAR* Mesh; const TCHAR* Clip; float Scale; bool bSea; };
	static const FCreature Creatures[] = {
		{ TEXT("Deer"), TEXT("/Game/Animals_Free/Animals/Meshes/SKM_Deer_001"), TEXT("/Game/Animals_Free/Animals/Animations/ANIM_Deer_001_Anim_Deer_001_idle"), 1, false },
		{ TEXT("Dog"), TEXT("/Game/Animals_Free/Animals/Meshes/SKM_Dog_001"), TEXT("/Game/Animals_Free/Animals/Animations/ANIM_Dog_001_Anim_Dog_001_idle"), 1, false },
		{ TEXT("Horse"), TEXT("/Game/Animals_Free/Animals/Meshes/SKM_Horse_001"), TEXT("/Game/Animals_Free/Animals/Animations/ANIM_Horse_001_Anim_Horse_001_idle"), 1, false },
		{ TEXT("Chicken"), TEXT("/Game/Animals_Free/Animals/Meshes/SKM_Chicken_001"), TEXT("/Game/Animals_Free/Animals/Animations/ANIM_Chicken_001_Anim_Chicken_001_idle"), 1, false },
		{ TEXT("Cat"), TEXT("/Game/Animals_Free/Animals/Meshes/SKM_Kitty_001"), TEXT("/Game/Animals_Free/Animals/Animations/ANIM_Kitty_001_Anim_Kitty_001_idle"), 1, false },
		{ TEXT("Penguin"), TEXT("/Game/Animals_Free/Animals/Meshes/SKM_Pinguin_001"), TEXT("/Game/Animals_Free/Animals/Animations/ANIM_Pinguin_001_Anim_Pinguin_001_idle"), 1, false },
		{ TEXT("Tiger"), TEXT("/Game/Animals_Free/Animals/Meshes/SKM_Tiger_001"), TEXT("/Game/Animals_Free/Animals/Animations/ANIM_Tiger_001_Anim_Tiger_001_idle"), 1, false },
		{ TEXT("Blue whale"), TEXT("/Game/BlueWhaleAndWhaleCalf/BaseMesh/BlueWhale/SK_blue_whale"), TEXT("/Game/BlueWhaleAndWhaleCalf/Animations/Whale/AS_While_swimming_03"), 6, true },
		{ TEXT("Whale calf"), TEXT("/Game/BlueWhaleAndWhaleCalf/BaseMesh/BlueWhaleCalf/SK_blue_whale_calf"), TEXT("/Game/BlueWhaleAndWhaleCalf/Animations/WhaleCalf/AS_WhileCalf_swimming_03"), 4, true },
		{ TEXT("Megalodon"), TEXT("/Game/Shark_Megalodon/Base_mesh/SK_Shark_Megalodon"), TEXT("/Game/Shark_Megalodon/Animations/Anim_Shark_Megalodon_Swim_03_WithoutMoving"), 23, true },
	};
	const FCreature& C = Creatures[FMath::Clamp(Which, 0, (int32)UE_ARRAY_COUNT(Creatures) - 1)];
	auto Path = [](const TCHAR* Package) { const FString P(Package); return P + TEXT(".") + FPaths::GetBaseFilename(P); };
	USkeletalMesh* CreatureMesh = LoadObject<USkeletalMesh>(nullptr, *Path(C.Mesh));
	UAnimSequence* Clip = LoadObject<UAnimSequence>(nullptr, *Path(C.Clip));
	UWwWorldSubsystem* WorldSystem = GetGameInstance() ? GetGameInstance()->GetSubsystem<UWwWorldSubsystem>() : nullptr;
	const std::shared_ptr<ww::FTerrainSampler> Sampler = WorldSystem ? WorldSystem->GetSampler() : nullptr;
	if (!CreatureMesh || !Sampler) { Notice = FString::Printf(TEXT("%s is not in the project (its asset pack is missing)."), C.Name); NoticeSeconds = 5.f; return; }
	const float Scale = C.Scale * FMath::Max(Size, 0.05f);
	const FBoxSphereBounds Bounds = CreatureMesh->GetBounds();
	const double Long = FMath::Max3(Bounds.BoxExtent.X, Bounds.BoxExtent.Y, Bounds.BoxExtent.Z) * 2.0 * Scale;
	// In front of where the player looks, far enough that it does not stand on them; on the ground, or under the
	// water's surface for the sea's creatures.
	APlayerController* PC = Cast<APlayerController>(GetController());
	const FRotator Facing(0, PC ? PC->GetControlRotation().Yaw : GetActorRotation().Yaw, 0);
	const FVector At = GetActorLocation() + Facing.Vector() * (400.0 + Long * 0.7);
	const ww::FTerrainSample T = Sampler->Sample(At.X / 100.0, At.Y / 100.0, 2);
	const bool bWater = T.Water != ww::EWaterKind::None && T.WaterLevel > T.Height;
	double Z = T.Height * 100.0;
	// (a sea creature's back lies just under the surface, wherever its mesh keeps its origin; in water too shallow for
	// it the bed does not lift it out: it is a test, and a giant in a puddle is the tester's affair)
	if (bWater && C.bSea) Z = T.WaterLevel * 100.0 - (Bounds.Origin.Z + Bounds.BoxExtent.Z * 1.25) * Scale;
	else if (bWater) Z = T.WaterLevel * 100.0;
	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASkeletalMeshActor* Creature = GetWorld()->SpawnActor<ASkeletalMeshActor>(FVector(At.X, At.Y, Z), FRotator(0, Facing.Yaw + 90.f, 0), Params);
	if (!Creature) return;
	USkeletalMeshComponent* Shown = Creature->GetSkeletalMeshComponent();
	Shown->SetMobility(EComponentMobility::Movable);
	Shown->SetSkeletalMesh(CreatureMesh);
	Shown->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Creature->SetActorScale3D(FVector(Scale));
	if (Clip)
	{
		Shown->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		Shown->PlayAnimation(Clip, true);
		// (a giant moves slowly: the time of a movement grows with the root of the size)
		Shown->SetPlayRate(1.f / FMath::Sqrt(FMath::Max(Scale, 1.f)));
	}
	Spawned.Add(Creature);
	Notice = FString::Printf(TEXT("%s, %.1f m long, put %s (debug)."), C.Name, Long / 100.0, bWater ? TEXT("in the water ahead") : TEXT("on the ground ahead"));
	NoticeSeconds = 5.f;
	UE_LOG(LogWayfarer, Display, TEXT("Debug: %s"), *Notice);
}

void AWwCharacter::UpdateBody(float DeltaSeconds, bool bSprinting)
{
	FWwSettings& Set = FWwSettings::Get();
	if (Set.Changed(TEXT("char.body"), BodySeen)) LoadBody();
	// (aiming: the camera comes in over the right shoulder)
	const float Aim = IsAiming() ? 1.f : 0.f;
	Boom->TargetArmLength = FMath::FInterpTo(Boom->TargetArmLength, FMath::Lerp((float)Set.Num(TEXT("char.camera")) * 100.f, 190.f, Aim), DeltaSeconds, 8.f);
	Boom->SocketOffset = FMath::VInterpTo(Boom->SocketOffset, FVector(0, 55.f * Aim, 90.f - 30.f * Aim), DeltaSeconds, 8.f);
	if (!bHasBody) return;
	// Which animation: a swing of the arm while it lasts; treading water; crouched; else by how fast the feet move.
	AttackSeconds -= DeltaSeconds;
	const double Speed = GetVelocity().Size2D();
	EPose Wanted = EPose::Idle;
	float Rate = 1.f;
	if (AttackSeconds > 0) Wanted = EPose::Attack;
	else if (bSwimming) { Wanted = EPose::Swim; Rate = 0.8f + (float)FMath::Clamp(Speed / 250.0, 0.0, 1.0); }
	else if (bFly) Wanted = EPose::Idle;
	else if (bCrouching && Speed < 30.0) Wanted = EPose::Crouch;
	else if (Speed > 30.0)
	{
		// (the steps keep pace with the ground covered: the walk as keyed suits 1.5 m/s, the run 5.2 m/s)
		Wanted = bSprinting || Speed > 650.0 ? EPose::Run : EPose::Walk;
		Rate = (float)FMath::Clamp(Speed / (Wanted == EPose::Run ? 520.0 : 150.0), 0.5, 2.4);
	}
	USkeletalMeshComponent* Body = GetMesh();
	if (Wanted != Pose)
	{
		Pose = Wanted;
		if (UAnimSequence* Clip = Clips.IsValidIndex((int32)Pose - 1) ? Clips[(int32)Pose - 1].Get() : nullptr) Body->PlayAnimation(Clip, Pose != EPose::Attack);
	}
	Body->SetPlayRate(Pose == EPose::Attack ? 1.f : Rate);
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
		Home = SpawnPoint;
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
	// (arriving over water, by the map or the menu: at the surface, not on the bed far below)
	if (bMoved)
	{
		const ww::FTerrainSample There = WorldSystem->GetSampler()->Sample(SpawnPoint.X / 100.0, SpawnPoint.Y / 100.0, 2);
		if (There.Water != ww::EWaterKind::None && There.WaterLevel * 100.0 > Hit.ImpactPoint.Z + 150.0)
			SetActorLocation(FVector(SpawnPoint.X, SpawnPoint.Y, There.WaterLevel * 100.0 - 40.0), false, nullptr, ETeleportType::TeleportPhysics);
	}
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
		if (PC->IsLocalController() && !bMoved) { PC->SetInputMode(FInputModeGameOnly()); PC->bShowMouseCursor = false; PC->SetControlRotation(FRotator(-12, 35, 0));
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

bool AWwCharacter::KeyDown(const FKey& Key) const { return !bMenuOpen && InputPC && InputPC->IsInputKeyDown(Key); }
bool AWwCharacter::KeyPressed(const FKey& Key) const { return !bMenuOpen && InputPC && InputPC->WasInputKeyJustPressed(Key); }

void AWwCharacter::GoTo(double X, double Z)
{
	UWwWorldSubsystem* WorldSystem = GetGameInstance() ? GetGameInstance()->GetSubsystem<UWwWorldSubsystem>() : nullptr;
	const std::shared_ptr<ww::FTerrainSampler> Sampler = WorldSystem ? WorldSystem->GetSampler() : nullptr;
	if (!Sampler) return;
	const double Half = Sampler->Plan->Profile().WorldSize * 0.5 - 50;
	X = FMath::Clamp(X, -Half, Half); Z = FMath::Clamp(Z, -Half, Half);
	const ww::FTerrainSample T = Sampler->Sample(X, Z, 2);
	// (onto the ground or the bed; in deep water the swimmer comes up by itself)
	SpawnPoint = UWwWorldSubsystem::ToUnreal(X, Z, T.Height);
	bFly = false; bSwimming = false; bPlaced = false; bMoved = true;
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->SetMovementMode(MOVE_None);
	GetCharacterMovement()->GravityScale = 0;
	SetActorLocation(SpawnPoint + FVector(0, 0, 300), false, nullptr, ETeleportType::TeleportPhysics);
	UE_LOG(LogWayfarer, Display, TEXT("Menu: going to %.2f km east, %.2f km south (%s)"), X / 1000, Z / 1000, *WorldSystem->BiomeNameAt(X, Z));
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

	// The menu (WwMenu): while it is open the game hears no keys and the view does not turn; its settings and buttons.
	AWwHUD* Hud = Cast<AWwHUD>(PC->GetHUD());
	InputPC = PC;
	bMenuOpen = Hud && (Hud->IsMenuOpen() || Hud->IsMapOpen());
	FWwSettings& Set = FWwSettings::Get();
	Camera->SetFieldOfView((float)Set.Num(TEXT("view.fov")));
	MouseSensitivity = (float)Set.Num(TEXT("view.sens"));
	if (Set.Changed(TEXT("test.fly"), FlySeen) && Set.On(TEXT("test.fly")) != bFly) SetFly(!bFly);
	Set.Show(TEXT("test.fly"), bFly ? 1 : 0);
	if (Set.Changed(TEXT("test.dump"), DumpSeen)) DebugDump();
	const bool bGo = Set.Changed(TEXT("test.go"), GoSeen), bHome = Set.Changed(TEXT("test.home"), HomeSeen);
	if (bGo || bHome)
	{
		if (bMenuOpen && Hud) Hud->ToggleMenu();
		if (bGo) GoTo(Set.Num(TEXT("test.gox")) * 1000.0, Set.Num(TEXT("test.goz")) * 1000.0);
		else GoTo(Home.X / 100.0, Home.Y / 100.0);
		return;
	}
	// Esc: the menu (or out of the map); M: the map. (With the menu or the map open and in focus, they hear the key themselves.)
	if (PC->WasInputKeyJustPressed(EKeys::Escape) && Hud && AutoFlySpeed <= 0) { if (Hud->IsMapOpen()) Hud->ToggleMap(); else Hud->ToggleMenu(); }
	if (PC->WasInputKeyJustPressed(EKeys::M) && Hud && !Hud->IsMenuOpen()) Hud->ToggleMap();

	if (AutoFlySpeed > 0)
	{
		Move->MaxFlySpeed = AutoFlySpeed * 100;
		Move->MaxAcceleration = 1e7f;
		AddMovementInput(FVector(1, 0.35, 0).GetSafeNormal(), 1);
		return;
	}

	UpdateBody(DeltaSeconds, KeyDown(EKeys::LeftShift) && !bCrouching);
	// Look.
	float MouseX = 0, MouseY = 0;
	PC->GetInputMouseDelta(MouseX, MouseY);
	if (bMenuOpen) { MouseX = 0; MouseY = 0; }
	FRotator Control = PC->GetControlRotation();
	Control.Yaw += MouseX * MouseSensitivity * 20;
	Control.Pitch = FMath::Clamp(FRotator::NormalizeAxis(Control.Pitch) + MouseY * MouseSensitivity * 20, -85.0, 85.0);
	PC->SetControlRotation(Control);

	if (KeyPressed(EKeys::F)) SetFly(!bFly);
	if (KeyPressed(EKeys::F9)) DebugDump();
	UpdateGameplay(DeltaSeconds, PC, KeyDown(EKeys::LeftShift) && (KeyDown(EKeys::W) || KeyDown(EKeys::S) || KeyDown(EKeys::A) || KeyDown(EKeys::D)));

	const float Forward = (KeyDown(EKeys::W) ? 1.f : 0.f) - (KeyDown(EKeys::S) ? 1.f : 0.f);
	const float Right = (KeyDown(EKeys::D) ? 1.f : 0.f) - (KeyDown(EKeys::A) ? 1.f : 0.f);
	const bool bShift = KeyDown(EKeys::LeftShift), bCtrl = KeyDown(EKeys::LeftControl);
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
				// Wading or swimming stirs the water: rings spread from the player, stronger the faster they move.
				const bool bInWater = T.Water != ww::EWaterKind::None && Level - Feet > 0.08;
				const float Moving = (float)FMath::Clamp(GetVelocity().Size() / 300.0, 0.0, 1.0);
				WorldSystem->SetRipple(Here.X / 100.0, Here.Y / 100.0, bInWater ? 0.12f + 0.88f * Moving : 0.f);
			}
	if (bSwimming)
	{
		if (Move->MovementMode != MOVE_Flying) Move->SetMovementMode(MOVE_Flying);
		Move->BrakingDecelerationFlying = 0; // (the swim sets the velocity itself each frame; the engine's braking cancelled it)
		const float Speed = bShift ? 3.2f : 1.8f; // m/s
		const FVector Flat = (FRotationMatrix(Yaw).GetUnitAxis(EAxis::X) * Forward + FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y) * Right).GetClampedToMaxSize(1.0);
		const bool bDive = KeyDown(EKeys::C) || KeyDown(EKeys::Q), bUp = KeyDown(EKeys::SpaceBar);
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
		const float Up = (KeyDown(EKeys::E) || KeyDown(EKeys::SpaceBar) ? 1.f : 0.f) - (KeyDown(EKeys::Q) ? 1.f : 0.f);
		AddMovementInput(Control.Vector(), Forward);
		AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y), Right);
		AddMovementInput(FVector::UpVector, Up);
	}
	else
	{
		Move->MaxWalkSpeed = bCrouching || IsAiming() ? 160.f : bShift ? SprintSpeed : WalkSpeed;
		Move->MaxAcceleration = 2048;
		AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::X), Forward);
		AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y), Right);
		if (KeyPressed(EKeys::SpaceBar)) Jump();
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
	for (TActorIterator<AWwWildlife> It(GetWorld()); It; ++It) { Out += TEXT("Wildlife: ") + It->Describe() + TEXT("\n"); break; }
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
	if (Sky) Game->SetWeather(Sky->GetWind().DirX, Sky->GetWind().DirZ, Sky->GetWind().Speed, Sky->GetWeather().Precipitation, Sky->GetClock().Season, Sky->GetLatitude());
	if (!bFly) Game->Tick(Sky ? Sky->GetHoursStep() : 0.0, Sky ? Sky->Hour : 12.0, X, Z, Feet, bSwimming, bSprinting ? 1.0 : 0.0);
	FString Said;
	if (const FString Debugged = UpdateDebug(Game); !Debugged.IsEmpty()) Said = Debugged;
	if (const FString Bowed = UpdateBow(DeltaSeconds, PC, Game); !Bowed.IsEmpty()) Said = Bowed;
	if (KeyPressed(EKeys::I)) bPackOpen = !bPackOpen;
	if (KeyPressed(EKeys::C) && !bSwimming && !bFly) bCrouching = !bCrouching;
	// The wild animals: a dead one beside the player is butchered before anything else is picked up; a blow lands on an
	// animal within reach before it lands on a tree.
	if (!Wildlife) for (TActorIterator<AWwWildlife> It(GetWorld()); It; ++It) { Wildlife = *It; break; }
	if (!bFly && KeyPressed(EKeys::E) && !(Wildlife && Wildlife->Butcher(Here, Game, Said))) Said = Game->Use(X, Z, Feet);
	if (!bFly && !bPackOpen && KeyPressed(EKeys::LeftMouseButton))
	{
		const FVector Look = FRotationMatrix(FRotator(0, PC->GetControlRotation().Yaw, 0)).GetUnitAxis(EAxis::X);
		if (!(Wildlife && Wildlife->Hit(Here, PC->GetControlRotation().Yaw, 280.f, 35.f, Said))) Said = Game->Strike(X, Z, Look.X, Look.Y);
		AttackSeconds = 0.8f; Pose = EPose::None;
		SetActorRotation(FRotator(0, PC->GetControlRotation().Yaw, 0)); // (the blow goes where the player looks)
	}
	if (KeyPressed(EKeys::T)) Said = Game->Eat();
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
		for (int32 i = 0; i < 10; i++) if (KeyPressed(Numbers[i])) Said = Game->Make(i, X, Z);
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
	if (Sky)
	{
		// Footsteps while walking on the ground (faster when sprinting), and a sound for what was just done.
		if (!bFly && !bSwimming && GetCharacterMovement()->IsMovingOnGround() && GetVelocity().SizeSquared2D() > 100.0 * 100.0)
		{
			if ((StepSeconds -= DeltaSeconds) <= 0) { StepSeconds = bSprinting ? 0.29f : 0.46f; Sky->Blip(ww::FSoundscape::Step); }
		}
		else StepSeconds = 0.1f;
		// (only what worked makes a sound: a refusal is just words)
		const bool bWood = Said.StartsWith(TEXT("Chop")) || Said.StartsWith(TEXT("It creaks")) || Said.StartsWith(TEXT("Timber")) || Said.StartsWith(TEXT("The trunk splits"));
		const bool bTook = Said.StartsWith(TEXT("Picked up")) || Said.StartsWith(TEXT("Plant fibre")) || Said.StartsWith(TEXT("You eat"));
		if (bWood) Sky->Blip(ww::FSoundscape::Wood);
		else if (Said.StartsWith(TEXT("Made"))) Sky->Blip(ww::FSoundscape::Craft);
		else if (Said.StartsWith(TEXT("You died"))) Sky->Blip(ww::FSoundscape::Hurt);
		else if (bTook) Sky->Blip(ww::FSoundscape::Other);
	}
	if (!Said.IsEmpty()) { Notice = Said; NoticeSeconds = 5.f; UE_LOG(LogWayfarer, Display, TEXT("Game: %s"), *Said); }
	else if ((NoticeSeconds -= DeltaSeconds) <= 0) Notice.Reset();
}
