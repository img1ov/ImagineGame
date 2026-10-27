#include "ImagineGame.h"
#include "Modules/ModuleManager.h"
#include "Private/Traversal/IMGTraversalDebug.h"

class FIMGGameModule : public FDefaultGameModuleImpl
{
public:
	virtual void ShutdownModule() override
	{
#if ENABLE_DRAW_DEBUG
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
