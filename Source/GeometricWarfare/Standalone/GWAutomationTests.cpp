#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GWController.h"
#include "GWGameInstance.h"
#include "GWGameMode.h"
#include "GWGameState.h"
#include "GWPlayerState.h"
#include "GWText.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "UnrealClient.h"
#include "GWWorld.h"
#include "Camera/CameraComponent.h"

namespace
{
UWorld* GameWorld()
{
    return GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
}
class FGWSceneCheck : public IAutomationLatentCommand
{
public:
    FGWSceneCheck(FAutomationTestBase* InTest, bool InSingle, FString InCulture, FString InShot)
        : Test(InTest), Single(InSingle), Culture(MoveTemp(InCulture)), Shot(MoveTemp(InShot)),
          Start(FPlatformTime::Seconds())
    {
    }
    virtual bool Update() override
    {
        UWorld* W = GameWorld();
        auto* C = W ? Cast<AGWController>(W->GetFirstPlayerController()) : nullptr;
        auto* P = C ? C->GetPlayerState<AGWPlayerState>() : nullptr;
        auto* S = W ? W->GetGameState<AGWGameState>() : nullptr;
        auto* G = W ? W->GetAuthGameMode<AGWGameMode>() : nullptr;
        double Now = FPlatformTime::Seconds();
        if (Now - Start > 30)
        {
            Test->AddError(TEXT("Standalone game world did not complete scene check within 30 seconds"));
            return true;
        }
        if (!C || !P || !S || !G || !C->GetHUD())
            return false;
        if (Stage == 0)
        {
            GWText::SetCulture(Culture);
            if (S->InMatch)
            {
                S->Finished = true;
                G->ReturnToLobby(C);
            }
            if (Single)
            {
                Test->TestTrue(TEXT("Start single player"), G->BeginRound(C, true));
                Test->TestTrue(TEXT("Player has a controlled unit"), P->UnitId > 0);
                Test->TestEqual(TEXT("Player starts with three AI teammates"), S->Units.Num(), 4);
                S->Simulation->advance(5);
                S->Publish();
            }
            Test->TestFalse(TEXT("Live interaction module removed"),
                            FModuleManager::Get().IsModuleLoaded(TEXT("LiveInteraction")));
            Test->TestFalse(TEXT("Spout module removed"),
                            FModuleManager::Get().IsModuleLoaded(TEXT("LiveSpoutOutput")));
            Stage = 1;
            Next = Now + 3;
            return false;
        }
        if (Stage == 1 && Now >= Next)
        {
            if (Single && !CameraTested && C->WorldView)
            {
                auto* Camera = C->WorldView->FindComponentByClass<UCameraComponent>();
                if (Camera)
                {
                    BeforeCamera = Camera->GetComponentLocation();
                    for (size_t I = 32; I < S->Simulation->maze.walls.size() - 32; ++I)
                        if (S->Simulation->maze.walls[I] > 0)
                        {
                            auto Position = S->Simulation->maze.center(static_cast<int>(I));
                            S->Simulation->maze.damageWalls(Position, Position, 5, 500);
                            break;
                        }
                    S->Publish();
                    Stage = 3;
                    Next = Now + .08;
                    return false;
                }
            }
            FString Path = FPaths::ProjectSavedDir() / TEXT("StandaloneVerification") / (Shot + TEXT(".png"));
            IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
            FScreenshotRequest::RequestScreenshot(Path, false, false);
            Stage = 2;
            Next = Now + 2;
            return false;
        }
        if (Stage == 3 && Now >= Next)
        {
            auto* Camera = C->WorldView->FindComponentByClass<UCameraComponent>();
            Test->TestTrue(TEXT("Wall-only update preserves player camera"),
                           Camera && (Camera->GetComponentLocation() - BeforeCamera).Size() < 200);
            CameraTested = true;
            Stage = 1;
            Next = Now;
            return false;
        }
        if (Stage == 2 && Now >= Next)
        {
            FString Path = FPaths::ProjectSavedDir() / TEXT("StandaloneVerification") / (Shot + TEXT(".png"));
            Test->TestTrue(TEXT("Rendered screenshot saved"), IFileManager::Get().FileExists(*Path));
            if (Single)
            {
                S->Finished = true;
                G->ReturnToLobby(C);
            }
            return true;
        }
        return false;
    }

private:
    FAutomationTestBase* Test;
    bool Single;
    FString Culture, Shot;
    double Start = 0, Next = 0;
    int Stage = 0;
    bool CameraTested = false;
    FVector BeforeCamera = FVector::ZeroVector;
};
} // namespace
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGWLocalizationTest, "GW.Standalone.Resources",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
                                     EAutomationTestFlags::EngineFilter)
bool FGWLocalizationTest::RunTest(const FString&)
{
    GWText::Initialize();
    FString Previous = GWText::Culture();
    const TCHAR* Cultures[] = {TEXT("en"), TEXT("zh-Hans"), TEXT("ja"), TEXT("de")};
    const TCHAR* Expected[] = {TEXT("Start game"), TEXT("开始游戏"), TEXT("ゲーム開始"), TEXT("Spiel starten")};
    const TCHAR* Board[] = {TEXT("Mission board"), TEXT("任务板"), TEXT("ミッションボード"), TEXT("Missionsbrett")};
    for (int I = 0; I < 4; ++I)
    {
        GWText::SetCulture(Cultures[I]);
        TestEqual(TEXT("Localized string table"), GWText::Get(TEXT("Menu.Start")).ToString(), FString(Expected[I]));
        TestEqual(TEXT("Localized mission board"), GWText::Get(TEXT("Board.Title")).ToString(), FString(Board[I]));
        TestTrue(TEXT("Wave format keeps both parameters"),
                 GWText::Get(TEXT("Defense.Wave")).ToString().Contains(TEXT("{Wave}")) &&
                     GWText::Get(TEXT("Defense.Wave")).ToString().Contains(TEXT("{Count}")));
        TestTrue(TEXT("Format parameter remains usable"),
                 GWText::Number(TEXT("Combat.Tail"), TEXT("Count"), 3).ToString().Contains(TEXT("3")));
    }
    GWText::SetCulture(Previous);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGWVisualCheck, "GW.Standalone.Visual",
                                 EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FGWVisualCheck::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(FGWSceneCheck(this, false, TEXT("zh-Hans"), TEXT("menu-zh")));
    ADD_LATENT_AUTOMATION_COMMAND(FGWSceneCheck(this, false, TEXT("ja"), TEXT("menu-ja")));
    ADD_LATENT_AUTOMATION_COMMAND(FGWSceneCheck(this, false, TEXT("de"), TEXT("menu-de")));
    ADD_LATENT_AUTOMATION_COMMAND(FGWSceneCheck(this, true, TEXT("zh-Hans"), TEXT("single-zh")));
    return true;
}
#endif
