#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GWController.h"
#include "GWGameMode.h"
#include "GWGameState.h"
#include "GWPlayerState.h"
#include "GWText.h"
#include "GWWorld.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

// Uses the controller's existing stress driver, including its real prediction, RPC and fixed-step input path.
// Only initial placement is staged: the test walks through the doorway, rather than teleporting to the room centre.
class FGWDoorEntryCheck : public IAutomationLatentCommand
{
public:
    FGWDoorEntryCheck(FAutomationTestBase* InTest, bool InWaitAtDoor)
        : Test(InTest), WaitAtDoor(InWaitAtDoor), Started(FPlatformTime::Seconds()) {}
    ~FGWDoorEntryCheck()
    {
        if (ChangedCulture)
            GWText::SetCulture(PreviousCulture);
    }
    bool Update() override
    {
        const double Now = FPlatformTime::Seconds();
        auto* W = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
        auto* C = W ? Cast<AGWController>(W->GetFirstPlayerController()) : nullptr;
        auto* G = W ? W->GetAuthGameMode<AGWGameMode>() : nullptr;
        auto* S = W ? W->GetGameState<AGWGameState>() : nullptr;
        auto* P = C ? C->GetPlayerState<AGWPlayerState>() : nullptr;
        if (Now - Started > 35)
        {
            Test->AddError(TEXT("Door entry test timed out"));
            if (C) C->Autopilot = false;
            return true;
        }
        if (!C || !G || !S || !P || !C->WorldView)
            return false;
        if (Stage == 0)
        {
            PreviousCulture = GWText::Culture();
            ChangedCulture = true;
            GWText::SetCulture(TEXT("zh-Hans"));
            if (S->InMatch) S->Finished = true;
            if (!Test->TestTrue(TEXT("Expedition starts"), G->BeginExpedition(C, 0)))
                return true;
            auto& B = *S->Simulation;
            B.config.aiEnabled = false;
            for (int32 I = 0; I < static_cast<int32>(B.expedition.rooms.size()); ++I)
                if (B.expedition.rooms[I].kind == gwgame::RoomKind::Combat) { Room = I; break; }
            if (!Test->TestTrue(TEXT("Combat room has a door"), Room >= 0 && !B.expedition.rooms[Room].doors.empty()))
                return true;
            const auto& R = B.expedition.rooms[Room];
            Door = R.doors.front();
            DoorMiddle = B.maze.center(Door);
            const gwgame::Vec2 Middle{(R.x + R.w * .5) * B.maze.cellSize, (R.y + R.h * .5) * B.maze.cellSize};
            const int32 DX = Door % B.maze.width;
            Inward = DX >= R.x && DX < R.x + R.w ? gwgame::Vec2{0, Middle.y > DoorMiddle.y ? 1. : -1.}
                                                      : gwgame::Vec2{Middle.x > DoorMiddle.x ? 1. : -1., 0};
            auto* U = B.find(P->UnitId);
            U->position = DoorMiddle + Inward * (WaitAtDoor ? B.maze.cellSize * .5 + U->radius * .4 : -60);
            U->velocity = U->impulse = {};
            B.setInput(U->id, {});
            C->Autopilot = false;
            C->Prediction.valid = false;
            C->AutopilotLast = {};
            C->AutopilotDistance = 0;
            S->Publish();
            Stage = 1;
            Next = Now + .35;
            return false;
        }
        auto* B = S->Simulation.Get();
        auto* U = B ? B->find(P->UnitId) : nullptr;
        if (!B || !U) return false;
        U->hp = U->maxHp;
        auto Drive = [&]()
        {
            C->AutopilotGoal = DoorMiddle + Inward * 1000;
            C->AutopilotTurn = C->AutopilotAct = W->GetTimeSeconds() + 1000;
            C->Autopilot = true;
        };
        if (Stage == 1 && Now >= Next)
        {
            if (WaitAtDoor)
            {
                Test->TestTrue(TEXT("Body across threshold keeps the door open"), B->expedition.rooms[Room].state == gwgame::RoomState::Armed);
                Test->TestTrue(TEXT("Door remains non-solid during grace"), B->maze.walls[Door] == 0);
            }
            else
                Drive();
            Stage = 2;
        }
        if (Stage == 2 && B->expedition.rooms[Room].state == gwgame::RoomState::Locked)
        {
            Test->TestTrue(TEXT("Closed door leaves authority body clear"), B->maze.walkable(U->position, U->radius));
            Test->TestEqual(TEXT("Player stays inside the combat room"), B->roomAt(U->position), Room);
            AfterLock = U->position;
            Drive();
            Stage = 3;
            Next = Now + .65;
        }
        if (Stage == 3 && Now >= Next)
        {
            Test->TestTrue(TEXT("Real controller walks after door closure"), (U->position - AfterLock).length() > 60);
            Test->TestTrue(TEXT("Authoritative player remains free"), B->maze.walkable(U->position, U->radius));
            Test->TestTrue(TEXT("Prediction geometry received the closed door"),
                           Door < static_cast<int32>(C->WorldView->Geometry.walls.size()) && C->WorldView->Geometry.walls[Door] > 0);
            Test->TestTrue(TEXT("Predicted body also remains free"), C->WorldView->Geometry.walkable(C->Prediction.position, U->radius));
            Test->TestTrue(TEXT("Prediction follows the authority through closure"), (C->Prediction.position - U->position).length() < 90);
            Test->AddInfo(FString::Printf(TEXT("%s: walked %.1f units after closing, prediction error %.1f units"),
                                         WaitAtDoor ? TEXT("Grace expiry") : TEXT("Normal entry"),
                                         (U->position - AfterLock).length(), (C->Prediction.position - U->position).length()));
            C->Autopilot = false;
            B->setInput(U->id, {});
            const FString Path = FPaths::ProjectSavedDir() / TEXT("DoorRegression") /
                                 (WaitAtDoor ? TEXT("grace-expired-free.png") : TEXT("walked-through-free.png"));
            IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
            FScreenshotRequest::RequestScreenshot(Path, false, false);
            Stage = 4;
            Next = Now + .3;
        }
        return Stage == 4 && Now >= Next;
    }
private:
    FAutomationTestBase* Test;
    bool WaitAtDoor;
    bool ChangedCulture = false;
    FString PreviousCulture;
    double Started, Next = 0;
    int32 Stage = 0, Room = -1, Door = -1;
    gwgame::Vec2 Inward{}, DoorMiddle{}, AfterLock{};
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGWDoorEntryTest, "GW.Expedition.DoorEntry",
                                 EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FGWDoorEntryTest::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(FGWDoorEntryCheck(this, false));
    ADD_LATENT_AUTOMATION_COMMAND(FGWDoorEntryCheck(this, true));
    return true;
}
#endif
