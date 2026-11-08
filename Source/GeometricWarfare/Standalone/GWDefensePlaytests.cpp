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
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

// Rendered acceptance scenes for line defense: the new title menu, mission board and profile page, a page in
// its build phase with a drawn maze and pinned turret, a wave on the march, the boss with its laser, and the
// result cards. The player's real profiles wait beside their slots (*.sav.playtest) while it runs.
// Screenshots land in Saved/DefenseVerification.
namespace
{
using namespace gwgame;
class FDefenseScenes : public IAutomationLatentCommand
{
public:
    explicit FDefenseScenes(FAutomationTestBase* T) : Test(T), Start(FPlatformTime::Seconds())
    {
    }
    bool Update() override
    {
        auto* W = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
        auto* C = W ? Cast<AGWController>(W->GetFirstPlayerController()) : nullptr;
        auto* G = W ? W->GetAuthGameMode<AGWGameMode>() : nullptr;
        auto* S = W ? W->GetGameState<AGWGameState>() : nullptr;
        auto* P = C ? C->GetPlayerState<AGWPlayerState>() : nullptr;
        auto* GI = W ? W->GetGameInstance<UGWGameInstance>() : nullptr;
        auto* H = C ? Cast<AGWHUD>(C->GetHUD()) : nullptr;
        const double Now = FPlatformTime::Seconds();
        if (Now - Start > 150)
        {
            Test->AddError(TEXT("Defense scene timeout"));
            RestoreProfiles(GI);
            return true;
        }
        if (!C || !G || !S || !P || !GI || !H || !C->WorldView || Now < Next)
            return false;
        auto Capture = [&](const TCHAR* Name)
        {
            const FString Path = FPaths::ProjectSavedDir() / TEXT("DefenseVerification") / (FString(Name) + TEXT(".png"));
            IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
            FScreenshotRequest::RequestScreenshot(Path, false, false);
            ++Shots;
        };
        auto Wait = [&](double Seconds)
        {
            Next = Now + Seconds;
            ++Stage;
            return false;
        };
        auto Cell = [&](int32 X, int32 Y) { return Y * S->Simulation->maze.width + X; };
        switch (Stage)
        {
        case 0:
            GWText::SetCulture(TEXT("zh-Hans"));
            if (S->InMatch)
            {
                S->Finished = true;
                G->ReturnToLobby(C);
            }
            for (int32 Slot = 0; Slot < UGWProfileSave::SlotCount; ++Slot)
            {
                const FString Path = UGWProfileSave::SlotPath(Slot), Backup = Path + TEXT(".playtest");
                if (IFileManager::Get().FileExists(*Backup))
                    UGWProfileSave::Delete(Slot); // left over from an interrupted playtest
                else if (IFileManager::Get().FileExists(*Path))
                    IFileManager::Get().Move(*Backup, *Path, true);
            }
            Parked = true;
            OriginalSlot = GI->ProfileSlot();
            GI->SelectProfile(0);
            H->SetPage(AGWHUD::EPage::Main);
            return Wait(1.5);
        case 1:
            Capture(TEXT("menu-main-zh"));
            return Wait(.4);
        // The mission board is a hub station now; GW.Hub.Scenes captures it.
        case 2:
        case 3:
            return Wait(.05);
        case 4:
            H->SetPage(AGWHUD::EPage::Saves);
            return Wait(1);
        case 5:
            Capture(TEXT("profiles-zh"));
            return Wait(.4);
        case 6:
        {
            Test->TestTrue(TEXT("Solo opens page 1-1"), G->BeginDefense(C, 0, 0));
            Test->TestTrue(TEXT("Defense rule set is replicated"), S->Defense());
            Test->TestTrue(TEXT("The player has a unit beside the core"), P->UnitId > 0);
            int32 Bots = 0;
            for (const auto& U : S->Simulation->units)
                Bots += U.bot;
            Test->TestEqual(TEXT("Solo brings no AI teammates by default"), Bots, 0);
            // A serpentine start: two drawn walls force the route to snake, plus a pinned square turret.
            const int32 Painter = P->UnitId;
            int32 Drawn = 0;
            for (int32 Y = 1; Y <= 7; ++Y)
                Drawn += S->Simulation->paintWall(Painter, Cell(5, Y));
            for (int32 Y = 3; Y <= 9; ++Y)
                Drawn += S->Simulation->paintWall(Painter, Cell(11, Y));
            Test->TestEqual(TEXT("Fourteen wall cells drawn"), Drawn, 14);
            Test->TestFalse(TEXT("Sealing the core is refused"),
                            S->Simulation->paintWall(Painter, Cell(5, 8)) && S->Simulation->paintWall(Painter, Cell(5, 9)));
            gwgame::Unit Square;
            Square.id = 9900;
            Square.kind = UnitKind::Companion;
            Square.state = UnitState::Idle;
            Square.sides = 4;
            Square.shape = Shape::Square;
            Square.maxHp = Square.hp = 220 * sideHealth(4);
            Square.position = S->Simulation->maze.center(Cell(8, 5));
            S->Simulation->units.push_back(Square);
            gwgame::Unit Pent = Square;
            Pent.id = 9901;
            Pent.sides = 5;
            Pent.shape = Shape::Pentagon;
            Pent.position = S->Simulation->maze.center(Cell(13, 2));
            S->Simulation->units.push_back(Pent);
            S->Simulation->advance(.2);
            S->Publish();
            return Wait(2.5);
        }
        case 7:
        {
            const auto* Turret = S->Simulation->find(9900);
            Test->TestTrue(TEXT("A resting square pins itself as a turret"), Turret && Turret->turret);
            Test->TestTrue(TEXT("The client rebuilt the page from replicated cells"), C->WorldView->Geometry.width == S->GridWidth);
            Capture(TEXT("defense-build-zh"));
            return Wait(.4);
        }
        case 8:
            GWText::SetCulture(TEXT("en"));
            return Wait(.6);
        case 9:
            Capture(TEXT("defense-build-en"));
            GWText::SetCulture(TEXT("zh-Hans"));
            return Wait(.3);
        case 10:
        {
            // Wave one marches; the player stands in the corridor by the first wall.
            Test->TestTrue(TEXT("Calling the wave early"), S->Simulation->callWave(P->UnitId));
            if (auto* U = S->Simulation->find(P->UnitId))
            {
                U->position = S->Simulation->maze.center(Cell(4, 6));
                U->aim = {-1, 0};
            }
            S->Simulation->advance(9);
            S->Publish();
            return Wait(2);
        }
        case 11:
            Test->TestEqual(TEXT("Wave one is on the page"), static_cast<int32>(S->Phase), static_cast<int32>(DefensePhase::Wave));
            Test->TestTrue(TEXT("Enemies are marching"), S->EnemiesLeft > 0);
            Capture(TEXT("defense-wave-zh"));
            return Wait(.4);
        case 12:
        {
            // Skip to the boss: the last wave of the page with a fresh core.
            auto& D = S->Simulation->defense;
            for (auto& U : S->Simulation->units)
                if (U.kind == UnitKind::Monster)
                    U.alive = false;
            D.queue.clear();
            D.queued = 0;
            S->Simulation->advance(.1);
            D.wavesCleared = D.waves - 1;
            D.coreHealth = D.coreMaxHealth;
            D.phase = DefensePhase::Build;
            S->Simulation->callWave(P->UnitId);
            S->Simulation->advance(9.5);
            if (auto* U = S->Simulation->find(P->UnitId))
                U->position = S->Simulation->maze.center(Cell(3, 4));
            for (auto& U : S->Simulation->units)
                if (U.kind == UnitKind::Monster && U.shape == Shape::Trapezoid)
                {
                    U.position = S->Simulation->maze.center(Cell(4, 5));
                    U.laserCooldown = .05;
                    U.cooldown = 30;
                    BossId = U.id;
                }
            S->Simulation->advance(.6);
            S->Publish();
            return Wait(1.5);
        }
        case 13:
            Test->TestTrue(TEXT("The boss closes the page"), BossId > 0 && S->BossActive);
            Capture(TEXT("defense-boss-zh"));
            return Wait(.3);
        case 14:
        {
            // Victory: every enemy gone on the last wave; rewards reach this player's own profile.
            for (auto& U : S->Simulation->units)
                if (U.kind == UnitKind::Monster)
                    U.alive = false;
            S->Simulation->defense.queue.clear();
            S->Simulation->defense.queued = 0;
            S->Simulation->advance(.1);
            G->Tick(.01f);
            S->Publish();
            return Wait(1.5);
        }
        case 15:
        {
            Test->TestTrue(TEXT("The last wave wins the page"), S->Finished && S->Winner == 1);
            const UGWProfileSave* Profile = GI->ActiveProfile();
            Test->TestTrue(TEXT("The clear is written to the profile"), Profile && Profile->Cleared(TEXT("1-1")));
            Test->TestTrue(TEXT("Stickers reached the profile"), Profile && Profile->Stickers >= 50);
            Test->TestTrue(TEXT("The next page opened"), Profile && Profile->Unlocked(1));
            Capture(TEXT("defense-victory-zh"));
            return Wait(.4);
        }
        case 16:
        {
            Test->TestTrue(TEXT("Retry from the result card"), G->BeginDefense(C, 1, 0));
            S->Simulation->defense.coreHealth = 1;
            S->Simulation->callWave(P->UnitId);
            for (int32 I = 0; I < 60 * 60 && !S->Simulation->finished; ++I)
                S->Simulation->advance(1. / 60);
            S->Publish();
            return Wait(1.5);
        }
        case 17:
            Test->TestTrue(TEXT("The core falling loses the page"), S->Finished && S->Winner == 2);
            Capture(TEXT("defense-defeat-zh"));
            return Wait(.4);
        default:
            S->Finished = true;
            G->ReturnToLobby(C);
            H->SetPage(AGWHUD::EPage::Main);
            RestoreProfiles(GI);
            Test->TestEqual(TEXT("Captured every defense scene"), Shots, 8);
            return true;
        }
    }

private:
    void RestoreProfiles(UGWGameInstance* GI)
    {
        if (!Parked)
            return;
        Parked = false;
        for (int32 Slot = 0; Slot < UGWProfileSave::SlotCount; ++Slot)
        {
            const FString Path = UGWProfileSave::SlotPath(Slot), Backup = Path + TEXT(".playtest");
            UGWProfileSave::Delete(Slot);
            if (IFileManager::Get().FileExists(*Backup))
                IFileManager::Get().Move(*Path, *Backup, true);
        }
        // The player's own slot choice comes back too, and the profile is re-read without writing anything.
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
    int32 Stage = 0, Shots = 0, BossId = -1, OriginalSlot = 0;
    bool Parked = false;
};
} // namespace
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGWDefenseScenes, "GW.Defense.Scenes", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FGWDefenseScenes::RunTest(const FString& Parameters)
{
    ADD_LATENT_AUTOMATION_COMMAND(FDefenseScenes(this));
    return true;
}
#endif
