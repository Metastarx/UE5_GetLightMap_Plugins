


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



bool SaveImage(UTexture2D* Texture2D, const FString& Path)
{
    // setup required parameters
    TextureCompressionSettings OldCompressionSettings = Texture2D->CompressionSettings;
    TextureMipGenSettings OldMipGenSettings = Texture2D->MipGenSettings;
    bool OldSRGB = Texture2D->SRGB;

    Texture2D->CompressionSettings = TextureCompressionSettings::TC_VectorDisplacementmap;
    Texture2D->MipGenSettings = TextureMipGenSettings::TMGS_NoMipmaps;
    Texture2D->SRGB = false;
    Texture2D->UpdateResource();

    FTexture2DMipMap& mipmap = Texture2D->GetPlatformData()->Mips[0];

    uint8* Data = (uint8*)mipmap.BulkData.Lock(LOCK_READ_WRITE);
    //FColor* FormatedImageData = static_cast<FColor*>(mipmap.BulkData.Lock(LOCK_READ_WRITE));
    if (Data == nullptr)
    {
        mipmap.BulkData.Unlock(); // 不释放，会卡住
        Texture2D->UpdateResource();
        return false;
    }

    EPixelFormat PixelFormat = Texture2D->GetPixelFormat();
    // 典型情况：PF_B8G8R8A8
    // 这是未压缩的纹理
    if (PixelFormat == PF_B8G8R8A8 || PixelFormat == PF_R8G8B8A8)
    {

        int width = mipmap.SizeX;
        int height = mipmap.SizeY;
        TArray<FColor> nColors;

        for (int32 y = 0; y < height; y++)
        {
            for (int32 x = 0; x < width; x++)
            {
                FColor bColor;
                bColor.B = Data[(y * width + x) * 4 + 0];//B 0 - 255
                bColor.G = Data[(y * width + x) * 4 + 1];//G
                bColor.R = Data[(y * width + x) * 4 + 2];//R
                bColor.A = Data[(y * width + x) * 4 + 3];//A 
                nColors.Add(bColor);
            }
        }
        mipmap.BulkData.Unlock();

        // return old parameters
        Texture2D->CompressionSettings = OldCompressionSettings;
        Texture2D->MipGenSettings = OldMipGenSettings;
        Texture2D->SRGB = OldSRGB;

        Texture2D->UpdateResource();

        //获取 uint8 数据，保存
        TArray64<uint8> ImgData;
        //FImageUtils::CompressImageArray(width, height, nColors, ImgData);

        //最新的API数据 
        FImageUtils::PNGCompressImageArray(width, height, nColors, ImgData);


        //PNGCompressImageArray
        return FFileHelper::SaveArrayToFile(ImgData, *Path);
    }
    return false;
}

FString MakeSafeFilename(const FString& In)
{
    FString Out = In;
    Out.ReplaceInline(TEXT(":"), TEXT("_"));
    Out.ReplaceInline(TEXT("/"), TEXT("_"));
    Out.ReplaceInline(TEXT("\\"), TEXT("_"));
    Out.ReplaceInline(TEXT(" "), TEXT("_"));
    return Out;
}

bool ProcessLightMap2D(FMeshMapBuildData* MeshBuildData, const FGuid& MapBuildDataId, const FString& BaseName, FString timeName, const FLightmapDumpOptions& Options)
{
    bool flag = false;

    if (!MeshBuildData) return flag;

    //获取 FLightMap2D 数据格式 
    FLightMap2D* LightMap2D = MeshBuildData->LightMap ? MeshBuildData->LightMap->GetLightMap2D() : nullptr;
    if (!LightMap2D)
    {
        UE_LOG(LogTemp, Verbose, TEXT("  [%s] No FLightMap2D (Guid %s)"), *BaseName, *MapBuildDataId.ToString());
        return flag;
    }

    //调试信息 可以不用
    //const FVector2D CoordScale = LightMap2D->GetCoordinateScale();
    //const FVector2D CoordBias = LightMap2D->GetCoordinateBias();
    //UE_LOG(LogTemp, Log, TEXT("  [%s] Guid=%s CoordScale=(%f,%f) CoordBias=(%f,%f)"), *BaseName, *MapBuildDataId.ToString(), CoordScale.X, CoordScale.Y, CoordBias.X, CoordBias.Y);

    // 是否为虚拟纹理（VT）
    if (LightMap2D->IsVirtualTextureValid())
    {
        UE_LOG(LogTemp, Log, TEXT("    VirtualTexture lightmap (VT) - skipping pixel export for %s"), *BaseName);
        // 如果需要导出 VT 的像素，需使用虚拟纹理系统导出（更复杂）
        return flag;
    }

    // 实际上就是HQ 和LQ 
    for (uint32 Basis = 0; Basis < 2; ++Basis)
    {
        if (!LightMap2D->IsValid(Basis))
        {
            continue;
        }

        UTexture2D* Tex = LightMap2D->GetTexture(Basis);
        if (!Tex)
        {
            UE_LOG(LogTemp, Verbose, TEXT("    Basis %u: no UTexture2D"), Basis);
            continue;
        }

        UE_LOG(LogTemp, Log, TEXT("    Basis %u: Texture=%s"), Basis, *Tex->GetFullName());
        //UE_LOG(LogTemp, Log, TEXT("---------------asdasd----------"), Basis, *Tex->GetFullName());

        //ExprotUTexture2D(Tex, Options.OutDirectory + "/" + BaseName + "_" + MapBuildDataId.ToString() + "_" + FString::FromInt(Basis) + "_" + MakeSafeFilename(Tex->GetName()) + ".png");
        //return Tex;

        //只用在这里进行保存
        if (Tex)
        {
            UE_LOG(LogTemp, Log, TEXT("Get UTexture2D success"));
            //bool isSave = SaveImage(Tex, Options.OutDirectory + "/" + BaseName + "_" + MapBuildDataId.ToString() + "_" + MakeSafeFilename(Tex->GetName()) + ".png");



            FString TexName = MakeSafeFilename(Tex->GetName());

            FString pngName = TexName + "_t_" + MakeSafeFilename(timeName);
            bool isSave = SaveImage(Tex, Options.OutDirectory + "/" + pngName + ".png");

            flag = isSave;

        }
        else
        {
            UE_LOG(LogTemp, Log, TEXT("Get UTexture2D fail"));
        }

    }

    return flag;
}



void SaveLightmap::DumpAllLightmapsFromWorld(UWorld* World, FString timeName, const FLightmapDumpOptions& Options)
{
    //这个是获取当前运行时的世界 调试时使用的
    //UWorld *World = GEngine->GetWorldContexts()[0].World();

    if (!World)
    {
        UE_LOG(LogTemp, Warning, TEXT("DumpAllLightmapsFromWorld: World is null."));
        return;
    }

    FString OutDir = Options.OutDirectory;


    IFileManager::Get().MakeDirectory(*OutDir, true);

    if (OutDir.IsEmpty())
    {
        OutDir = FPaths::ProjectSavedDir() / TEXT("Lightmaps");
        IFileManager::Get().MakeDirectory(*OutDir, true);
    }

    UE_LOG(LogTemp, Log, TEXT("Starting lightmap dump for world"));


    // 遍历世界中的所有 Actor
    for (TActorIterator<AActor> It(World, AActor::StaticClass()); It; ++It)
    {
        AActor* Actor = *It;
        if (!Actor) continue;

        // 收集组件（包括实例化网格）
        TArray<UActorComponent*> Components;
        Actor->GetComponents(Components);

        for (UActorComponent* Component : Components)
        {
            if (!Component) continue;

            // 先处理 UStaticMeshComponent
            if (UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(Component))
            {
                UE_LOG(LogTemp, Log, TEXT("-------------------Start UStaticMeshComponent----------------------"));


                // 每个 LOD 有一个 MapBuildDataId（在 SMC->LODData）
                for (int32 LODIndex = 0; LODIndex < SMC->LODData.Num(); ++LODIndex)
                {
                    const FStaticMeshComponentLODInfo& LODInfo = SMC->LODData[LODIndex];
                    const FGuid& MapBuildDataId = LODInfo.MapBuildDataId;

                    //const TArray<FStaticMeshComponentLODInfo>& LODData = SMC->LODData;
                    //const FStaticMeshComponentLODInfo& LODInfo = LODData[LODIndex];
                    //const FGuid& MapBuildDataId = LODInfo.MapBuildDataId;

                    if (!MapBuildDataId.IsValid()) continue;

                    UMapBuildDataRegistry* Registry = UMapBuildDataRegistry::Get(SMC);
                    if (!Registry)
                    {
                        UE_LOG(LogTemp, Warning, TEXT("  [%s] No MapBuildDataRegistry for component %s"), *Actor->GetName(), *SMC->GetName());
                        continue;
                    }

                    FMeshMapBuildData* MeshBuildData = Registry->GetMeshBuildData(MapBuildDataId);

                    // 如果MeshBuildData 为空，尝试使用 GetMeshBuildDataDuringBuild
                    if (!MeshBuildData)
                    {
                        // 尝试在“构建期间”访问（不会被 ResourceCluster 空检查阻止）
                        FMeshMapBuildData* MeshBuildDataDuring = Registry->GetMeshBuildDataDuringBuild(MapBuildDataId);

                        UE_LOG(LogTemp, Verbose, TEXT("Get MeshBuildData fail"));
                        UE_LOG(LogTemp, Verbose, TEXT("Run MeshBuildDataDuring start"));

                        if (MeshBuildDataDuring)
                        {
                            UE_LOG(LogTemp, Verbose, TEXT("Run MeshBuildDataDuring success"));

                            // 找到了数据，但 GetMeshBuildData 返回空，通常说明 ResourceCluster 还没初始化
                            // 可以继续使用 MeshBuildDataDuring（注意 ResourceCluster 相关字段可能为空）
                            MeshBuildData = MeshBuildDataDuring;
                        }
                        else
                        {
                            // 真正没有数据 —— lighting 未构建或 GUID 不存在于 Registry
                            UE_LOG(LogTemp, Verbose, TEXT("Run MeshBuildDataDuring fail"));
                            continue;
                        }
                    }
                    else
                    {
                        // 正常获取到 MeshBuildData，继续
                        UE_LOG(LogTemp, Verbose, TEXT("Get MeshBuildData success"));
                    }

                    const FString BaseName = FString::Printf(TEXT("%s_%s_LOD%d"), *MakeSafeFilename(Actor->GetName()), *timeName, LODIndex);

                    bool flag = ProcessLightMap2D(MeshBuildData, MapBuildDataId, BaseName, timeName, Options);

                    //HQ 和 LQ 只要有一个保存成功就行
                    if (flag)
                    {
                        UE_LOG(LogTemp, Log, TEXT("Save success"));
                    }
                    else
                    {
                        UE_LOG(LogTemp, Log, TEXT("Save fail"));
                    }


                }
                UE_LOG(LogTemp, Log, TEXT("-------------------End UStaticMeshComponent----------------------"));
            }
            // 处理 InstancedStaticMeshComponent（每个 ISMC 的实例可以有 PerInstanceLightmapData）
            else if (UInstancedStaticMeshComponent* ISMC = Cast<UInstancedStaticMeshComponent>(Component))
            {
                UE_LOG(LogTemp, Log, TEXT("-------------------Start UInstancedStaticMeshComponent----------------------"));

                // ISMC 也有 LODData with MapBuildDataId (取第0 LOD 或所有 LODs)
                for (int32 LODIndex = 0; LODIndex < ISMC->LODData.Num(); ++LODIndex)
                {
                    const FStaticMeshComponentLODInfo& LODInfo = ISMC->LODData[LODIndex];
                    const FGuid& MapBuildDataId = LODInfo.MapBuildDataId;

                    //const TArray<FStaticMeshComponentLODInfo>& LODData = SMC->LODData;
                    //const FStaticMeshComponentLODInfo& LODInfo = LODData[LODIndex];
                    //const FGuid& MapBuildDataId = LODInfo.MapBuildDataId;

                    if (!MapBuildDataId.IsValid()) continue;

                    UMapBuildDataRegistry* Registry = UMapBuildDataRegistry::Get(ISMC);
                    if (!Registry)
                    {
                        UE_LOG(LogTemp, Warning, TEXT("  [%s] No MapBuildDataRegistry for component %s"), *Actor->GetName(), *ISMC->GetName());
                        continue;
                    }

                    FMeshMapBuildData* MeshBuildData = Registry->GetMeshBuildData(MapBuildDataId);

                    // 如果MeshBuildData 为空，尝试使用 GetMeshBuildDataDuringBuild
                    if (!MeshBuildData)
                    {
                        // 尝试在“构建期间”访问（不会被 ResourceCluster 空检查阻止）
                        FMeshMapBuildData* MeshBuildDataDuring = Registry->GetMeshBuildDataDuringBuild(MapBuildDataId);

                        UE_LOG(LogTemp, Verbose, TEXT("Get MeshBuildData fail"));
                        UE_LOG(LogTemp, Verbose, TEXT("Run MeshBuildDataDuring start"));

                        if (MeshBuildDataDuring)
                        {
                            UE_LOG(LogTemp, Verbose, TEXT("Run MeshBuildDataDuring success"));

                            // 找到了数据，但 GetMeshBuildData 返回空，通常说明 ResourceCluster 还没初始化
                            // 可以继续使用 MeshBuildDataDuring（注意 ResourceCluster 相关字段可能为空）
                            MeshBuildData = MeshBuildDataDuring;
                        }
                        else
                        {
                            // 真正没有数据 —— lighting 未构建或 GUID 不存在于 Registry
                            UE_LOG(LogTemp, Verbose, TEXT("Run MeshBuildDataDuring fail"));
                            continue;
                        }
                    }
                    else
                    {
                        // 正常获取到 MeshBuildData，继续
                        UE_LOG(LogTemp, Verbose, TEXT("Get MeshBuildData success"));
                    }

                    const FString BaseName = FString::Printf(TEXT("%s_%s_LOD%d"), *MakeSafeFilename(Actor->GetName()), *MakeSafeFilename(ISMC->GetName()), LODIndex);

                    bool flag = ProcessLightMap2D(MeshBuildData, MapBuildDataId, BaseName, timeName, Options);

                    if (flag)
                    {
                        UE_LOG(LogTemp, Log, TEXT("Save success"));
                    }
                    else
                    {
                        UE_LOG(LogTemp, Log, TEXT("Save fail"));
                    }

                    UE_LOG(LogTemp, Log, TEXT("-------------------End UInstancedStaticMeshComponent----------------------"));

                }
            }
        }
    }
    //UE_LOG(LogTemp, Log, TEXT("Finished lightmap dump."));
}


