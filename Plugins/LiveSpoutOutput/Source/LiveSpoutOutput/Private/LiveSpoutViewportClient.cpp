#include "LiveSpoutViewportClient.h"
#include "LiveSpoutSenderComponent.h"
#include "LiveSpoutOutputModule.h"
#include "Engine/World.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/Actor.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWindow.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "RHI.h"

namespace
{
    bool ReadLaunchInteger(const TCHAR* CommandLine, const TCHAR* Name, int32 Minimum, int32 Maximum, int32& OutValue)
    {
        const FString Flag = FString(TEXT("-")) + Name;
        const TCHAR* Cursor = CommandLine;
        FString Token, Value;
        while (FParse::Token(Cursor, Token, false))
        {
            if (Token.Equals(Flag, ESearchCase::IgnoreCase))
            {
                if (!FParse::Token(Cursor, Value, false)) return false;
            }
            else if (Token.StartsWith(Flag + TEXT("="), ESearchCase::IgnoreCase)) Value = Token.Mid(Flag.Len() + 1);
            else continue;
            if (Value.IsEmpty()) return false;
            int32 Parsed = 0;
            for (const TCHAR Character : Value)
            {
                if (Character < TEXT('0') || Character > TEXT('9')) return false;
                // Bound before multiplying; arbitrarily long inputs cannot overflow.
                if (Parsed > Maximum / 10) return false;
                Parsed = Parsed * 10 + Character - TEXT('0');
                if (Parsed > Maximum) return false;
            }
            if (Parsed < Minimum) return false;
            OutValue = Parsed;
            return true;
        }
        return false;
    }
}

FLiveCloudViewportLaunch ULiveSpoutViewportClient::ParseCloudViewportLaunch(const TCHAR* CommandLine)
{
    FLiveCloudViewportLaunch Result;
    int32 Cloud = 0;
    Result.bCloud = ReadLaunchInteger(CommandLine, TEXT("cloud-game"), 0, 1, Cloud) && Cloud == 1;
    if (Result.bCloud)
    {
        ReadLaunchInteger(CommandLine, TEXT("screen-width"), 320, 8192, Result.Width);
        ReadLaunchInteger(CommandLine, TEXT("screen-height"), 320, 8192, Result.Height);
        ReadLaunchInteger(CommandLine, TEXT("screen-fullscreen"), 0, 1, Result.Fullscreen);
    }
    return Result;
}

bool ULiveSpoutViewportClient::IsLaunchEligible(const TCHAR* CommandLine)
{
    if (FParse::Param(CommandLine, TEXT("NoSpout")) || FParse::Param(CommandLine, TEXT("nullrhi"))) return false;
    // Platform cloud flags use both space-delimited and equals forms. A cloud run never fixes its portrait viewport.
    const TCHAR* Cursor = CommandLine;
    FString Token;
    bool bHasPlatformToken = FParse::Param(CommandLine, TEXT("GWCredentialStdin"));
    while (FParse::Token(Cursor, Token, false))
    {
        if (Token.Equals(TEXT("-token"), ESearchCase::IgnoreCase) || Token.StartsWith(TEXT("-token="), ESearchCase::IgnoreCase)) bHasPlatformToken = true;
    }
    if (ParseCloudViewportLaunch(CommandLine).bCloud) return false;
#if !UE_BUILD_SHIPPING
    if (FParse::Param(CommandLine, TEXT("GWLocalTest")) && !bHasPlatformToken)
        return FParse::Param(CommandLine, TEXT("Spout"));
#endif
    return true;
}

bool ULiveSpoutViewportClient::ShouldOutput() const
{
#if PLATFORM_WINDOWS
    if (GIsEditor || IsRunningCommandlet() || !FApp::CanEverRender() || !IsLaunchEligible(FCommandLine::Get())) return false;
    if (!GDynamicRHI || FString(GDynamicRHI->GetName()) != TEXT("D3D12")) return false;
    return FModuleManager::LoadModuleChecked<FLiveSpoutOutputModule>(TEXT("LiveSpoutOutput")).IsNativeReady();
#else
    return false;
#endif
}

TSharedRef<FSceneViewport> ULiveSpoutViewportClient::CreateViewport(TSharedPtr<SViewport> InViewportWidget)
{
    StopOutput();
    CloudLaunch = ParseCloudViewportLaunch(FCommandLine::Get());
    bCloudSettingsApplied = false;
    bFixedOutputRequested = ShouldOutput();
    if ((bFixedOutputRequested || (CloudLaunch.bCloud && (CloudLaunch.Width > 0 || CloudLaunch.Height > 0))) && InViewportWidget.IsValid())
        InViewportWidget->SetRenderDirectlyToWindow(false);
    TSharedRef<FSceneViewport> Result = Super::CreateViewport(InViewportWidget);
    OutputViewport = Result;
    return Result;
}

void ULiveSpoutViewportClient::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    UWorld* GameWorld = GetWorld();
    if (CloudLaunch.bCloud && !bCloudSettingsApplied && !GIsEditor && FApp::CanEverRender() && !IsRunningCommandlet()
        && OutputViewport.IsValid() && GameWorld && GameWorld->WorldType == EWorldType::Game && GetWindow().IsValid())
    {
        const FIntPoint Existing = OutputViewport->GetSizeXY();
        const int32 Width = CloudLaunch.Width > 0 ? CloudLaunch.Width : FMath::Max(Existing.X, 320);
        const int32 Height = CloudLaunch.Height > 0 ? CloudLaunch.Height : FMath::Max(Existing.Y, 320);
        const EWindowMode::Type Mode = CloudLaunch.Fullscreen == 1 ? EWindowMode::Fullscreen
            : (CloudLaunch.Fullscreen == 0 ? EWindowMode::Windowed : GetWindow()->GetWindowMode());
        // Static resolution request and direct preview sizing do not SaveSettings or change persisted preferences.
        UGameUserSettings::RequestResolutionChange(Width, Height, Mode, false);
        GetWindow()->SetWindowMode(Mode);
        GetWindow()->Resize(FVector2D(Width, Height));
        if (CloudLaunch.Width > 0 || CloudLaunch.Height > 0) OutputViewport->SetFixedViewportSize(Width, Height);
        bCloudSettingsApplied = true;
        UE_LOG(LogSpoutSender, Display, TEXT("LiveSpoutOutput: cloud viewport %dx%d fullscreen=%d from platform flags; Spout disabled; no persisted settings."), Width, Height, CloudLaunch.Fullscreen);
    }
    if (!bFixedOutputRequested || !OutputViewport.IsValid() || !GameWorld || GameWorld->WorldType != EWorldType::Game || !GetWindow().IsValid()) return;
#if !UE_BUILD_SHIPPING
    // Explicit development fixture drives the game's own preview window through multiple sizes.
    // It never runs on a production or cloud launch unless this test-only flag is supplied.
    if (FParse::Param(FCommandLine::Get(), TEXT("SpoutResizeTest")))
    {
        ResizeFixtureElapsed += DeltaTime;
        const double AtSeconds[] = { 2, 6, 10 };
        const FIntPoint PreviewSizes[] = { FIntPoint(1280, 720), FIntPoint(320, 180), FIntPoint(640, 360) };
        if (ResizeFixtureStage < 3 && ResizeFixtureElapsed >= AtSeconds[ResizeFixtureStage])
        {
            const FIntPoint Requested = PreviewSizes[ResizeFixtureStage++];
            GetWindow()->Resize(FVector2D(Requested));
            UE_LOG(LogSpoutSender, Display, TEXT("LiveSpoutOutput resize fixture: preview requested %dx%d; main render target %dx%d (stage %d)."),
                Requested.X, Requested.Y, OutputViewport->GetSizeXY().X, OutputViewport->GetSizeXY().Y, ResizeFixtureStage);
        }
    }
#endif
    if (!OutputViewport->HasFixedSize() || OutputViewport->GetSizeXY() != FIntPoint(1920, 1080))
    {
        OutputViewport->SetFixedViewportSize(1920, 1080);
        UE_LOG(LogSpoutSender, Display, TEXT("LiveSpoutOutput: fixed main viewport 1920x1080; Canvas HUD included, local window only scales preview."));
    }
    if (IsValid(SenderActor) && SenderActor->GetWorld() == GameWorld && !SenderActor->IsActorBeingDestroyed()) return;
    StopOutput();
    FActorSpawnParameters SpawnParameters;
    SpawnParameters.ObjectFlags |= RF_Transient;
    SenderActor = GameWorld->SpawnActor<AActor>(AActor::StaticClass(), SpawnParameters);
    if (!SenderActor) return;
    Sender = NewObject<ULiveSpoutSenderComponent>(SenderActor);
    SenderActor->AddInstanceComponent(Sender);
    Sender->RegisterComponent();
    FString AppId;
    GConfig->GetString(TEXT("DouyinLiveProvider"), TEXT("AppId"), AppId, GGameIni);
    const FString SenderName = AppId.IsEmpty() || AppId == TEXT("YOUR_APP_ID")
        ? TEXT("mate_spout_local") : TEXT("mate_spout_") + AppId;
    Sender->StartBroadcastGameViewport(SenderName, 60, true);
}

void ULiveSpoutViewportClient::StopOutput()
{
    if (IsValid(Sender)) Sender->StopBroadcast();
    Sender = nullptr;
    if (IsValid(SenderActor) && !SenderActor->IsActorBeingDestroyed()) SenderActor->Destroy();
    SenderActor = nullptr;
}

void ULiveSpoutViewportClient::BeginDestroy()
{
    StopOutput();
    OutputViewport.Reset();
    Super::BeginDestroy();
}
