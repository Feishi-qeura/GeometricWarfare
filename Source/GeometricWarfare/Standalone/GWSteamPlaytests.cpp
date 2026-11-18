#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/NetDriver.h"
#include "GWGameInstance.h"
#include "GWGameState.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemNames.h"
#include "OnlineSubsystemUtils.h"

// Runs against the real signed-in Steam client. Use a separate -saveddirsuffix, never player saves.
class FGWSteamSmokeCheck : public IAutomationLatentCommand
{
public:
    explicit FGWSteamSmokeCheck(FAutomationTestBase* InTest) : Test(InTest), Started(FPlatformTime::Seconds()) {}
    bool Update() override
    {
        UWorld* W = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
        auto* GI = W ? W->GetGameInstance<UGWGameInstance>() : nullptr;
        if (FPlatformTime::Seconds() - Started > 45)
        {
            Test->AddError(TEXT("Steam lobby smoke test timed out"));
            return true;
        }
        if (!GI || !W->GetFirstPlayerController()) return false;
        if (Stage == 0)
        {
            if (!Test->TestTrue(TEXT("Steam provider is ready"), GI->PlatformReady(EFU_OnlineProvider::Steam))) return true;
            Test->TestEqual(TEXT("Steamworks uses the development AppID"), GI->SteamAppId(), 480);
            Test->TestFalse(TEXT("Steam account nickname is available"), GI->PlatformAccount(EFU_OnlineProvider::Steam).IsEmpty());
            Stage = 1;
            GI->OpenHub();
            return false;
        }
        auto* State = W->GetGameState<AGWGameState>();
        if (GI->Busy || !State || !State->Hub()) return false;
        Test->TestTrue(TEXT("Hub is a Steam room"), GI->RoomKind() == EGWRoomKind::Steam);
        Test->TestTrue(TEXT("Hub listens for remote players"), W->GetNetMode() == NM_ListenServer);
        UNetDriver* Driver = W->GetNetDriver();
        Test->TestTrue(TEXT("SteamSockets transports gameplay"), Driver && Driver->GetClass()->GetName() == TEXT("SteamSocketsNetDriver"));
        IOnlineSubsystem* Steam = Online::GetSubsystem(W, STEAM_SUBSYSTEM);
        const IOnlineSessionPtr Sessions = Steam ? Steam->GetSessionInterface() : nullptr;
        const FNamedOnlineSession* Room = Sessions.IsValid() ? Sessions->GetNamedSession(NAME_GameSession) : nullptr;
        if (Test->TestTrue(TEXT("Steam lobby exists"), Room && Room->SessionInfo.IsValid()))
        {
            Test->TestTrue(TEXT("Lobby supports Steam invitations"), Room->SessionSettings.bAllowInvites);
            Test->TestTrue(TEXT("Lobby uses Steam lobbies"), Room->SessionSettings.bUseLobbiesIfAvailable);
            Test->TestTrue(TEXT("Lobby is friends-only"), Room->SessionSettings.bAllowJoinViaPresenceFriendsOnly);
            Test->TestEqual(TEXT("Lobby capacity"), Room->SessionSettings.NumPublicConnections, UGWGameInstance::RoomCapacity);
        }
        GI->RefreshFriendRooms();
        if (FParse::Param(FCommandLine::Get(), TEXT("GWTestSteamInviteUI")))
            Test->TestTrue(TEXT("Game opens Steam's invitation UI for this lobby"), GI->ShowInvite());
        return true;
    }
private:
    FAutomationTestBase* Test;
    double Started;
    int32 Stage = 0;
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGWSteamSmoke, "GW.Steam.Smoke", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FGWSteamSmoke::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(FGWSteamSmokeCheck(this));
    return true;
}
#endif
