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
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
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
            Test->TestTrue(TEXT("The player is born up on the platform"), S->Simulation->find(P->UnitId) && S->Simulation->find(P->UnitId)->deck);
            // A serpentine start on the small first page: two short walls make the road snake, the refused seal, a bolt
            // tower, a mortar and a tesla coil on open floor, barbed wire in the gap, and a ladder from the top platform.
            const int32 Painter = P->UnitId;
            int32 Drawn = 0;
            for (int32 Y = 1; Y <= 3; ++Y)
                Drawn += S->Simulation->paintWall(Painter, Cell(3, Y));
            for (int32 Y = 3; Y <= 5; ++Y)
                Drawn += S->Simulation->paintWall(Painter, Cell(7, Y));
            Test->TestEqual(TEXT("Six wall cells drawn"), Drawn, 6);
            Test->TestFalse(TEXT("Sealing the core is refused"),
                            S->Simulation->paintWall(Painter, Cell(3, 4)) && S->Simulation->paintWall(Painter, Cell(3, 5)));
            Test->TestFalse(TEXT("No tower on a block"), S->Simulation->buildTower(Painter, Cell(3, 2)));
            Test->TestTrue(TEXT("A bolt tower stands on open floor"), S->Simulation->buildTower(Painter, Cell(5, 1)));
            Test->TestTrue(TEXT("It grows a fifth side"), S->Simulation->buildTower(Painter, Cell(5, 1)));
            S->Simulation->find(Painter)->ink += 300;
            Test->TestTrue(TEXT("A mortar stands"), S->Simulation->buildTower(Painter, Cell(9, 5), static_cast<int32>(gwgame::TowerKind::Mortar)));
            Test->TestTrue(TEXT("A tesla coil stands"), S->Simulation->buildTower(Painter, Cell(5, 5), static_cast<int32>(gwgame::TowerKind::Tesla)));
            Test->TestTrue(TEXT("Barbed wire lies in the gap"), S->Simulation->buildTower(Painter, Cell(3, 5), static_cast<int32>(gwgame::TowerKind::Wire)));
            S->Simulation->find(Painter)->ink += 100;
            Test->TestTrue(TEXT("A ladder hangs from the platform"), S->Simulation->buildLadder(Painter, Cell(6, 0)));
            S->Simulation->advance(.2);
            S->Publish();
            return Wait(2.5);
        }
        case 7:
        {
            Test->TestTrue(TEXT("Three towers stand on open floor"),
                           S->Simulation->towerAt(Cell(5, 1)) > 0 && S->Simulation->towerAt(Cell(9, 5)) > 0 && S->Simulation->towerAt(Cell(5, 5)) > 0);
            Test->TestTrue(TEXT("The wire reached the client"), S->Wire.IsValidIndex(Cell(3, 5)) && S->Wire[Cell(3, 5)] == 15);
            Test->TestEqual(TEXT("The first page is a small box"), static_cast<int32>(S->GridWidth * S->GridHeight), 11 * 7);
            Test->TestTrue(TEXT("The client rebuilt the page from replicated cells"), C->WorldView->Geometry.width == S->GridWidth);
            Test->TestTrue(TEXT("The client knows the platform"), C->WorldView->Geometry.isDeck(Cell(0, 0)));
            Test->TestEqual(TEXT("The ladder reached the client"), S->Ladders.Num(), 1);
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
            // Wave one marches; the player watches from the platform's bottom edge, an AI teammate has fallen and
            // waits as an outline for someone's ink.
            Test->TestTrue(TEXT("Calling the wave early"), S->Simulation->callWave(P->UnitId));
            if (auto* U = S->Simulation->find(P->UnitId))
            {
                U->deck = true;
                U->position = S->Simulation->maze.center(Cell(4, 6));
                U->aim = {0, -1};
            }
            // Wave one is seven runners and the towers clear it in about eleven seconds: the check below comes well
            // before that (it once passed with one runner to spare, only because effects hitching on their first
            // spawn cost the simulation time; frames over a quarter second lose time).
            const int32 Mate = S->Simulation->addBot();
            S->Simulation->advance(5);
            S->Simulation->damageUnit(-1, Mate, 99999);
            if (auto* Fallen = S->Simulation->find(Mate))
                Fallen->position = S->Simulation->maze.center(Cell(5, 6));
            S->Simulation->advance(1.5);
            S->Publish();
            return Wait(1.2);
        }
        case 11:
            Test->TestEqual(TEXT("Wave one is on the page"), static_cast<int32>(S->Phase), static_cast<int32>(DefensePhase::Wave));
            Test->TestTrue(TEXT("Enemies are marching"), S->EnemiesLeft > 0);
            Capture(TEXT("defense-wave-zh"));
            return Wait(.4);
        case 12:
        {
            // The rocket launcher: a shell lobbed from the platform over the blocks, in mid-flight with its landing
            // ring showing (and the dotted arc it leaves behind as a guide).
            if (auto* U = S->Simulation->find(P->UnitId))
            {
                S->Simulation->pickupWeapon(U->id, RocketWeapon);
                U->aim = Vec2{1, -1.6}.normalized();
                S->Simulation->lob(*U, .55);
            }
            S->Simulation->advance(.32);
            S->Publish();
            return Wait(.05);
        }
        case 13:
        {
            bool Shell = false;
            for (const auto& Shot : S->Projectiles)
                Shell |= Shot.Weapon == LobWeapon;
            Test->TestTrue(TEXT("The lobbed shell reached the client"), Shell);
            Capture(TEXT("defense-rocket-zh"));
            return Wait(.4);
        }
        case 14:
        {
            // The local player a third of the way up the ladder (facing it, hands on the rails, 2026-11-17) with a
            // runner following below. The climb is slowed right down so the shot catches both on the rungs.
            S->Simulation->config.climbSeconds = 60;
            if (auto* U = S->Simulation->find(P->UnitId))
            {
                U->deck = false;
                U->climb = 0;
                U->climbUp = true;
                U->climbTime = .36;
            }
            const int32 Runner = S->Simulation->spawnWaveEnemy(Shape::Circle, false, 0);
            if (auto* M = S->Simulation->find(Runner))
            {
                M->climb = 0;
                M->climbUp = true;
                M->climbTime = .05;
            }
            S->Simulation->advance(.05);
            S->Publish();
            return Wait(.3);
        }
        case 15:
            Test->TestTrue(TEXT("Someone is on the ladder"), [&] {
                for (const auto& U : S->Units)
                    if (U.Alive && U.Climbing)
                        return true;
                return false;
            }());
            Capture(TEXT("defense-ladder-zh"));
            return Wait(.3);
        case 16:
        {
            // Skip to the boss: the last wave of the page with a fresh core (and ladders back at their own pace).
            S->Simulation->config.climbSeconds = .7;
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
            {
                U->deck = true;
                U->position = S->Simulation->maze.center(Cell(4, 0));
            }
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
        case 17:
            Test->TestTrue(TEXT("The boss closes the page"), BossId > 0 && S->BossActive);
            Capture(TEXT("defense-boss-zh"));
            return Wait(.3);
        case 18:
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
        case 19:
        {
            Test->TestTrue(TEXT("The last wave wins the page"), S->Finished && S->Winner == 1);
            const UGWProfileSave* Profile = GI->ActiveProfile();
            Test->TestTrue(TEXT("The clear is written to the profile"), Profile && Profile->Cleared(TEXT("1-1")));
            Test->TestTrue(TEXT("Stickers reached the profile"), Profile && Profile->Stickers >= 50);
            Test->TestTrue(TEXT("The next page opened"), Profile && Profile->Unlocked(1));
            Capture(TEXT("defense-victory-zh"));
            return Wait(.4);
        }
        case 20:
        {
            Test->TestTrue(TEXT("Retry from the result card"), G->BeginDefense(C, 1, 0));
            S->Simulation->defense.coreHealth = 1;
            S->Simulation->callWave(P->UnitId);
            for (int32 I = 0; I < 60 * 60 && !S->Simulation->finished; ++I)
                S->Simulation->advance(1. / 60);
            S->Publish();
            return Wait(1.5);
        }
        case 21:
            Test->TestTrue(TEXT("The core falling loses the page"), S->Finished && S->Winner == 2);
            Capture(TEXT("defense-defeat-zh"));
            return Wait(.4);
        case 22:
            // Chapter 2: page 2-1's platform is broken by rock in the middle of its long sides.
            Test->TestTrue(TEXT("Page 2-1 opens"), G->BeginDefense(C, 3, 0));
            return Wait(2.5);
        case 23:
            Test->TestTrue(TEXT("The client sees the broken rim"),
                           C->WorldView->Geometry.isDeck(Cell(3, 0)) && !C->WorldView->Geometry.isDeck(Cell(5, 0)) && C->WorldView->Geometry.isFixed(Cell(5, 0)));
            Capture(TEXT("defense-rim-zh"));
            return Wait(.4);
        case 24:
        {
            // Chapter 3: on page 3-2 raiders climb in over the outer rim while the player stands up there.
            S->Finished = true;
            Test->TestTrue(TEXT("Page 3-2 opens"), G->BeginDefense(C, 7, 0));
            Test->TestEqual(TEXT("Four raid gates"), static_cast<int32>(S->Simulation->defense.raids.size()), 4);
            S->Simulation->callWave(P->UnitId);
            S->Simulation->defense.queue.clear();
            if (auto* U = S->Simulation->find(P->UnitId))
            {
                U->deck = true;
                U->position = S->Simulation->maze.center(Cell(5, 0));
                U->hp = U->maxHp = 5000;
            }
            for (Shape Kind : {Shape::Circle, Shape::Circle, Shape::Triangle})
                Test->TestTrue(TEXT("A raider climbs in"), S->Simulation->spawnWaveEnemy(Kind, false, 0, true) > 0);
            for (int32 I = 0; I < 30; ++I)
                S->Simulation->advance(1. / 60);
            S->Publish();
            return Wait(1.5);
        }
        case 25:
            Test->TestTrue(TEXT("Raiders walk the platform"), [&] {
                for (const auto& U : S->Units)
                    if (U.Alive && U.Kind == 2 && U.Deck)
                        return true;
                return false;
            }());
            {
                gwgame::Maze Page;
                TArray<int32> Gates;
                int32 Core = -1;
                Test->TestTrue(TEXT("The client knows the raid gates are not tunnels"), S->BuildGeometry(Page, Gates, Core) && Gates.Num() == 2);
            }
            Capture(TEXT("defense-raid-zh"));
            return Wait(.4);
        // The expedition has its own rendered scenes now (GW.Expedition.Scenes, GWExpeditionPlaytests.cpp).
        case 26:
        case 27:
        case 28:
        case 29:
        case 30:
        case 31:
        case 32:
            return Wait(.05);
        case 33:
        {
            // The party playlist's rocket twist on a page: everyone starts with the launcher, and a tag says so. The raid
            // page in progress is called over first (Publish copies the simulation's state, so in this same stage).
            S->Finished = true;
            G->PendingTwist = 2;
            Test->TestTrue(TEXT("A twisted page opens"), G->BeginDefense(C, 0, 0));
            Test->TestEqual(TEXT("The twist is replicated"), static_cast<int32>(S->Twist), 2);
            const auto* U = S->Simulation->find(P->UnitId);
            Test->TestTrue(TEXT("Rocket party hands out launchers"), U && U->weapon == gwgame::RocketWeapon && U->perks.weapon == gwgame::RocketWeapon);
            Test->TestEqual(TEXT("The twist is used up"), static_cast<int32>(G->PendingTwist), 0);
            return Wait(2.5);
        }
        case 34:
            Capture(TEXT("defense-twist-zh"));
            return Wait(.4);
        default:
            S->Finished = true;
            G->ReturnToLobby(C);
            H->SetPage(AGWHUD::EPage::Main);
            RestoreProfiles(GI);
            Test->TestEqual(TEXT("Captured every defense scene"), Shots, 13);
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

// The build ring on page 1-1 (GWBuildRing.cpp): it opens on open floor (and not on rock), offers the four builds,
// takes the bolt tower, pops it up like jelly with its build cue, opens again on the tower with upgrade and rub
// out, grows it a side (the squash and stretch and the climbing bands), and greys out what the ink cannot pay
// for. Screenshots (ring-*, tower-*) land in Saved/DefenseVerification; profiles are parked as in the scenes above.
class FRingScenes : public IAutomationLatentCommand
{
public:
    explicit FRingScenes(FAutomationTestBase* T) : Test(T), Start(FPlatformTime::Seconds())
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
        if (Now - Start > 90)
        {
            Test->AddError(TEXT("Build ring scene timeout"));
            Finish(H, GI);
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
        const int32 Width = S->Simulation ? S->Simulation->maze.width : S->GridWidth;
        const int32 Spot = 3 * Width + 4, Rock = 2 * Width + 5, Spare = 1 * Width + 7;
        auto Me = [&]() { return S->Simulation ? S->Simulation->find(P->UnitId) : nullptr; };
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
                    UGWProfileSave::Delete(Slot);
                else if (IFileManager::Get().FileExists(*Path))
                    IFileManager::Get().Move(*Backup, *Path, true);
            }
            Parked = true;
            OriginalSlot = GI->ProfileSlot();
            GI->SelectProfile(0);
            return Wait(1);
        case 1:
            Test->TestTrue(TEXT("Solo opens page 1-1"), G->BeginDefense(C, 0, 0));
            if (auto* U = Me())
                U->ink += 300;
            S->Publish();
            // The tower cues set themselves up on first use (a hitch): each plays once by the core before the captures.
            for (const TCHAR* Name : {TEXT("NS_GW_TowerBuild"), TEXT("NS_GW_TowerUpgrade")})
                if (auto* System = LoadObject<UNiagaraSystem>(nullptr, *FString::Printf(TEXT("/Game/Sketch/VFX/%s.%s"), Name, Name)))
                    UNiagaraFunctionLibrary::SpawnSystemAtLocation(W, System, S->CorePosition);
            return Wait(1.8);
        case 2:
        {
            Test->TestFalse(TEXT("No ring on rock"), H->OpenRing(Rock));
            Test->TestTrue(TEXT("A ring opens on open floor"), H->OpenRing(Spot));
            const FGWUnitView* Local = S->FindUnit(P->UnitId);
            const auto Slots = Local ? H->RingSlots(S, *Local, Spot) : TArray<AGWHUD::FRingSlot>();
            Test->TestEqual(TEXT("Open floor offers the four builds"), Slots.Num(), 4);
            Test->TestTrue(TEXT("The bolt tower can be paid for"), Slots.Num() == 4 && Slots[0].Denied == 0);
            H->RingHoverOverride = 0;
            return Wait(.55);
        }
        case 3:
            Capture(TEXT("ring-open-zh"));
            return Wait(.25);
        case 4:
            H->TakeRingSlot(0);
            H->RingHoverOverride = -1;
            Test->TestTrue(TEXT("The ring folds away once a build is taken"), C->RingCell < 0);
            return Wait(.12);
        case 5:
            Capture(TEXT("tower-build-zh"));
            return Wait(.22);
        case 6:
            Capture(TEXT("tower-built-zh"));
            return Wait(1.2);
        case 7:
        {
            Test->TestTrue(TEXT("The bolt tower stands"), S->Simulation && S->Simulation->towerAt(Spot) > 0);
            Test->TestTrue(TEXT("The ring opens on the tower"), H->OpenRing(Spot));
            const FGWUnitView* Local = S->FindUnit(P->UnitId);
            const auto Slots = Local ? H->RingSlots(S, *Local, Spot) : TArray<AGWHUD::FRingSlot>();
            Test->TestTrue(TEXT("A tower offers upgrade and rub out"),
                           Slots.Num() == 2 && Slots[0].Kind == AGWHUD::ERingSlot::Upgrade && Slots[1].Kind == AGWHUD::ERingSlot::Remove);
            Test->TestTrue(TEXT("Rubbing out pays back what the tower cost"), Slots.Num() == 2 && Slots[1].Price >= S->TowerInk - 1);
            H->RingHoverOverride = 0;
            return Wait(.55);
        }
        case 8:
            Capture(TEXT("ring-tower-zh"));
            return Wait(.25);
        case 9:
            H->TakeRingSlot(0);
            H->RingHoverOverride = -1;
            return Wait(.07);
        case 10:
            Capture(TEXT("tower-upgrade-squash-zh"));
            return Wait(.2);
        case 11:
            Capture(TEXT("tower-upgrade-zh"));
            return Wait(1.2);
        case 12:
        {
            const int32 Tower = S->Simulation ? S->Simulation->towerAt(Spot) : -1;
            const auto* Grown = Tower >= 0 && S->Simulation ? S->Simulation->find(Tower) : nullptr;
            Test->TestTrue(TEXT("The tower grew a fifth side"), Grown && Grown->sides == 5);
            // Out of ink: the ring still opens, its builds greyed with the reason in the middle.
            if (auto* U = Me())
                U->ink = 5;
            S->Publish();
            return Wait(.3);
        }
        case 13:
        {
            Test->TestTrue(TEXT("A ring opens with no ink"), H->OpenRing(Spare));
            const FGWUnitView* Local = S->FindUnit(P->UnitId);
            const auto Slots = Local ? H->RingSlots(S, *Local, Spare) : TArray<AGWHUD::FRingSlot>();
            Test->TestTrue(TEXT("Every build is refused for ink"), Slots.Num() == 4 && Slots[0].Denied == 1 && Slots[3].Denied == 1);
            H->RingHoverOverride = 1;
            return Wait(.55);
        }
        case 14:
            Capture(TEXT("ring-noink-zh"));
            return Wait(.25);
        case 15:
            H->TakeRingSlot(1);
            Test->TestTrue(TEXT("A refused slot keeps the ring open"), C->RingCell == Spare);
            return Wait(.12);
        default:
            Test->TestTrue(TEXT("Captured every build ring scene"), Shots >= 7);
            Finish(H, GI);
            S->Finished = true;
            G->ReturnToLobby(C);
            return true;
        }
    }

private:
    void Finish(AGWHUD* H, UGWGameInstance* GI)
    {
        if (H)
        {
            H->RingHoverOverride = -1;
            H->CloseRing();
        }
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
} // namespace
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGWDefenseScenes, "GW.Defense.Scenes", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FGWDefenseScenes::RunTest(const FString& Parameters)
{
    ADD_LATENT_AUTOMATION_COMMAND(FDefenseScenes(this));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGWDefenseRing, "GW.Defense.Ring", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FGWDefenseRing::RunTest(const FString& Parameters)
{
    ADD_LATENT_AUTOMATION_COMMAND(FRingScenes(this));
    return true;
}
#endif
