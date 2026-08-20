#define LOCTEXT_NAMESPACE "TracesUnrealExtensionModule"

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

class FTracesUnrealExtensionModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override {};
	virtual void ShutdownModule() override {};
};

IMPLEMENT_MODULE(FTracesUnrealExtensionModule, TracesUnrealExtension)

#undef LOCTEXT_NAMESPACE