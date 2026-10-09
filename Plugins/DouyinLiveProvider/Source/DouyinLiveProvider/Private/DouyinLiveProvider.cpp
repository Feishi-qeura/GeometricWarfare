#include "LiveInteractionProvider.h"
#include "LiveInteractionSubsystem.h"
#include "DouyinHostTransport.h"
#include "DouyinGiftDecoder.h"
#include "LiveGiftRules.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/AutomationTest.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/SecureHash.h"
#include "HAL/FileManager.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Windows/WindowsHWrapper.h"

DEFINE_LOG_CATEGORY_STATIC(LogDouyinLiveProvider,Log,All);
namespace {
FString Str(const TSharedPtr<FJsonObject>& Obj,const TCHAR* Key)
{
    FString Value;if(Obj)Obj->TryGetStringField(Key,Value);return Value;
}
int64 Num(const TSharedPtr<FJsonObject>& Obj,const TCHAR* Key,int64 Default=0)
{
    const auto* Field=Obj?Obj->Values.Find(Key):nullptr;
    if(!Field||!Field->IsValid())return Default;
    FString Decimal;
    // FJsonValueNumber can stringify itself. Inspect its actual type before
    // accepting the lossless decimal-string form, otherwise 2^53 slips through.
    if((*Field)->Type==EJson::String&&Obj->TryGetStringField(Key,Decimal)) {
        const bool Negative=Decimal.StartsWith(TEXT("-"));
        const int32 Start=Negative?1:0;
        if(Decimal.Len()<=Start||Decimal.Len()>20)return Default;
        const uint64 Limit=Negative?uint64(MAX_int64)+1:uint64(MAX_int64);uint64 Integer=0;
        for(int32 I=Start;I<Decimal.Len();++I) {
            if(Decimal[I]<'0'||Decimal[I]>'9')return Default;
            const uint64 Digit=Decimal[I]-'0';
            if(Integer>(Limit-Digit)/10)return Default;
            Integer=Integer*10+Digit;
        }
        return Negative?(Integer==uint64(MAX_int64)+1?MIN_int64:-static_cast<int64>(Integer)):static_cast<int64>(Integer);
    }
    double Value=0;return (*Field)->Type==EJson::Number&&Obj->TryGetNumberField(Key,Value)&&FMath::IsFinite(Value)&&FMath::FloorToDouble(Value)==Value&&FMath::Abs(Value)<=9007199254740991.0?static_cast<int64>(Value):Default;
}
TSharedPtr<FJsonObject> Obj(const TSharedPtr<FJsonObject>& Parent,const TCHAR* Key)
{
    const TSharedPtr<FJsonObject>* Value=nullptr;return Parent&&Parent->TryGetObjectField(Key,Value)?*Value:nullptr;
}
FString Encode(const TSharedRef<FJsonObject>& Value)
{
    FString Text;FJsonSerializer::Serialize(Value,TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));return Text;
}
FString DiagnosticRecord(const FString& Stage,const FString& Operation,bool Success,int64 Code)
{
    static const TSet<FString> Stages={TEXT("provider_start"),TEXT("credential_missing"),TEXT("credential_received"),TEXT("host_started"),TEXT("init_queued"),TEXT("room_ready"),TEXT("command_sent"),TEXT("sdk_status"),TEXT("host_exited"),TEXT("room_timeout"),TEXT("event_received")};
    static const TSet<FString> Ops={TEXT("host"),TEXT("protocol"),TEXT("init"),TEXT("connection"),TEXT("events"),TEXT("ack"),TEXT("round"),TEXT("user_group"),TEXT("user_results"),TEXT("room_rank"),TEXT("complete"),TEXT("backend_round"),TEXT("backend_auth"),TEXT("stop")};
    static const TSet<FString> Types={TEXT("live_comment"),TEXT("live_like"),TEXT("live_gift"),TEXT("live_team"),TEXT("live_enter"),TEXT("live_follow")};
    const bool Subscription=Operation.StartsWith(TEXT("subscribe:"))&&Types.Contains(Operation.Mid(10));
    const auto Data=MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("utc"),FDateTime::UtcNow().ToIso8601());
    Data->SetStringField(TEXT("stage"),Stages.Contains(Stage)?Stage:TEXT("unknown"));
    Data->SetStringField(TEXT("op"),Ops.Contains(Operation)||Types.Contains(Operation)||Subscription?Operation:TEXT("unknown"));
    Data->SetBoolField(TEXT("success"),Success);Data->SetNumberField(TEXT("code"),static_cast<double>(Code));
    return Encode(Data)+TEXT("\n");
}
// Session-local correlation only. Never log raw identities, nicknames, message
// contents or avatar URLs, and never treat matching display names as identity.
FString IdentityDiagnosticRecord(const FString& Salt,const TSharedPtr<FJsonObject>& Message)
{
    static const TSet<FString> Types={TEXT("live_comment"),TEXT("live_team"),TEXT("live_enter"),TEXT("live_follow"),TEXT("live_gift")};
    const FString Type=Str(Message,TEXT("msg_type"));
    const auto User=Obj(Message,TEXT("user"));
    const FString RawId=Str(User,TEXT("open_id"));
    const FString Id=RawId.TrimStartAndEnd(),Name=Str(User,TEXT("nickname")),MsgId=Str(Message,TEXT("msg_id"));
    if(Salt.IsEmpty() || !Types.Contains(Type) || Id.IsEmpty() || Id.Len()>256 || Name.Len()>64 || MsgId.IsEmpty() || MsgId.Len()>128)return FString();
    const auto Fingerprint=[&](const TCHAR* Domain,const FString& Value) {
        if(Value.IsEmpty())return FString();
        const FString Input=Salt+TEXT(":")+Domain+TEXT(":")+Value;
        const FTCHARToUTF8 Bytes(*Input);uint8 Hash[20];
        FSHA1::HashBuffer(Bytes.Get(),Bytes.Length(),Hash);
        return BytesToHex(Hash,UE_ARRAY_COUNT(Hash));
    };
    const auto Data=MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("utc"),FDateTime::UtcNow().ToIso8601());
    Data->SetStringField(TEXT("stage"),TEXT("identity_event"));Data->SetStringField(TEXT("op"),Type);
    Data->SetStringField(TEXT("user_fingerprint"),Fingerprint(TEXT("user"),Id));
    Data->SetStringField(TEXT("name_fingerprint"),Fingerprint(TEXT("name"),Name));
    Data->SetStringField(TEXT("message_fingerprint"),Fingerprint(TEXT("message"),MsgId));
    Data->SetNumberField(TEXT("user_id_length"),Id.Len());
    Data->SetBoolField(TEXT("id_whitespace_normalized"),RawId!=Id);
    FString Action=TEXT("event");
    if(Type==TEXT("live_comment")) {
        const FString Content=Str(Message,TEXT("content")).TrimStartAndEnd();
        Action=Content==TEXT("加入")?TEXT("join"):
            Content.Len()==1 && Content[0]>=TEXT('1') && Content[0]<=TEXT('4')?TEXT("number"):
            Content.StartsWith(TEXT("武器"))?TEXT("weapon"):
            Content.StartsWith(TEXT("步枪"))?TEXT("rifle"):TEXT("other");
        if(Action==TEXT("weapon")) {
            FString Number=Content.Mid(2).TrimStartAndEnd();
            if(Number.StartsWith(TEXT("+")) || Number.StartsWith(TEXT("＋")))Number=Number.Mid(1).TrimStartAndEnd();
            if(Number.Len()==1 && Number[0]>=TEXT('1') && Number[0]<=TEXT('6'))Data->SetNumberField(TEXT("weapon_number"),Number[0]-TEXT('0'));
        }
    } else if(Type==TEXT("live_enter")) {
        Data->SetNumberField(TEXT("follow_status"),static_cast<double>(Num(Message,TEXT("follow_status"),-1)));
        Data->SetNumberField(TEXT("enter_type"),static_cast<double>(Num(Message,TEXT("enter_type"),-1)));
        Data->SetStringField(TEXT("sdk_timestamp_ms"),LexToString(Num(Message,TEXT("timestamp"))));
    }
    Data->SetStringField(TEXT("action"),Action);
    return Encode(Data)+TEXT("\n");
}
FString GiftNameForId(const FString& GiftId)
{
    // Encrypted IDs contain '=' padding, so IDs must be INI values, not keys.
    TArray<FString> Mappings;GConfig->GetSection(TEXT("DouyinLiveProvider.Gifts"),Mappings,GGameIni);
    for(const FString& Mapping:Mappings) {
        FString Name,Id;
        if(Mapping.Split(TEXT("="),&Name,&Id)&&Id==GiftId)return Name;
    }
    return FString();
}
FString GiftDiagnosticRecord(const FLiveGift& Gift,const FString& Rejection,bool Delivered)
{
    // No message/user IDs or arbitrary SDK strings. This distinguishes mapping,
    // review provenance, audience routing and downstream rejection in live logs.
    static const TSet<FString> Reasons={TEXT(""),TEXT("wrong_type"),TEXT("invalid_count"),TEXT("invalid_test_flag"),TEXT("mapping_missing"),TEXT("other_audience"),TEXT("delivery_rejected")};
    const auto Data=MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("utc"),FDateTime::UtcNow().ToIso8601());Data->SetStringField(TEXT("stage"),TEXT("gift_delivery"));
    Data->SetBoolField(TEXT("success"),Delivered);Data->SetBoolField(TEXT("is_test"),Gift.bIsTestData);
    Data->SetStringField(TEXT("count"),LexToString(Gift.Count));
    Data->SetStringField(TEXT("gift_name"),liveinteraction::IsSupportedGift(Gift.GiftName)?Gift.GiftName:FString());
    Data->SetStringField(TEXT("reason"),Reasons.Contains(Rejection)?Rejection:TEXT("unknown"));
    return Encode(Data)+TEXT("\n");
}
FString HostEnvironmentArgs()
{
    const TSet<FString> Allowed={TEXT("-cloud-game"),TEXT("-mobile"),TEXT("-screen-width"),TEXT("-screen-height")};
    TMap<FString,int32> Values;const TCHAR* Cursor=FCommandLine::Get();FString Arg;
    while(FParse::Token(Cursor,Arg,false)) {
        FString Key=Arg,Value;
        const int32 Equal=Arg.Find(TEXT("="));
        if(Equal>=0) {Key=Arg.Left(Equal);Value=Arg.Mid(Equal+1);}
        Key.ToLowerInline();if(!Allowed.Contains(Key))continue;
        if(Equal<0&&!FParse::Token(Cursor,Value,false))continue;
        int32 Number=0;
        if(!LexTryParseString(Number,*Value))continue;
        const bool Size=Key==TEXT("-screen-width")||Key==TEXT("-screen-height");
        if(Size?(Number>=320&&Number<=8192):(Number==0||Number==1))Values.Add(Key,Number);
    }
    FString Result;for(const auto& Pair:Values)Result+=FString::Printf(TEXT(" %s %d"),*Pair.Key,Pair.Value);return Result;
}
bool ReadLaunchCredential(FString& Token)
{
    if(!FParse::Param(FCommandLine::Get(),TEXT("GWCredentialStdin")))return false;
    const HANDLE Input=GetStdHandle(STD_INPUT_HANDLE);
    DWORD Available=0;
    if(Input==INVALID_HANDLE_VALUE||Input==nullptr||!PeekNamedPipe(Input,nullptr,0,nullptr,&Available,nullptr)
        ||Available==0||Available>16384)return false;
    TArray<uint8> Bytes;
    if(!FPlatformProcess::ReadPipeToArray(Input,Bytes)||Bytes.Num()!=static_cast<int32>(Available)||Bytes.Last()!='\n')return false;
    const int32 Length=Bytes.Num()-1;
    for(int32 I=0;I<Length;++I)if(Bytes[I]=='\n'||Bytes[I]=='\r'||Bytes[I]==0)return false;
    const FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()),Length);
    Token=FString(Text.Length(),Text.Get());
    FMemory::Memzero(Bytes.GetData(),Bytes.Num());
    return !Token.IsEmpty();
}

class FDouyinLiveProvider final : public ILiveInteractionProvider
{
public:
    FString GetPlatformId() const override {return TEXT("douyin");}
    bool Start(ULiveInteractionSubsystem& InHost) override
    {
        Host=&InHost;
        const FString Directory=FPaths::ProjectSavedDir()/TEXT("LivePlatform");
        IFileManager::Get().MakeDirectory(*Directory,true);
        DiagnosticPath=Directory/(TEXT("provider-diagnostics-")+FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT(".jsonl"));
        Note(TEXT("provider_start"));
        GConfig->GetString(TEXT("DouyinLiveProvider"),TEXT("AppId"),AppId,GGameIni);
        FString Token;
        if(!ReadLaunchCredential(Token)) {
            Note(TEXT("credential_missing"),FParse::Param(FCommandLine::Get(),TEXT("GWCredentialStdin"))?TEXT("protocol"):TEXT("init"),false);
            UE_LOG(LogDouyinLiveProvider,Display,TEXT("Waiting for credential launcher; no authenticated room."));
            Host->EndProviderSession(*this,FParse::Param(FCommandLine::Get(),TEXT("GWCredentialStdin"))?TEXT("LAUNCH_CREDENTIAL_PIPE_FAILED"):TEXT("LAUNCH_TOKEN_MISSING"));return true;
        }
        Note(TEXT("credential_received"));
        if(AppId.IsEmpty()||AppId==TEXT("YOUR_APP_ID")) {
            Token.Reset();Host->EndProviderSession(*this,TEXT("APP_ID_NOT_CONFIGURED"));return true;
        }
        auto Plugin=IPluginManager::Get().FindPlugin(TEXT("DouyinLiveProvider"));
        const FString Executable=Plugin?FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir()/TEXT("Binaries/Win64/SdkHost/DouyinSdkHost.exe")):FString();
        if(!FPaths::FileExists(Executable)) {Token.Reset();Host->EndProviderSession(*this,TEXT("SDK_HOST_MISSING"));return true;}
        Transport=MakeUnique<FDouyinHostTransport>();
        if(!Transport->Launch(Executable,HostEnvironmentArgs())) {Token.Reset();Transport.Reset();Host->EndProviderSession(*this,TEXT("SDK_HOST_START_FAILED"));return true;}
        Note(TEXT("host_started"));
        const auto Init=MakeShared<FJsonObject>();
        Init->SetStringField(TEXT("op"),TEXT("init"));Init->SetStringField(TEXT("id"),TEXT("init"));
        Init->SetStringField(TEXT("app_id"),AppId);Init->SetStringField(TEXT("token"),Token);
        bool WorldEnabled=true;GConfig->GetBool(TEXT("LiveInteraction"),TEXT("WorldLeaderboardEnabled"),WorldEnabled,GGameIni);
        FString BackendUrl;GConfig->GetString(TEXT("DouyinLiveProvider"),TEXT("BackendUrl"),BackendUrl,GGameIni);
        if(WorldEnabled&&!BackendUrl.IsEmpty())Init->SetStringField(TEXT("backend_url"),BackendUrl);
#if !UE_BUILD_SHIPPING
        bool AllowLoopback=false;GConfig->GetBool(TEXT("DouyinLiveProvider"),TEXT("AllowInsecureBackendLoopback"),AllowLoopback,GGameIni);
        Init->SetBoolField(TEXT("allow_insecure_backend"),AllowLoopback);
#endif
        TArray<TSharedPtr<FJsonValue>> Types;
        for(const TCHAR* Type:{TEXT("live_comment"),TEXT("live_like"),TEXT("live_gift"),TEXT("live_team"),TEXT("live_enter"),TEXT("live_follow")})Types.Add(MakeShared<FJsonValueString>(Type));
        Init->SetArrayField(TEXT("msg_types"),Types);
        const bool Queued=Transport->Enqueue(Encode(Init));Init->RemoveField(TEXT("token"));Token.Reset();
        if(!Queued) {Transport->Shutdown();Transport.Reset();Host->EndProviderSession(*this,TEXT("SDK_HOST_START_FAILED"));return true;}
        Note(TEXT("init_queued"));
        StartTime=FPlatformTime::Seconds();
        TickHandle=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this,&FDouyinLiveProvider::Tick));
        return true;
    }
    void Stop() override
    {
        if(TickHandle.IsValid()) {FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);TickHandle.Reset();}
        if(Transport) {Transport->Shutdown();Transport.Reset();}
        Host.Reset();PendingEvents.Empty();PendingCommands.Empty();Session=FLiveSession();
    }
    ~FDouyinLiveProvider() override {Stop();}
    bool SendCommand(const FString& RequestId,const FString& Operation,const TSharedRef<FJsonObject>& Payload) override
    {
        if(!Host.IsValid() || !Transport || !Host->IsEventFromCurrentSession(Session) || PendingCommands.Num()>=8192)return false;
        static const TSet<FString> Operations={TEXT("ack"),TEXT("round"),TEXT("user_group"),TEXT("user_results"),TEXT("room_rank"),TEXT("complete"),TEXT("backend_round")};
        if(!Operations.Contains(Operation))return false;
        const auto Command=MakeShared<FJsonObject>();Command->Values=Payload->Values;
        Command->SetStringField(TEXT("op"),Operation);Command->SetStringField(TEXT("id"),RequestId);
        Command->SetStringField(TEXT("room_id"),Session.RoomId);
        if(Operation==TEXT("backend_round"))Command->SetStringField(TEXT("app_id"),Session.AppId);
        if(!Transport->Enqueue(Encode(Command)))return false;
        if(Operation!=TEXT("ack"))Note(TEXT("command_sent"),Operation,true,Operation==TEXT("round")?Num(Command,TEXT("status")):0);
        const double Timeout=Operation==TEXT("backend_round")?45.0:30.0;
        PendingCommands.Add(RequestId,{Session,FPlatformTime::Seconds()+Timeout});return true;
    }
private:
    struct FPendingCommand {FLiveSession Session;double Deadline=0;};
    TWeakObjectPtr<ULiveInteractionSubsystem> Host;
    TUniquePtr<FDouyinHostTransport> Transport;
    FTSTicker::FDelegateHandle TickHandle;
    FLiveSession Session;
    FString AppId;
    FString DiagnosticPath;
    FString IdentityDiagnosticSalt=FGuid::NewGuid().ToString(EGuidFormats::Digits);
    TMap<FString,int64> DiagnosticEvents;
    TArray<TSharedPtr<FJsonObject>> PendingEvents;
    TMap<FString,FPendingCommand> PendingCommands;
    double StartTime=0;
    bool bReceivedRoom=false;
    void Note(const FString& Stage,const FString& Operation=TEXT("init"),bool Success=true,int64 Code=0)
    {
        const FString Record=DiagnosticRecord(Stage,Operation,Success,Code);
        if(IFileManager::Get().FileSize(*DiagnosticPath)+Record.Len()*4>1024*1024)return;
        FFileHelper::SaveStringToFile(Record,*DiagnosticPath,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,&IFileManager::Get(),FILEWRITE_Append);
    }

    bool Tick(float)
    {
        if(!Host.IsValid() || !Transport)return false;
        if(Transport->HasFailed()) {
            Note(TEXT("host_exited"),TEXT("host"),false);
            Host->EndProviderSession(*this,TEXT("SDK_HOST_EXITED"));Transport->Shutdown();Transport.Reset();return false;
        }
        if(!bReceivedRoom && FPlatformTime::Seconds()-StartTime>90) {
            Note(TEXT("room_timeout"),TEXT("room"),false);
            Host->EndProviderSession(*this,TEXT("SDK_ROOM_TIMEOUT"));Transport->Shutdown();Transport.Reset();return false;
        }
        FString Line;
        for(int32 I=0;I<256&&Transport->Dequeue(Line);++I) {
            TSharedPtr<FJsonObject> Message;
            if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Line),Message) || !Message)continue;
            const FString Kind=Str(Message,TEXT("kind"));
            if(Kind==TEXT("room")) {
                const FString Room=Str(Message,TEXT("room_id"));
                const FString ReceivedApp=Str(Message,TEXT("app_id"));
                const FString Anchor=Str(Obj(Message,TEXT("anchor")),TEXT("open_id"));
                if(ReceivedApp!=AppId || Room.IsEmpty() || Anchor.IsEmpty())continue;
                if(!Host->IsEventFromCurrentSession(Session) || Session.RoomId!=Room || Session.AnchorUserId!=Anchor) {
                    PendingEvents.Empty();PendingCommands.Empty();
                    IdentityDiagnosticSalt=FGuid::NewGuid().ToString(EGuidFormats::Digits);
                    Session=Host->BeginProviderSession(*this,AppId,Room,Anchor);
                    Note(TEXT("room_ready"));
                    UE_LOG(LogDouyinLiveProvider,Display,TEXT("SDK room connected: app_id=%s room_id=%s"),*AppId,*Room);
                }
                Host->SetAnchorProfile(Session,Str(Obj(Message,TEXT("anchor")),TEXT("nickname")),Str(Obj(Message,TEXT("anchor")),TEXT("avatar_url")));
                bReceivedRoom=Session.Nonce.IsValid();
            } else if(Kind==TEXT("status")) {
                bool StatusSuccess=false;Message->TryGetBoolField(TEXT("success"),StatusSuccess);
                if(Str(Message,TEXT("op"))!=TEXT("ack")||!StatusSuccess)Note(TEXT("sdk_status"),Str(Message,TEXT("op")),StatusSuccess,Num(Message,TEXT("err_code"),-1));
                if(!StatusSuccess)UE_LOG(LogDouyinLiveProvider,Warning,TEXT("SDK operation failed: op=%s err_code=%lld state=%s"),*Str(Message,TEXT("op")),Num(Message,TEXT("err_code"),-1),*Str(Message,TEXT("state")));
                const FString Request=Str(Message,TEXT("request_id"));
                if(auto* Pending=PendingCommands.Find(Request)) {
                    const bool Current=Host->IsEventFromCurrentSession(Pending->Session);
                    bool Success=false;Message->TryGetBoolField(TEXT("success"),Success);
                    PendingCommands.Remove(Request);
                    if(Current)Host->ReportCommandResult(*this,Request,Success,static_cast<int32>(Num(Message,TEXT("err_code"),-1)),Obj(Message,TEXT("data")));
                }
                const FString State=Str(Message,TEXT("state"));
                if(State==TEXT("disconnected") || State==TEXT("failed")) {
                    Host->EndProviderSession(*this,State==TEXT("disconnected")?TEXT("SDK_DISCONNECTED"):TEXT("SDK_AUTH_OR_PUSH_FAILED"));
                    Session=FLiveSession();PendingEvents.Empty();PendingCommands.Empty();
                    if(State==TEXT("failed")) {Transport->Shutdown();Transport.Reset();return false;}
                }
            } else if(Kind==TEXT("event") && Host->IsEventFromCurrentSession(Session) && Str(Message,TEXT("room_id"))==Session.RoomId) {
                const FString EventType=Str(Message,TEXT("msg_type"));
                const int64 EventCount=++DiagnosticEvents.FindOrAdd(EventType);
                if(EventCount==1||EventCount%100==0)Note(TEXT("event_received"),EventType,true,EventCount);
                const FString IdentityRecord=IdentityDiagnosticRecord(IdentityDiagnosticSalt,Message);
                if(!IdentityRecord.IsEmpty() && IFileManager::Get().FileSize(*DiagnosticPath)+FTCHARToUTF8(*IdentityRecord).Length()<=1024*1024)
                    FFileHelper::SaveStringToFile(IdentityRecord,*DiagnosticPath,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,&IFileManager::Get(),FILEWRITE_Append);
                if(PendingEvents.Num()>=4096) {Host->EndProviderSession(*this,TEXT("SDK_EVENT_BACKPRESSURE"));Transport->Shutdown();Transport.Reset();return false;}
                PendingEvents.Add(Message);
            }
        }
        // Preserve SDK arrival order (join before command, follow before leave).
        for(int32 I=0,Processed=0;I<PendingEvents.Num()&&Processed<256;++Processed) {
            if(Deliver(PendingEvents[I]))PendingEvents.RemoveAt(I,1,EAllowShrinking::No);else break;
        }
        TArray<FString> Expired;
        for(const auto& Pair:PendingCommands)if(FPlatformTime::Seconds()>Pair.Value.Deadline)Expired.Add(Pair.Key);
        for(const FString& Request:Expired) {PendingCommands.Remove(Request);Host->ReportCommandResult(*this,Request,false,-408);}
        return true;
    }
    /** true means remove from the local retry queue, not fulfillment. */
    bool Deliver(const TSharedPtr<FJsonObject>& Message)
    {
        if(!Host->IsEventFromCurrentSession(Session))return true;
        const FString Type=Str(Message,TEXT("msg_type")),Id=Str(Message,TEXT("msg_id"));
        auto User=Obj(Message,TEXT("user"));
        const FString UserId=Str(User,TEXT("open_id")),Name=Str(User,TEXT("nickname")),Avatar=Str(User,TEXT("avatar_url"));
        if(Type==TEXT("live_comment")) {
            if(Host->OnComment.GetAllObjects().IsEmpty())return false;
            FLiveComment E;E.Session=Session;E.MessageId=Id;E.UserId=UserId;E.Nickname=Name;E.AvatarUrl=Avatar;E.Content=Str(Message,TEXT("content"));Host->DeliverComment(E);
        } else if(Type==TEXT("live_like")) {
            if(Host->OnLike.GetAllObjects().IsEmpty())return false;
            const int64 Count=Num(Message,TEXT("count"));if(Count<1)return true;
            FLiveLike E;E.Session=Session;E.MessageId=Id;E.UserId=UserId;E.Nickname=Name;E.Count=Count;Host->DeliverLike(E);
        } else if(Type==TEXT("live_gift")) {
            if(Host->OnGift.GetAllObjects().IsEmpty())return false;
            FLiveGift E;FString Rejection;
            const bool Decoded=FDouyinGiftDecoder::Decode(Message,Session,E,Rejection);
            const bool Delivered=Decoded&&Host->DeliverGift(E);
            if(Decoded&&!Delivered)Rejection=TEXT("delivery_rejected");
            const FString Record=GiftDiagnosticRecord(E,Rejection,Delivered);
            if(IFileManager::Get().FileSize(*DiagnosticPath)+FTCHARToUTF8(*Record).Length()<=1024*1024)
                FFileHelper::SaveStringToFile(Record,*DiagnosticPath,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,&IFileManager::Get(),FILEWRITE_Append);
            if(Rejection==TEXT("mapping_missing")) {
                if(Host->ConnectionStatus!=TEXT("SDK_GIFT_MAPPING_MISSING"))UE_LOG(LogDouyinLiveProvider,Warning,TEXT("Unmapped gift received; check DouyinLiveProvider.Gifts. No effect or fulfillment ACK submitted."));
                Host->ConnectionStatus=TEXT("SDK_GIFT_MAPPING_MISSING");
            }
        } else if(Type==TEXT("live_follow")) {
            if(Host->OnFollow.GetAllObjects().IsEmpty())return false;
            FLiveFollow E;E.Session=Session;E.MessageId=Id;E.UserId=UserId;E.Nickname=Name;E.AvatarUrl=Avatar;
            E.TargetUserId=Str(Message,TEXT("follow_target"));E.Action=static_cast<int32>(Num(Message,TEXT("follow_action")));Host->DeliverFollow(E);
        } else if(Type==TEXT("live_enter")) {
            if(Host->OnPresence.GetAllObjects().IsEmpty())return false;
            FLivePresence E;E.Session=Session;E.MessageId=Id;E.UserId=UserId;E.Nickname=Name;E.AvatarUrl=Avatar;
            E.TimestampMs=Num(Message,TEXT("timestamp"));E.EnterType=static_cast<int32>(Num(Message,TEXT("enter_type")));
            E.FollowStatus=static_cast<int32>(Num(Message,TEXT("follow_status")));E.EnterRoomScene=static_cast<int32>(Num(Message,TEXT("enter_scene")));
            E.InviterId=Str(Message,TEXT("inviter_id"));Host->DeliverPresence(E);
        } else if(Type==TEXT("live_team")) {
            if(Host->OnTeamSelection.GetAllObjects().IsEmpty())return false;
            FLiveTeamSelection E;E.Session=Session;E.MessageId=Id;E.UserId=UserId;E.Nickname=Name;E.AvatarUrl=Avatar;
            E.GroupId=Str(Message,TEXT("group_id"));Host->DeliverTeamSelection(E);
        }
        return true;
    }
};
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDouyinHostContractTest,"GeometricWarfare.LiveInteraction.DouyinHostContract",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDouyinHostContractTest::RunTest(const FString&)
{
    FConfigFile FixtureConfig;
    FixtureConfig.ProcessInputFileContents(TEXT("[DouyinLiveProvider]\nBackendUrl=\"https://example.invalid/live-api\"\n"),TEXT("PublicConfigFixture"));
    FString ConfiguredBackend;
    FixtureConfig.GetString(TEXT("DouyinLiveProvider"),TEXT("BackendUrl"),ConfiguredBackend);
    TestEqual(TEXT("Quoted INI preserves complete HTTPS backend URL"),ConfiguredBackend,FString(TEXT("https://example.invalid/live-api")));
    const FString PrivateLabel=TEXT("fake-token-private-comment");
    const FString Record=DiagnosticRecord(PrivateLabel,PrivateLabel,false,-1006);
    TestFalse(TEXT("Diagnostics reject arbitrary secret-bearing labels"),Record.Contains(PrivateLabel));
    TestTrue(TEXT("Diagnostics retain subscription stage"),DiagnosticRecord(TEXT("sdk_status"),TEXT("subscribe:live_team"),true,0).Contains(TEXT("subscribe:live_team")));
    auto Identity=MakeShared<FJsonObject>(),IdentityUser=MakeShared<FJsonObject>();
    Identity->SetStringField(TEXT("msg_type"),TEXT("live_comment"));Identity->SetStringField(TEXT("msg_id"),TEXT("private-message-id"));
    Identity->SetStringField(TEXT("content"),TEXT("private-comment-token"));
    IdentityUser->SetStringField(TEXT("open_id"),TEXT("private-user-id"));IdentityUser->SetStringField(TEXT("nickname"),TEXT("private-nickname"));
    IdentityUser->SetStringField(TEXT("avatar_url"),TEXT("https://private-avatar.invalid"));Identity->SetObjectField(TEXT("user"),IdentityUser);
    const FString IdentityRecord=IdentityDiagnosticRecord(TEXT("private-session-salt"),Identity);
    TestFalse(TEXT("Identity audit persists no raw ID, nickname, content, URL, message ID or salt"),IdentityRecord.Contains(TEXT("private-")));
    TSharedPtr<FJsonObject> Correlation;
    TestTrue(TEXT("Identity audit is valid JSON"),FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(IdentityRecord),Correlation));
    if(Correlation) {
        const FString UserFingerprint=Str(Correlation,TEXT("user_fingerprint")),NameFingerprint=Str(Correlation,TEXT("name_fingerprint"));
        TestEqual(TEXT("Identity fingerprint uses full session-local digest"),UserFingerprint.Len(),40);
        TestEqual(TEXT("Unknown comment content is classified without persisting it"),Str(Correlation,TEXT("action")),FString(TEXT("other")));
        Identity->SetStringField(TEXT("content"),TEXT("武器3"));
        TSharedPtr<FJsonObject> WeaponDiagnostic;
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(IdentityDiagnosticRecord(TEXT("private-session-salt"),Identity)),WeaponDiagnostic);
        TestEqual(TEXT("Weapon diagnostics retain numeric selection without comment text"),Num(WeaponDiagnostic,TEXT("weapon_number"),-1),int64(3));
        Identity->SetStringField(TEXT("msg_type"),TEXT("live_enter"));
        Identity->SetNumberField(TEXT("follow_status"),1);Identity->SetNumberField(TEXT("enter_type"),1);Identity->SetNumberField(TEXT("timestamp"),0);
        TSharedPtr<FJsonObject> PresenceDiagnostic;
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(IdentityDiagnosticRecord(TEXT("private-session-salt"),Identity)),PresenceDiagnostic);
        TestEqual(TEXT("Presence diagnostics retain platform follow qualification"),Num(PresenceDiagnostic,TEXT("follow_status"),-1),int64(1));
        TestEqual(TEXT("Presence diagnostics retain enter versus leave direction"),Num(PresenceDiagnostic,TEXT("enter_type"),-1),int64(1));
        TestEqual(TEXT("Presence diagnostics distinguish zero SDK timestamps"),Str(PresenceDiagnostic,TEXT("sdk_timestamp_ms")),FString(TEXT("0")));
        TestFalse(TEXT("Presence diagnostics persist no raw identities or private text"),IdentityDiagnosticRecord(TEXT("private-session-salt"),Identity).Contains(TEXT("private-")));
        Identity->SetStringField(TEXT("msg_type"),TEXT("live_comment"));
        Identity->SetStringField(TEXT("content"),TEXT("private-comment-token"));
        IdentityUser->SetStringField(TEXT("open_id"),TEXT("  private-user-id  "));
        TSharedPtr<FJsonObject> Normalized;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(IdentityDiagnosticRecord(TEXT("private-session-salt"),Identity)),Normalized);
        TestEqual(TEXT("Whitespace matches the gameplay identity normalization"),Str(Normalized,TEXT("user_fingerprint")),UserFingerprint);
        IdentityUser->SetStringField(TEXT("open_id"),TEXT("second-private-user-id"));
        TSharedPtr<FJsonObject> SameName;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(IdentityDiagnosticRecord(TEXT("private-session-salt"),Identity)),SameName);
        TestEqual(TEXT("Same-name identities can be correlated by display-name digest"),Str(SameName,TEXT("name_fingerprint")),NameFingerprint);
        TestNotEqual(TEXT("Distinct platform IDs retain different identity fingerprints"),Str(SameName,TEXT("user_fingerprint")),UserFingerprint);
        IdentityUser->SetStringField(TEXT("open_id"),TEXT("private-user-id"));
        TSharedPtr<FJsonObject> NewSession;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(IdentityDiagnosticRecord(TEXT("another-session-salt"),Identity)),NewSession);
        TestNotEqual(TEXT("New session cannot correlate the old identity digest"),Str(NewSession,TEXT("user_fingerprint")),UserFingerprint);
    }
    Identity->SetStringField(TEXT("msg_type"),TEXT("live_like"));
    TestTrue(TEXT("Like floods do not consume the identity diagnostic budget"),IdentityDiagnosticRecord(TEXT("salt"),Identity).IsEmpty());
    Identity->SetStringField(TEXT("msg_type"),TEXT("arbitrary-private-type"));
    TestTrue(TEXT("Unknown identity event types are not persisted"),IdentityDiagnosticRecord(TEXT("salt"),Identity).IsEmpty());
    auto Data=MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("count"),TEXT("9223372036854775807"));TestEqual(TEXT("Maximum SDK Int64 count is lossless"),Num(Data,TEXT("count")),MAX_int64);
    Data->SetStringField(TEXT("count"),TEXT("9007199254740993"));TestEqual(TEXT("Counts beyond double precision are lossless"),Num(Data,TEXT("count")),int64(9007199254740993LL));
    for(const TCHAR* Invalid:{TEXT("9223372036854775808"),TEXT("12x"),TEXT("1.2"),TEXT(""),TEXT("-"),TEXT(" 4")}) {
        Data->SetStringField(TEXT("count"),Invalid);TestEqual(TEXT("Malformed or overflowing count is rejected"),Num(Data,TEXT("count"),-99),int64(-99));
    }
    Data->SetNumberField(TEXT("count"),2.5);TestEqual(TEXT("Fractional numeric count is rejected"),Num(Data,TEXT("count"),-99),int64(-99));
    Data->SetNumberField(TEXT("count"),9007199254740992.0);TestEqual(TEXT("Unsafe numeric count requires decimal string"),Num(Data,TEXT("count"),-99),int64(-99));
    const TCHAR* Key=TEXT("__GiftPaddingContract");FString Old;
    const bool Had=GConfig->GetString(TEXT("DouyinLiveProvider.Gifts"),Key,Old,GGameIni);
    GConfig->SetString(TEXT("DouyinLiveProvider.Gifts"),Key,TEXT("sdk/base64+id=="),GGameIni);
    TestEqual(TEXT("Gift ID INI values preserve equals padding"),GiftNameForId(TEXT("sdk/base64+id==")),FString(Key));
    TestTrue(TEXT("Unknown encrypted gift IDs have no fallback effect"),GiftNameForId(TEXT("__unknown__")).IsEmpty());
    if(Had)GConfig->SetString(TEXT("DouyinLiveProvider.Gifts"),Key,*Old,GGameIni);else GConfig->RemoveKey(TEXT("DouyinLiveProvider.Gifts"),Key,GGameIni);
    auto GiftMessage=MakeShared<FJsonObject>(),GiftSender=MakeShared<FJsonObject>();
    GiftMessage->SetStringField(TEXT("msg_type"),TEXT("live_gift"));GiftMessage->SetStringField(TEXT("msg_id"),TEXT("private-gift-message"));
    GiftSender->SetStringField(TEXT("open_id"),TEXT("private-user"));GiftSender->SetStringField(TEXT("nickname"),TEXT("private-name"));GiftMessage->SetObjectField(TEXT("user"),GiftSender);
    FLiveSession GiftSession;GiftSession.AnchorUserId=TEXT("anchor");
    FLiveGift DecodedGift;FString Rejection;
    const TCHAR* GiftNames[]={TEXT("仙女棒"),TEXT("能力药丸"),TEXT("魔法镜"),TEXT("甜甜圈"),TEXT("能量电池")};
    for(const TCHAR* GiftName:GiftNames)for(bool IsTest:{false,true}) {
        FString GiftId;TestTrue(TEXT("Every supported gift has a configured official mapping"),GConfig->GetString(TEXT("DouyinLiveProvider.Gifts"),GiftName,GiftId,GGameIni));
        GiftMessage->SetStringField(TEXT("gift_id"),GiftId);GiftMessage->SetStringField(TEXT("count"),TEXT("66"));GiftMessage->SetBoolField(TEXT("is_test"),IsTest);
        TestTrue(TEXT("Both paid and platform-review gifts pass the production adapter"),FDouyinGiftDecoder::Decode(GiftMessage,GiftSession,DecodedGift,Rejection));
        TestEqual(TEXT("Official name resolves exactly"),DecodedGift.GiftName,FString(GiftName));TestEqual(TEXT("Review provenance is never discarded"),DecodedGift.bIsTestData,IsTest);
        TestEqual(TEXT("Batch count is preserved"),DecodedGift.Count,int64(66));
        TestFalse(TEXT("Gift audit contains no raw identity or message data"),GiftDiagnosticRecord(DecodedGift,TEXT(""),true).Contains(TEXT("private-")));
    }
    GiftMessage->SetStringField(TEXT("gift_id"),TEXT("unknown-encrypted-gift"));
    TestFalse(TEXT("Review marker cannot turn an unknown gift into a reward"),FDouyinGiftDecoder::Decode(GiftMessage,GiftSession,DecodedGift,Rejection));
    TestEqual(TEXT("Missing mapping is diagnosable"),Rejection,FString(TEXT("mapping_missing")));
    FString BatteryId;GConfig->GetString(TEXT("DouyinLiveProvider.Gifts"),TEXT("能量电池"),BatteryId,GGameIni);GiftMessage->SetStringField(TEXT("gift_id"),BatteryId);
    GiftMessage->SetStringField(TEXT("audience_open_id"),TEXT("other-audience"));
    TestFalse(TEXT("Review gift for another co-play recipient remains excluded"),FDouyinGiftDecoder::Decode(GiftMessage,GiftSession,DecodedGift,Rejection));
    TestEqual(TEXT("Audience rejection is diagnosable"),Rejection,FString(TEXT("other_audience")));
    GiftMessage->SetStringField(TEXT("audience_open_id"),TEXT("anchor"));GiftMessage->SetStringField(TEXT("count"),TEXT("0"));
    TestFalse(TEXT("Review data must still have a positive count"),FDouyinGiftDecoder::Decode(GiftMessage,GiftSession,DecodedGift,Rejection));
    GiftMessage->SetStringField(TEXT("count"),TEXT("1"));GiftMessage->SetStringField(TEXT("is_test"),TEXT("true"));
    TestFalse(TEXT("Malformed review flag is rejected"),FDouyinGiftDecoder::Decode(GiftMessage,GiftSession,DecodedGift,Rejection));
    GiftMessage->RemoveField(TEXT("is_test"));
    TestTrue(TEXT("Absent optional SDK test flag preserves ordinary paid delivery"),FDouyinGiftDecoder::Decode(GiftMessage,GiftSession,DecodedGift,Rejection));
    TestFalse(TEXT("Absent test flag is false"),DecodedGift.bIsTestData);
    return true;
}
#endif
}
bool FDouyinGiftDecoder::Decode(const TSharedPtr<FJsonObject>& Message,const FLiveSession& Session,FLiveGift& Gift,FString& Rejection)
{
    Gift=FLiveGift();Rejection.Reset();
    const auto Reject=[&](const TCHAR* Reason){Rejection=Reason;return false;};
    if(Str(Message,TEXT("msg_type"))!=TEXT("live_gift"))return Reject(TEXT("wrong_type"));
    Gift.Count=Num(Message,TEXT("count"));
    if(Message->HasField(TEXT("is_test"))&&(!Message->HasTypedField<EJson::Boolean>(TEXT("is_test"))||!Message->TryGetBoolField(TEXT("is_test"),Gift.bIsTestData)))return Reject(TEXT("invalid_test_flag"));
    if(Gift.Count<1)return Reject(TEXT("invalid_count"));
    Gift.GiftName=GiftNameForId(Str(Message,TEXT("gift_id")));
    if(!liveinteraction::IsSupportedGift(Gift.GiftName))return Reject(TEXT("mapping_missing"));
    // The SDK marks platform review gifts as test data. They still require
    // gameplay/fulfillment; the consumer isolates purchases and statistics.
    const FString Audience=Str(Message,TEXT("audience_open_id"));
    if(!Audience.IsEmpty()&&Audience!=Session.AnchorUserId)return Reject(TEXT("other_audience"));
    const auto User=Obj(Message,TEXT("user"));
    Gift.Session=Session;Gift.MessageId=Str(Message,TEXT("msg_id"));Gift.UserId=Str(User,TEXT("open_id"));Gift.Nickname=Str(User,TEXT("nickname"));
    return true;
}
class FDouyinLiveProviderModule final : public IModuleInterface
{
public:
    void StartupModule() override {FLiveInteractionProviderRegistry::Register(TEXT("douyin"),[]{return MakeShared<FDouyinLiveProvider>();});}
    void ShutdownModule() override {FLiveInteractionProviderRegistry::Unregister(TEXT("douyin"));}
};
IMPLEMENT_MODULE(FDouyinLiveProviderModule,DouyinLiveProvider)
