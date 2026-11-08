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
// Single-player combat on the real server path: squad charge and throw, damage numbers, friendly fire that
// shoves instead of hurting, AI teammates that hold formation and shoot, and a scheduled elite encounter.
namespace
{
class FCombatPolishCheck : public IAutomationLatentCommand
{
public:
    explicit FCombatPolishCheck(FAutomationTestBase* In) : Test(In), Start(FPlatformTime::Seconds()) {}
    virtual bool Update() override
    {
        double Now = FPlatformTime::Seconds();
        UWorld* W = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
        auto* C = W ? Cast<AGWController>(W->GetFirstPlayerController()) : nullptr;
        auto* G = W ? W->GetAuthGameMode<AGWGameMode>() : nullptr;
        auto* S = W ? W->GetGameState<AGWGameState>() : nullptr;
        auto* P = C ? C->GetPlayerState<AGWPlayerState>() : nullptr;
        if (Now - Start > 45)
        {
            Test->AddError(TEXT("Combat polish rendered check timed out"));
            if (C)
                C->SetActorTickEnabled(true);
            return true;
        }
        if (!C || !G || !S || !P || !C->WorldView)
            return false;
        auto Capture = [&](const TCHAR* Name)
        {
            FString Path = FPaths::ProjectSavedDir() / TEXT("StandaloneVerification") / (FString(Name) + TEXT(".png"));
            IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
            FScreenshotRequest::RequestScreenshot(Path, false, false);
        };
        auto CheckFile = [&](const TCHAR* Name)
        {
            Test->TestTrue(TEXT("Rendered combat screenshot exists"),
                           IFileManager::Get().FileExists(*(FPaths::ProjectSavedDir() / TEXT("StandaloneVerification") /
                                                            (FString(Name) + TEXT(".png")))));
        };
        if (Stage == 0)
        {
            GWText::SetCulture(TEXT("zh-Hans"));
            if (S->InMatch)
            {
                S->Finished = true;
                G->ReturnToLobby(C);
            }
            // The controller would otherwise stream real mouse input over the scripted head input.
            C->SetActorTickEnabled(false);
            Test->TestTrue(TEXT("Single player run starts"), G->BeginRound(C, true));
            auto& B = *S->Simulation;
            B.config.aiEnabled = false;
            B.config.enemiesEnabled = false;
            B.config.bossEnabled = false;
            auto* Head = B.find(P->UnitId);
            Head->position = {6200, 6200};
            Head->hp = Head->maxHp = 100000;
            for (auto& U : B.units)
                if (U.bot)
                {
                    U.position = Head->position + gwgame::Vec2{-200. + Bots.Num() * 200., 420.};
                    U.hp = U.maxHp = 100000;
                    Bots.Add(U.id);
                }
            Test->TestEqual(TEXT("Three AI teammates"), Bots.Num(), 3);
            for (int I = 0; I < 3; ++I)
            {
                gwgame::Unit Ally;
                Ally.id = 1001 + I;
                Ally.kind = gwgame::UnitKind::Companion;
                Ally.state = gwgame::UnitState::Idle;
                Ally.hp = Ally.maxHp = 100000;
                Ally.position = Head->position + gwgame::Vec2{I == 1 ? -100. : -60., (I - 1) * 50.};
                B.units.push_back(Ally);
                Test->TestEqual(TEXT("Stationary companion joins the squad"), B.recruit(Head->id), 1001 + I);
            }
            // A target on the same open lane makes real squad shots produce real numbers.
            gwgame::Unit Enemy;
            Enemy.id = 1100;
            Enemy.team = 2;
            Enemy.kind = gwgame::UnitKind::Monster;
            Enemy.position = {6500, 6200};
            Enemy.hp = Enemy.maxHp = 100000;
            B.units.push_back(Enemy);
            gwgame::Input Input;
            Input.fire = true;
            Input.aim = {1, 0};
            B.setInput(Head->id, Input);
            C->MouseAim = {1, 0};
            First = B.tailIds(Head->id).front();
            C->ServerCharge(0, {1, 0});
            S->Publish();
            Next = Now + 1.1;
            Stage = 1;
            return false;
        }
        if (Now < Next)
            return false;
        auto& B = *S->Simulation;
        if (Stage == 1)
        {
            Test->TestTrue(TEXT("Server advances held charge"), B.find(P->UnitId)->chargeTime > .5);
            bool SquadShot = false;
            for (const auto& Ally : B.units)
                if (Ally.followerOf == P->UnitId && Ally.ammo < gwgame::weaponStats(Ally.weapon).ammo)
                    SquadShot = true;
            Test->TestTrue(TEXT("Recruited squad fires with head"), SquadShot);
            B.damageUnit(P->UnitId, 1100, 88);
            S->Publish();
            Test->TestTrue(TEXT("Damage numbers published"), S->DamageNumbers.Num() > 0);
            Capture(TEXT("polish-charge"));
            Stage = 2;
            Next = Now + .25;
            return false;
        }
        if (Stage == 2)
        {
            CheckFile(TEXT("polish-charge"));
            C->ServerCharge(1, {.6, -.8});
            C->MouseAim = {.6, -.8};
            Test->TestEqual(TEXT("First recruited ally is launched"), static_cast<int>(B.find(First)->state),
                            static_cast<int>(gwgame::UnitState::Launched));
            Test->TestTrue(TEXT("Throw uses released mouse direction"),
                           B.find(First)->velocity.normalized().dot({.6, -.8}) > .999);
            Test->TestTrue(TEXT("Held charge increases throw power"), B.find(First)->launchPower > .3);
            // An AI teammate's sniper round crosses the player: no damage, a push along the shot and a cue.
            auto* Head = B.find(P->UnitId);
            Head->input.fire = false;
            const double HeadHP = Head->hp;
            gwgame::Projectile Round;
            Round.id = 70000;
            Round.owner = Bots[0];
            Round.squad = Bots[0];
            Round.team = 1;
            Round.weapon = 3;
            Round.damage = 95;
            Round.position = Head->position - gwgame::Vec2{80, 0};
            Round.velocity = {2400, 0};
            Round.remaining = .1;
            B.projectiles.push_back(Round);
            // The player's own bullet crosses its own follower: a squad never shoves itself.
            const int Tail = B.tailIds(Head->id).back();
            auto* Follower = B.find(Tail);
            Follower->impulse = {};
            const double TailHP = Follower->hp;
            gwgame::Projectile Own;
            Own.id = 70001;
            Own.owner = Head->id;
            Own.squad = Head->id;
            Own.team = 1;
            Own.damage = 24;
            Own.position = Follower->position - gwgame::Vec2{0, 60};
            Own.velocity = {0, 1300};
            Own.remaining = .1;
            B.projectiles.push_back(Own);
            const size_t EffectsBefore = B.effects.size();
            B.advance(2. / 60);
            Head = B.find(P->UnitId);
            Follower = B.find(Tail);
            Test->TestEqual(TEXT("Friendly bullet deals no damage"), Head->hp, HeadHP);
            Test->TestTrue(TEXT("Friendly bullet pushes along its flight"), Head->impulse.x > 150 && FMath::Abs(Head->impulse.y) < 1);
            Test->TestTrue(TEXT("Friendly push is capped"), Head->impulse.length() <= gwgame::MaxFriendlyImpulse + 1e-6);
            bool Cue = false;
            for (size_t I = EffectsBefore; I < B.effects.size(); ++I)
                Cue |= B.effects[I].style == gwgame::EffectStyle::Shove;
            Test->TestTrue(TEXT("Friendly push emits a shove cue"), Cue);
            Test->TestEqual(TEXT("Own squad bullet deals no damage"), Follower->hp, TailHP);
            Test->TestTrue(TEXT("Own squad bullet does not shove"), Follower->impulse.length() < 1);
            S->Publish();
            Stage = 3;
            Next = Now + .12;
            return false;
        }
        if (Stage == 3)
        {
            Capture(TEXT("polish-friendly-push"));
            Stage = 4;
            Next = Now + .3;
            return false;
        }
        if (Stage == 4)
        {
            CheckFile(TEXT("polish-friendly-push"));
            // AI teammates on: they regroup around the player and open fire on visible monsters.
            B.config.aiEnabled = true;
            auto* Head = B.find(P->UnitId);
            gwgame::Unit Target;
            Target.id = 1200;
            Target.team = 2;
            Target.kind = gwgame::UnitKind::Monster;
            Target.position = Head->position + gwgame::Vec2{0, 900};
            Target.hp = Target.maxHp = 100000;
            B.units.push_back(Target);
            MonsterHP = 0;
            for (const auto& U : B.units)
                if (U.kind == gwgame::UnitKind::Monster && U.alive)
                    MonsterHP += U.hp;
            S->Publish();
            Stage = 5;
            Next = Now + 1.6;
            return false;
        }
        if (Stage == 5)
        {
            const auto* Head = B.find(P->UnitId);
            bool Fired = false, Close = true;
            for (int32 Id : Bots)
                if (const auto* Bot = B.find(Id))
                {
                    Fired |= Bot->ammo < gwgame::weaponStats(Bot->weapon).ammo || Bot->reload > 0;
                    Close &= (Bot->position - Head->position).length() < 650;
                }
            Test->TestTrue(TEXT("AI teammates shoot monsters"), Fired);
            Test->TestTrue(TEXT("AI teammates regroup around the player"), Close);
            double Left = 0;
            for (const auto& U : B.units)
                if (U.kind == gwgame::UnitKind::Monster && U.alive)
                    Left += U.hp;
            Test->TestTrue(TEXT("Monsters take damage from the team"), Left < MonsterHP);
            S->Publish();
            Capture(TEXT("polish-bots"));
            Stage = 6;
            Next = Now + .25;
            return false;
        }
        if (Stage == 6)
        {
            CheckFile(TEXT("polish-bots"));
            C->ServerCharge(0, {1, 0});
            Test->TestTrue(TEXT("Charge starts with a squad"), B.find(P->UnitId)->charging);
            Stage = 7;
            Next = Now + .2;
            return false;
        }
        if (Stage == 7)
        {
            const int Tail = B.tailIds(P->UnitId).empty() ? -1 : B.tailIds(P->UnitId).back();
            C->ServerCharge(2, {0, 0});
            Test->TestFalse(TEXT("Charge can cancel without throwing"), B.find(P->UnitId)->charging);
            Test->TestTrue(TEXT("Cancelled charge keeps companion"), Tail > 0 && B.find(Tail)->followerOf == P->UnitId);
            // The first elite of the schedule: regular waves stay capped so the encounter reads clearly.
            B.config.enemiesEnabled = B.config.bossEnabled = true;
            B.config.maxMonsters = 0;
            B.elapsed = gwgame::BossSchedule[0] - .05;
            B.advance(.1);
            gwgame::Unit* Elite = nullptr;
            for (auto& U : B.units)
                if (U.alive && U.kind == gwgame::UnitKind::Monster && U.radius >= 100)
                    Elite = &U;
            Test->TestNotNull(TEXT("Scheduled elite arrives"), Elite);
            if (Elite)
            {
                Elite->position = B.find(P->UnitId)->position + gwgame::Vec2{520, -260};
                Elite->cooldown = 0;
            }
            B.advance(.05);
            S->Publish();
            Test->TestTrue(TEXT("Elite health bar replicated"), S->BossActive && S->BossHP > 0);
            Stage = 8;
            Next = Now + .5;
            return false;
        }
        if (Stage == 8)
        {
            Capture(TEXT("polish-elite"));
            Stage = 9;
            Next = Now + .3;
            return false;
        }
        CheckFile(TEXT("polish-elite"));
        S->Finished = true;
        G->ReturnToLobby(C);
        C->SetActorTickEnabled(true);
        return true;
    }

private:
    FAutomationTestBase* Test;
    double Start = 0, Next = 0, MonsterHP = 0;
    int Stage = 0, First = -1;
    TArray<int32> Bots;
};
} // namespace
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGWCombatPolishTest, "GW.Standalone.CombatPolish",
                                 EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FGWCombatPolishTest::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(FCombatPolishCheck(this));
    return true;
}
#endif
