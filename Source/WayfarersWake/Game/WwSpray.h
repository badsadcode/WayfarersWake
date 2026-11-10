#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"

/// Billowing spray and mist (after Thornwake Game/Rewrite/Spray.cs): an emitter of soft puffs born in a box, sent off
/// along a direction, slowed by the air, lifted or pulled down, growing and fading over their life. Thornwake runs these
/// as GPU particles; here they are a few hundred points a fall, moved on the game thread and drawn as camera-facing
/// instances (the project is code-driven: no particle assets).
struct FWwSprayEmitter
{
	// Where: the emitter's place (world, cm) and frame (across, downstream; up is the world's).
	FVector Origin = FVector::ZeroVector, Across = FVector::RightVector, Along = FVector::ForwardVector;
	// What (metres, seconds, as Thornwake's Spray.Emitter): box half-extents (across, up, along), direction (across, up, along).
	FVector Box = FVector(1, 1, 1), Direction = FVector(0, 1, 0);
	float SpreadDegrees = 30, SpeedMin = 1, SpeedMax = 2, SizeMin = 1, SizeMax = 2, Grow = 2, Lifetime = 3, Rise = 0, Peak = 0.5f;
	int32 Amount = 10;

	struct FPuff { FVector Position, Velocity; float Age, Size, Angle, Spin, Damping, Seed; };
	TArray<FPuff> Puffs;
	FRandomStream Random;
	float Clock = 0;
	bool bStarted = false;

	/// Advances the puffs (born evenly over the lifetime, as Thornwake's with no explosiveness; started full).
	void Tick(float DeltaSeconds);
	/// Size now (metres) and density now (0..Peak) of a puff.
	float SizeOf(const FPuff& P) const { return P.Size * FMath::Lerp(1.f / Grow, 1.f, FMath::Clamp(P.Age / Lifetime, 0.f, 1.f)); }
	float DensityOf(const FPuff& P) const;
private:
	void Spawn(FPuff& P, float Age);
};

/// The emitters of one waterfall (Thornwake's WaterfallView.Add, billowing spray): a cloud rolling up at the foot and
/// drifting downstream; on a fall over 8 m also fine spray off its face, veils down its sides and spray thrown off the lip.
struct FWwFallSpray
{
	TArray<FWwSprayEmitter> Emitters;
	/// Lip (world metres, x east, z south), the fall's direction and size, and the model's foot and face points
	/// (metres from the lip; y absolute), lip level and the water's reach to each side.
	void Build(double LipX, double LipZ, double Dx, double Dz, float Height, float Width, const FVector& Foot, const FVector& Face, float LipLevel, float Left, float Right, int32 Seed);
};
