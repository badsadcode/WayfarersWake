#include "Game/WwSpray.h"

void FWwSprayEmitter::Spawn(FPuff& P, float Age)
{
	// Born in the box; sent off within a cone about the direction.
	const FVector Local(Random.FRandRange(-Box.X, Box.X), Random.FRandRange(-Box.Y, Box.Y), Random.FRandRange(-Box.Z, Box.Z));
	P.Position = Origin + (Across * Local.X + FVector::UpVector * Local.Y + Along * Local.Z) * 100.0;
	const FVector Dir = (Across * Direction.X + FVector::UpVector * Direction.Y + Along * Direction.Z).GetSafeNormal();
	const FVector Sent = Random.VRandCone(Dir, FMath::DegreesToRadians(SpreadDegrees));
	P.Velocity = Sent * Random.FRandRange(SpeedMin, SpeedMax) * 100.0;
	P.Size = Random.FRandRange(SizeMin, SizeMax);
	P.Angle = Random.FRandRange(0.f, 360.f);
	P.Spin = Random.FRandRange(-12.f, 12.f);
	P.Damping = Random.FRandRange(0.8f, 1.6f);
	P.Seed = Random.FRand();
	P.Age = 0;
	// (a puff that should already be part-way through its life: run it forward)
	for (float Done = 0; Done < Age; Done += 0.1f)
	{
		const float Step = FMath::Min(0.1f, Age - Done);
		const float Speed = P.Velocity.Size();
		if (Speed > 1.f) P.Velocity *= FMath::Max(0.f, Speed - P.Damping * 100.f * Step) / Speed;
		P.Velocity.Z += Rise * 100.f * Step;
		P.Position += P.Velocity * Step;
		P.Angle += P.Spin * Step;
		P.Age += Step;
	}
}

void FWwSprayEmitter::Tick(float DeltaSeconds)
{
	if (!bStarted)
	{
		bStarted = true;
		Puffs.SetNum(FMath::Max(1, Amount));
		for (int32 i = 0; i < Puffs.Num(); i++) Spawn(Puffs[i], Lifetime * (i + Random.FRand() * 0.5f) / Puffs.Num());
		return;
	}
	const float Step = FMath::Min(DeltaSeconds, 0.1f);
	for (FPuff& P : Puffs)
	{
		P.Age += Step;
		if (P.Age >= Lifetime) { Spawn(P, P.Age - Lifetime); continue; }
		const float Speed = P.Velocity.Size();
		if (Speed > 1.f) P.Velocity *= FMath::Max(0.f, Speed - P.Damping * 100.f * Step) / Speed; // slowed by the air
		P.Velocity.Z += Rise * 100.f * Step;
		P.Position += P.Velocity * Step;
		P.Angle += P.Spin * Step;
	}
}

float FWwSprayEmitter::DensityOf(const FPuff& P) const
{
	// Rising quickly to the peak, fading out over the life (Thornwake's colour ramp: 0, peak at 0.12, 0.6 peak at 0.55, 0).
	const float T = FMath::Clamp(P.Age / Lifetime, 0.f, 1.f);
	if (T < 0.12f) return Peak * T / 0.12f;
	if (T < 0.55f) return Peak * FMath::Lerp(1.f, 0.6f, (T - 0.12f) / 0.43f);
	return Peak * 0.6f * (1.f - (T - 0.55f) / 0.45f);
}

void FWwFallSpray::Build(double LipX, double LipZ, double Dx, double Dz, float H, float W, const FVector& Foot, const FVector& Face, float LipLevel, float Left, float Right, int32 Seed)
{
	// Frame as Thornwake's: x across (to the fall's right), y up, z downstream. Metres from the lip to Unreal's world.
	const FVector Across(Dz, -Dx, 0), Along(Dx, Dz, 0);
	auto World = [&](const FVector& FromLip) { return FVector((LipX + FromLip.X) * 100.0, (LipZ + FromLip.Z) * 100.0, FromLip.Y * 100.0); };
	auto Add = [&](const FVector& At, const FVector& Box, const FVector& Direction, float Spread, float SpeedMin, float SpeedMax, float SizeMin, float SizeMax,
		float Grow, float Lifetime, int32 Amount, float Rise, float Peak)
	{
		FWwSprayEmitter& E = Emitters.AddDefaulted_GetRef();
		E.Origin = At; E.Across = Across; E.Along = Along;
		E.Box = Box; E.Direction = Direction; E.SpreadDegrees = Spread; E.SpeedMin = SpeedMin; E.SpeedMax = SpeedMax; E.SizeMin = SizeMin; E.SizeMax = SizeMax;
		E.Grow = Grow; E.Lifetime = Lifetime; E.Amount = Amount; E.Rise = Rise; E.Peak = Peak;
		E.Random.Initialize(Seed * 31 + Emitters.Num());
	};
	// Billowing spray: rolling up at the foot and drifting downstream.
	Add(World(Foot + FVector(0, 0.8, 0)), FVector(FMath::Max(1.5f, W * .45f), .5f, 1.5f + H * .02f), FVector(0, 1, .7f), 40,
		1.2f + H * .03f, 2.5f + H * .07f, 2.2f + W * .04f + H * .05f, 3.5f + W * .08f + H * .12f, 2.4f, FMath::Clamp(3.5f + H * .03f, 3.5f, 7.f),
		FMath::Clamp((int32)(10 + W * 1.6f + H * 1.2f), 12, 180), .25f, .5f);
	if (H <= 8) return;
	// Finer spray off the face of a high fall.
	Add(World(Face), FVector(FMath::Max(1.f, W * .45f), H * .28f, .6f), FVector(0, -.2f, 1), 25, .6f, 1.8f,
		.9f + W * .02f, 1.8f + W * .04f + H * .02f, 2, 2.6f, FMath::Clamp((int32)(H * .8f + W * .6f), 8, 90), -.4f, .35f);
	// Spray veiling the falling water's sides all the way down, and fine spray thrown off where it bends over the lip.
	const float Half = (Left + Right) * .5f - .5f, Thick = FMath::Clamp(W * .1f, .6f, 3.f);
	for (float Side : { -1.f, 1.f })
	{
		const FVector At = World(Face) + (Across * Side * (Half + .3f) - Along * (Thick * .5f)) * 100.0;
		Add(At, FVector(.9f, H * .36f, .6f + Thick * .5f), FVector(Side * .8f, -.3f, .4f), 30, .4f, 1.3f,
			.8f + H * .01f, 1.6f + H * .025f, 1.8f, 2.6f, FMath::Clamp((int32)(H * .7f), 10, 70), -.5f, .32f);
	}
	const FVector Lip = World(FVector(0, LipLevel - .3f, 0)) + (Across * ((Right - Left) * .5f) + Along * 1.2f) * 100.0;
	Add(Lip, FVector(FMath::Max(1.f, Half * .9f), .25f, .7f), FVector(0, .5f, 1), 25, .5f, 1.4f,
		.5f + W * .01f, 1.1f + W * .015f, 1.6f, 1.8f, FMath::Clamp((int32)(W * .8f), 8, 60), -1.2f, .3f);
}
