#include "Game/WwWildlife.h"
#include "Game/WwCharacter.h"
#include "Game/WwGame.h"
#include "Game/WwGameplay.h"
#include "Game/WwSettings.h"
#include "Game/WwWorldSubsystem.h"
#include "WayfarersWake.h"
#include "Animation/AnimSequence.h"
#include "Animation/SkeletalMeshActor.h"
#include "AnimationRuntime.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"

namespace
{
// Thornwake's deer (Content/Gameplay/gameplay_animals.json), in cm and cm/s.
const float WlSight = 3400, WlHearing = 1600, WlWalk = 150, WlRun = 1100, WlHealth = 60;
// (asked when first needed, not when the program starts: a packaged game has no command line yet at that moment, and
// reading it then ends the program before it can even open its log)
bool WlTestOn() { static const bool bOn = FParse::Param(FCommandLine::Get(), TEXT("WwDeerTest")); return bOn; }
}

AWwWildlife::AWwWildlife()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

bool AWwWildlife::LoadAssets()
{
	if (bTried) return bLoaded;
	bTried = true;
	DeerMesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Animals_Free/Animals/Meshes/SKM_Deer_001.SKM_Deer_001"));
	IdleClip = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Animals_Free/Animals/Animations/ANIM_Deer_001_Anim_Deer_001_idle.ANIM_Deer_001_Anim_Deer_001_idle"));
	WalkClip = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Animals_Free/Animals/Animations/ANIM_Deer_001_Anim_Deer_001_walk.ANIM_Deer_001_Anim_Deer_001_walk"));
	RunClip = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Animals_Free/Animals/Animations/ANIM_Deer_001_Anim_Deer_001_run.ANIM_Deer_001_Anim_Deer_001_run"));
	bLoaded = DeerMesh && IdleClip && WalkClip && RunClip;
	if (!bLoaded) { UE_LOG(LogWayfarer, Warning, TEXT("Wildlife: the deer is not in the project (the Animals_Free asset pack is missing): no wild animals")); return false; }
	// Which way the model faces in its own space: toward its head. The head is the bone named so, or failing that the
	// highest bone of the standing animal (head and antlers).
	const FReferenceSkeleton& Bones = DeerMesh->GetRefSkeleton();
	int32 HeadBone = INDEX_NONE;
	double Highest = -1e9;
	for (int32 i = 0; i < Bones.GetNum(); i++)
	{
		if (Bones.GetBoneName(i).ToString().Contains(TEXT("head"), ESearchCase::IgnoreCase)) { HeadBone = i; break; }
		const double Z = FAnimationRuntime::GetComponentSpaceTransformRefPose(Bones, i).GetLocation().Z;
		if (Z > Highest) { Highest = Z; HeadBone = i; }
	}
	if (HeadBone != INDEX_NONE)
	{
		const FVector Head = FAnimationRuntime::GetComponentSpaceTransformRefPose(Bones, HeadBone).GetLocation();
		// (models face along an axis: an ear a little to one side must not turn the whole animal ten degrees)
		if (Head.Size2D() > 5.0) MeshYaw = FMath::RoundToFloat(FMath::RadiansToDegrees(FMath::Atan2(Head.Y, Head.X)) / 90.f) * 90.f;
		UE_LOG(LogWayfarer, Display, TEXT("Wildlife: the deer faces %.0f degrees in its own space (its bone %s is at %s; %d bones)"), MeshYaw, *Bones.GetBoneName(HeadBone).ToString(), *Head.ToCompactString(), Bones.GetNum());
	}
	return true;
}

bool AWwWildlife::Walkable(const FVector2D& At) const
{
	const UWwWorldSubsystem* WorldSystem = GetGameInstance() ? GetGameInstance()->GetSubsystem<UWwWorldSubsystem>() : nullptr;
	const std::shared_ptr<ww::FTerrainSampler> Sampler = WorldSystem ? WorldSystem->GetSampler() : nullptr;
	if (!Sampler) return false;
	const double X = At.X / 100.0, Z = At.Y / 100.0;
	const ww::FTerrainSample T = Sampler->Sample(X, Z, 4);
	if (T.Water != ww::EWaterKind::None || T.Land <= 0) return false;
	// (dry and not steep: no more than about 30 degrees)
	return FMath::Abs(Sampler->Height(X + 2, Z, 4) - T.Height) < 1.2 && FMath::Abs(Sampler->Height(X, Z + 2, 4) - T.Height) < 1.2;
}

void AWwWildlife::SetMood(FAnimal& Animal, EMood Mood, float Seconds)
{
	if (WlTestOn() && Animal.Mood != Mood)
	{
		static const TCHAR* Names[] = { TEXT("grazes"), TEXT("wanders"), TEXT("watches"), TEXT("bolts"), TEXT("is dead") };
		UE_LOG(LogWayfarer, Display, TEXT("Wildlife: a deer of herd %d %s"), Animal.Herd, Names[(int32)Mood]);
	}
	Animal.Mood = Mood;
	Animal.Timer = Seconds;
	Animal.Speed = Mood == EMood::Wander ? WlWalk : Mood == EMood::Flee ? WlRun : 0.f;
}

void AWwWildlife::Place(FAnimal& Animal)
{
	if (!Animal.Actor) return;
	const UWwWorldSubsystem* WorldSystem = GetGameInstance()->GetSubsystem<UWwWorldSubsystem>();
	const std::shared_ptr<ww::FTerrainSampler> Sampler = WorldSystem->GetSampler();
	const double Ground = Sampler ? Sampler->Height(Animal.At.X / 100.0, Animal.At.Y / 100.0, 2) * 100.0 : 0.0;
	// (a dead deer lies on its side)
	Animal.Actor->SetActorLocationAndRotation(FVector(Animal.At.X, Animal.At.Y, Ground + (Animal.Mood == EMood::Dead ? 25.0 : 0.0)),
		FRotator(0, Animal.Yaw - MeshYaw, Animal.Mood == EMood::Dead ? 88.f : 0.f));
	if (Animal.Shown == Animal.Mood) return;
	Animal.Shown = Animal.Mood;
	USkeletalMeshComponent* Shown = Animal.Actor->GetSkeletalMeshComponent();
	if (Animal.Mood == EMood::Dead) { Shown->Stop(); return; }
	Shown->PlayAnimation(Animal.Mood == EMood::Wander ? WalkClip : Animal.Mood == EMood::Flee ? RunClip : IdleClip, true);
	// (each a little out of step with the others)
	Shown->SetPosition(FMath::FRand() * 0.8f);
	Shown->SetPlayRate(Animal.Mood == EMood::Flee ? 1.15f : 1.f);
}

void AWwWildlife::SpawnHerd(const FVector& Near, float YawDegrees, float AheadCm, int32 Count)
{
	if (!LoadAssets()) return;
	const FVector2D Centre = FVector2D(Near.X, Near.Y) + FVector2D(FMath::Cos(FMath::DegreesToRadians(YawDegrees)), FMath::Sin(FMath::DegreesToRadians(YawDegrees))) * AheadCm;
	const int32 Herd = NextHerd++;
	int32 Made = 0;
	for (int32 i = 0; i < Count * 4 && Made < Count; i++)
	{
		const FVector2D At = Centre + FVector2D(FMath::FRandRange(-700.f, 700.f), FMath::FRandRange(-700.f, 700.f));
		if (!Walkable(At)) continue;
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ASkeletalMeshActor* Actor = GetWorld()->SpawnActor<ASkeletalMeshActor>(FVector(At.X, At.Y, 0), FRotator::ZeroRotator, Params);
		if (!Actor) continue;
		USkeletalMeshComponent* Shown = Actor->GetSkeletalMeshComponent();
		Shown->SetMobility(EComponentMobility::Movable);
		Shown->SetSkeletalMesh(DeerMesh);
		Shown->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		Shown->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		// (does and young ones: a little smaller, each its own size)
		Actor->SetActorScale3D(FVector(FMath::FRandRange(0.82f, 1.0f)));
		FAnimal Animal;
		Animal.Actor = Actor; Animal.At = At; Animal.Yaw = FMath::FRandRange(0.f, 360.f); Animal.Herd = Herd; Animal.Health = WlHealth;
		SetMood(Animal, EMood::Graze, FMath::FRandRange(4.f, 14.f));
		Place(Animal);
		Animals.Add(Animal);
		Actors.Add(Actor);
		Made++;
	}
	if (WlTestOn()) UE_LOG(LogWayfarer, Display, TEXT("Wildlife: herd %d of %d deer at %.0f, %.0f m"), Herd, Made, Centre.X / 100, Centre.Y / 100);
}

int32 AWwWildlife::GetAlive() const
{
	int32 Count = 0;
	for (const FAnimal& Animal : Animals) Count += Animal.Mood != EMood::Dead;
	return Count;
}

bool AWwWildlife::NearestAlive(const FVector& From, FVector& Chest) const
{
	double Best = 1e18;
	for (const FAnimal& Animal : Animals)
	{
		if (Animal.Mood == EMood::Dead || !Animal.Actor) continue;
		const double D = FVector::DistSquared(Animal.Actor->GetActorLocation(), From);
		if (D < Best) { Best = D; Chest = Animal.Actor->GetActorLocation() + FVector(0, 0, 90); }
	}
	return Best < 1e17;
}

FString AWwWildlife::Describe() const
{
	int32 Moods[5] = { 0, 0, 0, 0, 0 };
	for (const FAnimal& Animal : Animals) Moods[(int32)Animal.Mood]++;
	return FString::Printf(TEXT("%d deer near: %d grazing, %d wandering, %d watching, %d running, %d dead"), Animals.Num(), Moods[0], Moods[1], Moods[2], Moods[3], Moods[4]);
}

void AWwWildlife::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	AWwCharacter* Player = Cast<AWwCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
	if (!Player || !Player->IsPlaced() || !FWwSettings::Get().On(TEXT("debug.wildlife")) || !LoadAssets()) return;
	const FVector2D Me(Player->GetActorLocation().X, Player->GetActorLocation().Y);
	const float Speed = (float)Player->GetVelocity().Size2D();
	const bool bStill = Speed < 20.f, bRunning = Speed > 700.f, bSneaking = Player->IsSneaking(), bFlying = Player->IsFlying() && !WlTestOn(); // (the scripted check approaches by automatic flight)
	AWwSky* Sky = nullptr;
	for (TActorIterator<AWwSky> It(GetWorld()); It; ++It) { Sky = *It; break; }
	const bool bNight = Sky && (Sky->Hour < 5.5f || Sky->Hour > 20.5f);
	// How far the player is seen and heard. (Flying is the developer's way of travelling: the animals take no notice.)
	const float Sight = bFlying ? 0.f : WlSight * (bSneaking ? 0.5f : 1.f) * (bStill ? 0.75f : bRunning ? 1.5f : 1.f) * (bNight ? 0.7f : 1.f);
	const float Hearing = bFlying ? 0.f : WlHearing * (bStill ? 0.f : bSneaking ? 0.35f : bRunning ? 2.2f : 1.f);
	const FVector2D WindTo = Sky ? FVector2D(Sky->GetWind().DirX, Sky->GetWind().DirZ) : FVector2D::ZeroVector;
	const float WindSpeed = Sky ? Sky->GetWind().SteadySpeed : 0.f;

	// A scripted check (-WwDeerTest): a herd ahead of the player's automatic flight, then a blow and the butchering.
	if (WlTestOn())
	{
		TestSeconds += DeltaSeconds;
		if (TestStep == 0 && TestSeconds > 2) { TestStep = 1; SpawnHerd(Player->GetActorLocation(), 19.3f, 7000.f, 3); }
		if (TestStep == 1 && TestSeconds > 16 && Animals.Num() > 0)
		{
			TestStep = 2;
			const FVector At(Animals[0].At.X, Animals[0].At.Y, 0);
			FString Said;
			UWwWorldSubsystem* WorldSystem = GetGameInstance()->GetSubsystem<UWwWorldSubsystem>();
			UE_LOG(LogWayfarer, Display, TEXT("Wildlife test: %s"), *Describe());
			Hit(At - FVector(100, 0, 0), 0.f, 300.f, 35.f, Said); UE_LOG(LogWayfarer, Display, TEXT("Wildlife test: %s"), *Said);
			Hit(FVector(Animals[0].At.X - 100, Animals[0].At.Y, 0), 0.f, 300.f, 35.f, Said); UE_LOG(LogWayfarer, Display, TEXT("Wildlife test: %s"), *Said);
			Butcher(FVector(Animals[0].At.X, Animals[0].At.Y, 0), WorldSystem->GetGameplay(), Said); UE_LOG(LogWayfarer, Display, TEXT("Wildlife test: %s"), *Said);
		}
	}

	for (FAnimal& Animal : Animals)
	{
		if (Animal.Mood == EMood::Dead) continue;
		const FVector2D Away = Animal.At - Me;
		const float Distance = (float)Away.Size();
		const FVector2D AwayDir = Distance > 1.f ? Away / Distance : FVector2D(1, 0);
		// Scent: downwind of the player (the wind blows from them to the deer) they are known from much farther off.
		const float Downwind = (float)FVector2D::DotProduct(WindTo, AwayDir);
		const float Scent = bFlying ? 0.f : 1.f + 0.9f * FMath::Clamp((Downwind - 0.3f) / 0.5f, 0.f, 1.f) * FMath::Clamp(WindSpeed / 6.f, 0.f, 1.f);
		const float Notice = FMath::Max(Sight, Hearing) * Scent;
		const float ToMe = FMath::RadiansToDegrees(FMath::Atan2(-AwayDir.Y, -AwayDir.X));
		auto Bolt = [&](FAnimal& Which)
		{
			// The whole herd goes, each on its own line away.
			for (FAnimal& Other : Animals)
				if (Other.Mood != EMood::Dead && Other.Mood != EMood::Flee && Other.Herd == Which.Herd)
				{
					SetMood(Other, EMood::Flee, FMath::FRandRange(5.f, 8.f));
					const FVector2D From = Other.At - Me;
					Other.Yaw = FMath::RadiansToDegrees(FMath::Atan2(From.Y, From.X)) + FMath::FRandRange(-25.f, 25.f);
				}
		};
		Animal.Timer -= DeltaSeconds;
		switch (Animal.Mood)
		{
		case EMood::Graze:
		case EMood::Wander:
			if (Distance < Notice * 0.45f) { Bolt(Animal); break; }
			if (Distance < Notice) { SetMood(Animal, EMood::Watch, FMath::FRandRange(1.5f, 3.f)); Animal.Yaw = ToMe; break; }
			if (Animal.Timer <= 0)
			{
				if (Animal.Mood == EMood::Graze) { SetMood(Animal, EMood::Wander, FMath::FRandRange(3.f, 7.f)); Animal.Yaw += FMath::FRandRange(-120.f, 120.f); }
				else SetMood(Animal, EMood::Graze, FMath::FRandRange(6.f, 18.f));
			}
			break;
		case EMood::Watch:
			Animal.Yaw = ToMe;
			if (Distance < Notice * 0.6f) Bolt(Animal);
			else if (Animal.Timer <= 0) { if (Distance < Notice * 0.95f) Bolt(Animal); else SetMood(Animal, EMood::Graze, FMath::FRandRange(4.f, 10.f)); }
			break;
		case EMood::Flee:
		{
			// Away from the player, turning as fast as a deer can; far enough and long enough, it settles.
			const float Wanted = FMath::RadiansToDegrees(FMath::Atan2(AwayDir.Y, AwayDir.X));
			Animal.Yaw = FMath::FixedTurn(Animal.Yaw, Wanted, 200.f * DeltaSeconds);
			if (Animal.Timer <= 0 && Distance > 11000.f) SetMood(Animal, EMood::Graze, FMath::FRandRange(6.f, 12.f));
			break;
		}
		default: break;
		}
		if (Animal.Speed > 0)
		{
			// Round water and steep ground: the nearest heading that is open.
			static const float Tries[] = { 0, 30, -30, 60, -60, 100, -100, 150, -150 };
			for (float Turn : Tries)
			{
				const float Heading = FMath::DegreesToRadians(Animal.Yaw + Turn);
				const FVector2D Dir(FMath::Cos(Heading), FMath::Sin(Heading));
				if (!Walkable(Animal.At + Dir * 400.f)) continue;
				Animal.Yaw += Turn * FMath::Min(1.f, 6.f * DeltaSeconds);
				Animal.At += Dir * Animal.Speed * DeltaSeconds;
				break;
			}
		}
		Place(Animal);
	}

	// Herds come and go with the player: a few within 350 m, none kept beyond 450 m.
	if ((HerdTimer -= DeltaSeconds) > 0) return;
	HerdTimer = 3.f;
	TSet<int32> Near;
	for (int32 i = Animals.Num() - 1; i >= 0; i--)
	{
		const float Distance = (float)FVector2D::Distance(Animals[i].At, Me);
		if (Distance > 45000.f || (Animals[i].Mood == EMood::Dead && Distance > 15000.f))
		{
			if (Animals[i].Actor) { Actors.Remove(Animals[i].Actor); Animals[i].Actor->Destroy(); }
			Animals.RemoveAtSwap(i);
		}
		else if (Distance < 35000.f && Animals[i].Mood != EMood::Dead) Near.Add(Animals[i].Herd);
	}
	if (Near.Num() < 3 && !WlTestOn())
	{
		const float Angle = FMath::FRandRange(0.f, 360.f), Out = FMath::FRandRange(16000.f, 30000.f);
		SpawnHerd(Player->GetActorLocation(), Angle, Out, FMath::RandRange(2, 4));
	}
}

bool AWwWildlife::Hit(const FVector& From, float YawDegrees, float ReachCm, float Damage, FString& Said)
{
	const FVector2D Me(From.X, From.Y), Facing(FMath::Cos(FMath::DegreesToRadians(YawDegrees)), FMath::Sin(FMath::DegreesToRadians(YawDegrees)));
	FAnimal* Best = nullptr;
	float BestDistance = ReachCm;
	for (FAnimal& Animal : Animals)
	{
		if (Animal.Mood == EMood::Dead) continue;
		const FVector2D To = Animal.At - Me;
		const float Distance = (float)To.Size();
		if (Distance < BestDistance && (Distance < 60.f || FVector2D::DotProduct(To / Distance, Facing) > 0.5)) { Best = &Animal; BestDistance = Distance; }
	}
	if (!Best) return false;
	Wound(*Best, Damage, Me, Said);
	return true;
}

void AWwWildlife::Wound(FAnimal& Animal, float Damage, const FVector2D& From, FString& Said)
{
	Animal.Health -= Damage;
	if (Animal.Health <= 0)
	{
		SetMood(Animal, EMood::Dead, 0);
		Place(Animal);
		Said = TEXT("The deer falls. (E beside it to butcher it.)");
	}
	else Said = TEXT("You wound the deer.");
	// The rest of the herd, and the wounded one, are gone.
	for (FAnimal& Other : Animals)
		if (Other.Mood != EMood::Dead && Other.Herd == Animal.Herd)
		{
			SetMood(Other, EMood::Flee, FMath::FRandRange(6.f, 9.f));
			const FVector2D Away = Other.At - From;
			Other.Yaw = FMath::RadiansToDegrees(FMath::Atan2(Away.Y, Away.X)) + FMath::FRandRange(-25.f, 25.f);
		}
}

bool AWwWildlife::Shot(const FVector& A, const FVector& B, float Damage, FString& Said)
{
	for (FAnimal& Animal : Animals)
	{
		if (Animal.Mood == EMood::Dead || !Animal.Actor) continue;
		// (the body: a ball about the chest, 90 cm above the feet)
		const FVector Chest = Animal.Actor->GetActorLocation() + FVector(0, 0, 90);
		if (FMath::PointDistToSegment(Chest, A, B) > 60.f) continue;
		Wound(Animal, Damage, FVector2D(A.X, A.Y), Said);
		return true;
	}
	return false;
}

bool AWwWildlife::Butcher(const FVector& From, FWwGameplay* Game, FString& Said)
{
	if (!Game) return false;
	for (int32 i = 0; i < Animals.Num(); i++)
	{
		if (Animals[i].Mood != EMood::Dead || FVector2D::Distance(Animals[i].At, FVector2D(From.X, From.Y)) > 280.0) continue;
		// (Thornwake's yield for a deer)
		Game->Give(TEXT("raw_meat"), 4); Game->Give(TEXT("hide"), 1); Game->Give(TEXT("bone"), 2);
		if (Animals[i].Actor) { Actors.Remove(Animals[i].Actor); Animals[i].Actor->Destroy(); }
		Animals.RemoveAtSwap(i);
		Said = TEXT("You butcher the deer: 4 raw meat, 1 hide, 2 bone.");
		return true;
	}
	return false;
}
