#include "ArenaLiveRounds.h"
#include "ArenaGameMode.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace {
TSharedPtr<FJsonValue> JsonValue(const TSharedRef<FJsonObject>& Value) {return MakeShared<FJsonValueObject>(Value);}
bool MetricInteger(const TSharedPtr<FJsonObject>& Object,const FString& Key,int32& Out)
{
    const auto Value=Object->TryGetField(Key);if(!Value)return false;
    if(Value->Type==EJson::String){const FString Text=Value->AsString();int64 Number=0;
        if(!LexTryParseString(Number,*Text)||Number<0||Number>MAX_int32||LexToString(Number)!=Text)return false;Out=static_cast<int32>(Number);return true;}
    if(Value->Type!=EJson::Number)return false;
    const double Number=Value->AsNumber();if(!FMath::IsFinite(Number)||Number<0||Number>MAX_int32||FMath::FloorToDouble(Number)!=Number)return false;Out=static_cast<int32>(Number);return true;
}
bool AllocateRoundId(const FLiveSession& Session,int64& Result)
{
    const FString Directory=FPaths::ProjectSavedDir()/TEXT("LivePlatform");
    const FString Path=Directory/TEXT("RoundCounters.json");
    IFileManager::Get().MakeDirectory(*Directory,true);
    TSharedPtr<FJsonObject> Counters=MakeShared<FJsonObject>();
    FString Text;
    if(FPaths::FileExists(Path) && (!FFileHelper::LoadFileToString(Text,*Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Counters) || !Counters))return false;
    const auto Part=[](const FString& V){return FString::FromInt(V.Len())+TEXT(":")+V;};
    const FString Key=Part(Session.PlatformId)+Part(Session.AppId)+Part(Session.RoomId);
    double Old=0;Counters->TryGetNumberField(Key,Old);
    if(!FMath::IsFinite(Old)||Old<0||Old>9007199254740000.0)return false;
    Result=FMath::Max(FDateTime::UtcNow().ToUnixTimestamp()*1000,static_cast<int64>(Old)+1);
    Counters->SetNumberField(Key,static_cast<double>(Result));Text.Reset();
    FJsonSerializer::Serialize(Counters.ToSharedRef(),TJsonWriterFactory<>::Create(&Text));
    const FString Temp=Path+TEXT(".tmp");
    return FFileHelper::SaveStringToFile(Text,*Temp,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)&&IFileManager::Get().Move(*Path,*Temp,true,true);
}
}

FArenaLiveRoundReporter::FArenaLiveRoundReporter(AArenaGameMode& InGame):Game(&InGame),Bridge(InGame.GetBridge())
{
    Session=Bridge->GetCurrentSession();
    GConfig->GetString(TEXT("LiveInteraction"),TEXT("RedGroupId"),RedGroup,GGameIni);
    GConfig->GetString(TEXT("LiveInteraction"),TEXT("BlueGroupId"),BlueGroup,GGameIni);
    GConfig->GetString(TEXT("LiveInteraction"),TEXT("GrayGroupId"),GrayGroup,GGameIni);
    GConfig->GetBool(TEXT("LiveInteraction"),TEXT("WorldLeaderboardEnabled"),bBackendRequired,GGameIni);
    ResultHandle=Bridge->OnCommandReply.AddRaw(this,&FArenaLiveRoundReporter::Result);
    Recover();
}
FArenaLiveRoundReporter::~FArenaLiveRoundReporter()
{
    if(Bridge.IsValid())Bridge->OnCommandReply.Remove(ResultHandle);
}
void AArenaGameMode::TickLiveRound()
{
    if(GetBridge()->IsLocalTestMode()||!GetBridge()->IsConnected())return;
    if(!LiveRoundReporter)LiveRoundReporter=MakeShared<FArenaLiveRoundReporter>(*this);
    LiveRoundReporter->Tick();
}
void AArenaGameMode::ResetLiveRound() {LiveRoundId=0;LiveRoundReporter.Reset();bPlatformTestSession=false;PlatformTestWeaponUnlocks.Reset();}
bool FArenaLiveRoundReporter::IsCurrentRoundActive() const {
    return Game.IsValid() && Bridge.IsValid() && bStarted && !bEndQueued && !bReplay
        && MatchRound==Game->GetMatch().round && RoundId>0 && Game->LiveRoundId==RoundId
        && Bridge->IsEventFromCurrentSession(Session);
}
bool FArenaLiveRoundReporter::StartRound()
{
    if(RedGroup.IsEmpty()||BlueGroup.IsEmpty()||GrayGroup.IsEmpty()||RedGroup==BlueGroup||GrayGroup==RedGroup||GrayGroup==BlueGroup) {Game->LastEvent=TEXT("平台阵营标识未配置，等待配置后开始对局");return false;}
    if(!AllocateRoundId(Session,RoundId)) {Game->LastEvent=TEXT("平台局号保存失败，暂不开始新局");return false;}
    MatchRound=Game->GetMatch().round;StartedAt=FDateTime::UtcNow().ToUnixTimestamp();
    auto Payload=MakeShared<FJsonObject>();Payload->SetNumberField(TEXT("round_id"),static_cast<double>(RoundId));
    Payload->SetNumberField(TEXT("start_time"),static_cast<double>(StartedAt));Payload->SetNumberField(TEXT("status"),1);
    Steps.Add({TEXT("round"),Payload});bStarted=false;bEndQueued=false;ConfirmedSteps=0;OutboxPath.Reset();Game->LiveRoundId=0;ReportedGroups.Empty();return true;
}
void FArenaLiveRoundReporter::Tick()
{
    if(!Game.IsValid()||!Bridge.IsValid()||!Bridge->IsEventFromCurrentSession(Session))return;
    if(bRecoveryBlocked)return;
    if(!RequestId.IsEmpty()&&FPlatformTime::Seconds()-SentAt>(!Steps.IsEmpty()&&Steps[0].Operation==TEXT("backend_round")?50:35)) {FLiveCommandReply Reply;Reply.Session=Session;Reply.RequestId=RequestId;Reply.ErrorCode=-408;Result(Reply);}
    // Freeze the results snapshot while any prior group update or result batch
    // is awaiting confirmation. Automatic next-round reset must not erase it.
    if(bStarted && !bEndQueued && Game->GetMatch().phase==gw::Phase::Results)EndRound();
    // Retry the same operation and round id, never mark completion on transport success.
    if(!Steps.IsEmpty()) {SendNext();return;}
    if(MatchRound!=Game->GetMatch().round) {if(StartRound())SendNext();return;}
    if(!bStarted)return;
    if(!bEndQueued)QueueGroup();
    SendNext();
}
void FArenaLiveRoundReporter::QueueGroup()
{
    for(const auto& Pair:Game->GetViewers()) {
        const auto& Viewer=Pair.Value;
        if(Viewer.bDebugBot||Viewer.Team<0||Viewer.Team>2)continue;
        const FString Group=Viewer.Team==1?RedGroup:Viewer.Team==2?BlueGroup:GrayGroup;
        if(ReportedGroups.FindRef(Viewer.UserId)==Group)continue;
        auto Payload=MakeShared<FJsonObject>();Payload->SetNumberField(TEXT("round_id"),static_cast<double>(RoundId));
        Payload->SetStringField(TEXT("open_id"),Viewer.UserId);Payload->SetStringField(TEXT("group_id"),Group);
        Steps.Add({TEXT("user_group"),Payload});break;
    }
}
void FArenaLiveRoundReporter::EndRound()
{
    bEndQueued=true;
    Game->LiveRoundId=0;
    // Join bursts can arrive on the final frame. Freeze and queue every remaining
    // participant's group before ending the round or uploading its user results.
    TMap<FString,FString> QueuedGroups;
    for(const auto& Step:Steps)if(Step.Operation==TEXT("user_group"))
        QueuedGroups.Add(Step.Payload->GetStringField(TEXT("open_id")),Step.Payload->GetStringField(TEXT("group_id")));
    for(const auto& Pair:Game->GetViewers()) {
        const auto& Viewer=Pair.Value;
        if(Viewer.bDebugBot||Viewer.Team<0||Viewer.Team>2)continue;
        const FString Group=Viewer.Team==1?RedGroup:Viewer.Team==2?BlueGroup:GrayGroup;
        if(ReportedGroups.FindRef(Viewer.UserId)==Group)continue;
        if(QueuedGroups.FindRef(Viewer.UserId)==Group)continue;
        auto Payload=MakeShared<FJsonObject>();Payload->SetNumberField(TEXT("round_id"),static_cast<double>(RoundId));
        Payload->SetStringField(TEXT("open_id"),Viewer.UserId);Payload->SetStringField(TEXT("group_id"),Group);
        Steps.Add({TEXT("user_group"),Payload});
        QueuedGroups.Add(Viewer.UserId,Group);
    }
    const auto& Match=Game->GetMatch();
    const int64 EndedAt=FDateTime::UtcNow().ToUnixTimestamp();
    auto End=MakeShared<FJsonObject>();End->SetNumberField(TEXT("round_id"),static_cast<double>(RoundId));
    End->SetNumberField(TEXT("start_time"),static_cast<double>(StartedAt));End->SetNumberField(TEXT("end_time"),static_cast<double>(EndedAt));End->SetNumberField(TEXT("status"),2);
    TArray<TSharedPtr<FJsonValue>> Groups;
    for(int32 Team=1;Team<=2;++Team) {auto Group=MakeShared<FJsonObject>();Group->SetStringField(TEXT("group_id"),Team==1?RedGroup:BlueGroup);Group->SetNumberField(TEXT("result"),Game->IsPlatformTestSession()?3:Match.winnerTeam==Team?1:2);Groups.Add(JsonValue(Group));}
    auto Gray=MakeShared<FJsonObject>();Gray->SetStringField(TEXT("group_id"),GrayGroup);Gray->SetNumberField(TEXT("result"),3);Groups.Add(JsonValue(Gray));
    End->SetArrayField(TEXT("group_results"),Groups);Steps.Add({TEXT("round"),End});
    if(Game->IsPlatformTestSession()) {
        // Test-driven combat also affects opponents and later rounds. Close the
        // lifecycle without manufacturing paid statistics for any participant.
        auto Complete=MakeShared<FJsonObject>();Complete->SetNumberField(TEXT("round_id"),static_cast<double>(RoundId));
        Complete->SetNumberField(TEXT("complete_time"),static_cast<double>(EndedAt));Complete->SetBoolField(TEXT("platform_test"),true);
        Steps.Add({TEXT("complete"),Complete});EnsurePersisted();return;
    }
    TArray<const gw::Fighter*> Ranked;
    for(const auto& Fighter:Match.fighters)if(!Fighter.isHost&&Fighter.team>0)if(const auto* V=Game->FindViewer(Fighter.id);V&&!V->bDebugBot)Ranked.Add(&Fighter);
    Ranked.Sort([](const gw::Fighter& A,const gw::Fighter& B){return A.score!=B.score?A.score>B.score:A.kills!=B.kills?A.kills>B.kills:A.id<B.id;});
    TArray<TSharedPtr<FJsonValue>> AllUsers;
    for(int32 Rank=0;Rank<Ranked.Num();++Rank) {
        const auto& Fighter=*Ranked[Rank];const auto* Viewer=Game->FindViewer(Fighter.id);auto User=MakeShared<FJsonObject>();
        User->SetStringField(TEXT("open_id"),Viewer->UserId);User->SetStringField(TEXT("group_id"),Fighter.team==1?RedGroup:BlueGroup);
        User->SetNumberField(TEXT("rank"),FMath::Min(Rank+1,1000));User->SetNumberField(TEXT("result"),Fighter.team==Match.winnerTeam?1:2);
        if(Fighter.score<=9007199254740991LL)User->SetNumberField(TEXT("score"),static_cast<double>(Fighter.score));else User->SetStringField(TEXT("score"),LexToString(Fighter.score));
        User->SetNumberField(TEXT("win_points"),Fighter.team==Match.winnerTeam?1:0);User->SetNumberField(TEXT("win_streak"),0);AllUsers.Add(JsonValue(User));
    }
    TArray<TSharedPtr<FJsonValue>> Top;Top.Append(AllUsers.GetData(),FMath::Min(150,AllUsers.Num()));
    // Gray participants have no competitive score or leaderboard place, but
    // their participation is included in the complete user-results upload.
    for(const auto& Fighter:Match.fighters)if(!Fighter.isHost&&Fighter.team==0)if(const auto* Viewer=Game->FindViewer(Fighter.id);Viewer&&!Viewer->bDebugBot) {
        auto User=MakeShared<FJsonObject>();User->SetStringField(TEXT("open_id"),Viewer->UserId);User->SetStringField(TEXT("group_id"),GrayGroup);
        User->SetNumberField(TEXT("rank"),1000);User->SetNumberField(TEXT("result"),3);User->SetNumberField(TEXT("score"),0);
        User->SetNumberField(TEXT("win_points"),0);User->SetNumberField(TEXT("win_streak"),0);AllUsers.Add(JsonValue(User));
    }
    if(bBackendRequired) {
        auto Snapshot=MakeShared<FJsonObject>();Snapshot->SetStringField(TEXT("app_id"),Session.AppId);Snapshot->SetStringField(TEXT("room_id"),Session.RoomId);
        Snapshot->SetStringField(TEXT("round_id"),LexToString(RoundId));Snapshot->SetNumberField(TEXT("start_time"),static_cast<double>(StartedAt));Snapshot->SetNumberField(TEXT("end_time"),static_cast<double>(EndedAt));
        TArray<TSharedPtr<FJsonValue>> Participants;
        for(const auto& Value:AllUsers) {const auto User=Value->AsObject();auto Record=MakeShared<FJsonObject>();
            Record->SetStringField(TEXT("open_id"),User->GetStringField(TEXT("open_id")));Record->SetStringField(TEXT("group_id"),User->GetStringField(TEXT("group_id")));
            const auto ScoreValue=User->TryGetField(TEXT("score"));
            const FString Score=ScoreValue->Type==EJson::String?ScoreValue->AsString():LexToString(static_cast<int64>(ScoreValue->AsNumber()));Record->SetStringField(TEXT("score"),Score);
            Record->SetNumberField(TEXT("result"),User->GetNumberField(TEXT("result")));Participants.Add(JsonValue(Record));
        }
        Snapshot->SetArrayField(TEXT("users"),Participants);Steps.Add({TEXT("backend_round"),Snapshot});
    }
    for(int32 Start=0;Start<AllUsers.Num();Start+=50) {
        TArray<TSharedPtr<FJsonValue>> Batch;Batch.Append(AllUsers.GetData()+Start,FMath::Min(50,AllUsers.Num()-Start));
        auto Data=MakeShared<FJsonObject>();Data->SetNumberField(TEXT("round_id"),static_cast<double>(RoundId));Data->SetArrayField(TEXT("users"),Batch);Steps.Add({TEXT("user_results"),Data});
    }
    auto RankData=MakeShared<FJsonObject>();RankData->SetNumberField(TEXT("round_id"),static_cast<double>(RoundId));RankData->SetArrayField(TEXT("users"),Top);Steps.Add({TEXT("room_rank"),RankData});
    auto Complete=MakeShared<FJsonObject>();Complete->SetNumberField(TEXT("round_id"),static_cast<double>(RoundId));Complete->SetNumberField(TEXT("complete_time"),static_cast<double>(EndedAt));Steps.Add({TEXT("complete"),Complete});
    EnsurePersisted();
}
void FArenaLiveRoundReporter::SendNext()
{
    if(Steps.IsEmpty()||!RequestId.IsEmpty()||FPlatformTime::Seconds()<RetryAfter||!EnsurePersisted())return;
    RequestId=Bridge->SubmitCommand(Steps[0].Operation,Steps[0].Payload.ToSharedRef());SentAt=FPlatformTime::Seconds();
    if(RequestId.IsEmpty()) {RetryAfter=SentAt+2;Game->LastEvent=TEXT("平台上报暂未提交，正在等待重试");}
}
void FArenaLiveRoundReporter::Result(const FLiveCommandReply& Reply)
{
    if(!Game.IsValid()||!Bridge.IsValid()||Reply.RequestId!=RequestId||Steps.IsEmpty()||!Bridge->IsEventFromCurrentSession(Reply.Session))return;
    RequestId.Reset();
    bool bSuccess=Reply.bSuccess;
    if(bSuccess&&Steps[0].Operation==TEXT("backend_round"))bSuccess=ApplyBackendReceipt(Reply.Data);
    if(bSuccess&&bEndQueued) {
        bSuccess=EnsurePersisted()&&FArenaLiveRoundOutbox::AppendReceipt(OutboxPath,ConfirmedSteps+1,Steps[0].Operation,Steps[0].Operation==TEXT("backend_round")?Reply.Data:nullptr);
        if(bSuccess)++ConfirmedSteps;
    }
    if(!bSuccess) {++Attempts;RetryAfter=FPlatformTime::Seconds()+FMath::Min(30.0,FMath::Pow(2.0,FMath::Min(Attempts,5)));Game->LastEvent=TEXT("平台上报失败，保留本局数据并重试");return;}
    const auto& Step=Steps[0];
    if(Step.Operation==TEXT("round")&&Step.Payload->GetNumberField(TEXT("status"))==1) {bStarted=true;Game->LiveRoundId=RoundId;Game->LastEvent=TEXT("平台对局已开始，等待观众加入");}
    if(Step.Operation==TEXT("user_group"))ReportedGroups.Add(Step.Payload->GetStringField(TEXT("open_id")),Step.Payload->GetStringField(TEXT("group_id")));
    const bool Complete=Step.Operation==TEXT("complete");
    if(Complete) {bool PlatformTest=false;Step.Payload->TryGetBoolField(TEXT("platform_test"),PlatformTest);Game->LiveRoundId=bReplay?0:RoundId;
        Game->LastEvent=PlatformTest?TEXT("平台测试对局已结束，测试数据未计入战绩及排行榜"):TEXT("本局战绩及直播间排行榜已提交成功");FArenaLiveRoundOutbox::Remove(OutboxPath);OutboxPath.Reset();}
    Steps.RemoveAt(0);Attempts=0;RetryAfter=0;
    if(Complete&&bReplay){bReplay=false;bStarted=false;bEndQueued=false;MatchRound=0;ConfirmedSteps=0;Recover();}
}
bool FArenaLiveRoundReporter::EnsurePersisted()
{
    if(!bEndQueued||!OutboxPath.IsEmpty())return true;
    if(FArenaLiveRoundOutbox::Save(Session,RoundId,StartedAt,MatchRound,Steps,OutboxPath))return true;
    Game->LastEvent=TEXT("本局结算快照尚未保存，保留战局等待重试");return false;
}
bool FArenaLiveRoundReporter::Recover()
{
    FArenaStoredRound Stored;FString Error;
    if(!FArenaLiveRoundOutbox::LoadOldest(Session,Stored,Error)){bRecoveryBlocked=!Error.IsEmpty();if(bRecoveryBlocked)Game->LastEvent=Error;return !bRecoveryBlocked;}
    RoundId=Stored.RoundId;StartedAt=Stored.StartedAt;MatchRound=Stored.MatchRound;Steps=MoveTemp(Stored.Steps);
    if(Stored.BackendReceipt&&!ApplyBackendReceipt(Stored.BackendReceipt)){bRecoveryBlocked=true;Game->LastEvent=TEXT("历史服务端战绩回执不匹配，保留快照等待恢复");return false;}
    Steps.RemoveAt(0,Stored.Cursor,EAllowShrinking::No);ConfirmedSteps=Stored.Cursor;OutboxPath=Stored.Path;bStarted=true;bEndQueued=true;bReplay=true;Game->LiveRoundId=0;return true;
}
bool FArenaLiveRoundReporter::ApplyBackendReceipt(const TSharedPtr<FJsonObject>& Data)
{
    if(!Data)return false;
    const FStep* Snapshot=Steps.FindByPredicate([](const FStep& Step){return Step.Operation==TEXT("backend_round");});
    if(!Snapshot)return false;
    bool Accepted=false;FString Round,Version;const TArray<TSharedPtr<FJsonValue>>* Users=nullptr;
    if(!Data->HasTypedField<EJson::Boolean>(TEXT("accepted"))||!Data->TryGetBoolField(TEXT("accepted"),Accepted)||!Accepted||!Data->HasTypedField<EJson::String>(TEXT("round_id"))||!Data->TryGetStringField(TEXT("round_id"),Round)||Round!=LexToString(RoundId)
        ||!Data->HasTypedField<EJson::String>(TEXT("world_rank_version"))||!Data->TryGetStringField(TEXT("world_rank_version"),Version)||Version.IsEmpty()||Version.Len()>256||!Data->TryGetArrayField(TEXT("users"),Users))return false;
    const TArray<TSharedPtr<FJsonValue>>* ExpectedArray=nullptr;if(!Snapshot->Payload||!Snapshot->Payload->TryGetArrayField(TEXT("users"),ExpectedArray))return false;
    const auto& Expected=*ExpectedArray;if(Users->Num()!=Expected.Num()||Users->Num()>5000)return false;
    TMap<FString,int32> ExpectedResults;for(const auto& Value:Expected){if(!Value||Value->Type!=EJson::Object)return false;const auto Object=Value->AsObject();FString Id;int32 ResultValue=0;
        if(!Object->HasTypedField<EJson::String>(TEXT("open_id"))||!Object->TryGetStringField(TEXT("open_id"),Id)||Id.IsEmpty()||!MetricInteger(Object,TEXT("result"),ResultValue)||ResultValue<1||ResultValue>3)return false;ExpectedResults.Add(Id,ResultValue);}
    if(ExpectedResults.Num()!=Expected.Num())return false;
    TMap<FString,TPair<int32,int32>> Metrics;
    for(const auto& Value:*Users){if(!Value||Value->Type!=EJson::Object)return false;const auto User=Value->AsObject();FString Id;int32 Points=0,Streak=0;
        if(!User->HasTypedField<EJson::String>(TEXT("open_id"))||!User->TryGetStringField(TEXT("open_id"),Id)||Metrics.Contains(Id)||!ExpectedResults.Contains(Id)||!MetricInteger(User,TEXT("win_points"),Points)||!MetricInteger(User,TEXT("win_streak"),Streak))return false;
        const bool Win=ExpectedResults[Id]==1;if(Points!=(Win?1:0)||(Win?Streak<1:Streak!=0))return false;Metrics.Add(Id,{Points,Streak});
    }
    // Validate the complete identity set before changing any frozen SDK rows.
    for(const auto& Step:Steps)if(Step.Operation==TEXT("user_results")||Step.Operation==TEXT("room_rank"))for(const auto& Value:Step.Payload->GetArrayField(TEXT("users")))
        if(!Value||Value->Type!=EJson::Object||!Metrics.Contains(Value->AsObject()->GetStringField(TEXT("open_id"))))return false;
    for(auto& Step:Steps)if(Step.Operation==TEXT("user_results")||Step.Operation==TEXT("room_rank"))for(const auto& Value:Step.Payload->GetArrayField(TEXT("users"))) {
        const auto User=Value->AsObject();const auto* Metric=Metrics.Find(User->GetStringField(TEXT("open_id")));if(!Metric)return false;
        User->SetNumberField(TEXT("win_points"),Metric->Key);User->SetNumberField(TEXT("win_streak"),Metric->Value);
    }
    return true;
}
