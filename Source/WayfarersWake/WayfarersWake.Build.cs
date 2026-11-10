using UnrealBuildTool;

public class WayfarersWake : ModuleRules
{
	public WayfarersWake(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// The world generator (WorldGen/) must reproduce Thornwake's terrain bit for bit: IEEE-exact floating point,
		// no fast-math and no FMA contraction. Verified by Tools/WorldGenTest and the in-game self test (-WwSelfTest).
		FPSemantics = FPSemanticsMode.Precise;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "ProceduralMeshComponent" });
		PrivateDependencyModuleNames.AddRange(new string[] { "RenderCore", "RHI", "NetCore", "MeshDescription", "StaticMeshDescription", "Slate", "SlateCore" });
		PublicIncludePaths.Add(ModuleDirectory);
	}
}
