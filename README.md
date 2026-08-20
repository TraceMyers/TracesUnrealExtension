# Trace's Unreal Extension

This is the unreal plugin that I want in all of my unreal projects so that I don't have to spend as much time on early (and mid) project boilerplate.

## Status

Early days! I'm filling it out as a part of a larger project.

## Features

<ul>
  <li><b>TMultiBuffer/TLockMultiBuffer</b>: locked/unlocked implementations for double/triple/n-ple buffering</li>
  <li><b>TLockBuffer</b>: a locked buffer</li>
  <li><b>TinyRaycast</b>: raycast to primitive shapes without involving the physics engine</li>
  <li><b>BasicMaterials</b>: loader + getter for basic materials that are in every project</li>
  <li><b>ComponentCreation</b>: macros to reduce component creation boilerplate</li>
  <li><b>FThreadTeam/FThreadJobber + FJobStack</b>: a convenient and powerful implementation for high performance multithreaded work.</li>
  <li><b>DebugDrawExt</b>: more debug draw functions</li>
  <li><b>GenerateAssetPaths</b>: a pre-build executable that creates a constant referring to every asset in your project.</li>
</ul>

## Feature Details

### Generate Asset Paths

generate_asset_paths.exe runs before compilation, creating a Generated/ProjectAssetReferences.h file within the plugin's public source directory.

The generated file will create a const wchar_t* referring to every asset path in your project, which looks something like this:

```
#pragma once
inline const wchar_t* ASSET_DA_AUDIOCONFIG = L"/Game/Data/DA_AudioConfig.DA_AudioConfig";
inline const wchar_t* ASSET_DA_MATERIALCONFIG = L"/Game/Data/DA_MaterialConfig.DA_MaterialConfig";
inline const wchar_t* ASSET_BP_NAVIGABILITYPROBE = L"/Game/Navigation/BP_NavigabilityProbe.BP_NavigabilityProbe";
inline const wchar_t* ASSET_BP_PERSON = L"/Game/People/BP_Person.BP_Person";
inline const wchar_t* ASSET_NINAJIRACHI_INFOHAZARD = L"/Game/Sound/NinaJirachi_Infohazard.NinaJirachi_Infohazard";
...
```

This opens up the possibility of a workflow wherein you can refer to assets directly in c++ without hard-coding the path.

(MaterialConfig.cpp)
```
#include "MaterialConfig.h"

TWeakObjectPtr<UMaterialConfig> UMaterialConfig::This = nullptr;

UMaterialConfig* UMaterialConfig::Get()
{
	if (!This.Get())
	{
		This = LoadObject<UMaterialConfig>(nullptr, ASSET_DA_MATERIALCONFIG);
	}
	return This.Get();
}
```

If the asset path changes, the name ASSET_DA_MATERIALCONFIG is still valid, and refers to the new path. Nothing is broken. If the asset is removed, the wchar_t* will no longer exist, which is a compile error, this enforces source corrections on asset deletion. 

The uncomfortable part is that if the asset is renamed, the name must also be corrected in source. But, this can be made easy by only referring to the wchar_t* once, so only one edit has to be made.

In the near future I will be converting this to a public header with a getter procedure and a source file with the wchar_t*'s so that the #include doesn't include a million lines. It could also only run when changes are noticed in the project assets directory.

To disable this feature, open the .uplugin file and remove the 'PreBuildSteps' entry.
