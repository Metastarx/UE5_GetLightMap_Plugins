// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Framework/Commands/Commands.h"
#include "GetLightMapStyle.h"

class FGetLightMapCommands : public TCommands<FGetLightMapCommands>
{
public:

	FGetLightMapCommands()
		: TCommands<FGetLightMapCommands>(TEXT("GetLightMap"), NSLOCTEXT("Contexts", "GetLightMap", "GetLightMap Plugin"), NAME_None, FGetLightMapStyle::GetStyleSetName())
	{
	}

	// TCommands<> interface
	virtual void RegisterCommands() override;

public:
	TSharedPtr< FUICommandInfo > PluginAction;
};
