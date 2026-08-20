#pragma once

#include "CoreMinimal.h"
#include "Materials/MaterialInterface.h"
#include "BasicMaterials.generated.h"

UENUM()
enum class EBasicMaterial
{
	EmissiveMaskedVertexColor,
	EmissiveTransparentVertexColor
};

// get one of a handful of basic materials that are in most projects.
// note that returned loaded material is not rooted. it should stay alive as long as there is
// a TObjectPtr of it around, or if you root it.
FORCENOINLINE TRACESUNREALEXTENSION_API TObjectPtr<UMaterialInterface> GetBasicMaterial(EBasicMaterial T);