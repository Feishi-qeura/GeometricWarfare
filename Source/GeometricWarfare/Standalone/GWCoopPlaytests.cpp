#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GWController.h"
#include "GWGameMode.h"
#include "GWGameState.h"
#include "GWPlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
// Two processes on one machine share a room over a direct IP connection, so the room rules can be checked
// without a Steam or Epic session: GW.Standalone.CoopHost listens, GW.Standalone.CoopClient joins 127.0.0.1.
// Covers ready/start, joining a friend's squad, shared pickups, the collective upgrade pause, friendly
// fire that shoves without hurting, and host-only saving.
namespace
{
class FCoopRoom : public IAutomationLatentCommand
{
public:
    FCoopRoom(FAutomationTestBase* T, bool H) : Test(T), Host(H), Start(FPlatformTime::Seconds()) {}
    bool Update() override
    {
        double Now = FPlatformTime::Seconds();
        if (Now - Start > 90)
        {
            Test->AddError(TEXT("Cooperative room check timed out"));
            return true;
        }
        auto* W = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
        auto* C = W ? Cast<AGWController>(W->GetFirstPlayerController()) : nullptr;
        auto* S = W ? W->GetGameState<AGWGameState>() : nullptr;
        auto* P = C ? C->GetPlayerState<AGWPlayerState>() : nullptr;
        if (!C || !S || !P)
            return false;
        if (Leaving)
        {
            if (!S->IsRoom && W->GetNetMode() == NM_Standalone)
            {
                C->SetActorTickEnabled(true);
                return true;
            }
            return false;
        }
        if (!S->IsRoom)
        {
            // The client retries until the host is listening; a pending connection is left alone.
            const ENetMode Mode = W->GetNetMode();
            if (Now >= Next && Mode != NM_Client && Mode != NM_ListenServer)
            {
                if (Host)
                    UGameplayStatics::OpenLevel(W, FName(TEXT("/Engine/Maps/Entry")), true, TEXT("listen?GWRoom=1"));
                else
                    GEngine->Exec(W, TEXT("open 127.0.0.1"));
                Next = Now + (Host ? 10 : 4);
            }
            return false;
        }
        C->SetActorTickEnabled(false);
        // The room opens into its hub; the guest waits there for the host to start the run.
        if (!Host && S->Hub())
            return false;
        if (Host && S->Hub())
        {
            if (S->PlayerArray.Num() < 2)
                return false;
            for (const auto& Item : S->PlayerArray)
                if (auto* Other = Cast<AGWPlayerState>(Item); Other && Other != P && Other->UnitId < 0)
                    return false;
            Test->TestTrue(TEXT("Host starts Survival from the hub"), W->GetAuthGameMode<AGWGameMode>()->BeginRound(C, false));
            auto& B = *S->Simulation;
            int32 Bots = 0;
            for (const auto& U : B.units)
                Bots += U.bot;
            Test->TestEqual(TEXT("Online rooms bring no AI teammates"), Bots, 0);
            Test->TestEqual(TEXT("Both players joined the run"), B.humanCount(), 2);
            B.config.enemiesEnabled = B.config.bossEnabled = false;
            auto* Head = B.find(P->UnitId);
            Head->position = {6200, 6200};
            for (auto& U : B.units)
                if (U.human && U.id != Head->id)
                    U.position = Head->position + gwgame::Vec2{90, 0};
            gwgame::Unit A;
            A.id = 1001;
            A.kind = gwgame::UnitKind::Companion;
            A.state = gwgame::UnitState::Idle;
            A.position = Head->position + gwgame::Vec2{-70, 0};
            B.units.push_back(A);
            Test->TestEqual(TEXT("Host recruits a companion"), B.recruit(Head->id), 1001);
            S->Publish();
            return false;
        }
        if (!S->InMatch || S->Hub())
            return false;
        const auto* Local = S->FindUnit(P->UnitId);
        if (!Local)
            return false;
        if (Host)
            return HostStep(W, C, S, P, Now);
        // Squad toggles are retried slowly: the server drops actions sent too close together, and a
        // retry only goes out once replication has had ample time to show the previous one landing.
        if (!Selected && !S->UpgradePaused && Local->FollowerOf < 0 && Now >= Retry)
        {
            C->ServerAction(5, 0);
            Retry = Now + 1;
            return false;
        }
        if (S->UpgradePaused && Local->PendingUpgrades > 0 && !Selected)
        {
            Test->TestTrue(TEXT("Human voluntarily joined squad"), Local->FollowerOf >= 0);
            Test->TestEqual(TEXT("Weapon shared to remote human"), static_cast<int>(Local->Weapon), 3);
            const auto* Ally = S->FindUnit(1001);
            Test->TestTrue(TEXT("Companion equipment replicated"), Ally && Ally->Weapon == 3);
            Frozen = S->Elapsed;
            C->ServerAction(7, 0);
            Selected = true;
            return false;
        }
        if (Selected && S->UpgradePaused && Local->PendingUpgrades == 0)
        {
            Test->TestEqual(TEXT("Remote clock frozen while waiting"), static_cast<double>(S->Elapsed), Frozen);
            SawWaiting = true;
            return false;
        }
        if (Selected && SawWaiting && !S->UpgradePaused && !Pushed && Local->FollowerOf >= 0 && Now >= Retry)
        {
            // Out of the host's squad, the host's bullets become friendly fire from another formation.
            C->ServerAction(5, 0);
            LeftSquad = true;
            Retry = Now + 1;
            return false;
        }
        if (LeftSquad && !Pushed)
        {
            if (Local->FollowerOf >= 0)
                return false;
            if (HP < 0)
            {
                HP = Local->HP;
                Before = Local->Position;
            }
            if (Local->Impulse.X > 40 || Local->Position.X > Before.X + 20)
            {
                Pushed = true;
                Test->TestEqual(TEXT("Friendly bullet deals no damage online"), Local->HP, static_cast<float>(HP));
                C->ServerAction(9, 0);
                Next = Now + 1;
            }
            return false;
        }
        if (Pushed && Now >= Next)
        {
            Test->TestEqual(TEXT("Only the host can save a room"), C->NoticeKey, FString(TEXT("Save.HostOnly")));
            UGameplayStatics::OpenLevel(W, FName(TEXT("/Engine/Maps/Entry")));
            Leaving = true;
        }
        return false;
    }

private:
    bool HostStep(UWorld* W, AGWController* C, AGWGameState* S, AGWPlayerState* P, double Now)
    {
        auto& B = *S->Simulation;
        gwgame::Unit* Other = nullptr;
        for (auto& U : B.units)
            if (U.human && U.id != P->UnitId)
                Other = &U;
        if (Other)
            OtherIdentity = Other->owner;
        if (!Upgraded && Other && Other->followerOf == P->UnitId)
        {
            Test->TestTrue(TEXT("Squad pickup equips everyone"), B.pickupWeapon(P->UnitId, 3));
            B.addExperience(100);
            Frozen = B.elapsed;
            Upgraded = true;
            S->Publish();
            return false;
        }
        if (Upgraded && !Resumed && Other && Other->pendingUpgrades == 0)
        {
            Test->TestTrue(TEXT("Host still holds collective pause"), B.upgradePaused());
            Test->TestEqual(TEXT("Clock frozen after remote choice"), B.elapsed, Frozen);
            if (WaitUntil == 0)
            {
                WaitUntil = Now + 1;
                return false;
            }
            if (Now < WaitUntil)
                return false;
            Test->TestTrue(TEXT("Host skill choice resumes room"), B.chooseUpgrade(P->UnitId, 0));
            Resumed = true;
            S->Publish();
            return false;
        }
        if (Resumed && !Shot && Other && Other->followerOf < 0)
        {
            // A sniper round from the host crosses the friend who just left the squad.
            OtherId = Other->id;
            OtherHP = Other->hp;
            OtherStart = Other->position;
            gwgame::Projectile Round;
            Round.id = 80000;
            Round.owner = Round.squad = P->UnitId;
            Round.team = 1;
            Round.weapon = 3;
            Round.damage = 95;
            Round.position = Other->position - gwgame::Vec2{80, 0};
            Round.velocity = {2400, 0};
            Round.remaining = .1;
            B.projectiles.push_back(Round);
            Shot = true;
            Next = Now + .5;
            return false;
        }
        if (Shot && !Checked && Now >= Next)
        {
            if (const auto* Friend = B.find(OtherId))
            {
                Test->TestEqual(TEXT("Friendly bullet deals no damage"), Friend->hp, OtherHP);
                Test->TestTrue(TEXT("Friendly bullet pushes the friend"), Friend->position.x > OtherStart.x + 20);
            }
            Checked = true;
            return false;
        }
        if (Checked && S->PlayerArray.Num() == 1)
        {
            Test->TestTrue(TEXT("World clock resumed"), B.elapsed > Frozen);
            bool Reserved = false;
            for (const auto& U : B.units)
                Reserved |= U.kind == gwgame::UnitKind::Fighter && !U.human && U.previousOwner == OtherIdentity;
            Test->TestTrue(TEXT("Departed friend keeps their character for a return"), Reserved);
            S->Finished = true;
            W->GetAuthGameMode<AGWGameMode>()->ReturnToLobby(C);
            UGameplayStatics::OpenLevel(W, FName(TEXT("/Engine/Maps/Entry")));
            Leaving = true;
        }
        return false;
    }
    FAutomationTestBase* Test;
    bool Host = false, Selected = false, SawWaiting = false, LeftSquad = false,
         Pushed = false, Upgraded = false, Resumed = false, Shot = false, Checked = false, Leaving = false;
    double Start = 0, Next = 0, Retry = 0, Frozen = 0, WaitUntil = 0, HP = -1, OtherHP = 0;
    int32 OtherId = -1, OtherIdentity = -1;
    FVector Before = FVector::ZeroVector;
    gwgame::Vec2 OtherStart{};
};
} // namespace
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGWCoopHost, "GW.Standalone.CoopHost",
                                 EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FGWCoopHost::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(FCoopRoom(this, true));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGWCoopClient, "GW.Standalone.CoopClient",
                                 EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FGWCoopClient::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(FCoopRoom(this, false));
    return true;
}
#endif
