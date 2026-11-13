#include "ImagineGame.h"
#include "Modules/ModuleManager.h"
#include "Private/Character/Traversal/IMGTraversalDebug.h"

class FIMGGameModule : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
		FDefaultGameModuleImpl::StartupModule();
#if IMG_TRAVERSAL_DEBUG_ENABLED
		IMGTraversalDebug::Startup();
#endif
	}

	virtual void ShutdownModule() override
	{
#if IMG_TRAVERSAL_DEBUG_ENABLED
		IMGTraversalDebug::Shutdown();
#endif
		FDefaultGameModuleImpl::ShutdownModule();
	}
};

IMPLEMENT_PRIMARY_GAME_MODULE(
	FIMGGameModule,
	ImagineGame,
	"ImagineGame"
);
