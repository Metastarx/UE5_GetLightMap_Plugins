

#pragma once

#include "CoreMinimal.h"

 



struct FLightmapDumpOptions;


struct FLightmapDumpOptions
{
    // 输出目录（绝对或相对项目路径）
    FString OutDirectory;

    // 是否把纹理保存为 PNG
    bool bSaveAsPNG = true;

    // 是否仅记录日志而不导出像素
    bool bOnlyLog = false;

    FLightmapDumpOptions() {}

    FLightmapDumpOptions(FString InOutDirectory, bool bInSaveAsPNG = true, bool bInOnlyLog = false)
        : OutDirectory(MoveTemp(InOutDirectory)), bSaveAsPNG(bInSaveAsPNG), bOnlyLog(bInOnlyLog) {
    }
};



class GETLIGHTMAP_API SaveLightmap
{


public:
    static TMap<FString, bool> TexMap;
    static bool CheckOrSetTrue(FString& TexName);

    // 遍历 World 中所有 Actors/Components，导出或记录 lightmap 数据
    static void DumpAllLightmapsFromWorld(UWorld* World, FString time, const FLightmapDumpOptions& Options = FLightmapDumpOptions());
};
