using UnrealBuildTool;

public class TracesUnrealExtension : ModuleRules
{
	public TracesUnrealExtension(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
			
		// editor
		
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", 
			"CoreUObject", 
			"Engine", 
			"InputCore", 
		});
	}
}