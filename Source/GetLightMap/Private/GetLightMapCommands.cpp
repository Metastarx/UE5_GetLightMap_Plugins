// Copyright Epic Games, Inc. All Rights Reserved.

#include "GetLightMapCommands.h"

#define LOCTEXT_NAMESPACE "FGetLightMapModule"

void FGetLightMapCommands::RegisterCommands()
{
	UI_COMMAND(PluginAction, "GetLightMap", "Execute GetLightMap action", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE
