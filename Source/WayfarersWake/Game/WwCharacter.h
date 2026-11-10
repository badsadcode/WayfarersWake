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
	double GetSecondsPlaced() const { return SecondsPlaced; }
	/// Metres travelled over the ground since placement (streaming tests).
	double GetDistanceTravelled() const { return Travelled; }

	UPROPERTY(EditAnywhere, Category = "Movement") float WalkSpeed = 450;
	UPROPERTY(EditAnywhere, Category = "Movement") float SprintSpeed = 900;
	/// Developer flight, cm/s; Shift multiplies by 8, Ctrl by 40.
	UPROPERTY(EditAnywhere, Category = "Movement") float FlySpeed = 4000;
	UPROPERTY(EditAnywhere, Category = "Movement") float MouseSensitivity = 0.12f;
	/// Automatic straight flight (m/s) for streaming tests: -WwAutoFly=<speed>.
	UPROPERTY(EditAnywhere, Category = "Movement") float AutoFlySpeed = 0;

private:
	void TryPlace();
	void SetFly(bool bEnable);
	void AddPart(const TCHAR* Name, const FVector& Centre, const FVector& Size, const FLinearColor& Colour);

	UPROPERTY(VisibleAnywhere, Category = "Camera") TObjectPtr<USpringArmComponent> Boom;
	UPROPERTY(VisibleAnywhere, Category = "Camera") TObjectPtr<UCameraComponent> Camera;
	UPROPERTY(VisibleAnywhere, Category = "Body") TArray<TObjectPtr<UStaticMeshComponent>> Parts;
	TArray<FLinearColor> PartColours;

	bool bPlaced = false, bHaveSpawn = false, bFly = false;
	double SecondsPlaced = 0, Travelled = 0;
	FVector SpawnPoint = FVector::ZeroVector, LastPlace = FVector::ZeroVector;
};
