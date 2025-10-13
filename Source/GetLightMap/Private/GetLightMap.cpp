// Copyright Epic Games, Inc. All Rights Reserved.

#include "GetLightMap.h"
#include "GetLightMapStyle.h"
#include "GetLightMapCommands.h"
#include "Misc/MessageDialog.h"
#include "ToolMenus.h"
#include "ULightmapSequenceControl.h"
#include "SaveLightmap.h"

#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Engine/Level.h"
#include "Engine/Light.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "UObject/Package.h"
#include "StaticLighting.h"
#include "LightingBuildOptions.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"          // 声明 extern UUnrealEdEngine* GUnrealEd
#include "Editor.h"
#include "EditorBuildUtils.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Engine/DirectionalLight.h"
#include "Async/Async.h"
#include "Engine/Texture2D.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/MapBuildDataRegistry.h"
#include "Misc/Guid.h"
#include "Engine/EngineTypes.h"
#include "RenderCommandFence.h"
#include "LightMap.h"
#include "RenderUtils.h"
#include "Engine/TextureCube.h"
#include "StaticMeshResources.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Modules/ModuleManager.h"
#include "IImageWrapperModule.h"
#include "IImageWrapper.h"
#include "ImageUtils.h"
#include "UObject/UObjectIterator.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include <Kismet/GameplayStatics.h>
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "SceneTypes.h"
#include "UObject/UObjectAnnotation.h"
#include "StaticMeshComponentLODInfo.h"
#include "Components/LightComponent.h"



static const FName GetLightMapTabName("GetLightMap");

#define LOCTEXT_NAMESPACE "FGetLightMapModule"

void FGetLightMapModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	
	FGetLightMapStyle::Initialize();
	FGetLightMapStyle::ReloadTextures();

	FGetLightMapCommands::Register();
	
	PluginCommands = MakeShareable(new FUICommandList);

	PluginCommands->MapAction(
		FGetLightMapCommands::Get().PluginAction,
		FExecuteAction::CreateRaw(this, &FGetLightMapModule::PluginButtonClicked),
		FCanExecuteAction());

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FGetLightMapModule::RegisterMenus));
}

void FGetLightMapModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.

	UToolMenus::UnRegisterStartupCallback(this);

	UToolMenus::UnregisterOwner(this);

	FGetLightMapStyle::Shutdown();

	FGetLightMapCommands::Unregister();
}

//--------------------------------点击后的入口函数-----------------------------------------
void FGetLightMapModule::PluginButtonClicked()
{
    // Put your "OnButtonClicked" stuff here
    //FText DialogText = FText::Format(
    //						LOCTEXT("PluginButtonDialogText", "Add code to {0} in {1} to override this button's actions"),
    //						FText::FromString(TEXT("FGetLightMapModule::PluginButtonClicked()")),
    //						FText::FromString(TEXT("GetLightMap.cpp"))
    //				   );
    //FMessageDialog::Open(EAppMsgType::Ok, DialogText);


	//烘焙光照的方式1 test
    //FEditorBuildUtils::EditorBuild(World, FName(TEXT("BuildLighting")), false);

	//烘焙光照的方式2 test
    //FLightingBuildOptions Options;
    //Options.bShowLightingBuildInfo = false; // 不显示 lighting stats 窗口
    //Options.bOnlyBuildSelected = false;
    //Options.bOnlyBuildVisibility = false;
    //// ...根据需要设置其他字段...
    //GUnrealEd->BuildLighting(Options);


	//无法使用这种方法获取World 这个是pie的
    //UWorld* World = GEngine->GetCurrentPlayWorld();

	//全局在这里获取一次即可
    UWorld* World = (GEditor) ? GEditor->GetEditorWorldContext().World() : nullptr;

    //FString TimeStamp = FString("asd");
	// 
    //SaveLightMapOnce(World);
    StartLightmapSequence(World);
}

void FGetLightMapModule::StartLightmapSequence(UWorld* World)
{
    if (!World)
    {
        UE_LOG(LogTemp, Warning, TEXT("GetLightMapData: World is null."));
        return ;
    }

	// 创建并初始化 LightmapSequenceManager 这个类是为了了在烘焙完成后回调 
    ULightmapSequenceControl* Manager = NewObject<ULightmapSequenceControl>(GetTransientPackage(), NAME_None, RF_Transient);

    if (!Manager)
    {
        UE_LOG(LogTemp, Warning, TEXT("GetLightMapData: Failed to create manager object."));
        return ;
    }

    Manager->Initialize(World);
    Manager->Start();


    return ;
}


void FGetLightMapModule::SaveLightMapOnce(UWorld* World)
{
    if (!World) return;


    FLightmapDumpOptions Options(TEXT("D:/Data/Image"), true, false);
    FString worldName = World->GetName();
    Options.OutDirectory = FString::Printf(TEXT("D:/Data/Image/%s"), *worldName);


	FString TimeStamp = FString("OnceTest");

	SaveLightmap::DumpAllLightmapsFromWorld(World, TimeStamp, Options);

    return;
}

//移动场景中的光 
void FGetLightMapModule::SetLightPos(int32 T) 
{
#if WITH_EDITOR
	UWorld* World = (GEditor) ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		UE_LOG(LogTemp, Warning, TEXT("No Editor World."));
		return;
	}

	AActor* LightActor = nullptr;
	if (!LightActor)
	{
		// 优先找方向光
		for (TActorIterator<ADirectionalLight> It(World); It; ++It) { LightActor = *It; break; }
		// 没有方向光就找任意光源
		if (!LightActor)
		{
			for (TActorIterator<ALight> It(World); It; ++It) { LightActor = *It; break; }
		}
	}
	if (!LightActor)
	{
		UE_LOG(LogTemp, Warning, TEXT("No LightActor found (not selected and none in level)."));
		return;
	}

	ULightComponent* LightComp = LightActor->FindComponentByClass<ULightComponent>();
	if (!LightComp)
	{
		UE_LOG(LogTemp, Warning, TEXT("Selected actor has no LightComponent: %s"), *LightActor->GetName());
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("Using LightActor: %s"), *LightActor->GetName());


	// t ∈ [0,24] → θ ∈ [0, π]
	const float R = 2000.0f;
	const float ClampedT = FMath::Clamp(static_cast<float>(T), 0.0f, 24.0f);
	const float Theta = PI * (ClampedT / 24.0f);

	// 半圆所在平面：XZ（Y=0）。t=0 在 (R,0,0)，t=12 在 (0,0,R)，t=24 在 (-R,0,0)
	const FVector NewPos(R * FMath::Cos(Theta), 0.0f, R * FMath::Sin(Theta));

	const EComponentMobility::Type OldMobility = LightComp->Mobility;
	if (OldMobility != EComponentMobility::Movable)
	{
		LightComp->SetMobility(EComponentMobility::Movable); // 会自动重新注册组件
	}

	LightActor->Modify(); // 支持撤销/重做，且能标记包为已修改
	// 移动 + 让光源朝向原点
	LightActor->SetActorLocation(NewPos, /*bSweep*/false, /*OutHit*/nullptr, ETeleportType::TeleportPhysics);

	const FVector ToOrigin = FVector::ZeroVector - NewPos; // 朝向原点
	const FRotator NewRot = ToOrigin.Rotation();
	LightActor->SetActorRotation(NewRot, ETeleportType::TeleportPhysics);


	// 还原Mobility
	if (OldMobility != EComponentMobility::Movable)
	{
		LightComp->SetMobility(OldMobility);
	}
	if (UPackage* Pkg = LightActor->GetOutermost())
	{
		Pkg->SetDirtyFlag(true); // 标脏，保存关卡后位置就被永久保存
	}

#endif // WITH_EDITOR
}




void FGetLightMapModule::RegisterMenus()
{
	// Owner will be used for cleanup in call to UToolMenus::UnregisterOwner
	FToolMenuOwnerScoped OwnerScoped(this);

	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
		{
			FToolMenuSection& Section = Menu->FindOrAddSection("WindowLayout");
			Section.AddMenuEntryWithCommandList(FGetLightMapCommands::Get().PluginAction, PluginCommands);
		}
	}

	{
		UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.PlayToolBar");
		{
			FToolMenuSection& Section = ToolbarMenu->FindOrAddSection("PluginTools");
			{
				FToolMenuEntry& Entry = Section.AddEntry(FToolMenuEntry::InitToolBarButton(FGetLightMapCommands::Get().PluginAction));
				Entry.SetCommandList(PluginCommands);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FGetLightMapModule, GetLightMap)