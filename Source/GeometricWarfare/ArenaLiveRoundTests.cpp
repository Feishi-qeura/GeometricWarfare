#include "ArenaGameMode.h"
#include "ArenaLiveRounds.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/ScopeExit.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace {
struct FRecordedRoundCommand {FString Id,Operation;TSharedPtr<FJsonObject> Payload;};
class FRoundAutomationProvider final : public ILiveInteractionProvider {
public:
    FLiveSession Session;
    FString Platform=TEXT("round-test-")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
    TArray<FRecordedRoundCommand> Commands;
    FString GetPlatformId() const override {return Platform;}
    bool Start(ULiveInteractionSubsystem& Host) override {Session=Host.BeginProviderSession(*this,TEXT("app"),TEXT("room"),TEXT("anchor"));return Session.Nonce.IsValid();}
    void Stop() override {}
    bool SendCommand(const FString& Id,const FString& Op,const TSharedRef<FJsonObject>& Data) override {Commands.Add({Id,Op,Data});return true;}
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArenaLiveRoundTest,"GeometricWarfare.Arena.LiveRoundReporting",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FArenaLiveRoundTest::RunTest(const FString&) {
    bool OldBackend=true;const bool HadBackend=GConfig->GetBool(TEXT("LiveInteraction"),TEXT("WorldLeaderboardEnabled"),OldBackend,GGameIni);
    GConfig->SetBool(TEXT("LiveInteraction"),TEXT("WorldLeaderboardEnabled"),false,GGameIni);
    const TCHAR* Keys[]={TEXT("RedGroupId"),TEXT("BlueGroupId"),TEXT("GrayGroupId")};
    FString Old[3];bool Had[3];
    for(int32 I=0;I<3;++I){Had[I]=GConfig->GetString(TEXT("LiveInteraction"),Keys[I],Old[I],GGameIni);GConfig->SetString(TEXT("LiveInteraction"),Keys[I],I==0?TEXT("red-test"):I==1?TEXT("blue-test"):TEXT("gray-test"),GGameIni);}
    auto Provider=MakeShared<FRoundAutomationProvider>();
    FLiveInteractionProviderRegistry::Register(Provider->Platform,[Provider]() -> TSharedPtr<ILiveInteractionProvider> {return Provider;});
    auto* Instance=NewObject<UGameInstance>(GEngine);Instance->InitializeStandalone(FName(TEXT("LiveRoundWorld")));
    auto* Bridge=Instance->GetSubsystem<ULiveInteractionSubsystem>();Bridge->StartPlatform(Provider->Platform);
    auto* World=Instance->GetWorld();FURL Url;Url.AddOption(TEXT("game=/Script/GeometricWarfare.ArenaGameMode"));
    World->SetGameMode(Url);World->InitializeActorsForPlay(Url);World->BeginPlay();
    ON_SCOPE_EXIT {
        if(HadBackend)GConfig->SetBool(TEXT("LiveInteraction"),TEXT("WorldLeaderboardEnabled"),OldBackend,GGameIni);else GConfig->RemoveKey(TEXT("LiveInteraction"),TEXT("WorldLeaderboardEnabled"),GGameIni);
        World->EndPlay(EEndPlayReason::Quit);World->DestroyWorld(false);Instance->Shutdown();GEngine->DestroyWorldContext(World);
        FLiveInteractionProviderRegistry::Unregister(Provider->Platform);
        for(int32 I=0;I<3;++I)if(Had[I])GConfig->SetString(TEXT("LiveInteraction"),Keys[I],*Old[I],GGameIni);else GConfig->RemoveKey(TEXT("LiveInteraction"),Keys[I],GGameIni);
    };
    auto* Game=World->GetAuthGameMode<AArenaGameMode>();
    if(!TestNotNull(TEXT("Round fixture created"),Game))return false;
    if(!TestTrue(TEXT("Round arena uses the fixture game instance and authenticated bridge"),Game->GetGameInstance()==Instance && Game->GetBridge()==Bridge && Bridge->IsEventFromCurrentSession(Provider->Session)))return false;
    Game->TickLiveRound();
    if(!TestEqual(TEXT("Only start is submitted before its result"),Provider->Commands.Num(),1)) {
        FString Red,Blue,Gray;GConfig->GetString(TEXT("LiveInteraction"),Keys[0],Red,GGameIni);GConfig->GetString(TEXT("LiveInteraction"),Keys[1],Blue,GGameIni);GConfig->GetString(TEXT("LiveInteraction"),Keys[2],Gray,GGameIni);
        AddError(FString::Printf(TEXT("Start diagnostic: state=%s connected=%d groups=%s/%s/%s counterFile=%d LastEvent=%s"),*Bridge->ConnectionStatus,Bridge->IsConnected(),*Red,*Blue,*Gray,FPaths::FileExists(FPaths::ProjectSavedDir()/TEXT("LivePlatform/RoundCounters.json")),*Game->LastEvent));
        return false;
    }
    auto Start=Provider->Commands.Last();const int64 RoundId=static_cast<int64>(Start.Payload->GetNumberField(TEXT("round_id")));
    TestEqual(TEXT("Start operation uses status 1"),Start.Payload->GetNumberField(TEXT("status")),1.0);
    TestEqual(TEXT("Transport submission alone does not start simulation"),Game->LiveRoundId,int64(0));
    TestFalse(TEXT("Host assist waits for accepted platform round"),Game->CanHostAssist());
    Game->Tick(.1f);TestEqual(TEXT("Simulation stays frozen before accepted start"),Game->Match.elapsed,0.0);
    Bridge->ReportCommandResult(*Provider,Start.Id,false,42);Game->TickLiveRound();
    TestEqual(TEXT("Failed start backs off without moving to another operation"),Provider->Commands.Num(),1);
    Game->LiveRoundReporter->RetryAfter=0;Game->TickLiveRound();Start=Provider->Commands.Last();
    TestEqual(TEXT("Retry keeps persisted round identity"),static_cast<int64>(Start.Payload->GetNumberField(TEXT("round_id"))),RoundId);
    Bridge->ReportCommandResult(*Provider,Start.Id,true,0);TestEqual(TEXT("Accepted start unlocks this round"),Game->LiveRoundId,RoundId);
    TestTrue(TEXT("Accepted platform round enables host assist"),Game->CanHostAssist());
    // Seed actual gameplay participants with ordered scores, including users
    // beyond the room leaderboard limit and gray spectators with kills.
    for(int32 I=1;I<=174;++I){const int32 Team=I>171?0:(I%2?1:2);const FString User=FString::Printf(TEXT("user-%03d"),I);
        Game->Match.add(I,gw::Shape::Triangle,Team);auto* Fighter=Game->Match.findFighter(I);Fighter->score=I>171?9999:10000-I;Fighter->kills=I;
        FViewerState Viewer;Viewer.UserId=User;Viewer.Name=User;Viewer.BodyId=I;Viewer.Team=Team;Game->Viewers.Add(User,Viewer);Game->Identities.Add(I,User);}
    Game->TickLiveRound();const auto PendingGroup=Provider->Commands.Last();
    TestEqual(TEXT("Joined participant reports configured group"),PendingGroup.Operation,FString(TEXT("user_group")));
    // A gray user can choose red while an older gray group request is pending.
    // The end snapshot must preserve both ordered receipts and finish on red.
    const FString ChangedUser=PendingGroup.Payload->GetStringField(TEXT("open_id"));
    auto& ChangedViewer=Game->Viewers.FindChecked(ChangedUser);
    ChangedViewer.Team=ChangedViewer.Team==1?2:1;
    auto* ChangedFighter=Game->Match.findFighter(ChangedViewer.BodyId);ChangedFighter->team=ChangedViewer.Team;
    if(ChangedViewer.BodyId>171)ChangedFighter->score=0; // Keep room top150 baseline stable.
    TSet<FString> GrayUsers;for(const auto& Pair:Game->Viewers)if(Pair.Value.Team==0)GrayUsers.Add(Pair.Key);
    Game->Match.phase=gw::Phase::Results;Game->Match.winnerTeam=1;Game->Match.intermissionRemaining=30;
    TestFalse(TEXT("Results disable production host assist"),Game->CanHostAssist());
    Game->Tick(.1f);TestEqual(TEXT("Results freeze while prior group receipt is pending"),Game->Match.intermissionRemaining,30.0);
    TestEqual(TEXT("End is not submitted ahead of the prior group receipt"),Provider->Commands.Last().Id,PendingGroup.Id);
    Bridge->ReportCommandResult(*Provider,PendingGroup.Id,true,0);
    TSet<FString> SyncedGroups;SyncedGroups.Add(PendingGroup.Payload->GetStringField(TEXT("open_id")));
    bool GroupFailureExercised=false;FRecordedRoundCommand End;
    for(int32 I=0;I<175;++I){Game->TickLiveRound();const auto Command=Provider->Commands.Last();
        if(Command.Operation==TEXT("round")){End=Command;break;}
        if(!TestEqual(TEXT("All outstanding participant groups precede end"),Command.Operation,FString(TEXT("user_group"))))break;
        const FString User=Command.Payload->GetStringField(TEXT("open_id"));
        if(User!=ChangedUser)TestFalse(TEXT("Confirmed unchanged participant group is not queued twice"),SyncedGroups.Contains(User));
        if(!GroupFailureExercised){GroupFailureExercised=true;Bridge->ReportCommandResult(*Provider,Command.Id,false,45);Game->LiveRoundReporter->RetryAfter=0;Game->TickLiveRound();
            TestEqual(TEXT("Group failure retries before end"),Provider->Commands.Last().Operation,FString(TEXT("user_group")));
            TestTrue(TEXT("Group retry retains exact participant payload"),Provider->Commands.Last().Payload==Command.Payload);
            Bridge->ReportCommandResult(*Provider,Provider->Commands.Last().Id,true,0);
        }else Bridge->ReportCommandResult(*Provider,Command.Id,true,0);
        SyncedGroups.Add(User);
    }
    TestTrue(TEXT("Group failure regression exercised"),GroupFailureExercised);
    TestEqual(TEXT("All 174 gray/red/blue groups confirmed before end"),SyncedGroups.Num(),174);
    TestEqual(TEXT("Pending older group cannot suppress final changed group"),Game->LiveRoundReporter->ReportedGroups.FindRef(ChangedUser),ChangedViewer.Team==1?FString(TEXT("red-test")):FString(TEXT("blue-test")));
    if(!TestTrue(TEXT("End submitted after every group receipt"),End.Payload.IsValid()))return false;
    TestEqual(TEXT("End follows all group receipts"),End.Operation,FString(TEXT("round")));TestEqual(TEXT("End has status 2"),End.Payload->GetNumberField(TEXT("status")),2.0);
    TestEqual(TEXT("All three group results are present"),End.Payload->GetArrayField(TEXT("group_results")).Num(),3);
    Bridge->ReportCommandResult(*Provider,End.Id,false,43);Game->LiveRoundReporter->RetryAfter=0;Game->TickLiveRound();
    TestEqual(TEXT("End failure retries end before user results"),Provider->Commands.Last().Operation,FString(TEXT("round")));
    Bridge->ReportCommandResult(*Provider,Provider->Commands.Last().Id,true,0);
    TSet<FString> Uploaded;int32 Batches=0;bool FailedBatch=false,CompleteSeen=false;
    for(int32 Step=0;Step<16 && !CompleteSeen;++Step){Game->TickLiveRound();const auto Command=Provider->Commands.Last();
        TestEqual(TEXT("Every snapshot operation keeps original round ID"),static_cast<int64>(Command.Payload->GetNumberField(TEXT("round_id"))),RoundId);
        if(Command.Operation==TEXT("user_results")){
            if(Batches==1 && !FailedBatch){FailedBatch=true;Bridge->ReportCommandResult(*Provider,Command.Id,false,44);Game->LiveRoundReporter->RetryAfter=0;Game->TickLiveRound();
                TestEqual(TEXT("Failed batch cannot advance to rank or complete"),Provider->Commands.Last().Operation,FString(TEXT("user_results")));
                TestTrue(TEXT("Failed batch snapshot identity is retained"),Provider->Commands.Last().Payload==Command.Payload);
                // Mutating live score after snapshot creation must not alter upload.
                Game->Match.findFighter(1)->score=0;
                Bridge->ReportCommandResult(*Provider,Provider->Commands.Last().Id,true,0);
            }else Bridge->ReportCommandResult(*Provider,Command.Id,true,0);
            const auto& Users=Command.Payload->GetArrayField(TEXT("users"));TestTrue(TEXT("User results batches contain at most 50 users"),Users.Num()<=50);
            for(const auto& Value:Users){const auto User=Value->AsObject();const FString Id=User->GetStringField(TEXT("open_id"));TestFalse(TEXT("User appears only once across confirmed batches"),Uploaded.Contains(Id));Uploaded.Add(Id);
                if(GrayUsers.Contains(Id)){TestEqual(TEXT("Gray rank uses no competitive place"),User->GetNumberField(TEXT("rank")),1000.0);TestEqual(TEXT("Gray result is participation"),User->GetNumberField(TEXT("result")),3.0);TestEqual(TEXT("Gray score remains zero"),User->GetNumberField(TEXT("score")),0.0);}}
            ++Batches;
        }else if(Command.Operation==TEXT("room_rank")){
            TestEqual(TEXT("All participant results confirmed before leaderboard"),Uploaded.Num(),174);const auto& Users=Command.Payload->GetArrayField(TEXT("users"));TestEqual(TEXT("Room leaderboard contains top 150 red and blue participants"),Users.Num(),150);
            for(int32 I=0;I<Users.Num();++I)TestEqual(TEXT("Leaderboard frozen order excludes gray and retains pre-mutation score"),Users[I]->AsObject()->GetStringField(TEXT("open_id")),FString::Printf(TEXT("user-%03d"),I+1));
            Bridge->ReportCommandResult(*Provider,Command.Id,true,0);
        }else if(Command.Operation==TEXT("complete")){CompleteSeen=true;TestEqual(TEXT("All four user batches precede completion"),Batches,4);TestEqual(TEXT("Completion transport does not resume simulation"),Game->LiveRoundId,int64(0));Bridge->ReportCommandResult(*Provider,Command.Id,true,0);}
        else {AddError(TEXT("Unexpected operation in result upload sequence"));break;}
    }
    TestTrue(TEXT("Completion is eventually submitted after all receipts"),CompleteSeen);TestTrue(TEXT("Batch failure regression was exercised"),FailedBatch);TestEqual(TEXT("Completion receipt resumes result intermission"),Game->LiveRoundId,RoundId);
    // Reconnect invalidates requests and the reporter, then requires a fresh
    // authenticated start receipt. A late receipt cannot unlock the new arena.
    ++Game->Match.round;Game->Match.phase=gw::Phase::Battle;
    TestFalse(TEXT("Previous round receipt cannot enable host in next battle before reporter tick"),Game->CanHostAssist());
    Game->TickLiveRound();TestFalse(TEXT("New round submission still waits for its own receipt"),Game->CanHostAssist());const auto OldStart=Provider->Commands.Last();
    Provider->Session=Bridge->BeginProviderSession(*Provider,TEXT("app"),TEXT("new-room"),TEXT("anchor"));Game->TickLiveRound();const auto NewStart=Provider->Commands.Last();
    TestNotEqual(TEXT("New session has a distinct start request"),NewStart.Id,OldStart.Id);Bridge->ReportCommandResult(*Provider,OldStart.Id,true,0);TestEqual(TEXT("Old-session receipt cannot unlock new simulation"),Game->LiveRoundId,int64(0));
    Bridge->ReportCommandResult(*Provider,NewStart.Id,true,0);TestTrue(TEXT("New session unlocks only after its own receipt"),Game->LiveRoundId>0);
    return true;
}
#endif
