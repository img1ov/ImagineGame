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
			"ImagineGame"
		});

		PrivateDependencyModuleNames.AddRange(new[]
		{
			"InputCore",
			"Slate",
			"SlateCore",
			"ToolMenus",
			"EditorStyle",
			"DataValidation",
			"MessageLog",
			"Projects",
			"DeveloperToolSettings",
			"CollectionManager",
			"SourceControl",
			"Chaos",
			"ExternalRpcRegistry"
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
