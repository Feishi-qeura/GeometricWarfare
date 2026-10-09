#include "ArenaGameMode.h"
#include "ArenaLiveRounds.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/ScopeExit.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace {
struct FBackendCommand {FString Id,Operation;TSharedPtr<FJsonObject> Payload;};
class FBackendRoundProvider final : public ILiveInteractionProvider {
public:
    FLiveSession Session;TArray<FBackendCommand> Commands;
    FString Platform=TEXT("backend-test-")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
    FString GetPlatformId() const override {return Platform;}
    bool Start(ULiveInteractionSubsystem& Host) override {Session=Host.BeginProviderSession(*this,TEXT("backend-app"),TEXT("backend-room"),TEXT("anchor"));return Session.Nonce.IsValid();}
    void Stop() override {}
    bool SendCommand(const FString& Id,const FString& Op,const TSharedRef<FJsonObject>& Data) override {Commands.Add({Id,Op,Data});return true;}
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArenaBackendRoundTest,"GeometricWarfare.Arena.BackendRound",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FArenaBackendRoundTest::RunTest(const FString&) {
    const TCHAR* Keys[]={TEXT("RedGroupId"),TEXT("BlueGroupId"),TEXT("GrayGroupId")};FString Old[3];bool Had[3];
    const FString Groups[]={TEXT("red-")+FString::ChrN(124,TEXT('r')),TEXT("blue-")+FString::ChrN(123,TEXT('b')),TEXT("gray-")+FString::ChrN(123,TEXT('g'))};
    for(int32 I=0;I<3;++I){Had[I]=GConfig->GetString(TEXT("LiveInteraction"),Keys[I],Old[I],GGameIni);GConfig->SetString(TEXT("LiveInteraction"),Keys[I],*Groups[I],GGameIni);}
    bool OldBackend=true;const bool HadBackend=GConfig->GetBool(TEXT("LiveInteraction"),TEXT("WorldLeaderboardEnabled"),OldBackend,GGameIni);GConfig->SetBool(TEXT("LiveInteraction"),TEXT("WorldLeaderboardEnabled"),true,GGameIni);
    auto Provider=MakeShared<FBackendRoundProvider>();FLiveInteractionProviderRegistry::Register(Provider->Platform,[Provider]() -> TSharedPtr<ILiveInteractionProvider> {return Provider;});
    auto* Instance=NewObject<UGameInstance>(GEngine);Instance->InitializeStandalone(FName(TEXT("BackendRoundWorld")));
    auto* Bridge=Instance->GetSubsystem<ULiveInteractionSubsystem>();Bridge->StartPlatform(Provider->Platform);
    auto* World=Instance->GetWorld();FURL Url;Url.AddOption(TEXT("game=/Script/GeometricWarfare.ArenaGameMode"));World->SetGameMode(Url);World->InitializeActorsForPlay(Url);World->BeginPlay();
    FString OutboxPath;
    ON_SCOPE_EXIT {
        FArenaLiveRoundOutbox::Remove(OutboxPath);World->EndPlay(EEndPlayReason::Quit);World->DestroyWorld(false);Instance->Shutdown();GEngine->DestroyWorldContext(World);FLiveInteractionProviderRegistry::Unregister(Provider->Platform);
        for(int32 I=0;I<3;++I)if(Had[I])GConfig->SetString(TEXT("LiveInteraction"),Keys[I],*Old[I],GGameIni);else GConfig->RemoveKey(TEXT("LiveInteraction"),Keys[I],GGameIni);
        if(HadBackend)GConfig->SetBool(TEXT("LiveInteraction"),TEXT("WorldLeaderboardEnabled"),OldBackend,GGameIni);else GConfig->RemoveKey(TEXT("LiveInteraction"),TEXT("WorldLeaderboardEnabled"),GGameIni);
    };
    auto* Game=World->GetAuthGameMode<AArenaGameMode>();if(!TestNotNull(TEXT("Backend fixture game exists"),Game))return false;
    Game->TickLiveRound();if(!TestEqual(TEXT("Backend fixture starts through authenticated provider"),Provider->Commands.Num(),1))return false;
    Bridge->ReportCommandResult(*Provider,Provider->Commands.Last().Id,true,0);
    const int64 RoundId=Game->LiveRoundId;
    // Real simulation participants exercise serialization of Int64 scores and a
    // complete maximum-size identity set. Group reporting has its own regression.
    const int32 ParticipantCount=gw::Match::ViewerCapacity,RedCapacity=gw::Match::TeamCapacity(1),BlueCapacity=gw::Match::TeamCapacity(2);
    const int32 ExpectedBatches=(ParticipantCount+49)/50;
    for(int32 I=1;I<=ParticipantCount;++I){const int32 Team=I<=RedCapacity?1:I<=RedCapacity+BlueCapacity?2:0;const FString Id=FString::Printf(TEXT("participant-%04d-"),I)+FString::ChrN(239,TEXT('x'));
        if(!Game->Match.add(I,gw::Shape::Triangle,Team)){AddError(TEXT("Maximum-size fixture must respect configured team limits"));return false;}auto* Fighter=Game->Match.findFighter(I);Fighter->score=I==1?MAX_int64:I==2?9007199254740990LL:ParticipantCount-I;
        FViewerState Viewer;Viewer.UserId=Id;Viewer.Name=Id;Viewer.BodyId=I;Viewer.Team=Team;Game->Viewers.Add(Id,Viewer);Game->Identities.Add(I,Id);
        Game->LiveRoundReporter->ReportedGroups.Add(Id,Groups[Team==1?0:Team==2?1:2]);}
    Game->Match.phase=gw::Phase::Results;Game->Match.winnerTeam=1;Game->Match.intermissionRemaining=30;Game->TickLiveRound();
    if(!TestEqual(TEXT("SDK end precedes backend submission"),Provider->Commands.Last().Operation,FString(TEXT("round"))))return false;
    OutboxPath=Game->LiveRoundReporter->OutboxPath;TestTrue(TEXT("Full settlement is durable before SDK end receipt"),IFileManager::Get().FileSize(*OutboxPath)>0);
    Bridge->ReportCommandResult(*Provider,Provider->Commands.Last().Id,true,0);Game->TickLiveRound();
    if(!TestEqual(TEXT("Backend snapshot precedes every SDK result batch"),Provider->Commands.Last().Operation,FString(TEXT("backend_round"))))return false;
    auto Command=Provider->Commands.Last();const auto Snapshot=Command.Payload;const auto& Users=Snapshot->GetArrayField(TEXT("users"));
    TestEqual(TEXT("Backend includes all real participants"),Users.Num(),ParticipantCount);TestEqual(TEXT("Snapshot app identity"),Snapshot->GetStringField(TEXT("app_id")),FString(TEXT("backend-app")));TestEqual(TEXT("Snapshot room identity"),Snapshot->GetStringField(TEXT("room_id")),FString(TEXT("backend-room")));
    TestTrue(TEXT("Round identity is a decimal JSON string"),Snapshot->HasTypedField<EJson::String>(TEXT("round_id")));TestEqual(TEXT("Decimal round identity retained"),Snapshot->GetStringField(TEXT("round_id")),LexToString(RoundId));
    TestTrue(TEXT("Start and end are ordered Unix seconds"),Snapshot->GetNumberField(TEXT("start_time"))>0&&Snapshot->GetNumberField(TEXT("end_time"))>=Snapshot->GetNumberField(TEXT("start_time")));
    TestEqual(TEXT("Int64 maximum score retains every decimal digit"),Users[0]->AsObject()->GetStringField(TEXT("score")),LexToString(MAX_int64));TestEqual(TEXT("Large safe numeric score emits canonical decimal string"),Users[1]->AsObject()->GetStringField(TEXT("score")),FString(TEXT("9007199254740990")));
    bool AllDecimal=true;for(const auto& Value:Users){const auto User=Value->AsObject();const FString Score=User->GetStringField(TEXT("score"));int64 Number=0;AllDecimal&=User->HasTypedField<EJson::String>(TEXT("score"))&&LexTryParseString(Number,*Score)&&Number>=0&&LexToString(Number)==Score;}
    TestTrue(TEXT("Every participant score uses a nonnegative canonical Int64 string"),AllDecimal);
    FString Body;FJsonSerializer::Serialize(Snapshot.ToSharedRef(),TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Body));FTCHARToUTF8 Utf8(*Body);
    TestTrue(TEXT("Maximum participant snapshot stays below the 4 MiB transport bound"),Utf8.Length()<4*1024*1024);AddInfo(FString::Printf(TEXT("%d-user backend snapshot: %d UTF-8 bytes; outbox: %lld bytes"),ParticipantCount,Utf8.Length(),IFileManager::Get().FileSize(*OutboxPath)));
    auto Receipt=MakeShared<FJsonObject>();Receipt->SetBoolField(TEXT("accepted"),true);Receipt->SetStringField(TEXT("round_id"),LexToString(RoundId));Receipt->SetStringField(TEXT("world_rank_version"),TEXT("week-version"));TArray<TSharedPtr<FJsonValue>> Metrics;
    for(const auto& Value:Users){const auto User=Value->AsObject();const bool Win=User->GetNumberField(TEXT("result"))==1;auto Metric=MakeShared<FJsonObject>();Metric->SetStringField(TEXT("open_id"),User->GetStringField(TEXT("open_id")));Metric->SetStringField(TEXT("win_points"),Win?TEXT("1"):TEXT("0"));Metric->SetStringField(TEXT("win_streak"),Win?TEXT("4"):TEXT("0"));Metrics.Add(MakeShared<FJsonValueObject>(Metric));}Receipt->SetArrayField(TEXT("users"),Metrics);
    const auto FirstMetric=Metrics[0]->AsObject();
    // Each invalid receipt must leave the same backend operation pending and
    // cannot mutate any SDK row, even when earlier identities were valid.
    const auto Reject=[&](const TCHAR* Label){Bridge->ReportCommandResult(*Provider,Command.Id,true,0,Receipt);TestEqual(Label,Game->LiveRoundReporter->Steps[0].Operation,FString(TEXT("backend_round")));Game->LiveRoundReporter->RetryAfter=0;Game->TickLiveRound();Command=Provider->Commands.Last();TestTrue(TEXT("Rejected reply retries the exact frozen backend snapshot"),Command.Payload==Snapshot);};
    Receipt->SetStringField(TEXT("round_id"),TEXT("wrong-round"));Reject(TEXT("Mismatched round receipt cannot advance"));Receipt->SetStringField(TEXT("round_id"),LexToString(RoundId));
    Receipt->SetNumberField(TEXT("round_id"),static_cast<double>(RoundId));Reject(TEXT("Numeric round receipt violates decimal-string contract"));Receipt->SetStringField(TEXT("round_id"),LexToString(RoundId));
    FirstMetric->SetBoolField(TEXT("win_points"),true);Reject(TEXT("Boolean cannot masquerade as win points"));FirstMetric->SetStringField(TEXT("win_points"),TEXT("1"));
    FirstMetric->SetNumberField(TEXT("win_streak"),1.5);Reject(TEXT("Fractional streak rejected"));FirstMetric->SetStringField(TEXT("win_streak"),TEXT("4"));
    FirstMetric->SetStringField(TEXT("win_streak"),TEXT("2147483648"));Reject(TEXT("Out-of-range streak rejected"));FirstMetric->SetStringField(TEXT("win_streak"),TEXT("4"));
    const FString LastId=Metrics.Last()->AsObject()->GetStringField(TEXT("open_id"));Metrics.Last()->AsObject()->SetStringField(TEXT("open_id"),FirstMetric->GetStringField(TEXT("open_id")));Reject(TEXT("Duplicate identity rejected"));Metrics.Last()->AsObject()->SetStringField(TEXT("open_id"),TEXT("unknown-user"));Reject(TEXT("Unknown identity rejected"));Metrics.Last()->AsObject()->SetStringField(TEXT("open_id"),LastId);
    Metrics.Last()->AsObject()->SetStringField(TEXT("win_streak"),TEXT("1"));Reject(TEXT("Gray metrics must remain zero"));Metrics.Last()->AsObject()->SetStringField(TEXT("win_streak"),TEXT("0"));
    auto Missing=Metrics;Missing.Pop();Receipt->SetArrayField(TEXT("users"),Missing);Reject(TEXT("Missing participant receipt rejected"));Receipt->SetArrayField(TEXT("users"),Metrics);
    const auto* PendingBatch=Game->LiveRoundReporter->Steps.FindByPredicate([](const FArenaRoundReportStep& Step){return Step.Operation==TEXT("user_results");});
    if(!TestTrue(TEXT("SDK batches remain frozen behind backend"),PendingBatch!=nullptr))return false;TestEqual(TEXT("No invalid receipt partially mutates SDK metrics"),PendingBatch->Payload->GetArrayField(TEXT("users"))[0]->AsObject()->GetNumberField(TEXT("win_streak")),0.0);
    Bridge->ReportCommandResult(*Provider,Command.Id,true,0,Receipt);Game->TickLiveRound();const auto OldSdk=Provider->Commands.Last();if(!TestEqual(TEXT("Validated durable backend receipt enables SDK batches"),OldSdk.Operation,FString(TEXT("user_results"))))return false;
    // Another room cannot read or replay this durable job. Returning to the
    // original room resumes only unfinished steps with the stored enrichment.
    Provider->Session=Bridge->BeginProviderSession(*Provider,TEXT("backend-app"),TEXT("different-room"),TEXT("anchor"));Game->TickLiveRound();TestEqual(TEXT("Different room starts its own round instead of replaying"),Provider->Commands.Last().Operation,FString(TEXT("round")));TestTrue(TEXT("Original-room snapshot remains available"),IFileManager::Get().FileSize(*OutboxPath)>0);
    Provider->Session=Bridge->BeginProviderSession(*Provider,TEXT("backend-app"),TEXT("backend-room"),TEXT("anchor"));Game->TickLiveRound();TestEqual(TEXT("Same-room restart resumes unfinished SDK batch"),Provider->Commands.Last().Operation,FString(TEXT("user_results")));TestEqual(TEXT("Replayed prior round cannot unlock new simulation"),Game->LiveRoundId,int64(0));
    Bridge->ReportCommandResult(*Provider,OldSdk.Id,true,0);TestFalse(TEXT("Old-session response cannot consume resumed request"),Game->LiveRoundReporter->RequestId.IsEmpty());
    int32 Uploaded=0,Batches=0;bool Complete=false,CorrectMetrics=true;
    for(int32 I=0;I<ExpectedBatches+3&&!Complete;++I){const auto Current=Provider->Commands.Last();
        if(Current.Operation==TEXT("user_results")||Current.Operation==TEXT("room_rank")){const auto& Rows=Current.Payload->GetArrayField(TEXT("users"));if(Current.Operation==TEXT("user_results")){TestTrue(TEXT("Resumed SDK batches obey 50-user limit"),Rows.Num()<=50);Uploaded+=Rows.Num();++Batches;}else TestEqual(TEXT("Frozen leaderboard retains top 150"),Rows.Num(),150);
            for(const auto& Value:Rows){const auto Row=Value->AsObject();const bool Win=Row->GetNumberField(TEXT("result"))==1;CorrectMetrics&=Row->GetNumberField(TEXT("win_points"))==(Win?1:0)&&Row->GetNumberField(TEXT("win_streak"))==(Win?4:0);}}
        else if(Current.Operation==TEXT("complete")){Complete=true;TestEqual(TEXT("All participant SDK records precede completion"),Uploaded,ParticipantCount);}
        else {AddError(TEXT("Recovery unexpectedly repeated SDK end or backend credit"));return false;}
        Bridge->ReportCommandResult(*Provider,Current.Id,true,0);if(!Complete)Game->TickLiveRound();
    }
    TestTrue(TEXT("Enriched winner/loser/gray metrics survive restart"),CorrectMetrics);TestEqual(TEXT("Every participant is sent in the exact number of 50-user SDK batches"),Batches,ExpectedBatches);TestTrue(TEXT("Recovery completes only after all SDK receipts"),Complete);TestEqual(TEXT("Old settlement completion does not start the new arena"),Game->LiveRoundId,int64(0));TestFalse(TEXT("Completed durable snapshot is removed"),IFileManager::Get().FileSize(*OutboxPath)>=0);
    Game->TickLiveRound();TestEqual(TEXT("New arena still requires its own SDK start"),Provider->Commands.Last().Operation,FString(TEXT("round")));Bridge->ReportCommandResult(*Provider,Provider->Commands.Last().Id,true,0);TestTrue(TEXT("New start receipt alone unlocks simulation"),Game->LiveRoundId>0);
    return true;
}
#endif
