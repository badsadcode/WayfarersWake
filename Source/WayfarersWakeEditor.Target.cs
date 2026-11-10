using UnrealBuildTool;

public class WayfarersWakeEditorTarget : TargetRules
{
	public WayfarersWakeEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V8;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_9;
		ExtraModuleNames.Add("WayfarersWake");
	}
}
