using UnrealBuildTool;

public class ImagineEditor : ModuleRules
{
	public ImagineEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// AnimGraph node wrappers are serialized in runtime Blueprints, then removed during cook.
		OverridePackageType = PackageOverrideType.GameUncookedOnly;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"AnimGraph",
			"BlueprintGraph",
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
			"AnimGraphRuntime",
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
