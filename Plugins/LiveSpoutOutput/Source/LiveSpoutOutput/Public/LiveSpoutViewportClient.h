#pragma once
#include "CoreMinimal.h"
#include "Engine/GameViewportClient.h"
#include "LiveSpoutViewportClient.generated.h"

class ULiveSpoutSenderComponent;

struct FLiveCloudViewportLaunch
{
    bool bCloud = false;
    int32 Width = 0;
    int32 Height = 0;
    int32 Fullscreen = -1;
};

// Scene and AHUD Canvas draw once into this fixed main render target. Slate scales the local preview.
UCLASS(Config=Engine)
class LIVESPOUTOUTPUT_API ULiveSpoutViewportClient : public UGameViewportClient
{
    GENERATED_BODY()
public:
    virtual TSharedRef<FSceneViewport> CreateViewport(TSharedPtr<SViewport> InViewportWidget) override;
    virtual void Tick(float DeltaTime) override;
    virtual void BeginDestroy() override;
    TSharedPtr<FSceneViewport> GetSpoutSceneViewport() const { return OutputViewport; }
    static bool IsLaunchEligible(const TCHAR* CommandLine);
    static FLiveCloudViewportLaunch ParseCloudViewportLaunch(const TCHAR* CommandLine);
private:
    bool ShouldOutput() const;
    void StopOutput();
    TSharedPtr<FSceneViewport> OutputViewport;
    UPROPERTY(Transient) TObjectPtr<AActor> SenderActor;
    UPROPERTY(Transient) TObjectPtr<ULiveSpoutSenderComponent> Sender;
    bool bFixedOutputRequested = false;
    FLiveCloudViewportLaunch CloudLaunch;
    bool bCloudSettingsApplied = false;
#if !UE_BUILD_SHIPPING
    double ResizeFixtureElapsed = 0;
    int32 ResizeFixtureStage = 0;
#endif
};
