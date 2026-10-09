#pragma once
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSpoutSender, Log, All);

class LIVESPOUTOUTPUT_API FLiveSpoutOutputModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
    bool IsNativeReady() const { return SpoutHandle && DX12Handle; }
private:
    void* SpoutHandle = nullptr;
    void* DX12Handle = nullptr;
};
