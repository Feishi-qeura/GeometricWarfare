#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GWController.h"
#include "GWFx.h"
#include "GWGameInstance.h"
#include "GWGameMode.h"
#include "GWCinematic.h"
#include "GWGameState.h"
#include "GWHUD.h"
#include "GWMotion.h"
#include "GWPlayerState.h"
#include "GWSaveGame.h"
#include "GWText.h"
#include "GWWorld.h"
#include "HAL/FileManager.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "UnrealClient.h"

// Rendered acceptance scenes for the sketchbook presentation: every title page, the arena with its AI
// teammates, every skill cue, upgrade cards, settings with save slots, a real save and load, and results.
// Screenshots land in Saved/SketchVerification.
namespace
{
using namespace gwgame;
class FSketchScenes : public IAutomationLatentCommand
{
public:
    explicit FSketchScenes(FAutomationTestBase* T) : Test(T), Start(FPlatformTime::Seconds())
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
        if (Now - Start > 120)
        {
            Test->AddError(TEXT("Sketch scene timeout"));
            RestoreSaves(GI);
            return true;
        }
        if (!C || !G || !S || !P || !GI || !H || !C->WorldView)
            return false;
        if (Now < Next)
            return false;
        // Title pages include the Slate text fields, so those captures keep the UI layer.
        auto Capture = [&](const TCHAR* Name, bool WithFields = false)
        {
            const FString Path = FPaths::ProjectSavedDir() / TEXT("SketchVerification") / (FString(Name) + TEXT(".png"));
            IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
            FScreenshotRequest::RequestScreenshot(Path, WithFields, false);
            ++Shots;
        };
        auto Wait = [&](double Seconds)
        {
            Next = Now + Seconds;
            ++Stage;
            return false;
        };
        switch (Stage)
        {
        case 0:
            GWText::SetCulture(TEXT("zh-Hans"));
            if (S->InMatch)
            {
                S->Finished = true;
                G->ReturnToLobby(C);
            }
            // The run is saved to slot 1 and loaded back; the player's own saves wait beside the slots.
            for (int32 Slot = 0; Slot < UGWRunSave::SlotCount; ++Slot)
            {
                const FString Path = UGWRunSave::SlotPath(Slot), Backup = Path + TEXT(".playtest");
                if (IFileManager::Get().FileExists(*Backup))
                    UGWRunSave::DeleteSlot(Slot); // left over from an interrupted playtest
                else if (IFileManager::Get().FileExists(*Path))
                    IFileManager::Get().Move(*Backup, *Path, true);
            }
            Parked = true;
            GI->RefreshSaves();
            H->SetPage(AGWHUD::EPage::Main);
            return Wait(1.5);
        case 1:
            Capture(TEXT("menu-main-zh"));
            return Wait(.5);
        // Stages 2-5 captured the room browser, which the hub replaced (phase 3); its scenes are in
        // GW.Hub.Scenes.
        case 2:
        case 3:
        case 4:
        case 5:
            return Wait(.05);
        case 6:
            H->SetPage(AGWHUD::EPage::Intro);
            return Wait(.8);
        case 7:
            Capture(TEXT("menu-intro-zh"));
            return Wait(.4);
        case 8:
            GWText::SetCulture(TEXT("en"));
            return Wait(.5);
        case 9:
            Capture(TEXT("menu-intro-en"));
            return Wait(.4);
        case 10:
        {
            GWText::SetCulture(TEXT("zh-Hans"));
            H->SetPage(AGWHUD::EPage::Main);
            Test->TestTrue(TEXT("Single player starts"), G->BeginRound(C, true));
            auto& B = *S->Simulation;
            int32 Bots = 0;
            for (const auto& U : B.units)
                Bots += U.bot;
            Test->TestEqual(TEXT("Single player brings three AI teammates"), Bots, 3);
            B.config.aiEnabled = B.config.enemiesEnabled = B.config.bossEnabled = false;
            auto* Head = B.find(P->UnitId);
            Head->position = {6200, 6200};
            Head->hp = Head->maxHp = 900;
            Head->hp = 620;
            B.pickupWeapon(Head->id, 4);
            int32 Index = 0;
            for (auto& U : B.units)
                if (U.bot)
                {
                    const double A = Pi * .5 + Index++ * 2 * Pi / 3;
                    U.position = Head->position + Vec2{std::cos(A), std::sin(A)} * 180;
                    U.aim = Vec2{std::cos(A), std::sin(A)};
                    U.hp = U.maxHp - 80. * Index;
                }
            for (int32 I = 0; I < 3; ++I)
            {
                Unit A;
                A.id = 3000 + I;
                A.kind = UnitKind::Companion;
                A.state = UnitState::Idle;
                A.shape = static_cast<Shape>(I + 1);
                A.position = Head->position + Vec2{-70. - I * 60, 10. * I};
                B.units.push_back(A);
                B.recruit(Head->id);
            }
            Unit Idle;
            Idle.id = 3100;
            Idle.kind = UnitKind::Companion;
            Idle.state = UnitState::Idle;
            Idle.shape = Shape::Triangle;
            Idle.position = Head->position + Vec2{40, 150};
            B.units.push_back(Idle);
            for (int32 I = 0; I < 9; ++I)
            {
                Unit M;
                M.id = 4000 + I;
                M.team = 2;
                M.kind = UnitKind::Monster;
                M.shape = static_cast<Shape>(I % 4);
                M.radius = I == 4 ? 55 : 27;
                M.hp = M.maxHp = 5000;
                const double A = I * 2 * Pi / 9 + .3;
                M.position = Head->position + Vec2{std::cos(A), std::sin(A)} * (I == 4 ? 560. : 420.);
                M.aim = (Head->position - M.position).normalized();
                M.burnTime = I % 3 == 0 ? 60 : 0;
                M.burnDamage = 0.01;
                M.burnOwner = Head->id;
                M.slowTime = I % 3 == 1 ? 60 : 0;
                B.units.push_back(M);
            }
            Unit Boss;
            Boss.id = 4900;
            Boss.team = 2;
            Boss.kind = UnitKind::Monster;
            Boss.shape = Shape::Rectangle;
            Boss.radius = 110;
            Boss.hp = Boss.maxHp = 90000;
            Boss.position = Head->position + Vec2{-650, -420};
            Boss.aim = (Head->position - Boss.position).normalized();
            // A prop for the diorama: without this its first slam (1.5 s) kills the 60-HP teammate the
            // friendly-push and skill-pick checks below rely on (the telegraph cue is staged in case 14).
            Boss.cooldown = 600;
            B.units.push_back(Boss);
            B.crates.push_back({901, 3, Head->position + Vec2{-360, 260}, 0, 50, true});
            for (int32 I = 0; I < 3; ++I)
            {
                Projectile Shot;
                Shot.id = 60000 + I;
                Shot.owner = Head->id;
                Shot.squad = Head->id;
                Shot.team = 1;
                Shot.weapon = 4;
                const double A = -.5 + I * .3;
                Shot.position = Head->position + Vec2{std::cos(A), std::sin(A)} * (90. + I * 25);
                Shot.velocity = Vec2{std::cos(A), std::sin(A)} * 50.;
                Shot.remaining = 30;
                Shot.pierce = 100;
                B.projectiles.push_back(Shot);
            }
            S->Publish();
            return Wait(1.8);
        }
        case 11:
            Capture(TEXT("evolution-diorama"));
            return Wait(.3);
        case 12:
        {
            // Friendly fire: a real sniper round through an AI teammate pushes it and shows the shove cue.
            auto& B = *S->Simulation;
            auto* Head = B.find(P->UnitId);
            Unit* Mate = nullptr;
            for (auto& U : B.units)
                if (U.bot)
                    Mate = &U;
            Test->TestNotNull(TEXT("An AI teammate exists"), Mate);
            if (Mate)
            {
                MateId = Mate->id;
                MateHP = Mate->hp;
                MateStart = Mate->position;
                Projectile Round;
                Round.id = 61000;
                Round.owner = Head->id;
                Round.squad = Head->id;
                Round.team = 1;
                Round.weapon = 3;
                Round.damage = 95;
                Round.position = Mate->position - Vec2{60, 0};
                Round.velocity = {2400, 0};
                Round.remaining = .5;
                B.projectiles.push_back(Round);
                B.advance(.25);
            }
            S->Publish();
            return Wait(.12);
        }
        case 13:
        {
            auto& B = *S->Simulation;
            if (const Unit* Mate = B.find(MateId))
            {
                Test->TestEqual(TEXT("Friendly bullet deals no damage"), Mate->hp, MateHP);
                Test->TestTrue(TEXT("Friendly bullet pushes along the shot"), Mate->position.x > MateStart.x + 20);
            }
            Capture(TEXT("evolution-friendly-push"));
            return Wait(.3);
        }
        case 14:
        {
            // Every skill cue in one frame: fragment, combustion, kill blast, frost nova, chain, spread, voltage, telegraph.
            auto& B = *S->Simulation;
            auto* Head = B.find(P->UnitId);
            const Vec2 Hp = Head->position;
            auto Ring = [&](Vec2 At, double Radius, EffectStyle Style, int Kind = 1)
            {
                Effect E{At, At, Radius, .5, Kind, 1};
                E.style = Style;
                B.addEffect(E);
            };
            Ring(Hp + Vec2{300, -150}, 160, EffectStyle::Fragment);
            Ring(Hp + Vec2{-300, 120}, 180, EffectStyle::Combustion);
            Ring(Hp + Vec2{80, 300}, 180, EffectStyle::KillBlast);
            Ring(Hp + Vec2{-120, -330}, 210, EffectStyle::FrostNova, 2);
            Ring(Hp + Vec2{-650, -420}, 320, EffectStyle::Telegraph, 5);
            Effect Chain{Hp + Vec2{360, 0}, Hp + Vec2{250, 260}, 0, .3, 2, 1};
            Chain.style = EffectStyle::Chain;
            B.addEffect(Chain);
            Effect Spread{Hp + Vec2{-360, 0}, Hp + Vec2{-250, -260}, 0, .3, 2, 1};
            Spread.style = EffectStyle::Spread;
            B.addEffect(Spread);
            B.event(EffectStyle::Voltage, Hp, Hp, 1, 210);
            S->Publish();
            return Wait(.16);
        }
        case 15:
            Capture(TEXT("evolution-skills"));
            return Wait(.5);
        case 16:
        {
            auto& B = *S->Simulation;
            auto* Head = B.find(P->UnitId);
            Effect Slam{Head->position + Vec2{-500, -300}, Head->position + Vec2{-500, -300}, 320, .45, 1, 2};
            Slam.style = EffectStyle::BossSlam;
            B.addEffect(Slam);
            B.event(EffectStyle::Launch, Head->position, Head->position + Vec2{100, 0}, 1, 1);
            B.event(EffectStyle::PickupWeapon, Head->position + Vec2{-360, 260}, Head->position, 1, 3);
            for (int32 I = 4000; I < 4004; ++I)
                B.damageUnit(Head->id, I, 99999);
            S->Publish();
            return Wait(.2);
        }
        case 17:
            Capture(TEXT("evolution-impacts"));
            Test->TestTrue(TEXT("Niagara director spawned skill systems"),
                           C->WorldView->Effects() && C->WorldView->Effects()->SpawnCount() > 10);
            Test->TestTrue(TEXT("Status loops follow burning and frozen units"),
                           C->WorldView->Effects() && C->WorldView->Effects()->LoopCount() >= 3);
            return Wait(.3);
        case 18:
        {
            auto& B = *S->Simulation;
            B.addExperience(B.sharedLevel * 100);
            auto* Head = B.find(P->UnitId);
            Head->skills[4] = 2;
            Head->skills[8] = 1;
            Head->choices = {{5, 9, 16}};
            int32 BotRanks = 0;
            for (const auto& U : B.units)
                if (U.bot)
                    for (int Rank : U.skills)
                        BotRanks += Rank;
            Test->TestTrue(TEXT("AI teammates picked their own skill"), BotRanks >= 3);
            S->Publish();
            return Wait(.9);
        }
        case 19:
            Capture(TEXT("upgrade-zh"));
            return Wait(.3);
        case 20:
            // Language switches only after the previous capture has been written.
            GWText::SetCulture(TEXT("en"));
            return Wait(.4);
        case 21:
            Capture(TEXT("upgrade-en"));
            return Wait(.3);
        case 22:
            Test->TestTrue(TEXT("Pick an upgrade"), S->Simulation->chooseUpgrade(P->UnitId, 2));
            S->Publish();
            GWText::SetCulture(TEXT("zh-Hans"));
            return Wait(.8);
        case 23:
            Capture(TEXT("evolution-hud-zh"));
            return Wait(.3);
        case 24:
            H->ToggleSettings();
            return Wait(.5);
        case 25:
            Capture(TEXT("settings-zh"));
            return Wait(.3);
        case 26:
            SavedLevel = S->SharedLevel;
            SavedElapsed = S->Simulation->elapsed;
            C->ServerAction(9, 0);
            return Wait(.4);
        case 27:
        {
            Test->TestEqual(TEXT("Saving reports success"), C->NoticeKey, FString(TEXT("Save.Saved")));
            const UGWRunSave* Save = UGWRunSave::ReadSlot(0);
            Test->TestNotNull(TEXT("Slot 1 holds the run"), Save);
            if (Save)
            {
                Test->TestEqual(TEXT("Offline save"), static_cast<int32>(Save->Origin), static_cast<int32>(EGWSaveOrigin::Offline));
                Test->TestEqual(TEXT("Save keeps the team level"), Save->TeamLevel, SavedLevel);
                Test->TestEqual(TEXT("Save keeps the AI teammates"), Save->Bots, 3);
                Test->TestEqual(TEXT("Save records the local player by account"), Save->Players.Num(), 1);
            }
            H->ToggleSettings();
            return Wait(.4);
        }
        case 28:
        {
            auto& B = *S->Simulation;
            B.elapsed = B.config.duration - .02;
            B.advance(.1);
            S->Publish();
            return Wait(.9);
        }
        case 29:
            Capture(TEXT("result-zh"));
            return Wait(.3);
        case 30:
            S->Finished = true;
            G->ReturnToLobby(C);
            H->SetPage(AGWHUD::EPage::Saves);
            return Wait(1);
        case 31:
            Capture(TEXT("menu-saves-zh"));
            return Wait(.4);
        case 32:
        {
            // Standalone runs the server action on the spot, so the restored run is checked before it ticks on.
            C->ServerAction(10, 0);
            Test->TestTrue(TEXT("Loading a save resumes the run"), S->InMatch && S->Simulation.IsValid());
            if (S->Simulation)
            {
                const auto& B = *S->Simulation;
                Test->TestEqual(TEXT("Loaded team level"), B.sharedLevel, SavedLevel);
                Test->TestTrue(TEXT("Loaded clock"), FMath::Abs(B.elapsed - SavedElapsed) < .05);
                const Unit* Head = B.find(P->UnitId);
                Test->TestTrue(TEXT("Player reclaims the saved character"), Head && Head->human && Head->skills[4] == 2);
                int32 Bots = 0;
                for (const auto& U : B.units)
                    Bots += U.bot;
                Test->TestEqual(TEXT("Loaded AI teammates"), Bots, 3);
            }
            return Wait(1.5);
        }
        case 33:
            Capture(TEXT("evolution-restored"));
            return Wait(.3);
        default:
            S->Finished = true;
            G->ReturnToLobby(C);
            RestoreSaves(GI);
            Test->TestTrue(TEXT("Captured every sketch scene"), Shots >= 14);
            return true;
        }
    }

private:
    void RestoreSaves(UGWGameInstance* GI)
    {
        if (!Parked)
            return;
        Parked = false;
        for (int32 Slot = 0; Slot < UGWRunSave::SlotCount; ++Slot)
        {
            const FString Path = UGWRunSave::SlotPath(Slot), Backup = Path + TEXT(".playtest");
            UGWRunSave::DeleteSlot(Slot);
            if (IFileManager::Get().FileExists(*Backup))
                IFileManager::Get().Move(*Path, *Backup, true);
        }
        if (GI)
            GI->RefreshSaves();
    }
    FAutomationTestBase* Test;
    double Start = 0, Next = 0, SavedElapsed = 0;
    int32 Stage = 0, Shots = 0, MateId = -1, SavedLevel = 1;
    bool Parked = false;
    double MateHP = 0;
    Vec2 MateStart{};
};
// Every Niagara cue on an empty patch of paper, eight to a sheet (4x2) under a close camera, tinted through
// User.Color, so each system can be judged (and its colour binding verified) without combat noise. The last sheet
// repeats the big cues at the sizes the game spawns them at.
class FFxSheet : public IAutomationLatentCommand
{
public:
    explicit FFxSheet(FAutomationTestBase* T) : Test(T), Start(FPlatformTime::Seconds())
    {
    }
    bool Update() override
    {
        auto* W = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
        auto* C = W ? Cast<AGWController>(W->GetFirstPlayerController()) : nullptr;
        auto* G = W ? W->GetAuthGameMode<AGWGameMode>() : nullptr;
        auto* S = W ? W->GetGameState<AGWGameState>() : nullptr;
        auto* P = C ? C->GetPlayerState<AGWPlayerState>() : nullptr;
        const double Now = FPlatformTime::Seconds();
        if (Now - Start > 120)
        {
            Test->AddError(TEXT("Fx sheet timeout"));
            FGWCinematic::Get() = FGWCinematic();
            Clear();
            return true;
        }
        if (!C || !G || !S || !P || !C->WorldView || !C->GetHUD() || Now < Next)
            return false;
        auto Capture = [&](const FString& Name)
        {
            const FString Path = FPaths::ProjectSavedDir() / TEXT("SketchVerification") / (Name + TEXT(".png"));
            IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
            FScreenshotRequest::RequestScreenshot(Path, false, false);
        };
        auto Wait = [&](double Seconds)
        {
            Next = Now + Seconds;
            ++Stage;
            return false;
        };
        struct FCue
        {
            const TCHAR* Name;
            float Scale;
        };
        static const FCue Sheets[4][8] = {
            {{TEXT("Impact"), 1}, {TEXT("Muzzle"), 1.2f}, {TEXT("Blast"), .8f}, {TEXT("Death"), 1.2f}, {TEXT("Spawn"), 1.2f}, {TEXT("Pickup"), 1},
             {TEXT("Heal"), 1.2f}, {TEXT("Shield"), 1.2f}},
            {{TEXT("Burn"), 1.4f}, {TEXT("Frost"), 1.4f}, {TEXT("FrostNova"), .8f}, {TEXT("Arc"), 1}, {TEXT("Pulse"), .6f}, {TEXT("LevelUp"), .8f},
             {TEXT("Telegraph"), .45f}, {TEXT("Slam"), .45f}},
            {{TEXT("Launch"), 1.2f}, {TEXT("Build"), 1}, {TEXT("Erase"), 1.2f}, {TEXT("Shatter"), .9f}, {TEXT("Confetti"), .8f}, {TEXT("Assemble"), 1},
             {TEXT("InkSplash"), 1.3f}, {TEXT("EvolveAura"), 1.4f}},
            {{TEXT("Ambient"), 1}, {TEXT("Blast"), 1.3f}, {TEXT("Death"), 2}, {TEXT("TowerBuild"), .8f}, {TEXT("LevelUp"), 1.2f}, {TEXT("TowerUpgrade"), .8f},
             {TEXT("Assemble"), 1.3f}, {TEXT("Build"), 1.3f}}};
        const int32 Sheet = (Stage - 2) / 3, Step = (Stage - 2) % 3;
        if (Stage == 0)
        {
            if (S->InMatch)
            {
                S->Finished = true;
                G->ReturnToLobby(C);
            }
            return Wait(1);
        }
        if (Stage == 1)
        {
            Test->TestTrue(TEXT("Run starts"), G->BeginRound(C, true));
            auto& B = *S->Simulation;
            B.config.aiEnabled = B.config.enemiesEnabled = B.config.bossEnabled = false;
            auto* Head = B.find(P->UnitId);
            const int HeadId = Head->id;
            B.units.erase(std::remove_if(B.units.begin(), B.units.end(), [HeadId](const Unit& U) { return U.id != HeadId; }),
                          B.units.end());
            B.projectiles.clear();
            B.crates.clear();
            // The player stands aside, out of the sheet; the camera looks straight at the sheet from close by.
            B.find(HeadId)->position = {6200, 6900};
            S->Publish();
            C->GetHUD()->bShowHUD = false;
            FGWCinematic& Camera = FGWCinematic::Get();
            Camera.Active = Camera.Clean = true;
            Camera.Focus = FVector(6200, 6200, 0);
            Camera.Distance = 1500;
            Camera.Pitch = 60;
            Camera.Yaw = -90;
            return Wait(1.5);
        }
        if (Sheet < 4 && Step == 0)
        {
            // The last sheet's systems go first: the looping ones (burn, frost, aura, ambient) would never end.
            Clear();
            const FVector Middle(6200, 6200, 2);
            int32 Spawned = 0;
            for (int32 I = 0; I < 8; ++I)
            {
                const FCue& Cue = Sheets[Sheet][I];
                const FString Asset = FString(TEXT("NS_GW_")) + Cue.Name;
                auto* System = LoadObject<UNiagaraSystem>(nullptr, *FString::Printf(TEXT("/Game/Sketch/VFX/%s.%s"), *Asset, *Asset));
                if (!Test->TestNotNull(*Asset, System))
                    continue;
                const FVector At = Middle + FVector((I % 4 - 1.5f) * 285.f, (I / 4 - .5f) * 300.f, 0);
                auto* Fx = UNiagaraFunctionLibrary::SpawnSystemAtLocation(W, System, At, FRotator::ZeroRotator, FVector(Cue.Scale), true, false,
                                                                          ENCPoolMethod::None, true);
                if (!Fx)
                    continue;
                Fx->SetVariableLinearColor(TEXT("User.Color"), FLinearColor(.9f, .1f, .05f, 1));
                Fx->SetVariableVec2(TEXT("User.BeamSize"), FVector2D(40, 240));
                Fx->SetVariableVec3(TEXT("User.BeamDir"), FVector(1, 0, 0));
                Fx->Activate(true);
                Live.Add(Fx);
                ++Spawned;
            }
            Test->TestEqual(TEXT("Every Niagara system spawned"), Spawned, 8);
            return Wait(.14);
        }
        if (Sheet < 4 && Step == 1)
        {
            Capture(FString::Printf(TEXT("fx-sheet%d-early"), Sheet + 1));
            return Wait(.3);
        }
        if (Sheet < 4 && Step == 2)
        {
            Capture(FString::Printf(TEXT("fx-sheet%d-late"), Sheet + 1));
            return Wait(2.2);
        }
        FGWCinematic::Get() = FGWCinematic();
        C->GetHUD()->bShowHUD = true;
        S->Finished = true;
        G->ReturnToLobby(C);
        Clear();
        return true;
    }

private:
    void Clear()
    {
        for (const TWeakObjectPtr<UNiagaraComponent>& Fx : Live)
            if (Fx.IsValid())
                Fx->DestroyComponent();
        Live.Reset();
    }
    FAutomationTestBase* Test;
    double Start = 0, Next = 0;
    int32 Stage = 0;
    TArray<TWeakObjectPtr<UNiagaraComponent>> Live;
};
// The page wipe and the page assembling: from the title into a defense page (the covered page with its title
// card, the tiles flipping away, the blocks rising and everyone dropping in, the settled HUD), the wipe closing
// again, and the title page coming back with its menu sliding in. Screenshots land in Saved/SketchVerification.
class FTransitionScenes : public IAutomationLatentCommand
{
public:
    explicit FTransitionScenes(FAutomationTestBase* T) : Test(T), Start(FPlatformTime::Seconds())
    {
    }
    bool Update() override
    {
        auto* W = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
        auto* C = W ? Cast<AGWController>(W->GetFirstPlayerController()) : nullptr;
        auto* G = W ? W->GetAuthGameMode<AGWGameMode>() : nullptr;
        auto* S = W ? W->GetGameState<AGWGameState>() : nullptr;
        auto* H = C ? Cast<AGWHUD>(C->GetHUD()) : nullptr;
        const double Now = FPlatformTime::Seconds();
        FGWTransition& T = FGWTransition::Get();
        if (Now - Start > 90)
        {
            Test->AddError(TEXT("Transition scenes timeout"));
            T.ForceInTests = false;
            return true;
        }
        if (!C || !G || !S || !H || !C->WorldView || Now < Next)
            return false;
        auto Capture = [&](const TCHAR* Name)
        {
            const FString Path = FPaths::ProjectSavedDir() / TEXT("SketchVerification") / (FString(Name) + TEXT(".png"));
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
        switch (Stage)
        {
        case 0:
            GWText::SetCulture(TEXT("zh-Hans"));
            if (S->InMatch)
            {
                S->Finished = true;
                G->ReturnToLobby(C);
            }
            H->SetPage(AGWHUD::EPage::Main);
            return Wait(1);
        case 1:
            // From here on the wipes and the world's intro play as they do for players.
            T.ForceInTests = true;
            Test->TestTrue(TEXT("A defense page opens"), G->BeginDefense(C, 0, 0));
            return Wait(.45);
        case 2:
            Test->TestTrue(TEXT("A new scene is covered by the wipe"), T.Stage == FGWTransition::EStage::Closed);
            Capture(TEXT("transition-card-zh"));
            return Wait(1.05);
        case 3:
            Test->TestTrue(TEXT("The wipe opens once the page is in place"), T.Stage == FGWTransition::EStage::Opening);
            Capture(TEXT("transition-reveal-zh"));
            return Wait(.45);
        case 4:
            Capture(TEXT("transition-assemble-zh"));
            return Wait(.55);
        case 5:
            Capture(TEXT("transition-landing-zh"));
            return Wait(2.2);
        case 6:
            Test->TestTrue(TEXT("The wipe is gone"), T.Stage == FGWTransition::EStage::Idle);
            Capture(TEXT("transition-settled-zh"));
            return Wait(.3);
        case 7:
            // Leaving: the tiles sweep in over the page before anything changes.
            T.Close(FGWTransition::TitlePage, true, 4);
            return Wait(.3);
        case 8:
            Test->TestTrue(TEXT("The wipe is closing"), T.Stage == FGWTransition::EStage::Closing);
            Capture(TEXT("transition-closing-zh"));
            return Wait(.5);
        case 9:
            S->Finished = true;
            G->ReturnToLobby(C);
            return Wait(.9);
        case 10:
            Capture(TEXT("transition-title-reveal-zh"));
            return Wait(.45);
        case 11:
            Capture(TEXT("menu-motion-zh"));
            return Wait(2);
        case 12:
            Capture(TEXT("menu-settled-zh"));
            return Wait(.3);
        default:
            T.ForceInTests = false;
            Test->TestTrue(TEXT("Captured every transition scene"), Shots >= 9);
            return true;
        }
    }

private:
    FAutomationTestBase* Test;
    double Start = 0, Next = 0;
    int32 Stage = 0, Shots = 0;
};
// The title page's battle (AGWWorld's showcase): the menu over it in both languages, then its beats one by one with
// the HUD hidden (the fight is replayed from a fixed seed to a little before each and left to play into it, so every
// cue fires as it does for players): firefights from cover, a level-up's cards, roulette, shards and the skill taken
// in, the tail's arrival and launch, the boss, an elite, a throw and its landing, skills at work, the next minute;
// then spent shots in a match dropping to the paper when they run out of range. Screenshots land in
// Saved/SketchVerification; each beat logs how the battle stands.
class FShowcaseScenes : public IAutomationLatentCommand
{
public:
    explicit FShowcaseScenes(FAutomationTestBase* T) : Test(T), Start(FPlatformTime::Seconds())
    {
    }
    bool Update() override
    {
        auto* W = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
        auto* C = W ? Cast<AGWController>(W->GetFirstPlayerController()) : nullptr;
        auto* G = W ? W->GetAuthGameMode<AGWGameMode>() : nullptr;
        auto* S = W ? W->GetGameState<AGWGameState>() : nullptr;
        auto* P = C ? C->GetPlayerState<AGWPlayerState>() : nullptr;
        auto* H = C ? Cast<AGWHUD>(C->GetHUD()) : nullptr;
        const double Now = FPlatformTime::Seconds();
        if (Now - Start > 150)
        {
            Test->AddError(TEXT("Showcase scenes timeout"));
            FGWCinematic::Get() = FGWCinematic();
            if (H)
                H->bShowHUD = true;
            return true;
        }
        // The longest frame while the beats play into each capture, from a tenth of a second after the jump (the frames
        // round a screenshot wait for its readback).
        if (Stage > 5 && Stage < 5 + 2 * 17 && (Stage - 5) % 2 == 1 && Now > Next - .8)
        {
            Worst = FMath::Max(Worst, FApp::GetDeltaTime());
            BeatWorst = FMath::Max(BeatWorst, FApp::GetDeltaTime());
        }
        if (!C || !G || !S || !P || !H || !C->WorldView || Now < Next)
            return false;
        auto Capture = [&](const TCHAR* Name)
        {
            const FString Path = FPaths::ProjectSavedDir() / TEXT("SketchVerification") / (FString(Name) + TEXT(".png"));
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
        struct FBeat
        {
            const TCHAR* Name;
            float At;
        };
        static const FBeat Beats[] = {
            {TEXT("showcase-01-firefight"), 3.f},     {TEXT("showcase-02-cards"), 4.95f},     {TEXT("showcase-03-shards"), 5.9f},
            {TEXT("showcase-04-absorbed"), 6.4f},     {TEXT("showcase-05-tail"), 9.4f},       {TEXT("showcase-06-launch"), 16.05f},
            {TEXT("showcase-07-boss-warning"), 22.9f}, {TEXT("showcase-08-boss-lands"), 24.25f}, {TEXT("showcase-09-boss-fight"), 27.1f},
            {TEXT("showcase-10-evolve"), 31.4f},      {TEXT("showcase-11-cards-late"), 37.3f}, {TEXT("showcase-12-throw"), 42.2f},
            {TEXT("showcase-13-landing"), 43.f},      {TEXT("showcase-14-skills"), 47.f},      {TEXT("showcase-15-shards-late"), 50.9f},
            {TEXT("showcase-16-wide"), 58.f},         {TEXT("showcase-17-next-minute"), 62.f}};
        constexpr int32 BeatCount = UE_ARRAY_COUNT(Beats);
        constexpr float Lead = .9f;
        constexpr int32 FirstBeat = 5, AfterBeats = FirstBeat + 2 * BeatCount;
        AGWWorld* View = C->WorldView;
        if (Stage == 0)
        {
            GWText::SetCulture(TEXT("zh-Hans"));
            if (S->InMatch)
            {
                S->Finished = true;
                G->ReturnToLobby(C);
            }
            H->SetPage(AGWHUD::EPage::Main);
            return Wait(2);
        }
        if (Stage == 1)
        {
            Test->TestTrue(TEXT("The title page plays the brawl"), View->ShowcaseOn());
            Capture(TEXT("showcase-title-zh"));
            return Wait(.3);
        }
        if (Stage == 2)
        {
            GWText::SetCulture(TEXT("en"));
            return Wait(.5);
        }
        if (Stage == 3)
        {
            Capture(TEXT("showcase-title-en"));
            return Wait(.3);
        }
        if (Stage == 4)
        {
            GWText::SetCulture(TEXT("zh-Hans"));
            H->bShowHUD = false;
            return Wait(.2);
        }
        if (Stage < AfterBeats)
        {
            const FBeat& Beat = Beats[(Stage - FirstBeat) / 2];
            if ((Stage - FirstBeat) % 2 == 0)
            {
                View->ShowcaseSeek = Beat.At - Lead;
                return Wait(Lead);
            }
            if (BeatWorst > .05)
                Test->AddInfo(FString::Printf(TEXT("%s: a %.0f ms frame on the way"), Beat.Name, BeatWorst * 1000));
            BeatWorst = 0;
            Test->AddInfo(FString::Printf(TEXT("%s: %s"), Beat.Name, *View->ShowcaseStats()));
            Capture(Beat.Name);
            return Wait(.25);
        }
        if (Stage == AfterBeats)
        {
            Test->AddInfo(FString::Printf(TEXT("Longest showcase frame over the beats: %.0f ms"), Worst * 1000));
            // Spent shots in a match: a fan of every gun's rounds (and an enemy's bolt) into empty paper, all of them
            // short-ranged and flying over the walls, so each one runs out of range instead of hitting anything.
            H->bShowHUD = true;
            Test->TestTrue(TEXT("Run starts"), G->BeginRound(C, true));
            auto& B = *S->Simulation;
            B.config.aiEnabled = B.config.enemiesEnabled = B.config.bossEnabled = false;
            const int HeadId = B.find(P->UnitId)->id;
            B.units.erase(std::remove_if(B.units.begin(), B.units.end(), [HeadId](const Unit& U) { return U.id != HeadId; }), B.units.end());
            B.projectiles.clear();
            B.crates.clear();
            Unit* Head = B.find(HeadId);
            Head->position = {6200, 6200};
            Head->aim = {1, 0};
            for (int32 I = 0; I < 7; ++I)
            {
                Projectile Shot;
                Shot.id = 61000 + I;
                Shot.owner = I == 6 ? -1 : HeadId;
                Shot.squad = Shot.owner;
                Shot.team = I == 6 ? 2 : 1;
                Shot.weapon = I == 6 ? 9 : I % 5;
                Shot.high = true;
                const double A = -.55 + I * .18;
                const Vec2 Way{std::cos(A), std::sin(A)};
                Shot.position = Head->position + Way * 60.;
                Shot.velocity = Way * (1300. + I * 60);
                Shot.remaining = .42 + I * .03;
                B.projectiles.push_back(Shot);
            }
            S->Publish();
            FGWCinematic& Camera = FGWCinematic::Get();
            Camera.Active = Camera.Clean = true;
            Camera.Focus = FVector(6650, 6200, 0);
            Camera.Distance = 1500;
            Camera.Pitch = 56;
            Camera.Yaw = -90;
            return Wait(.3);
        }
        if (Stage == AfterBeats + 1)
        {
            Test->TestFalse(TEXT("The brawl stops for a match"), View->ShowcaseOn());
            Capture(TEXT("spent-flight"));
            return Wait(.32);
        }
        if (Stage == AfterBeats + 2)
        {
            Test->TestTrue(TEXT("Shots out of range drop instead of vanishing"), View->SpentShotCount() > 0);
            Capture(TEXT("spent-falling"));
            return Wait(.25);
        }
        if (Stage == AfterBeats + 3)
        {
            Capture(TEXT("spent-bounce"));
            return Wait(.75);
        }
        if (Stage == AfterBeats + 4)
        {
            Capture(TEXT("spent-rest"));
            return Wait(.3);
        }
        if (Stage == AfterBeats + 5)
        {
            FGWCinematic::Get() = FGWCinematic();
            S->Finished = true;
            G->ReturnToLobby(C);
            return Wait(1.5);
        }
        Test->TestTrue(TEXT("The brawl comes back on the title page"), View->ShowcaseOn());
        Test->TestTrue(TEXT("Captured every showcase scene"), Shots >= BeatCount + 6);
        return true;
    }

private:
    FAutomationTestBase* Test;
    double Start = 0, Next = 0;
    double Worst = 0, BeatWorst = 0;
    int32 Stage = 0, Shots = 0;
};
// The game's name in every language: a layout check at common window shapes (the name and its shapes stay on the
// screen and clear of the menu card), then the title page in Japanese, German and English and the page wipe's card in
// Japanese. Screenshots land in Saved/SketchVerification.
class FTitleFit : public IAutomationLatentCommand
{
public:
    explicit FTitleFit(FAutomationTestBase* T) : Test(T), Start(FPlatformTime::Seconds())
    {
    }
    bool Update() override
    {
        auto* W = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
        auto* C = W ? Cast<AGWController>(W->GetFirstPlayerController()) : nullptr;
        auto* G = W ? W->GetAuthGameMode<AGWGameMode>() : nullptr;
        auto* S = W ? W->GetGameState<AGWGameState>() : nullptr;
        auto* H = C ? Cast<AGWHUD>(C->GetHUD()) : nullptr;
        const double Now = FPlatformTime::Seconds();
        FGWTransition& T = FGWTransition::Get();
        if (Now - Start > 60)
        {
            Test->AddError(TEXT("Title fit timeout"));
            T.ForceInTests = false;
            GWText::SetCulture(TEXT("zh-Hans"));
            return true;
        }
        if (!C || !G || !S || !H || Now < Next)
            return false;
        auto Capture = [&](const TCHAR* Name)
        {
            const FString Path = FPaths::ProjectSavedDir() / TEXT("SketchVerification") / (FString(Name) + TEXT(".png"));
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
        switch (Stage)
        {
        case 0:
        {
            if (S->InMatch)
            {
                S->Finished = true;
                G->ReturnToLobby(C);
            }
            H->SetPage(AGWHUD::EPage::Main);
            // 16:9 at four sizes, 16:10, 21:9, 4:3, 5:4 and a small window.
            const FVector2D Sizes[] = {{1600, 900}, {1280, 720}, {1920, 1080}, {3840, 2160}, {1920, 1200},
                                       {2560, 1080}, {1024, 768}, {1280, 1024}, {800, 600}};
            for (const TCHAR* Culture : {TEXT("ja"), TEXT("de"), TEXT("en"), TEXT("zh-Hans")})
            {
                GWText::SetCulture(Culture);
                const FText Name = GWText::Get(TEXT("Game.Title"));
                for (const FVector2D& Size : Sizes)
                {
                    const AGWHUD::FTitleLayout Layout = H->MainTitle(Name, Size.X, Size.Y);
                    const float K = FMath::Max(.5f, FMath::Min(Size.X / 1600, Size.Y / 900));
                    const FString Where = FString::Printf(TEXT("%s at %.0fx%.0f"), Culture, Size.X, Size.Y);
                    Test->TestTrue(Where + TEXT(": the title and its shapes stay on the screen"), Layout.Right <= Size.X - Layout.X * .5f);
                    Test->TestTrue(Where + TEXT(": the title clears the menu card"), Layout.Bottom <= Size.Y * .58f);
                    Test->TestTrue(Where + TEXT(": the title stays large"), Layout.Size >= 50 * K);
                }
            }
            GWText::SetCulture(TEXT("ja"));
            {
                const AGWHUD::FTitleLayout Layout = H->MainTitle(GWText::Get(TEXT("Game.Title")), 1600, 900);
                Test->TestEqual(TEXT("The Japanese title sets on two lines at 16:9"), Layout.Lines.Num(), 2);
                Test->TestEqual(TEXT("The Japanese title keeps its full size"), Layout.Size, 86.f);
                if (Layout.Lines.Num() == 2)
                    Test->TestFalse(TEXT("The break drops the middle dot"), Layout.Lines[0].ToString().EndsWith(TEXT("\x30FB")) ||
                                                                               Layout.Lines[1].ToString().StartsWith(TEXT("\x30FB")));
            }
            GWText::SetCulture(TEXT("zh-Hans"));
            {
                const AGWHUD::FTitleLayout Layout = H->MainTitle(GWText::Get(TEXT("Game.Title")), 1600, 900);
                Test->TestEqual(TEXT("The Chinese title keeps one line"), Layout.Lines.Num(), 1);
                Test->TestEqual(TEXT("The Chinese title keeps its full size"), Layout.Size, 86.f);
            }
            GWText::SetCulture(TEXT("ja"));
            return Wait(2);
        }
        case 1:
            Capture(TEXT("title-fit-ja"));
            return Wait(.4);
        case 2:
            GWText::SetCulture(TEXT("de"));
            return Wait(.5);
        case 3:
            Capture(TEXT("title-fit-de"));
            return Wait(.4);
        case 4:
            GWText::SetCulture(TEXT("en"));
            return Wait(.5);
        case 5:
            Capture(TEXT("title-fit-en"));
            return Wait(.4);
        case 6:
            GWText::SetCulture(TEXT("ja"));
            T.ForceInTests = true;
            T.Close(FGWTransition::TitlePage, true, 8);
            ClosedAt = Now;
            return Wait(.6);
        case 7:
            // The wipe moves on only when the HUD draws, and an offscreen frame after a language switch can take half a
            // second (new glyphs rasterise): wait for the cover rather than for a fixed time.
            if (T.Stage != FGWTransition::EStage::Closed && Now - ClosedAt < 6)
                return false;
            Test->TestTrue(TEXT("The wipe covers the page"), T.Stage == FGWTransition::EStage::Closed);
            Capture(TEXT("title-card-ja"));
            return Wait(.4);
        case 8:
            // The page opens again once the patience runs out; the next tests start from an idle page.
            T.AwaitChange = false;
            if (T.Stage != FGWTransition::EStage::Idle && Now - ClosedAt < 15)
                return false;
            return Wait(.1);
        default:
            T.ForceInTests = false;
            GWText::SetCulture(TEXT("zh-Hans"));
            Test->TestTrue(TEXT("Captured every title scene"), Shots >= 4);
            return true;
        }
    }

private:
    FAutomationTestBase* Test;
    double Start = 0, Next = 0, ClosedAt = 0;
    int32 Stage = 0, Shots = 0;
};
} // namespace
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGWSketchTitleFit, "GW.Sketch.TitleFit", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FGWSketchTitleFit::RunTest(const FString& Parameters)
{
    ADD_LATENT_AUTOMATION_COMMAND(FTitleFit(this));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGWSketchFxSheet, "GW.Sketch.FxSheet", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FGWSketchFxSheet::RunTest(const FString& Parameters)
{
    ADD_LATENT_AUTOMATION_COMMAND(FFxSheet(this));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGWSketchTransitions, "GW.Sketch.Transitions",
                                 EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FGWSketchTransitions::RunTest(const FString& Parameters)
{
    ADD_LATENT_AUTOMATION_COMMAND(FTransitionScenes(this));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGWSketchShowcase, "GW.Sketch.Showcase",
                                 EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FGWSketchShowcase::RunTest(const FString& Parameters)
{
    ADD_LATENT_AUTOMATION_COMMAND(FShowcaseScenes(this));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGWSketchScenes, "GW.Sketch.Scenes",
                                 EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FGWSketchScenes::RunTest(const FString& Parameters)
{
    ADD_LATENT_AUTOMATION_COMMAND(FSketchScenes(this));
    return true;
}
#endif
