

#include "System/IMGGameData.h"

#include "System/IMGAssetManager.h"

UIMGGameData::UIMGGameData()
{
}

const UIMGGameData& UIMGGameData::Get()
{
	return UIMGAssetManager::Get().GetGameData();
}
