// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"



class FToolBarBuilder;
class FMenuBuilder;


class FGetLightMapModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	
	/** This function will be bound to Command. */
	void PluginButtonClicked();
	

	void SetLightPos(int32 T);

	//启动 烘焙->保存->移动光照流程 可以设置轮数
	void StartLightmapSequence(UWorld* World);
	//保存当前的 光照贴图 
    void SaveLightMapOnce(UWorld* World);

private:

	void RegisterMenus();


private:
	TSharedPtr<class FUICommandList> PluginCommands;
};



