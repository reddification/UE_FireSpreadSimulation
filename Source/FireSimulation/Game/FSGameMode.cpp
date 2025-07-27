// Copyright Epic Games, Inc. All Rights Reserved.

#include "FSGameMode.h"

#include "FSGameState.h"

AFSGameMode::AFSGameMode()
{
	GameStateClass = AFSGameState::StaticClass();
}
