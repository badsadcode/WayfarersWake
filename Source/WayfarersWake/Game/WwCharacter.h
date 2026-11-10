#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "WwCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UStaticMeshComponent;

/// The player: Alden Wayfarer as a figure of blocks (after the concept art), third-person camera, walking, sprinting,
/// jumping and a developer flight mode for crossing the world quickly. Waits, frozen, until the world is generated and
/// the ground under the starting place has arrived, then stands on it.
UCLASS()
class WAYFARERSWAKE_API AWwCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AWwCharacter();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	bool IsPlaced() const { return bPlaced; }
	bool IsFlying() const { return bFly; }
	bool IsSwimming() const { return bSwimming; }
	/// Crouched (C): seen and heard from less far.
	bool IsSneaking() const { return bCrouching; }
	/// The placeholder mechanics on screen: the last thing that happened (for a few seconds) and whether the pack is open.
	const FString& GetNotice() const { return Notice; }
	bool IsPackOpen() const { return bPackOpen; }
	double GetSecondsPlaced() const { return SecondsPlaced; }
	/// Metres travelled over the ground since placement (streaming tests).
	double GetDistanceTravelled() const { return Travelled; }
	/// A line for the HUD (the last debug dump), empty once it has been shown long enough.
	FString GetStatus() const { return GetWorld() && GetWorld()->GetTimeSeconds() < StatusUntil ? Status : FString(); }
	/// F9: writes a text dump of where the player is and what the game is doing, with a screenshot beside it, to
	/// Saved/Dumps (as Thornwake's F9, which writes to its own folder; the two never mix).
	void DebugDump();
	/// To a place in the world (metres): waits for the ground there, as at the start.
	void GoTo(double X, double Z);
	/// Where the world first put the player (Unreal units).
	const FVector& GetHome() const { return Home; }

	UPROPERTY(EditAnywhere, Category = "Movement") float WalkSpeed = 500;   // (Thornwake: 5 m/s)
	UPROPERTY(EditAnywhere, Category = "Movement") float SprintSpeed = 1100; // (11 m/s)
	/// Developer flight, cm/s (Thornwake: 45 m/s; Shift 220 m/s; Ctrl six times either).
	UPROPERTY(EditAnywhere, Category = "Movement") float FlySpeed = 4500;
	UPROPERTY(EditAnywhere, Category = "Movement") float MouseSensitivity = 0.12f;
	/// Automatic straight flight (m/s) for streaming tests: -WwAutoFly=<speed>.
	UPROPERTY(EditAnywhere, Category = "Movement") float AutoFlySpeed = 0;

private:
	void TryPlace();
	/// The body (one of Tom's base bodies, rigged: Content/Characters) and which of its animations plays.
	void LoadBody();
	/// The menu's Debug page: items, needs, creatures.
	FString UpdateDebug(class FWwGameplay* Game);
	void SpawnCreature(int32 Which, float Size);
	UPROPERTY(Transient) TArray<TObjectPtr<AActor>> Spawned;
	int32 HerdSeen = -1;
	UPROPERTY(Transient) TObjectPtr<class AWwWildlife> Wildlife;
	int32 GiveSeen = -1, GiveAllSeen = -1, HealSeen = -1, SpawnSeen = -1, ClearSeen = -1;
	void UpdateBody(float DeltaSeconds, bool bSprinting);
	enum class EPose : uint8 { None, Idle, Walk, Run, Swim, Attack, Crouch, Climb };
	UPROPERTY(Transient) TArray<TObjectPtr<class UAnimSequence>> Clips;
	EPose Pose = EPose::None;
	int32 BodySeen = -1;
	float AttackSeconds = 0, BodyHeight = 180;
	bool bCrouching = false, bHasBody = false;
	/// Keys as the game hears them: nothing while the menu is open.
	bool KeyDown(const FKey& Key) const;
	bool KeyPressed(const FKey& Key) const;
	class APlayerController* InputPC = nullptr;
	bool bMenuOpen = false, bMoved = false;
	int32 FlySeen = -1, DumpSeen = -1, GoSeen = -1, HomeSeen = -1;
	FVector Home = FVector::ZeroVector;
	void SetFly(bool bEnable);
	void AddPart(const TCHAR* Name, const FVector& Centre, const FVector& Size, const FLinearColor& Colour);

	UPROPERTY(VisibleAnywhere, Category = "Camera") TObjectPtr<USpringArmComponent> Boom;
	UPROPERTY(VisibleAnywhere, Category = "Camera") TObjectPtr<UCameraComponent> Camera;
	UPROPERTY(VisibleAnywhere, Category = "Body") TArray<TObjectPtr<UStaticMeshComponent>> Parts;
	TArray<FLinearColor> PartColours;

	bool bPlaced = false, bHaveSpawn = false, bFly = false, bSwimming = false, bPackOpen = false;
	void UpdateGameplay(float DeltaSeconds, class APlayerController* PC, bool bSprinting);
	FString Notice;
	float NoticeSeconds = 0, StepSeconds = 0;
	FString Status;
	double StatusUntil = 0;
	double SecondsPlaced = 0, Travelled = 0;
	FVector SpawnPoint = FVector::ZeroVector, LastPlace = FVector::ZeroVector;
};
