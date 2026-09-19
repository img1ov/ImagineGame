using UnrealBuildTool;
using System.Collections.Generic;

public class ImagineGameEditorTarget : TargetRules
{
	public ImagineGameEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;

		ExtraModuleNames.AddRange(new[] { "ImagineGame", "ImagineEditor" });

		// Useful for touch input development through Unreal Remote 2.
		EnablePlugins.Add("RemoteSession");
	}
}
