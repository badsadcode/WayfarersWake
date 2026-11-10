#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WwWildlife.generated.h"

class ASkeletalMeshActor;
class UAnimSequence;
class USkeletalMesh;
class FWwGameplay;

/// The wild animals round the player: for now deer, in small herds (the numbers are Thornwake's, from
/// Content/Gameplay/gameplay_animals.json: shy, two to four together, sight 34 m, hearing 16 m, walking 1.5 m/s,
/// running 11 m/s, 60 health; four meat, a hide and two bones). The first step of hunting, the game's first priority
/// (docs/new_direction_draft.md).
///
/// A deer grazes, wanders a little, and keeps to its herd. It notices the player by sight (less far if the player
/// crouches or stands still, farther if they run), by sound (running is loud, creeping nearly silent, standing still
/// silent) and by scent: downwind of the player it knows of them from much farther off, so a hunter comes from
/// downwind. Noticing, it lifts its head and watches; if the player stays close or comes on, it bolts, and the herd
/// with it. A blow within reach wounds or kills; what is left can be butchered with E.
///
/// Herds are kept round the player (a few within 350 m, on dry, gentle land) and forgotten far away. Not yet: herds
/// that belong to places and biomes and are the same on every visit, other species, tracks and signs, wounds that slow
/// a runner, a dying animation (the pack has none: a dead deer lies on its side), bows.
/// The animal models are from an asset pack that is not in the repository: without it there is no wildlife.
UCLASS()
class WAYFARERSWAKE_API AWwWildlife : public AActor
{
	GENERATED_BODY()
public:
	AWwWildlife();
	virtual void Tick(float DeltaSeconds) override;

	/// A blow from this place (cm) toward this heading (degrees): the nearest living animal within reach in front is
	/// hit. True if something was hit; what happened in words.
	bool Hit(const FVector& From, float YawDegrees, float ReachCm, float Damage, FString& Said);
	/// E beside a dead animal: its meat, hide and bone into the pack. True if there was one within reach.
	bool Butcher(const FVector& From, FWwGameplay* Game, FString& Said);
	/// A herd of this many, this far ahead of a place along a heading (tests and the debug page).
	void SpawnHerd(const FVector& Near, float YawDegrees, float AheadCm, int32 Count);
	int32 GetAlive() const;
	/// "3 deer near: 2 grazing, 1 watching" (the debug dump).
	FString Describe() const;

private:
	enum class EMood : uint8 { Graze, Wander, Watch, Flee, Dead };
	struct FAnimal
	{
		ASkeletalMeshActor* Actor = nullptr;
		FVector2D At = FVector2D::ZeroVector;   // cm
		float Yaw = 0, Speed = 0, Timer = 0, Health = 60;
		int32 Herd = 0;
		EMood Mood = EMood::Graze;
		EMood Shown = EMood::Dead;
	};
	bool LoadAssets();
	void Place(FAnimal& Animal);
	void SetMood(FAnimal& Animal, EMood Mood, float Seconds);
	bool Walkable(const FVector2D& At) const;

	TArray<FAnimal> Animals;
	UPROPERTY(Transient) TArray<TObjectPtr<ASkeletalMeshActor>> Actors;   // (keeps them from the collector)
	UPROPERTY(Transient) TObjectPtr<USkeletalMesh> DeerMesh;
	UPROPERTY(Transient) TObjectPtr<UAnimSequence> IdleClip;
	UPROPERTY(Transient) TObjectPtr<UAnimSequence> WalkClip;
	UPROPERTY(Transient) TObjectPtr<UAnimSequence> RunClip;
	float MeshYaw = 0, HerdTimer = 2, TestSeconds = 0;
	int32 NextHerd = 1, TestStep = 0;
	bool bTried = false, bLoaded = false;
};
