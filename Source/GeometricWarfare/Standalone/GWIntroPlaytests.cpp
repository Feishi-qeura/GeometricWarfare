#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GWController.h"
#include "GWGameInstance.h"
#include "GWHUD.h"
#include "GWText.h"
#include "HAL/FileManager.h"
#include "InputKeyEventArgs.h"
#include "MediaPlayer.h"
#include "MediaSoundComponent.h"
#include "MediaTexture.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

class FGWIntroCheck : public IAutomationLatentCommand
{
public:
    FGWIntroCheck(FAutomationTestBase* InTest, const TCHAR* InCulture, const TCHAR* InExpected, bool InSkip)
        : Test(InTest), Culture(InCulture), Expected(InExpected), Skip(InSkip), Started(FPlatformTime::Seconds()) {}
    virtual bool Update() override
    {
        const double Now = FPlatformTime::Seconds();
        UWorld* W = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
        auto* C = W ? Cast<AGWController>(W->GetFirstPlayerController()) : nullptr;
        auto* H = C ? Cast<AGWHUD>(C->GetHUD()) : nullptr;
        auto* GI = W ? W->GetGameInstance<UGWGameInstance>() : nullptr;
        if (Now - Started > 25)
        {
            Test->AddError(TEXT("Intro playback test timed out: ") + Culture);
            if (H) H->StopIntro();
            GWText::SetCulture(Previous);
            return true;
        }
        if (!H || !GI)
            return false;
        if (Stage == 0)
        {
            Previous = GWText::Culture();
            H->StopIntro();
            GI->StartupIntroAttempted = false;
            GWText::SetCulture(Culture);
            Test->TestEqual(TEXT("Localized skip hint"), GWText::Get(TEXT("Intro.Skip")).ToString(), Expected);
            H->StartIntro();
            Test->TestTrue(TEXT("Startup playback begins"), H->IntroPlaying());
            if (H->IntroSound)
                H->IntroSound->SetEnableEnvelopeFollowing(true);
            Stage = 1;
        }
        if (Stage == 1)
        {
            if (H->IntroSound)
                MaxEnvelope = FMath::Max(MaxEnvelope, H->IntroSound->GetEnvelopeValue());
            if (!H->IntroPlaying())
            {
                Test->AddError(TEXT("Movie failed before a decoded video frame: ") + Culture);
                GWText::SetCulture(Previous);
                return true;
            }
            if (!H->IntroPlayer->IsPlaying() || H->IntroPlayer->GetTime().GetTotalSeconds() < 1.5)
                return false;
            Test->TestTrue(TEXT("Decoded 1080p texture"), H->IntroTexture && H->IntroTexture->GetSurfaceWidth() == 1920);
            Test->TestTrue(TEXT("Prompt fully visible"), H->Intro.alpha > .99);
            Test->TestTrue(TEXT("Movie audio produces decoded samples"), MaxEnvelope > .00001f);
            // Escape and clicks must not reach the notebook or title actions.
            const FInputDeviceId Device = FInputDeviceId::CreateFromInternalId(0);
            C->InputKey(FInputKeyEventArgs(GEngine->GameViewport->Viewport, Device, EKeys::Escape, IE_Pressed, FPlatformTime::Cycles64()));
            Test->TestFalse(TEXT("Escape cannot open settings under the movie"), H->SettingsOpen());
            Test->TestTrue(TEXT("Title clicks are consumed"), H->Click(100, 300));
            Shot = FPaths::ProjectSavedDir() / TEXT("StartupIntro") / (TEXT("intro-") + Culture + TEXT(".png"));
            IFileManager::Get().MakeDirectory(*FPaths::GetPath(Shot), true);
            FScreenshotRequest::RequestScreenshot(Shot, false, false);
            Stage = 2;
            Next = Now + .3;
            return false;
        }
        if (Stage == 2 && Now >= Next)
        {
            Test->TestTrue(TEXT("Rendered localized screenshot saved"), IFileManager::Get().FileExists(*Shot));
            if (Skip)
            {
                C->InputKey(FInputKeyEventArgs(GEngine->GameViewport->Viewport, FInputDeviceId::CreateFromInternalId(0),
                                             EKeys::SpaceBar, IE_Pressed, FPlatformTime::Cycles64()));
                Test->TestTrue(TEXT("Actual Space input starts fade"), H->Intro.stopping);
            }
            Stage = 3;
            Next = Now + .4;
        }
        if (Stage == 3 && Now >= Next && !H->IntroPlaying())
        {
            Test->TestNull(TEXT("Player released"), H->IntroPlayer.Get());
            Test->TestNull(TEXT("Video audio released"), H->IntroSound.Get());
            H->StartIntro();
            Test->TestFalse(TEXT("Returning to title does not replay intro"), H->IntroPlaying());
            if (!Skip)
            {
                Stage = 4;
                Next = Now + 1.2;
                return false;
            }
            GWText::SetCulture(Previous);
            return true;
        }
        if (Stage == 4 && Now >= Next)
        {
            Test->TestTrue(TEXT("Title buttons return after the intro"), H->Hits.Num() > 0);
            Shot = FPaths::ProjectSavedDir() / TEXT("StartupIntro/title-after-intro.png");
            FScreenshotRequest::RequestScreenshot(Shot, false, false);
            Stage = 5;
            Next = Now + .3;
        }
        if (Stage == 5 && Now >= Next)
        {
            Test->TestTrue(TEXT("Rendered title screenshot saved"), IFileManager::Get().FileExists(*Shot));
            GWText::SetCulture(Previous);
            return true;
        }
        return false;
    }
private:
    FAutomationTestBase* Test;
    FString Culture, Expected, Previous, Shot;
    bool Skip;
    double Started, Next = 0;
    int32 Stage = 0;
    float MaxEnvelope = 0;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGWIntroPlaybackTest, "GW.Standalone.Intro",
                                 EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FGWIntroPlaybackTest::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(FGWIntroCheck(this, TEXT("en"), TEXT("Press Space to skip"), true));
    ADD_LATENT_AUTOMATION_COMMAND(FGWIntroCheck(this, TEXT("zh-Hans"), TEXT("按下空格键跳过"), true));
    ADD_LATENT_AUTOMATION_COMMAND(FGWIntroCheck(this, TEXT("ja"), TEXT("スペースキーでスキップ"), true));
    ADD_LATENT_AUTOMATION_COMMAND(FGWIntroCheck(this, TEXT("de"), TEXT("Leertaste zum Überspringen"), false));
    return true;
}
#endif
