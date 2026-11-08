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
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

// Phase 2 in a real two-process room over direct IP: GW.Net.PrankHost listens (rendered offscreen for the
// screenshots), GW.Net.PrankClient joins 127.0.0.1 headless. The client allows being thrown; the host grabs
// them, throws them onto open paper where they land as bait and pull enemies; the client rolls free, pours ink
// back to the host, the page is won with titles on the result card, and the host kicks the client.
// Screenshots land in Saved/DefenseVerification; the host's real profiles wait as *.sav.playtest meanwhile.
namespace
{
using namespace gwgame;
void ParkProfiles(bool Park)
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

class FPrankHost : public IAutomationLatentCommand
{
public:
    explicit FPrankHost(FAutomationTestBase* T) : Test(T), Start(FPlatformTime::Seconds()) {}
    bool Update() override
    {
        const double Now = FPlatformTime::Seconds();
        auto* W = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
        auto* C = W ? Cast<AGWController>(W->GetFirstPlayerController()) : nullptr;
        auto* S = W ? W->GetGameState<AGWGameState>() : nullptr;
        auto* G = W ? W->GetAuthGameMode<AGWGameMode>() : nullptr;
        auto* P = C ? C->GetPlayerState<AGWPlayerState>() : nullptr;
        auto* GI = W ? W->GetGameInstance<UGWGameInstance>() : nullptr;
        if (Now - Start > 180)
        {
            Test->AddError(FString::Printf(TEXT("Prank host timed out at stage %d"), Stage));
            Finish(GI);
            return true;
        }
        if (!C || !S || !P || Now < Next)
            return false;
        if (!S->IsRoom)
        {
            if (Stage == 0 && W->GetNetMode() != NM_ListenServer && Now >= Retry)
            {
                if (!Parked)
                {
                    ParkProfiles(true);
                    Parked = true;
                    if (GI)
                        GI->ReloadProfile();
                }
                GWText::SetCulture(TEXT("zh-Hans"));
                UGameplayStatics::OpenLevel(W, FName(TEXT("/Engine/Maps/Entry")), true, TEXT("listen?GWRoom=1"));
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
        auto Capture = [&](const TCHAR* Name)
        {
            const FString Path = FPaths::ProjectSavedDir() / TEXT("DefenseVerification") / (FString(Name) + TEXT(".png"));
            IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
            FScreenshotRequest::RequestScreenshot(Path, false, false);
            ++Shots;
        };
        AGWPlayerState* Guest = nullptr;
        for (const auto& Item : S->PlayerArray)
            if (auto* Other = Cast<AGWPlayerState>(Item); Other && Other != P)
                Guest = Other;
        Battle* B = S->Simulation.Get();
        switch (Stage)
        {
        case 0:
            // The guest has walked into the hub and allowed being thrown; the host keeps the room Friendly.
            if (!Guest || Guest->UnitId < 0 || !Guest->Throwable || !S->Hub())
                return false;
            Test->TestTrue(TEXT("The host sets the trolling level"), G->SetPrank(C, 1));
            return Wait(1.5);
        case 1:
            Capture(TEXT("prank-lobby-zh"));
            return Wait(.4);
        case 2:
        {
            Test->TestTrue(TEXT("The room deploys"), G->BeginDefense(C, 0, 0));
            B = S->Simulation.Get();
            if (!B || !Guest)
                return Wait(.1);
            GuestUnit = Guest->UnitId;
            HostUnit = P->UnitId;
            B->config.enemiesEnabled = false;
            for (auto& U : B->units)
                if (U.kind == UnitKind::Companion)
                    U.alive = false;
            Unit* Holder = B->find(HostUnit);
            Unit* Mate = B->find(GuestUnit);
            Test->TestTrue(TEXT("Both players are on the page"), Holder && Mate);
            if (!Holder || !Mate)
                return Wait(.1);
            Test->TestEqual(TEXT("Seats follow the room's join order"), Mate->seat, 1);
            Test->TestTrue(TEXT("The guest's permission reached the simulation"), Mate->throwable);
            Holder->position = B->maze.center(5 * B->maze.width + 4);
            Holder->aim = {1, 0};
            Mate->position = Holder->position + Vec2{90, 0};
            Test->TestEqual(TEXT("E grabs the willing teammate"), B->recruit(HostUnit), GuestUnit);
            Test->TestTrue(TEXT("The teammate rides in the holder's tail"), Mate->carried && Mate->followerOf == HostUnit);
            // A few drawn walls for the wall-artist title.
            for (int32 Y = 1; Y <= 3; ++Y)
                B->paintWall(HostUnit, Y * B->maze.width + 11);
            S->Publish();
            return Wait(1.5);
        }
        case 3:
            Capture(TEXT("prank-carried-zh"));
            return Wait(.4);
        case 4:
        {
            if (!B)
                return Wait(.1);
            Test->TestTrue(TEXT("Charging with a teammate in hand"), B->beginCharge(HostUnit));
            Test->TestEqual(TEXT("Q throws the teammate"), B->releaseCharge(HostUnit, {1, 0}), GuestUnit);
            ThrownAt = B->elapsed;
            S->Publish();
            return Wait(1.6);
        }
        case 5:
        {
            const Unit* Mate = B ? B->find(GuestUnit) : nullptr;
            Test->TestTrue(TEXT("The thrown teammate landed as bait"), Mate && Mate->bait > 0);
            if (!Mate)
                return Wait(.1);
            // Enemies around the bait: they leave their road and come for it.
            B->defense.phase = DefensePhase::Wave;
            B->defense.queue.clear();
            SpawnOrder Never;
            Never.at = 1e9;
            B->defense.queue.push_back(Never);
            B->defense.queued = 0;
            const Vec2 Spots[] = {{-420, -200}, {-380, 220}, {360, -260}, {420, 180}, {0, 430}};
            for (const Vec2& Offset : Spots)
            {
                const int32 Id = B->spawnWaveEnemy(Shape::Circle, false, 0);
                if (Unit* M = B->find(Id))
                {
                    M->position = B->maze.walkable(Mate->position + Offset, M->radius) ? Mate->position + Offset : Mate->position + Offset * .5;
                    M->hp = M->maxHp = 2000;
                }
            }
            S->Publish();
            return Wait(1.4);
        }
        case 6:
        {
            int32 Lured = 0;
            if (B)
                for (const auto& U : B->units)
                    Lured += U.alive && U.kind == UnitKind::Monster && U.lure == GuestUnit;
            Test->TestTrue(TEXT("The bait pulls the enemies around it"), Lured >= 3);
            Capture(TEXT("prank-bait-zh"));
            if (Unit* Holder = B ? B->find(HostUnit) : nullptr)
                Holder->ink = 100;
            return Wait(.4);
        }
        case 7:
        {
            // The guest rolls out of the bait and pours ink back to the host.
            const Unit* Mate = B ? B->find(GuestUnit) : nullptr;
            const Unit* Holder = B ? B->find(HostUnit) : nullptr;
            if (Mate && Mate->bait <= 0 && RolledAt < 0)
                RolledAt = B->elapsed;
            if (RolledAt >= 0 && Holder && Holder->ink > 100)
            {
                // Power-0 throws fly 1.1 s, then the bait lasts its full time unless the guest rolls out.
                Test->TestTrue(TEXT("The guest rolled free before the bait ran out"), RolledAt - ThrownAt < 1.1 + B->config.baitSeconds - .2);
                Test->TestTrue(TEXT("Ink poured from the guest reached the host"), Holder->ink > 100);
                return Wait(.1);
            }
            if (Now - Next > 12)
            {
                Test->AddError(TEXT("The guest neither rolled free nor poured ink"));
                return Wait(.1);
            }
            return false;
        }
        case 8:
        {
            // Win the page: the result card shows the titles of the run.
            if (!B)
                return Wait(.1);
            for (auto& U : B->units)
                if (U.kind == UnitKind::Monster)
                    U.alive = false;
            B->defense.queue.clear();
            B->defense.queued = 0;
            B->defense.wavesCleared = B->defense.waves - 1;
            B->advance(.1);
            G->Tick(.01f);
            S->Publish();
            return Wait(1.6);
        }
        case 9:
            Test->TestTrue(TEXT("The page is won"), S->Finished && S->Winner == 1);
            Capture(TEXT("prank-results-zh"));
            return Wait(.5);
        case 10:
            Test->TestTrue(TEXT("The host kicks the guest"), Guest && G->Kick(C, Guest->GetPlayerId()));
            return Wait(2);
        case 11:
            Test->TestEqual(TEXT("The kicked guest left the room"), S->PlayerArray.Num(), 1);
            Test->TestEqual(TEXT("Captured every prank scene"), Shots, 4);
            S->Finished = true;
            G->ReturnToLobby(C);
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
        ParkProfiles(false);
        if (GI)
            GI->ReloadProfile();
    }
    FAutomationTestBase* Test;
    double Start = 0, Next = 0, Retry = 0, ThrownAt = 0, RolledAt = -1;
    int32 Stage = 0, Shots = 0, HostUnit = -1, GuestUnit = -1;
    bool Parked = false;
};

class FPrankClient : public IAutomationLatentCommand
{
public:
    explicit FPrankClient(FAutomationTestBase* T) : Test(T), Start(FPlatformTime::Seconds()) {}
    bool Update() override
    {
        const double Now = FPlatformTime::Seconds();
        auto* W = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
        auto* C = W ? Cast<AGWController>(W->GetFirstPlayerController()) : nullptr;
        auto* S = W ? W->GetGameState<AGWGameState>() : nullptr;
        auto* P = C ? C->GetPlayerState<AGWPlayerState>() : nullptr;
        if (Now - Start > 180)
        {
            Test->AddError(TEXT("Prank client timed out"));
            return true;
        }
        if (!C || !S || !P)
            return false;
        if (!S->IsRoom)
        {
            // Back on the entry map after being kicked: everything the guest went through is checked here.
            if (Joined)
            {
                Test->TestTrue(TEXT("The guest saw themselves carried"), SawCarried);
                Test->TestTrue(TEXT("The guest saw themselves fly"), SawFlying);
                Test->TestTrue(TEXT("The guest saw themselves lying as bait"), SawBait);
                Test->TestTrue(TEXT("The guest rolled out of the bait"), SawRolled);
                Test->TestTrue(TEXT("The guest poured ink"), Poured > 0);
                return true;
            }
            if (Now >= Retry && W->GetNetMode() != NM_Client)
            {
                GEngine->Exec(W, TEXT("open 127.0.0.1"));
                Retry = Now + 4;
            }
            return false;
        }
        if (!Joined)
        {
            C->SendThrowable(true);
            C->ServerAction(1, 0);
            Joined = true;
            return false;
        }
        const FGWUnitView* Me = S->InMatch ? S->FindUnit(P->UnitId) : nullptr;
        if (!Me)
            return false;
        SawCarried |= Me->Carried;
        SawFlying |= Me->State == static_cast<uint8>(UnitState::Launched);
        SawBait |= Me->Bait > 0;
        // Lie as bait long enough for the host to see the pull, roll out (E), then pour ink back to the host.
        if (Me->Bait > 0 && S->BaitSeconds - Me->Bait > 4 && Now >= NextAction)
        {
            C->ServerAction(5, 0);
            NextAction = Now + .4;
        }
        if (SawBait && Me->Bait <= 0 && !SawRolled)
            SawRolled = true;
        if (SawRolled && Poured < 10 && Now >= NextAction)
            for (const auto& Item : S->PlayerArray)
                if (const auto* Other = Cast<AGWPlayerState>(Item); Other && Other->Identity == S->HostIdentity)
                {
                    C->ServerPour(Other->UnitId);
                    ++Poured;
                    NextAction = Now + .3;
                }
        return false;
    }

private:
    FAutomationTestBase* Test;
    double Start = 0, Retry = 0, NextAction = 0;
    bool Joined = false, SawCarried = false, SawFlying = false, SawBait = false, SawRolled = false;
    int32 Poured = 0;
};
} // namespace
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGWPrankHost, "GW.Net.PrankHost", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FGWPrankHost::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(FPrankHost(this));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGWPrankClient, "GW.Net.PrankClient", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FGWPrankClient::RunTest(const FString&)
{
    // Being kicked closes the connection, which the engine logs as an error: here it is the expected ending.
    AddExpectedMessagePlain(TEXT("Your connection to the host has been lost"), ELogVerbosity::Error, EAutomationExpectedMessageFlags::Contains, 0);
    ADD_LATENT_AUTOMATION_COMMAND(FPrankClient(this));
    return true;
}
#endif
