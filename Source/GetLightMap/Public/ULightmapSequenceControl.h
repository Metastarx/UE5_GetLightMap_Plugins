

#pragma once

#include "CoreMinimal.h"

#include "SaveLightmap.h"

#include "ULightmapSequenceControl.generated.h"   // 必须是最后一个 include

class ULightmapSequenceControl;


UCLASS(BlueprintType)
/** 迭代光照的管理器 */
class GETLIGHTMAP_API ULightmapSequenceControl : public UObject
{
    GENERATED_BODY()

public:
    ULightmapSequenceControl();



    void Initialize(UWorld* InWorld);
    void Start();
    void Cancel();
    int32 GetCurrentIteration() const { return CurrentIteration; }


    void OnLightingBuildSucceeded();
    void OnLightingBuildFailed();
    void TriggerBuild();
    void SetLightPos(int32 Iter);
    void DoAfterBuild();

protected:

    UWorld* World = nullptr;

    int32 TotalIterations = 4;
    int32 CurrentIteration = 0;
    bool bRunning = false;

    FDelegateHandle OnSuccessHandle;
    FDelegateHandle OnFailHandle;

    FLightmapDumpOptions Options;
};
