using System.IO;
using UnrealBuildTool;

public class TracesUnrealExtension : ModuleRules
{
	public TracesUnrealExtension(ReadOnlyTargetRules Target) : base(Target)
	{
		// PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PCHUsage = PCHUsageMode.NoSharedPCHs;
		
		// set this to =1 if you want to run the lock test. =0 otherwise, as 
		// it adds costly instrumentation to hot paths.
		PublicDefinitions.Add("TUE_LOCK_TEST_INSTRUMENTATION=0");
		
		// adds generated file directory (in intermediates) to includes.
		string GeneratedDir = Path.Combine(
			PluginDirectory,
			"Intermediate",
			"Generated"
		);
		Directory.CreateDirectory(GeneratedDir);
		PublicIncludePaths.Add(GeneratedDir);
		
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", 
			"CoreUObject", 
			"Engine", 
			"InputCore", 
		});
	}
}
