#include "LiveSpoutOutputModule.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

DEFINE_LOG_CATEGORY(LogSpoutSender);

void FLiveSpoutOutputModule::StartupModule()
{
#if PLATFORM_WINDOWS
    if (IsRunningCommandlet() || FParse::Param(FCommandLine::Get(), TEXT("nullrhi"))) return;
    TArray<FString> Directories;
    if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("LiveSpoutOutput")))
    {
        Directories.Add(FPaths::Combine(Plugin->GetBaseDir(), TEXT("Source/ThirdParty/bin/Win64")));
        Directories.Add(FPaths::Combine(Plugin->GetBaseDir(), TEXT("Binaries/Win64")));
    }
    Directories.Add(FPlatformProcess::BaseDir());
    for (const FString& Directory : Directories)
    {
        FPlatformProcess::PushDllDirectory(*Directory);
        if (!SpoutHandle) SpoutHandle = FPlatformProcess::GetDllHandle(TEXT("Spout.dll"));
        if (SpoutHandle && !DX12Handle) DX12Handle = FPlatformProcess::GetDllHandle(TEXT("SpoutDX12.dll"));
        FPlatformProcess::PopDllDirectory(*Directory);
        if (IsNativeReady()) break;
    }
    if (!IsNativeReady()) UE_LOG(LogSpoutSender, Error, TEXT("LiveSpoutOutput: matching native SDK DLLs unavailable; output disabled."));
#endif
}

void FLiveSpoutOutputModule::ShutdownModule()
{
    if (DX12Handle) FPlatformProcess::FreeDllHandle(DX12Handle);
    if (SpoutHandle) FPlatformProcess::FreeDllHandle(SpoutHandle);
    DX12Handle = SpoutHandle = nullptr;
}

IMPLEMENT_MODULE(FLiveSpoutOutputModule, LiveSpoutOutput)
