#pragma once

#include "CoreMinimal.h"
#include "Rules/WrRules.h"
#include "WorldGen/WwWorld.h"
#include <memory>

/// The placeholder mechanics in the running game (Thornwake's step 1, as light as there: Tom will redesign crafting and
/// building). The rules (Rules/WrRules) hold the true state; this object is where the player's machine asks them and
/// reads what came back. Alone, it asks directly. With several players the host would own the rules and these calls
/// would travel as messages; nothing here assumes one player beyond the player id.
/// In: needs (food, warmth, wetness, health), the pack and recipes, E to pick up what lies in a pile or pull long grass,
/// the axe on trees and fallen trunks, eating, dying.
/// Not in: loose stones and berry bushes to pick (the ground cover does not mark them yet), building (so no campfire
/// and no cooking), fishing, animals; pulled grass still shows; a felled tree vanishes instead of falling.
class FWwGameplay : public wr::IWorldQuery
{
public:
	static constexpr int Player = 1;

	FWwGameplay(std::shared_ptr<ww::FTerrainSampler> InSampler, const FString& ContentFolder);
	bool IsValid() const { return Rules != nullptr; }
	const FString& GetError() const { return Error; }

	/// Every frame: game time passes, needs follow what the body feels, fires burn, berries and grass grow back.
	/// X, Z, FeetY in metres; HourOfDay 0..24; GameHours: game hours passed this frame.
	void Tick(double GameHours, double HourOfDay, double X, double Z, double FeetY, bool bSwimming, double Effort);
	/// E: pick up what lies in a pile within reach, else pull long grass for fibre.
	FString Use(double X, double Z, double FeetY);
	/// The axe: a hit on the fallen trunk or the tree in front (DirX, DirZ: where the player looks, level).
	FString Strike(double X, double Z, double DirX, double DirZ);
	/// Make the recipe at this place in the list (0 first).
	FString Make(int32 Index, double X, double Z);
	/// Eat the most filling food in the pack.
	FString Eat();

	FString NeedsLine() const;
	/// The pack and the recipes, as lines for the screen.
	TArray<FString> PackLines(double X, double Z);
	/// Trees felled since the last call (lattice cells): the streamer takes them out of the picture.
	TArray<TPair<int64, int64>> TakeFelled() { TArray<TPair<int64, int64>> Out; Swap(Out, Felled); return Out; }
	/// True once after placed objects changed (the streamer redraws them).
	bool TakeObjectsChanged();
	/// True once after the character died (the game puts a new one at the start place).
	bool TakeDied() { const bool b = bDied; bDied = false; return b; }
	const std::map<long long, wr::FPlacedObject>& Objects() const { return Rules->Objects.All(); }
	const wr::FNeeds& GetNeeds() const { return Needs; }
	double GetHours() const { return GameTime; }

	// wr::IWorldQuery: the rules ask about the generated world.
	virtual bool Tree(long long cx, long long cz, double& x, double& z, double& scale, int& species) override;
	virtual wr::FObjectRemovals& Removals() override { return Gone; }
	virtual double Hours() override { return GameTime; }
	virtual void Wind(double& x, double& z, double& speed) override;
	virtual double Ground(double x, double z) override;
	virtual std::string WaterNear(double x, double z, double radius) override;
	virtual void GatherSpot(int layer, long long cx, long long cz, double& x, double& z) override;

private:
	FString Tell(const wr::FActionResult& Result);
	std::shared_ptr<ww::FTerrainSampler> Sampler;
	std::unique_ptr<wr::FCatalog> Catalog;
	std::unique_ptr<wr::FGameRules> Rules;
	wr::FObjectRemovals Gone;
	wr::FNeeds Needs;
	double GameTime = 0;
	TArray<TPair<int64, int64>> Felled;
	bool bDied = false;
	FString Error;
};
