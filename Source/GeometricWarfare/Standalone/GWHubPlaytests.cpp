#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GWController.h"
#include "GWGameInstance.h"
#include "GWGameMode.h"
#include "GWGameState.h"
#include "GWHUD.h"
#include "GWPlayerState.h"
#include "GWProfile.h"
#include "GWSettings.h"
#include "GWText.h"
#include "GWWorld.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

// Phase 3 acceptance. GW.Hub.Scenes (one process, rendered): a new save starts with the tutorial, every step is
// done in order and captured, finishing it opens the player's own hub (offline here), the stations and their
// sheets are captured, the board deploys a page that returns to the hub. GW.Net.HubHost / GW.Net.HubClient
// (two processes over direct IP): the guest walks straight into the host's hub (the LAN half of Gate 3's
// "within a minute"), joins are refused during the mission, everyone returns to the hub, and when the host
// leaves the guest lands in their own hub. Screenshots land in Saved/HubVerification; real profiles wait as
// *.sav.playtest meanwhile.
namespace
{
using namespace gwgame;
void ParkHubProfiles(bool Park)
{
    for (int32 Slot = 0; Slot < UGWProfileSave::SlotCount; ++Slot)
    {
        const FString Path = UGWProfileSave::SlotPath(Slot), Backup = Path + TEXT(".playtest");
        if (Park)
        {
            if (IFileManager::Get().FileExists(*Backup))
                UGWProfileSave::Delete(Slot); // left over from an interrupted playtest
            else if (IFileManager::Get().FileExists(*Path))
                IFileManager::Get().Move(*Backup, *Path, true);
        }
        else
        {
            UGWProfileSave::Delete(Slot);
            if (IFileManager::Get().FileExists(*Backup))
                IFileManager::Get().Move(*Path, *Backup, true);
        }
    }
}
void CaptureHub(const TCHAR* Name, int32& Shots)
{
    const FString Path = FPaths::ProjectSavedDir() / TEXT("HubVerification") / (FString(Name) + TEXT(".png"));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
    FScreenshotRequest::RequestScreenshot(Path, false, false);
    ++Shots;
}
// Ends the page in progress as a win: the last wave, nothing left on the paper.
void WinPage(Battle& B, int32 Player)
{
    auto& D = B.defense;
    for (auto& U : B.units)
        if (Battle::enemy(U))
            U.alive = false;
    D.queue.clear();
    D.queued = 0;
    D.wavesCleared = D.waves - 1;
    D.phase = DefensePhase::Build;
    B.callWave(Player);
    for (auto& U : B.units)
        if (Battle::enemy(U))
            U.alive = false;
    D.queue.clear();
    D.queued = 0;
    B.advance(.2);
}

class FHubScenes : public IAutomationLatentCommand
{
public:
    explicit FHubScenes(FAutomationTestBase* T) : Test(T), Start(FPlatformTime::Seconds()) {}
    bool Update() override
    {
        const double Now = FPlatformTime::Seconds();
        auto* W = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
        auto* C = W ? Cast<AGWController>(W->GetFirstPlayerController()) : nullptr;
        auto* G = W ? W->GetAuthGameMode<AGWGameMode>() : nullptr;
        auto* S = W ? W->GetGameState<AGWGameState>() : nullptr;
        auto* P = C ? C->GetPlayerState<AGWPlayerState>() : nullptr;
        auto* GI = W ? W->GetGameInstance<UGWGameInstance>() : nullptr;
        auto* H = C ? Cast<AGWHUD>(C->GetHUD()) : nullptr;
        if (Now - Start > 240)
        {
            Test->AddError(FString::Printf(TEXT("Hub scenes timed out at stage %d"), Stage));
            Finish(GI);
            return true;
        }
        if (!C || !G || !S || !P || !GI || !H || !C->WorldView || Now < Next)
            return false;
        auto Wait = [&](double Seconds)
        {
            Next = Now + Seconds;
            ++Stage;
            return false;
        };
        Battle* B = S->Simulation.Get();
        Unit* Me = B ? B->find(P->UnitId) : nullptr;
        auto Cell = [&](int32 X, int32 Y) { return B->maze.center(Y * B->maze.width + X); };
        switch (Stage)
        {
        case 0:
            GWText::SetCulture(TEXT("zh-Hans"));
            if (S->InMatch)
            {
                S->Finished = true;
                G->ReturnToLobby(C);
            }
            ParkHubProfiles(true);
            Parked = true;
            OriginalSlot = GI->ProfileSlot();
            GI->SelectProfile(0);
            H->SetPage(AGWHUD::EPage::Main);
            return Wait(1.5);
        case 1:
            Test->TestTrue(TEXT("A new save is offered the tutorial"), GI->TutorialPending());
            CaptureHub(TEXT("hub-title-zh"), Shots);
            return Wait(.4);
        case 2:
            // What 开始游戏 does for a new save.
            Test->TestTrue(TEXT("The tutorial starts"), G->BeginTutorial(C));
            return Wait(1.5);
        case 3:
            Test->TestTrue(TEXT("The tutorial page is replicated"), S->Tutorial && S->Defense());
            Test->TestEqual(TEXT("It opens at the first step"), static_cast<int32>(S->TutorialStep), static_cast<int32>(TutorialStep::Move));
            CaptureHub(TEXT("tutorial-move-zh"), Shots);
            return Wait(.4);
        case 4:
        {
            const TutorialPage& T = tutorialPage();
            if (Me)
                Me->position = Cell(T.moveX, T.moveY);
            B->advance(.1);
            // One paper target broken, the other wounded: the card counts 1 / 2.
            int32 Targets = 0;
            for (auto& U : B->units)
            {
                if (!U.dummy)
                    continue;
                if (Targets++ == 0)
                {
                    for (int32 I = 0; I < 10 && U.alive; ++I)
                        B->damageUnit(P->UnitId, U.id, 40);
                }
                else
                    B->damageUnit(P->UnitId, U.id, 60);
            }
            if (Me)
            {
                Me->position = Cell(6, 2);
                Me->aim = {-1, 0};
            }
            B->advance(.1);
            S->Publish();
            return Wait(1.2);
        }
        case 5:
            Test->TestEqual(TEXT("Walking into the ring moved on to shooting"), static_cast<int32>(S->TutorialStep), static_cast<int32>(TutorialStep::Shoot));
            CaptureHub(TEXT("tutorial-shoot-zh"), Shots);
            return Wait(.4);
        case 6:
            for (auto& U : B->units)
                if (U.dummy)
                    for (int32 I = 0; I < 10 && U.alive; ++I)
                        B->damageUnit(P->UnitId, U.id, 40);
            B->advance(.1);
            S->Publish();
            return Wait(1.4);
        case 7:
            Test->TestEqual(TEXT("Two broken targets moved on to recruiting"), static_cast<int32>(S->TutorialStep), static_cast<int32>(TutorialStep::Recruit));
            CaptureHub(TEXT("tutorial-recruit-zh"), Shots);
            return Wait(.4);
        case 8:
        {
            int32 Offered = 0;
            for (auto& U : B->units)
                if (U.alive && U.kind == UnitKind::Companion && U.followerOf < 0 && Me)
                    U.position = Me->position + Vec2{55. + 35 * Offered++, 25};
            B->recruit(P->UnitId);
            B->recruit(P->UnitId);
            B->advance(.05);
            B->merge(P->UnitId);
            B->advance(.1);
            S->Publish();
            return Wait(1.2);
        }
        case 9:
            Test->TestEqual(TEXT("Recruiting and merging reached the turret step"), static_cast<int32>(S->TutorialStep), static_cast<int32>(TutorialStep::Turret));
            CaptureHub(TEXT("tutorial-turret-zh"), Shots);
            return Wait(.4);
        case 10:
            if (Me)
            {
                Me->position = Cell(7, 2);
                Me->aim = {0, 1};
            }
            B->launch(P->UnitId);
            for (int32 I = 0; I < 240 && B->tutorialStep() == TutorialStep::Turret; ++I)
                B->advance(1. / 60);
            S->Publish();
            return Wait(1.6);
        case 11:
            Test->TestEqual(TEXT("A pinned turret reached the drawing step"), static_cast<int32>(S->TutorialStep), static_cast<int32>(TutorialStep::Draw));
            CaptureHub(TEXT("tutorial-draw-zh"), Shots);
            return Wait(.4);
        case 12:
        {
            int32 Drawn = 0;
            for (const int32 Wall : B->tutorialWall())
                Drawn += B->paintWall(P->UnitId, Wall);
            Test->TestEqual(TEXT("The three suggested cells can be drawn"), Drawn, 3);
            B->advance(.1);
            S->Publish();
            return Wait(1.4);
        }
        case 13:
            Test->TestEqual(TEXT("Drawing reached the last step"), static_cast<int32>(S->TutorialStep), static_cast<int32>(TutorialStep::Defend));
            CaptureHub(TEXT("tutorial-defend-zh"), Shots);
            return Wait(.4);
        case 14:
            Test->TestTrue(TEXT("The wave can start now"), B->callWave(P->UnitId));
            for (int32 I = 0; I < 60 * 90 && !B->finished; ++I)
                B->advance(1. / 60);
            G->Tick(.01f);
            S->Publish();
            return Wait(1.5);
        case 15:
            Test->TestTrue(TEXT("The tutorial is won"), S->Finished && S->Winner == 1);
            CaptureHub(TEXT("tutorial-done-zh"), Shots);
            return Wait(.4);
        case 16:
            // 进入大厅: marks the save and opens the player's own hub (offline: no Steam in the test).
            H->LeaveTutorial();
            return Wait(3);
        case 17:
            if (!S->Hub())
                return false;
            Test->TestFalse(TEXT("The tutorial is not offered again"), GI->TutorialPending());
            // Offline without Steam (the usual test machine); with Steam up it is a friends-only room of one.
            UE_LOG(LogTemp, Display, TEXT("GW hub opened as room kind %d"), static_cast<int32>(GI->RoomKind()));
            Test->TestTrue(TEXT("The player's own hub opened"), W->GetNetMode() != NM_Client);
            Test->TestTrue(TEXT("The player stands in the hub"), Me != nullptr && Me->alive);
            CaptureHub(TEXT("hub-overview-zh"), Shots);
            return Wait(.4);
        case 18:
            // Walk up to the mission board: its tag shows the E prompt.
            if (Me)
                Me->position = hubSpots()[static_cast<int32>(HubStation::Board)].front + Vec2{0, 60};
            B->advance(.05);
            S->Publish();
            return Wait(1.4);
        // A screenshot is taken at the end of the frame, so every capture gets a stage of its own: changing
        // the panel or the culture in the same stage would photograph the change.
        case 19:
            CaptureHub(TEXT("hub-board-prompt-zh"), Shots);
            return Wait(.4);
        case 20:
            Test->TestTrue(TEXT("E at the board opens it"), H->Interact() && H->CurrentPanel() == AGWHUD::EPanel::Board);
            return Wait(1);
        case 21:
            CaptureHub(TEXT("hub-board-zh"), Shots);
            return Wait(.4);
        case 22:
            GWText::SetCulture(TEXT("en"));
            return Wait(.6);
        case 23:
            CaptureHub(TEXT("hub-board-en"), Shots);
            return Wait(.4);
        case 24:
            GWText::SetCulture(TEXT("zh-Hans"));
            H->OpenPanel(AGWHUD::EPanel::Shop);
            return Wait(.8);
        case 25:
            CaptureHub(TEXT("hub-shop-zh"), Shots);
            return Wait(.4);
        case 26:
            H->OpenPanel(AGWHUD::EPanel::Door);
            return Wait(.8);
        case 27:
            CaptureHub(TEXT("hub-door-zh"), Shots);
            return Wait(.4);
        case 28:
            H->OpenPanel(AGWHUD::EPanel::Training);
            return Wait(.8);
        case 29:
            CaptureHub(TEXT("hub-training-zh"), Shots);
            return Wait(.4);
        case 30:
            H->OpenPanel(AGWHUD::EPanel::Achievements);
            return Wait(.8);
        case 31:
            CaptureHub(TEXT("hub-achievements-zh"), Shots);
            return Wait(.4);
        case 32:
        {
            H->OpenPanel(AGWHUD::EPanel::None);
            // The training corner: a gentle throw onto the bullseye and a broken target.
            if (Me)
            {
                Me->position = B->hub.bullseye + Vec2{300, -40};
                Me->aim = {-1, 0};
            }
            Unit Tri;
            Tri.id = 9700;
            Tri.kind = UnitKind::Companion;
            Tri.state = UnitState::Idle;
            Tri.position = Me ? Me->position + Vec2{-45, 0} : B->hub.bullseye;
            B->units.push_back(Tri);
            Test->TestEqual(TEXT("A triangle joins the tail in the hub"), B->recruit(P->UnitId), 9700);
            Test->TestEqual(TEXT("And can be thrown"), B->launch(P->UnitId), 9700);
            if (Unit* Thrown = B->find(9700))
            {
                Thrown->velocity = {-420, 0};
                Thrown->launchInitialSpeed = 420;
            }
            for (int32 I = 0; I < 180 && B->find(9700) && B->find(9700)->state == UnitState::Launched; ++I)
                B->advance(1. / 60);
            for (auto& U : B->units)
                if (U.dummy)
                {
                    B->damageUnit(P->UnitId, U.id, 80);
                    break;
                }
            S->Publish();
            return Wait(.5);
        }
        case 33:
            Test->TestTrue(TEXT("The bullseye counted the throw"), S->HubBullseyes >= 1);
            Test->TestFalse(TEXT("Breaking targets never pauses the hub"), S->UpgradePaused);
            CaptureHub(TEXT("hub-training-corner-zh"), Shots);
            return Wait(.4);
        case 34:
            Test->TestTrue(TEXT("The host deploys from the board"), G->RequestDeploy(C, 0, 0));
            return Wait(1);
        case 35:
            Test->TestTrue(TEXT("The countdown runs"), S->DeployIn() > 0);
            CaptureHub(TEXT("hub-deploy-zh"), Shots);
            return Wait(3);
        case 36:
            if (S->Hub())
                return false;
            Test->TestTrue(TEXT("The deploy opened page 1-1"), S->Defense() && !S->Tutorial && S->Level == 0);
            Test->TestTrue(TEXT("The mission remembers it left the hub"), G->RunFromHub());
            WinPage(*B, P->UnitId);
            G->Tick(.01f);
            S->Publish();
            return Wait(1.6);
        case 37:
            Test->TestTrue(TEXT("The page is won"), S->Finished && S->Winner == 1);
            CaptureHub(TEXT("hub-results-zh"), Shots);
            return Wait(.4);
        case 38:
            G->ReturnToLobby(C);
            return Wait(1.2);
        case 39:
            Test->TestTrue(TEXT("The result card returns to the hub"), S->Hub());
            Test->TestEqual(TEXT("Captured every hub scene"), Shots, 19);
            Finish(GI);
            GI->LeaveRoom(false);
            return Wait(2);
        default:
            return true;
        }
    }

private:
    void Finish(UGWGameInstance* GI)
    {
        if (!Parked)
            return;
        Parked = false;
        ParkHubProfiles(false);
        if (auto* Settings = UGWUserSettings::Get())
        {
            Settings->ProfileSlot = OriginalSlot;
            Settings->SaveSettings();
        }
        if (GI)
            GI->ReloadProfile();
    }
    FAutomationTestBase* Test;
    double Start = 0, Next = 0;
    int32 Stage = 0, Shots = 0, OriginalSlot = 0;
    bool Parked = false;
};

class FHubHost : public IAutomationLatentCommand
{
public:
    explicit FHubHost(FAutomationTestBase* T) : Test(T), Start(FPlatformTime::Seconds()) {}
    bool Update() override
    {
        const double Now = FPlatformTime::Seconds();
        auto* W = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
        auto* C = W ? Cast<AGWController>(W->GetFirstPlayerController()) : nullptr;
        auto* S = W ? W->GetGameState<AGWGameState>() : nullptr;
        auto* G = W ? W->GetAuthGameMode<AGWGameMode>() : nullptr;
        auto* P = C ? C->GetPlayerState<AGWPlayerState>() : nullptr;
        auto* GI = W ? W->GetGameInstance<UGWGameInstance>() : nullptr;
        auto* H = C ? Cast<AGWHUD>(C->GetHUD()) : nullptr;
        if (Now - Start > 200)
        {
            Test->AddError(FString::Printf(TEXT("Hub host timed out at stage %d"), Stage));
            Finish(GI);
            return true;
        }
        if (!C || !S || !P || !H || Now < Next)
            return false;
        if (!S->IsRoom)
        {
            if (Stage == 0 && W->GetNetMode() != NM_ListenServer && Now >= Retry)
            {
                if (!Parked)
                {
                    ParkHubProfiles(true);
                    Parked = true;
                    if (GI)
                        GI->ReloadProfile();
                }
                GWText::SetCulture(TEXT("zh-Hans"));
                UGameplayStatics::OpenLevel(W, FName(TEXT("/Engine/Maps/Entry")), true, TEXT("listen?GWRoom=1?GWHub=1"));
                Retry = Now + 10;
            }
            return false;
        }
        if (!G)
            return false;
        auto Wait = [&](double Seconds)
        {
            Next = Now + Seconds;
            ++Stage;
            return false;
        };
        AGWPlayerState* Guest = nullptr;
        for (const auto& Item : S->PlayerArray)
            if (auto* Other = Cast<AGWPlayerState>(Item); Other && Other != P)
                Guest = Other;
        Battle* B = S->Simulation.Get();
        switch (Stage)
        {
        case 0:
            // The room opened straight into the hub; wait for the guest to walk in.
            Test->TestTrue(TEXT("A room opens into its hub"), S->Hub());
            if (!Guest || Guest->UnitId < 0)
                return false;
            return Wait(1.5);
        // Captures get stages of their own (the screenshot is taken at the end of the frame).
        case 1:
            CaptureHub(TEXT("hub-room-zh"), Shots);
            return Wait(.4);
        case 2:
            H->OpenPanel(AGWHUD::EPanel::Board);
            return Wait(.8);
        case 3:
            CaptureHub(TEXT("hub-room-board-zh"), Shots);
            return Wait(.4);
        case 4:
            H->OpenPanel(AGWHUD::EPanel::None);
            // AI teammates are asked for but never come along with a friend in the room.
            Test->TestTrue(TEXT("The host deploys"), G->RequestDeploy(C, 0, 3));
            return Wait(1);
        case 5:
            CaptureHub(TEXT("hub-room-deploy-zh"), Shots);
            return Wait(3);
        case 6:
        {
            if (S->Hub() || !B)
                return false;
            Test->TestTrue(TEXT("Everyone left the hub for page 1-1"), S->Defense() && B->humanCount() == 2);
            int32 Bots = 0;
            for (const auto& U : B->units)
                Bots += U.bot;
            Test->TestEqual(TEXT("No AI teammates with a friend in the room"), Bots, 0);
            FString Error;
            G->PreLogin(TEXT(""), TEXT("127.0.0.2"), FUniqueNetIdRepl(), Error);
            Test->TestEqual(TEXT("Nobody joins during a mission"), Error, FString(TEXT("ROOM_BUSY")));
            return Wait(2);
        }
        case 7:
            WinPage(*B, P->UnitId);
            G->Tick(.01f);
            S->Publish();
            return Wait(1.6);
        case 8:
            Test->TestTrue(TEXT("The page is won"), S->Finished && S->Winner == 1);
            CaptureHub(TEXT("hub-room-results-zh"), Shots);
            return Wait(.4);
        case 9:
        {
            G->ReturnToLobby(C);
            Test->TestTrue(TEXT("Everyone is back in the hub"), S->Hub());
            FString Error;
            G->PreLogin(TEXT(""), TEXT("127.0.0.2"), FUniqueNetIdRepl(), Error);
            Test->TestTrue(TEXT("The hub takes joins again"), Error.IsEmpty());
            return Wait(3);
        }
        case 10:
            // The host goes back to the title: the guest is sent home to their own hub.
            Test->TestEqual(TEXT("Captured every room scene"), Shots, 4);
            Finish(GI);
            UGameplayStatics::OpenLevel(W, FName(TEXT("/Engine/Maps/Entry")));
            return true;
        default:
            return true;
        }
    }

private:
    void Finish(UGWGameInstance* GI)
    {
        if (!Parked)
            return;
        Parked = false;
        ParkHubProfiles(false);
        if (GI)
            GI->ReloadProfile();
    }
    FAutomationTestBase* Test;
    double Start = 0, Next = 0, Retry = 0;
    int32 Stage = 0, Shots = 0;
    bool Parked = false;
};

class FHubClient : public IAutomationLatentCommand
{
public:
    explicit FHubClient(FAutomationTestBase* T) : Test(T), Start(FPlatformTime::Seconds()) {}
    bool Update() override
    {
        const double Now = FPlatformTime::Seconds();
        auto* W = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
        auto* C = W ? Cast<AGWController>(W->GetFirstPlayerController()) : nullptr;
        auto* S = W ? W->GetGameState<AGWGameState>() : nullptr;
        auto* P = C ? C->GetPlayerState<AGWPlayerState>() : nullptr;
        auto* GI = W ? W->GetGameInstance<UGWGameInstance>() : nullptr;
        if (Now - Start > 200)
        {
            Test->AddError(TEXT("Hub client timed out"));
            return true;
        }
        if (GI && GI->StatusKey == TEXT("Network.HostLeft"))
            SawHostLeft = true;
        if (!C || !S || !P)
            return false;
        if (!SawHub)
        {
            if (!S->IsRoom)
            {
                if (Now >= Retry && W->GetNetMode() != NM_Client)
                {
                    GEngine->Exec(W, TEXT("open 127.0.0.1"));
                    AskedAt = Now;
                    Retry = Now + 4;
                }
                return false;
            }
            // In the host's hub with a body of our own: the join is complete.
            if (!S->Hub() || !S->FindUnit(P->UnitId))
                return false;
            SawHub = true;
            const double Seconds = Now - AskedAt;
            UE_LOG(LogTemp, Display, TEXT("GW hub join: %.2f s from asking to standing in the host's hub (LAN)"), Seconds);
            Test->TestTrue(TEXT("The guest walks straight into the host's hub"), W->GetNetMode() == NM_Client && S->IsRoom);
            Test->TestTrue(TEXT("Joining takes well under a minute on LAN (Gate 3)"), Seconds < 60);
            Test->TestTrue(TEXT("An invited newcomer skips the tutorial"), !S->Tutorial);
            return false;
        }
        if (!SawMission)
        {
            SawMission = S->InMatch && S->Defense() && !S->Hub() && S->FindUnit(P->UnitId);
            return false;
        }
        if (!SawHubAgain)
        {
            SawHubAgain = S->Hub() && W->GetNetMode() == NM_Client;
            return false;
        }
        // The host left: the engine dropped this machine on the entry map and it opened its own hub, which is a
        // room of its own (a listen server friends can join; standalone only when it is offline).
        if (W->GetNetMode() == NM_Client || !S->Hub() || !S->FindUnit(P->UnitId))
            return false;
        Test->TestTrue(TEXT("The guest saw the mission"), SawMission);
        Test->TestTrue(TEXT("The guest saw everyone return to the hub"), SawHubAgain);
        Test->TestTrue(TEXT("The guest was told the host left"), SawHostLeft);
        Test->TestTrue(TEXT("The guest is in their own hub"), S->FindUnit(P->UnitId) != nullptr);
        return true;
    }

private:
    FAutomationTestBase* Test;
    double Start = 0, Retry = 0, AskedAt = 0;
    bool SawHub = false, SawMission = false, SawHubAgain = false, SawHostLeft = false;
};
} // namespace
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGWHubScenes, "GW.Hub.Scenes", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FGWHubScenes::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(FHubScenes(this));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGWHubHost, "GW.Net.HubHost", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FGWHubHost::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(FHubHost(this));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGWHubClient, "GW.Net.HubClient", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FGWHubClient::RunTest(const FString&)
{
    // The host leaving closes the connection, which the engine logs as an error: here it is the expected ending.
    // The host closing it is reported first (FailureReceived, its text localized), then the lost connection.
    AddExpectedMessagePlain(TEXT("Your connection to the host has been lost"), ELogVerbosity::Error, EAutomationExpectedMessageFlags::Contains, 0);
    AddExpectedMessagePlain(TEXT("BroadcastNetworkFailure: FailureType = FailureReceived"), ELogVerbosity::Error, EAutomationExpectedMessageFlags::Contains, 0);
    ADD_LATENT_AUTOMATION_COMMAND(FHubClient(this));
    return true;
}
#endif
