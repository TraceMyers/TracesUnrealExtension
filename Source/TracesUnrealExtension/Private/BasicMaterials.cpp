#include "BasicMaterials.h"

TObjectPtr<UMaterialInterface> GetBasicMaterial(EBasicMaterial T)
{
	UEnum* MatTypeEnum = StaticEnum<EBasicMaterial>();
	// short name, not fully-qualified class name
	const FString MatName = MatTypeEnum->GetNameStringByValue((int64)T);
	const FString MatPath = FString::Printf(L"/TracesUnrealExtension/Materials/M_%s.M_%s", *MatName, *MatName);
	return TObjectPtr<UMaterialInterface>(LoadObject<UMaterialInterface>(nullptr, *MatPath));
}
