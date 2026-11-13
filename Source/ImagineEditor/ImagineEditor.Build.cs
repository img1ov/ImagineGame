using UnrealBuildTool;

public class ImagineEditor : ModuleRules
{
	public ImagineEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"GameplayTags",
			"EditorFramework",
			"UnrealEd",
			"PhysicsCore",
			"GameplayTagsEditor",
			"GameplayTasksEditor",
			"GameplayAbilities",
			"GameplayAbilitiesEditor",
			"AnimationModifiers",
			"AnimationBlueprintLibrary",
			"StudioTelemetry",
			"AssetTools",
			"Slate",
			"SlateCore",
			"RewindDebuggerInterface",
			"TraceServices",
			"ImagineGame"
		});

		PrivateDependencyModuleNames.AddRange(new[]
		{
			"InputCore",
			"ApplicationCore",
			"Json",
			"JsonUtilities",
			"PropertyEditor",
			"ToolMenus",
			"EditorStyle",
			"DataValidation",
			"MessageLog",
			"Projects",
			"DeveloperToolSettings",
			"CollectionManager",
			"SourceControl",
			"Chaos",
			"ExternalRpcRegistry",
			"TraceAnalysis",
			"TraceLog"
		});

		// External RPC support is disabled in shipping builds.
		if (Target.Configuration == UnrealTargetConfiguration.Shipping)
		{
			PublicDefinitions.Add("WITH_RPC_REGISTRY=0");
			PublicDefinitions.Add("WITH_HTTPSERVER_LISTENERS=0");
		}
		else
		{
			PrivateDependencyModuleNames.Add("HTTPServer");
			PublicDefinitions.Add("WITH_RPC_REGISTRY=1");
			PublicDefinitions.Add("WITH_HTTPSERVER_LISTENERS=1");
		}

		PublicDefinitions.Add("SHIPPING_DRAW_DEBUG_ERROR=1");
	}
}
