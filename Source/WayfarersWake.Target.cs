using UnrealBuildTool;

public class WayfarersWakeTarget : TargetRules
{
	public WayfarersWakeTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V8;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_9;
		ExtraModuleNames.Add("WayfarersWake");
	}
}
