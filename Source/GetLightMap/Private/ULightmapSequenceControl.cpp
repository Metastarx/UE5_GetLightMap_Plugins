


#include "ULightmapSequenceControl.h"
#include "GetLightMap.h"
#include "SaveLightmap.h"

#include "Editor.h"
#include "EditorBuildUtils.h"
#include "Engine/World.h"
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
#include "HAL/PlatformProcess.h"
#include "Templates/UniquePtr.h"
#include "Engine/MapBuildDataRegistry.h"
#include "StaticLightingBuildContext.h"
#include "SlateFwd.h"
#include "AssetCompilingManager.h"
#include "TextureCompiler.h"
#include "Engine/Texture2D.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
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
#include "TimerManager.h"
#include "Engine/DirectionalLight.h"
#include "Async/Async.h"
#include "Components/LightComponent.h"





ULightmapSequenceControl::ULightmapSequenceControl()
{
}


void ULightmapSequenceControl::Initialize(UWorld* InWorld)
{
    World = InWorld;
    TotalIterations = 24;
    CurrentIteration = 0;
    bRunning = false;

    FString worldName = World->GetName();
    Options.OutDirectory = FString::Printf(TEXT("D:/Data/Image/%s"), *worldName);

    //设置光照的初始值
    SetLightPos(CurrentIteration);
}

void ULightmapSequenceControl::Start()
{
    if (!World)
    {
        UE_LOG(LogTemp, Warning, TEXT("ULightmapSequenceManager: World is null, aborting."));
        return;
    }

    if (bRunning)
    {
        UE_LOG(LogTemp, Warning, TEXT("ULightmapSequenceManager: already running."));
        return;
    }

    bRunning = true;

	//将自己加入根对象，防止被GC
    AddToRoot();


	// 绑定委托（编辑器范围内的光照构建成功/失败）
    OnSuccessHandle = FEditorDelegates::OnLightingBuildSucceeded.AddUObject(this, &ULightmapSequenceControl::OnLightingBuildSucceeded);
    OnFailHandle = FEditorDelegates::OnLightingBuildFailed.AddUObject(this, &ULightmapSequenceControl::OnLightingBuildFailed);


    if (OnSuccessHandle.IsValid())
    {
        UE_LOG(LogTemp, Log, TEXT("OnSuccessHandle is valid"));
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("OnSuccessHandle is NOT valid"));
    }


    UE_LOG(LogTemp, Log, TEXT("UTriggerBuild in"));

	// 开始第一轮构建
    TriggerBuild();
}

void ULightmapSequenceControl::Cancel()
{
    if (!bRunning) return;

    bRunning = false;
    // Remove delegates
    if (OnSuccessHandle.IsValid())
    {
        FEditorDelegates::OnLightingBuildSucceeded.Remove(OnSuccessHandle);
        OnSuccessHandle.Reset();
    }
    if (OnFailHandle.IsValid())
    {
        FEditorDelegates::OnLightingBuildFailed.Remove(OnFailHandle);
        OnFailHandle.Reset();
    }

    //移除根节点
    RemoveFromRoot();

    // Note: manager object will be GC'd if nothing references it (it's transient)
    UE_LOG(LogTemp, Log, TEXT("ULightmapSequenceManager: Cancelled"));
}

//////////////////////////////////////////////////////////////////////////
// Internal helpers (editor-only)

void ULightmapSequenceControl::TriggerBuild()
{

    
    UE_LOG(LogTemp, Log, TEXT("-------TriggerBuild---------"));

    if (!World)
    {
        UE_LOG(LogTemp, Warning, TEXT("ULightmapSequenceManager::TriggerBuild: World null"));
        Cancel();
        return;
    }


    UE_LOG(LogTemp, Log, TEXT("ULightmapSequenceManager: Triggering BuildLighting for iteration %d"), CurrentIteration);


    //if (auto* Subsys = GEditor->GetEditorSubsystem<UEditorBuildSubsystem>())
    //{
    //    // 通常为静默构建，不会弹“Lighting Options”
    //    const bool bStarted = Subsys->EditorBuildLighting();
    //}

    // 在你的工具/命令中：
    //GConfig->SetBool(TEXT("LightingBuildOptions"), TEXT("ShowLightingBuildInfo"), false, GEditorPerProjectIni);

	// 下面这种方式会弹出 Lighting Options 窗口
    //FEditorBuildUtils::EditorBuild(World, FName(TEXT("BuildLighting")), false);


    FLightingBuildOptions LBOptions;
    LBOptions.bShowLightingBuildInfo = false; // 不显示 lighting stats 窗口
    LBOptions.bOnlyBuildSelected = false;
    LBOptions.bOnlyBuildVisibility = false;
    // ...根据需要设置其他字段...
    GUnrealEd->BuildLighting(LBOptions);



    //// 触发与通知按钮同样的行为：请求导入（相当于点击 Apply Now）
    // 但是该方法是私有的 需要改引擎才能公开
    //FStaticLightingManager::ImportRequested();

}



void ULightmapSequenceControl::OnLightingBuildSucceeded()
{
    if (!bRunning) return;

    UE_LOG(LogTemp, Log, TEXT("ULightmapSequenceManager: Lighting build succeeded (iteration %d)"), CurrentIteration);


    // 异步等待一段时间 让引擎加载烘焙好的数据
    const float DelaySeconds = 2.0f; // 

    // 后台线程睡眠，不阻塞编辑器
    AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this, DelaySeconds]()
        {
            FPlatformProcess::Sleep(DelaySeconds);

            // 回到游戏线程执行真正的工作
            AsyncTask(ENamedThreads::GameThread, [this]()
                {
                    DoAfterBuild(); // 把原来的导出/移灯/下一轮放到这里
                });
        });
}


void ULightmapSequenceControl::DoAfterBuild()
{
    UE_LOG(LogTemp, Log, TEXT("DoAfterBuild success"));

    //FLightmapDumpOptions Options;
    //FString IterDir = FString::Printf(TEXT("Iter_%d"), CurrentIteration);
    //Options.OutDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Lightmaps"), IterDir);
    //Options.bSaveAsPNG = true;
    //Options.bOnlyLog = false;
    //IFileManager::Get().MakeDirectory(*Options.OutDirectory, true);


    //FAssetCompilingManager::Get().FinishAllCompilation();
    //FTextureCompilingManager::Get().FinishAllCompilation();
    //IStreamingManager::Get().GetTextureStreamingManager().BlockTillAllRequestsFinished();


    //const FString TimeStamp = FString::Printf(TEXT("%d"), CurrentIteration);
    //LightmapDumper::DumpAllLightmapsFromWorld(World, TimeStamp, Options);


    FString TimeStamp = FString::Printf(TEXT("%d"), CurrentIteration);
    SaveLightmap::DumpAllLightmapsFromWorld(World, TimeStamp, Options);


    CurrentIteration++;

    //设置光照新的位子
    SetLightPos(CurrentIteration);

    if (CurrentIteration < TotalIterations)
    {
        TriggerBuild();
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("ULightmapSequenceManager: Completed all iterations."));
        Cancel();
    }
}


void ULightmapSequenceControl::OnLightingBuildFailed()
{
    if (!bRunning) return;
    UE_LOG(LogTemp, Error, TEXT("ULightmapSequenceManager: Lighting build FAILED at iteration %d — aborting sequence."), CurrentIteration);
    Cancel();
}


void ULightmapSequenceControl::SetLightPos(int32 T)
{
#if WITH_EDITOR
    //UWorld* World = (GEditor) ? GEditor->GetEditorWorldContext().World() : nullptr;
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


