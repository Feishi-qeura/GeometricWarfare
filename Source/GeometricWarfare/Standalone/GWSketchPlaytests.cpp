#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GWController.h"
#include "GWFx.h"
#include "GWGameInstance.h"
#include "GWGameMode.h"
#include "GWGameState.h"
#include "GWHUD.h"
#include "GWPlayerState.h"
#include "GWSaveGame.h"
#include "GWText.h"
#include "GWWorld.h"
#include "HAL/FileManager.h"
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
// Every Niagara cue on an empty patch of paper in a 5x4 grid, tinted through User.Color, so each
// system can be judged (and its colour binding verified) without combat noise.
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
        if (Now - Start > 90)
        {
            Test->AddError(TEXT("Fx sheet timeout"));
            return true;
        }
        if (!C || !G || !S || !P || !C->WorldView || !C->GetHUD() || Now < Next)
            return false;
        auto Capture = [&](const TCHAR* Name)
        {
            const FString Path = FPaths::ProjectSavedDir() / TEXT("SketchVerification") / (FString(Name) + TEXT(".png"));
            IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
            FScreenshotRequest::RequestScreenshot(Path, false, false);
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
            if (S->InMatch)
            {
                S->Finished = true;
                G->ReturnToLobby(C);
            }
            return Wait(1);
        case 1:
        {
            Test->TestTrue(TEXT("Run starts"), G->BeginRound(C, true));
            auto& B = *S->Simulation;
            B.config.aiEnabled = B.config.enemiesEnabled = B.config.bossEnabled = false;
            auto* Head = B.find(P->UnitId);
            Head->position = {6200, 6200};
            const int HeadId = Head->id;
            B.units.erase(std::remove_if(B.units.begin(), B.units.end(), [HeadId](const Unit& U) { return U.id != HeadId; }),
                          B.units.end());
            B.projectiles.clear();
            B.crates.clear();
            S->Publish();
            C->GetHUD()->bShowHUD = false;
            return Wait(1.5);
        }
        case 2:
        {
            const TCHAR* Names[] = {TEXT("Impact"), TEXT("Muzzle"), TEXT("Blast"),     TEXT("Death"),  TEXT("Spawn"),
                                    TEXT("Burn"),   TEXT("Frost"),  TEXT("FrostNova"), TEXT("Arc"),    TEXT("Pulse"),
                                    TEXT("LevelUp"), TEXT("Pickup"), TEXT("Heal"),     TEXT("Shield"), TEXT("Telegraph"),
                                    TEXT("Slam"),   TEXT("Launch"), TEXT("Ambient")};
            const int32 Count = static_cast<int32>(UE_ARRAY_COUNT(Names));
            const FVector Head = S->FindUnit(P->UnitId)->Position;
            int32 Spawned = 0;
            for (int32 I = 0; I < Count; ++I)
            {
                const FString Asset = FString(TEXT("NS_GW_")) + Names[I];
                auto* System = LoadObject<UNiagaraSystem>(nullptr, *FString::Printf(TEXT("/Game/Sketch/VFX/%s.%s"), *Asset, *Asset));
                if (!Test->TestNotNull(*Asset, System))
                    continue;
                const FVector At = Head + FVector((I % 5 - 2) * 290.f, (I / 5 - 1.5f) * 230.f, 2);
                auto* Fx = UNiagaraFunctionLibrary::SpawnSystemAtLocation(W, System, At, FRotator::ZeroRotator, FVector(.6f), true, false,
                                                                          ENCPoolMethod::None, true);
                if (!Fx)
                    continue;
                Fx->SetVariableLinearColor(TEXT("User.Color"), FLinearColor(.9f, .1f, .05f, 1));
                Fx->SetVariableVec2(TEXT("User.BeamSize"), FVector2D(40, 240));
                Fx->SetVariableVec3(TEXT("User.BeamDir"), FVector(1, 0, 0));
                Fx->Activate(true);
                ++Spawned;
            }
            Test->TestEqual(TEXT("Every Niagara system spawned"), Spawned, Count);
            return Wait(.12);
        }
        case 3:
            Capture(TEXT("fx-sheet-early"));
            return Wait(.25);
        case 4:
            Capture(TEXT("fx-sheet-late"));
            return Wait(.3);
        default:
            C->GetHUD()->bShowHUD = true;
            S->Finished = true;
            G->ReturnToLobby(C);
            return true;
        }
    }

private:
    FAutomationTestBase* Test;
    double Start = 0, Next = 0;
    int32 Stage = 0;
};
} // namespace
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGWSketchFxSheet, "GW.Sketch.FxSheet", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FGWSketchFxSheet::RunTest(const FString& Parameters)
{
    ADD_LATENT_AUTOMATION_COMMAND(FFxSheet(this));
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
