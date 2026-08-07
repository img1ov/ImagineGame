#include "Development/IMGBotCheats.h"
#include "Engine/World.h"
#include "GameFramework/CheatManagerDefines.h"
#include "GameModes/IMGBotCreationComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGBotCheats)

//////////////////////////////////////////////////////////////////////
// UIMGBotCheats

UIMGBotCheats::UIMGBotCheats()
{
#if WITH_SERVER_CODE && UE_WITH_CHEAT_MANAGER
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		UCheatManager::RegisterForOnCheatManagerCreated(FOnCheatManagerCreated::FDelegate::CreateLambda(
			[](UCheatManager* CheatManager)
			{
				CheatManager->AddCheatManagerExtension(NewObject<ThisClass>(CheatManager));
			}));
	}
#endif
}

void UIMGBotCheats::AddPlayerBot()
{
#if WITH_SERVER_CODE && UE_WITH_CHEAT_MANAGER
	if (UIMGBotCreationComponent* BotComponent = GetBotComponent())
	{
		BotComponent->Cheat_AddBot();
	}
#endif	
}

void UIMGBotCheats::RemovePlayerBot()
{
#if WITH_SERVER_CODE && UE_WITH_CHEAT_MANAGER
	if (UIMGBotCreationComponent* BotComponent = GetBotComponent())
	{
		BotComponent->Cheat_RemoveBot();
	}
#endif	
}

UIMGBotCreationComponent* UIMGBotCheats::GetBotComponent() const
{
	if (UWorld* World = GetWorld())
	{
		if (AGameStateBase* GameState = World->GetGameState())
		{
			return GameState->FindComponentByClass<UIMGBotCreationComponent>();
		}
	}

	return nullptr;
}

