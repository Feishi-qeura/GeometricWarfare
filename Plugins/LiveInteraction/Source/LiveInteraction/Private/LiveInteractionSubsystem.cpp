#include "LiveInteractionSubsystem.h"
#include "LiveInteractionRules.h"
#include "LiveGiftRules.h"
#include "Async/Async.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "IWebSocket.h"
#include "WebSocketsModule.h"
#include "AvatarThumbnail.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Engine/Texture2D.h"
#include "Dom/JsonObject.h"

bool ULiveInteractionSubsystem::SimulateComment(const FString& UserId,const FString& Nickname,const FString& Content)
{
    if(!bLocalTestMode || bDevRelayMode || !CurrentSession.Nonce.IsValid()) return false;
    FLiveComment Event; Event.Session=CurrentSession; Event.MessageId=FGuid::NewGuid().ToString(); Event.UserId=UserId;
    Event.Nickname=Nickname; Event.Content=Content;
    return DeliverComment(Event);
}
bool ULiveInteractionSubsystem::DeliverComment(const FLiveComment& Comment)
{
    check(IsInGameThread());
    if(!IsEventFromCurrentSession(Comment.Session)) return false;
    FLiveComment Event=Comment;
    Event.UserId.TrimStartAndEndInline(); Event.MessageId.TrimStartAndEndInline(); Event.Content.TrimStartAndEndInline();
    if(Event.UserId.IsEmpty() || Event.MessageId.IsEmpty() || Event.Content.IsEmpty() || Event.UserId.Len()>256 || Event.MessageId.Len()>128 || Event.Content.Len()>128 || Event.Nickname.Len()>64 || Event.AvatarUrl.Len()>4096) return false;
    if(OnComment.GetAllObjects().IsEmpty()) return false;
    if(!AcceptEventId(Event.MessageId)) return false;
    OnComment.Broadcast(Event); return true;
}
bool ULiveInteractionSubsystem::SimulateLike(const FString& UserId,const FString& Nickname,int32 Count)
{
    if(!bLocalTestMode || bDevRelayMode || !CurrentSession.Nonce.IsValid()) return false;
    FLiveLike Event; Event.Session=CurrentSession; Event.MessageId=FGuid::NewGuid().ToString(); Event.UserId=UserId;
    Event.Nickname=Nickname; Event.Count=Count;
    return DeliverLike(Event);
}
bool ULiveInteractionSubsystem::SimulateShare(const FString& UserId,const FString& Nickname)
{
    if(!bLocalTestMode || bDevRelayMode || !CurrentSession.Nonce.IsValid()) return false;
    FLiveShare Event; Event.Session=CurrentSession; Event.MessageId=FGuid::NewGuid().ToString(); Event.UserId=UserId; Event.Nickname=Nickname;
    return DeliverShare(Event);
}
bool ULiveInteractionSubsystem::DeliverLike(const FLiveLike& Like)
{
    check(IsInGameThread());
    if(!IsEventFromCurrentSession(Like.Session)) return false;
    FLiveLike Event=Like;
    Event.UserId.TrimStartAndEndInline(); Event.MessageId.TrimStartAndEndInline();
    if(Event.UserId.IsEmpty() || Event.MessageId.IsEmpty() || Event.UserId.Len()>256 || Event.MessageId.Len()>128 || Event.Nickname.Len()>64 || Event.Count<=0 || (bLocalTestMode && Event.Count>100)) return false;
    if(OnLike.GetAllObjects().IsEmpty()) return false;
    if(!AcceptEventId(Event.MessageId)) return false;
    OnLike.Broadcast(Event); return true;
}
bool ULiveInteractionSubsystem::DeliverShare(const FLiveShare& Share)
{
    check(IsInGameThread());
    if(!IsEventFromCurrentSession(Share.Session)) return false;
    FLiveShare Event=Share;
    Event.UserId.TrimStartAndEndInline(); Event.MessageId.TrimStartAndEndInline();
    if(Event.UserId.IsEmpty() || Event.MessageId.IsEmpty() || Event.UserId.Len()>256 || Event.MessageId.Len()>128 || Event.Nickname.Len()>64) return false;
    if(OnShare.GetAllObjects().IsEmpty()) return false;
    if(!AcceptEventId(Event.MessageId)) return false;
    OnShare.Broadcast(Event); return true;
}
bool ULiveInteractionSubsystem::SimulateGift(const FString& UserId,const FString& Nickname,const FString& GiftName,int32 Count)
{
    if(!bLocalTestMode || bDevRelayMode || !CurrentSession.Nonce.IsValid()) return false;
    FLiveGift Event; Event.Session=CurrentSession; Event.MessageId=FGuid::NewGuid().ToString(); Event.UserId=UserId;
    Event.Nickname=Nickname; Event.GiftName=GiftName; Event.Count=Count;
    return DeliverGift(Event);
}
bool ULiveInteractionSubsystem::DeliverGift(const FLiveGift& Gift)
{
    check(IsInGameThread());
    if(!IsEventFromCurrentSession(Gift.Session)) return false;
    FLiveGift Event=Gift; Event.UserId.TrimStartAndEndInline(); Event.MessageId.TrimStartAndEndInline();
    if(Event.UserId.IsEmpty() || Event.MessageId.IsEmpty() || Event.UserId.Len()>256 || Event.MessageId.Len()>128 ||
       Event.Nickname.Len()>64 || !liveinteraction::IsSupportedGift(Event.GiftName) || Event.Count<=0 || (bLocalTestMode && Event.Count>100)) return false;
    if(OnGift.GetAllObjects().IsEmpty()) return false;
    if(!AcceptEventId(Event.MessageId)) return false;
    OnGift.Broadcast(Event); return true;
}
namespace {
bool ValidLiveUserEnvelope(const FString& MessageId,const FString& UserId,const FString& Nickname,const FString& AvatarUrl) {
    return !MessageId.IsEmpty() && !UserId.IsEmpty() && MessageId.Len()<=128 && UserId.Len()<=256 && Nickname.Len()<=64 && AvatarUrl.Len()<=4096;
}
}
bool ULiveInteractionSubsystem::DeliverFollow(const FLiveFollow& Follow)
{
    check(IsInGameThread());
    if(!IsEventFromCurrentSession(Follow.Session))return false;
    FLiveFollow Event=Follow;
    Event.MessageId.TrimStartAndEndInline();Event.UserId.TrimStartAndEndInline();Event.TargetUserId.TrimStartAndEndInline();
    if(!ValidLiveUserEnvelope(Event.MessageId,Event.UserId,Event.Nickname,Event.AvatarUrl) || Event.TargetUserId.IsEmpty() || Event.TargetUserId.Len()>256 || Event.Action<1 || Event.Action>3)return false;
    if(OnFollow.GetAllObjects().IsEmpty() || !AcceptEventId(Event.MessageId))return false;
    OnFollow.Broadcast(Event);return true;
}
bool ULiveInteractionSubsystem::DeliverPresence(const FLivePresence& Presence)
{
    check(IsInGameThread());
    if(!IsEventFromCurrentSession(Presence.Session))return false;
    FLivePresence Event=Presence;
    Event.MessageId.TrimStartAndEndInline();Event.UserId.TrimStartAndEndInline();Event.InviterId.TrimStartAndEndInline();
    if(!ValidLiveUserEnvelope(Event.MessageId,Event.UserId,Event.Nickname,Event.AvatarUrl) || Event.TimestampMs<0 || Event.EnterType<1 || Event.EnterType>2 || Event.FollowStatus<0 || Event.FollowStatus>3 || Event.InviterId.Len()>256 || Event.EnterRoomScene<0)return false;
    if(OnPresence.GetAllObjects().IsEmpty() || !AcceptEventId(Event.MessageId))return false;
    OnPresence.Broadcast(Event);return true;
}
bool ULiveInteractionSubsystem::DeliverTeamSelection(const FLiveTeamSelection& Selection)
{
    check(IsInGameThread());
    if(!IsEventFromCurrentSession(Selection.Session))return false;
    FLiveTeamSelection Event=Selection;
    Event.MessageId.TrimStartAndEndInline();Event.UserId.TrimStartAndEndInline();Event.GroupId.TrimStartAndEndInline();
    if(!ValidLiveUserEnvelope(Event.MessageId,Event.UserId,Event.Nickname,Event.AvatarUrl) || Event.GroupId.IsEmpty() || Event.GroupId.Len()>128)return false;
    if(OnTeamSelection.GetAllObjects().IsEmpty() || !AcceptEventId(Event.MessageId))return false;
    OnTeamSelection.Broadcast(Event);return true;
}
FString ULiveInteractionSubsystem::SubmitCommand(const FString& Operation,const TSharedRef<FJsonObject>& Payload)
{
    check(IsInGameThread());
    if(!IsConnected() || Operation.IsEmpty() || Operation.Len()>128 || PendingCommands.Num()>=4096)return FString();
    const FString RequestId=FGuid::NewGuid().ToString(EGuidFormats::Digits);
    PendingCommands.Add(RequestId,CurrentSession);
    if(!Provider->SendCommand(RequestId,Operation,Payload)) { PendingCommands.Remove(RequestId);return FString(); }
    return RequestId;
}
void ULiveInteractionSubsystem::ReportCommandResult(ILiveInteractionProvider& Source,const FString& RequestId,bool bSuccess,int32 ErrorCode,TSharedPtr<FJsonObject> Data)
{
    check(IsInGameThread());
    if(Provider.Get()!=&Source)return;
    const FLiveSession* Pending=PendingCommands.Find(RequestId);
    if(!Pending || !IsEventFromCurrentSession(*Pending))return;
    const FLiveSession Snapshot=*Pending;PendingCommands.Remove(RequestId);
    FLiveCommandReply Reply;Reply.Session=Snapshot;Reply.RequestId=RequestId;Reply.bSuccess=bSuccess;Reply.ErrorCode=ErrorCode;Reply.Data=MoveTemp(Data);
    OnCommandReply.Broadcast(Reply);
    if(!IsEventFromCurrentSession(Snapshot))return;
    OnCommandResult.Broadcast(Snapshot,RequestId,bSuccess,ErrorCode);
}
bool ULiveInteractionSubsystem::NotifyEventHandled(const FLiveSession& Session,const FString& MessageId,const FString& MessageType)
{
    check(IsInGameThread());
    if(!IsEventFromCurrentSession(Session) || !IsConnected() || MessageId.IsEmpty() || MessageId.Len()>128)return false;
    if(MessageType!=TEXT("live_comment") && MessageType!=TEXT("live_like") && MessageType!=TEXT("live_gift") && MessageType!=TEXT("live_follow") && MessageType!=TEXT("live_enter") && MessageType!=TEXT("live_team"))return false;
    auto Payload=MakeShared<FJsonObject>();Payload->SetStringField(TEXT("msg_id"),MessageId);Payload->SetStringField(TEXT("msg_type"),MessageType);
    return !SubmitCommand(TEXT("ack"),Payload).IsEmpty();
}
bool ULiveInteractionSubsystem::AcceptEventId(const FString& MessageId)
{
    const auto Part=[](const FString& Value) { return FString::FromInt(Value.Len())+TEXT(":")+Value; };
    const FString Key=Part(CurrentSession.PlatformId)+Part(CurrentSession.AppId)+Part(CurrentSession.RoomId)+MessageId;
    if(SeenIds.Contains(Key)) return false;
    if(IdOrder.Num()>=4096) { SeenIds.Remove(IdOrder[0]); IdOrder.RemoveAt(0); }
    SeenIds.Add(Key); IdOrder.Add(Key);
    return true;
}
bool ULiveInteractionSubsystem::IsValidRelayEndpoint(const FString& Url,const FString& RoomId)
{
    return !RoomId.TrimStartAndEnd().IsEmpty() && RoomId.Len()<=128 && Url.Len()<=4096 &&
       (Url.StartsWith(TEXT("wss://")) || Url.StartsWith(TEXT("ws://127.0.0.1:")) || Url.StartsWith(TEXT("ws://localhost:")));
}
bool ULiveInteractionSubsystem::ConnectRelay(const FString& Url,const FString& RoomId)
{
    if(!bLocalTestMode || !IsValidRelayEndpoint(Url,RoomId)) return false;
    CloseSocket(); bDevRelayMode=true;
    ChangeSession(TEXT("local"),TEXT("dev-relay"),RoomId,true);
    ConnectionStatus=TEXT("DEV_TEST_PROTOCOL_CONNECTING");
    Socket=FWebSocketsModule::Get().CreateWebSocket(Url);
    const auto Weak=TWeakObjectPtr<ULiveInteractionSubsystem>(this); const int64 Generation=SessionGeneration;
    Socket->OnConnected().AddLambda([Weak,Generation] {
        AsyncTask(ENamedThreads::GameThread,[Weak,Generation] { if(Weak.IsValid() && Weak->SessionGeneration==Generation) Weak->ConnectionStatus=TEXT("DEV_TEST_PROTOCOL_CONNECTED"); });
    });
    Socket->OnConnectionError().AddLambda([Weak,Generation](const FString&) {
        AsyncTask(ENamedThreads::GameThread,[Weak,Generation] { if(Weak.IsValid() && Weak->SessionGeneration==Generation) { Weak->InvalidateSession(); Weak->ConnectionStatus=TEXT("DEV_TEST_PROTOCOL_ERROR"); } });
    });
    Socket->OnClosed().AddLambda([Weak,Generation](int32,const FString&,bool) {
        AsyncTask(ENamedThreads::GameThread,[Weak,Generation] { if(Weak.IsValid() && Weak->SessionGeneration==Generation) { Weak->InvalidateSession(); Weak->ConnectionStatus=TEXT("DEV_TEST_PROTOCOL_DISCONNECTED"); } });
    });
    Socket->OnMessage().AddLambda([Weak,Generation](const FString& Json) {
        if(Json.Len()>65536) return;
        AsyncTask(ENamedThreads::GameThread,[Weak,Generation,Json] { if(Weak.IsValid() && Weak->SessionGeneration==Generation) Weak->Receive(Json); });
    });
    Socket->Connect(); return true;
}
void ULiveInteractionSubsystem::CloseSocket()
{
    if(Socket) { Socket->OnConnected().Clear(); Socket->OnConnectionError().Clear(); Socket->OnClosed().Clear(); Socket->OnMessage().Clear(); Socket->Close(); Socket.Reset(); }
    bDevRelayMode=false;
}
void ULiveInteractionSubsystem::DisconnectRelay()
{
    CloseSocket();
    if(bLocalTestMode) { ChangeSession(TEXT("local"),TEXT("simulator"),TEXT("local-test"),true); ConnectionStatus=TEXT("LOCAL_TEST"); }
}
void ULiveInteractionSubsystem::Receive(const FString& Json)
{
    if(!bLocalTestMode || !bDevRelayMode || !CurrentSession.Nonce.IsValid()) return;
    FString Room,Error; TArray<FLiveComment> Comments; TArray<FLiveLike> Likes; TArray<FLiveShare> Shares; TArray<FLiveGift> Gifts;
    if(!FLiveDevEventDecoder::DecodeEvents(Json,Room,Comments,Likes,Shares,Gifts,Error) || Room!=CurrentSession.RoomId) return;
    for(auto& Comment:Comments) { Comment.Session=CurrentSession; DeliverComment(Comment); }
    for(auto& Like:Likes) { Like.Session=CurrentSession; DeliverLike(Like); }
    for(auto& Share:Shares) { Share.Session=CurrentSession; DeliverShare(Share); }
    for(auto& Gift:Gifts) { Gift.Session=CurrentSession; DeliverGift(Gift); }
}
void ULiveInteractionSubsystem::RequestAvatar(const FString& UserId,const FString& Url)
{
    check(IsInGameThread());
    if(!CurrentSession.Nonce.IsValid() || UserId.IsEmpty() || !Url.StartsWith(TEXT("https://")) || Url.Len()>4096) return;
    const FString AvatarUser=GetScopedUserId(UserId);
    if(AvatarRequests.IsPending(AvatarUser,Url)) return;
    const uint64 Ticket=AvatarRequests.Begin(AvatarUser,Url);
    if(auto* Cached=AvatarCache.Find(Url)) { AvatarRequests.Complete(AvatarUser,Ticket); OnAvatarReady.Broadcast(UserId,*Cached); return; }
    if(Requests.Num()>=100) { AvatarRequests.Complete(AvatarUser,Ticket); return; }
    auto Request=FHttpModule::Get().CreateRequest(); Request->SetURL(Url); Request->SetVerb(TEXT("GET")); Request->SetTimeout(10);
    const auto Weak=TWeakObjectPtr<ULiveInteractionSubsystem>(this);
    Request->OnRequestProgress64().BindLambda([](FHttpRequestPtr InRequest,uint64,uint64 Received) { if(Received>2*1024*1024) InRequest->CancelRequest(); });
    Request->OnProcessRequestComplete().BindLambda([Weak,UserId,AvatarUser,Url,Ticket](FHttpRequestPtr InRequest,FHttpResponsePtr Response,bool Success) {
        if(!Weak.IsValid()) return;
        Weak->Requests.Remove(InRequest);
        if(!Weak->AvatarRequests.Complete(AvatarUser,Ticket)) return;
        if(!Success || !Response || Response->GetResponseCode()!=200 || Response->GetContent().Num()>2*1024*1024) return;
        const auto& Bytes=Response->GetContent();
        auto& Images=FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
        auto Decoder=Images.CreateImageWrapper(Images.DetectImageFormat(Bytes.GetData(),Bytes.Num()));
        if(!Decoder || !Decoder->SetCompressed(Bytes.GetData(),Bytes.Num()) || Decoder->GetWidth()<=0 || Decoder->GetHeight()<=0 || Decoder->GetWidth()>1024 || Decoder->GetHeight()>1024) return;
        TArray64<uint8> Pixels;
        if(!Decoder->GetRaw(ERGBFormat::BGRA,8,Pixels)) return;
        TArray64<uint8> Thumbnail;
        Thumbnail.SetNumUninitialized(static_cast<int64>(liveinteraction::AvatarBytes));
        if(!liveinteraction::MakeAvatarThumbnailBGRA(Pixels.GetData(),static_cast<std::size_t>(Pixels.Num()),
            static_cast<int>(Decoder->GetWidth()),static_cast<int>(Decoder->GetHeight()),Thumbnail.GetData(),static_cast<std::size_t>(Thumbnail.Num()))) return;
        // Never allocate a full-resolution GPU texture: 5000 BGRA thumbnails
        // use 78.125 MiB of pixels, irrespective of the source image dimensions.
        UTexture2D* Texture=UTexture2D::CreateTransient(liveinteraction::AvatarSide,liveinteraction::AvatarSide,PF_B8G8R8A8,NAME_None,
            TConstArrayView64<uint8>(Thumbnail.GetData(),Thumbnail.Num()));
        if(Texture) { if(Weak->AvatarCache.Num()<128) Weak->AvatarCache.Add(Url,Texture); Weak->OnAvatarReady.Broadcast(UserId,Texture); }
    });
    Requests.Add(Request);
    if(!Request->ProcessRequest()) { Requests.Remove(Request); AvatarRequests.Complete(AvatarUser,Ticket); }
}
bool ULiveInteractionSubsystem::ShouldEnableLocalTest(const FString& CommandLine,bool bShipping)
{
    if(FParse::Param(*CommandLine,TEXT("GWCredentialStdin")))return false;
    const TCHAR* Cursor=*CommandLine;
    FString Argument;
    bool HasToken=false;
    while(FParse::Token(Cursor,Argument,false)) {
        if(Argument.Equals(TEXT("-token"),ESearchCase::IgnoreCase) || Argument.StartsWith(TEXT("-token="),ESearchCase::IgnoreCase)) { HasToken=true; break; }
    }
    return !bShipping && !HasToken && FParse::Param(*CommandLine,TEXT("GWLocalTest"));
}
void ULiveInteractionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    if(ShouldEnableLocalTest(FCommandLine::Get(),UE_BUILD_SHIPPING!=0)) EnableLocalTest();
    else StartPlatform(TEXT("douyin"));
}
void ULiveInteractionSubsystem::InvalidateSession(bool bNotify)
{
    AnchorNickname.Reset();
    ++SessionGeneration; CurrentSession=FLiveSession(); PendingCommands.Empty();
    AvatarRequests.Reset();
    for(auto& Request:Requests) { Request->OnProcessRequestComplete().Unbind(); Request->OnRequestProgress64().Unbind(); Request->CancelRequest(); }
    Requests.Empty(); AvatarCache.Empty();
    if(bNotify) OnSessionChanged.Broadcast();
}
void ULiveInteractionSubsystem::ChangeSession(const FString& PlatformId,const FString& AppId,const FString& RoomId,bool bLocalTest,const FString& AnchorUserId)
{
    InvalidateSession(false);
    CurrentSession.PlatformId=PlatformId; CurrentSession.AppId=AppId; CurrentSession.RoomId=RoomId; CurrentSession.AnchorUserId=AnchorUserId;
    CurrentSession.Generation=SessionGeneration; CurrentSession.bLocalTest=bLocalTest; CurrentSession.Nonce=FGuid::NewGuid();
    OnSessionChanged.Broadcast();
}
void ULiveInteractionSubsystem::SetAnchorProfile(const FLiveSession& Session,const FString& Nickname,const FString& AvatarUrl)
{
    if(!IsEventFromCurrentSession(Session) || Session.AnchorUserId.IsEmpty())return;
    AnchorNickname=Nickname.Left(64);
    if(!AvatarUrl.IsEmpty())RequestAvatar(Session.AnchorUserId,AvatarUrl);
}
void ULiveInteractionSubsystem::EnableLocalTest()
{
    check(IsInGameThread());
    CloseSocket();
    auto PreviousProvider=MoveTemp(Provider);
    InvalidateSession(false);
    if(PreviousProvider) PreviousProvider->Stop();
    bLocalTestMode=true; SelectedPlatform=TEXT("local");
    ChangeSession(TEXT("local"),TEXT("simulator"),TEXT("local-test"),true);
    ConnectionStatus=TEXT("LOCAL_TEST");
}
bool ULiveInteractionSubsystem::StartPlatform(const FString& PlatformId)
{
    check(IsInGameThread());
    CloseSocket();
    auto PreviousProvider=MoveTemp(Provider);
    bLocalTestMode=false; SelectedPlatform=PlatformId; InvalidateSession();
    if(PreviousProvider) PreviousProvider->Stop();
    Provider=FLiveInteractionProviderRegistry::Create(PlatformId);
    if(!Provider || Provider->GetPlatformId()!=PlatformId) { Provider.Reset(); ConnectionStatus=TEXT("SDK_UNAVAILABLE"); return false; }
    ConnectionStatus=TEXT("SDK_CONNECTING");
    if(!Provider->Start(*this)) {
        auto FailedProvider=MoveTemp(Provider);
        InvalidateSession(); FailedProvider->Stop();
        ConnectionStatus=TEXT("SDK_START_FAILED"); return false;
    }
    return true;
}
FLiveSession ULiveInteractionSubsystem::BeginProviderSession(ILiveInteractionProvider& Source,const FString& AppId,const FString& RoomId,const FString& AnchorUserId)
{
    check(IsInGameThread());
    if(bLocalTestMode || Provider.Get()!=&Source || AppId.TrimStartAndEnd().IsEmpty() || AppId.Len()>128 || RoomId.TrimStartAndEnd().IsEmpty() || RoomId.Len()>128 || AnchorUserId.Len()>256) return FLiveSession();
    ChangeSession(SelectedPlatform,AppId.TrimStartAndEnd(),RoomId.TrimStartAndEnd(),false,AnchorUserId.TrimStartAndEnd());
    ConnectionStatus=TEXT("SDK_CONNECTED"); return CurrentSession;
}
void ULiveInteractionSubsystem::EndProviderSession(ILiveInteractionProvider& Source,const FString& Status)
{
    check(IsInGameThread());
    if(Provider.Get()!=&Source || bLocalTestMode) return;
    InvalidateSession(); ConnectionStatus=Status;
}
bool ULiveInteractionSubsystem::IsEventFromCurrentSession(const FLiveSession& Session) const
{
    return CurrentSession.Nonce.IsValid() && Session.Nonce==CurrentSession.Nonce && Session.Generation==SessionGeneration &&
        Session.PlatformId==CurrentSession.PlatformId && Session.AppId==CurrentSession.AppId && Session.RoomId==CurrentSession.RoomId &&
        Session.AnchorUserId==CurrentSession.AnchorUserId && Session.bLocalTest==bLocalTestMode && Session.bLocalTest==CurrentSession.bLocalTest && (bLocalTestMode || Provider.IsValid());
}
FString ULiveInteractionSubsystem::GetScopedUserId(const FString& UserId) const
{
    if(bLocalTestMode) return TEXT("local:")+UserId;
    if(!CurrentSession.Nonce.IsValid()) return FString();
    const auto Part=[](const FString& Value) { return FString::FromInt(Value.Len())+TEXT(":")+Value; };
    return TEXT("live:")+Part(CurrentSession.PlatformId)+Part(CurrentSession.AppId)+Part(UserId);
}
void ULiveInteractionSubsystem::Deinitialize()
{
    CloseSocket();
    auto PreviousProvider=MoveTemp(Provider);
    bLocalTestMode=false; InvalidateSession();
    if(PreviousProvider) PreviousProvider->Stop();
    SeenIds.Empty(); IdOrder.Empty();
    Super::Deinitialize();
}
