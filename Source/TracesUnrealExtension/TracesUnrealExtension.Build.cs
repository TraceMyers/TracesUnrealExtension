using System.IO;
using UnrealBuildTool;

public class TracesUnrealExtension : ModuleRules
{
	public TracesUnrealExtension(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
			
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