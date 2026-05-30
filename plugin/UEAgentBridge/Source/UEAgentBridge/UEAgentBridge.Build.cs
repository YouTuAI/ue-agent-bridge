using UnrealBuildTool;

public class UEAgentBridge : ModuleRules
{
	public UEAgentBridge(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"Sockets",
			"Networking"
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"CoreUObject",
			"Engine",
			"Slate",
			"SlateCore",
			"Json",
			"JsonUtilities",
			"UnrealEd",
			"BlueprintGraph",
			"KismetCompiler",
			"EditorSubsystem",
			"Projects",
			"PythonScriptPlugin",
			"Kismet",
			"EditorScriptingUtilities"
		});
	}
}
