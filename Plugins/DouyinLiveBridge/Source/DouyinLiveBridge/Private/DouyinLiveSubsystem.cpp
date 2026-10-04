#include "DouyinLiveSubsystem.h"
#include "DouyinInteractionRules.h"
#include "DouyinGiftRules.h"
#include "Async/Async.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "IWebSocket.h"
#include "WebSocketsModule.h"
#include "AvatarThumbnail.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Engine/Texture2D.h"

bool UDouyinLiveSubsystem::SimulateComment(const FString& UserId,const FString& Nickname,const FString& Content)
{
    if(bRelayMode) return false;
    FDouyinComment Event; Event.MessageId=FGuid::NewGuid().ToString(); Event.UserId=UserId;
    Event.Nickname=Nickname; Event.Content=Content;
    return DeliverComment(Event);
}
bool UDouyinLiveSubsystem::DeliverComment(const FDouyinComment& Comment)
{
    check(IsInGameThread());
    FDouyinComment Event=Comment;
    Event.UserId.TrimStartAndEndInline(); Event.MessageId.TrimStartAndEndInline(); Event.Content.TrimStartAndEndInline();
    if(Event.UserId.IsEmpty() || Event.MessageId.IsEmpty() || Event.Content.IsEmpty() || Event.UserId.Len()>256 || Event.MessageId.Len()>128 || Event.Content.Len()>128 || Event.Nickname.Len()>64 || Event.AvatarUrl.Len()>4096) return false;
    if(!AcceptEventId(Event.MessageId)) return false;
    OnComment.Broadcast(Event); return true;
}
bool UDouyinLiveSubsystem::SimulateLike(const FString& UserId,const FString& Nickname,int32 Count)
{
    if(bRelayMode) return false;
    FDouyinLike Event; Event.MessageId=FGuid::NewGuid().ToString(); Event.UserId=UserId;
    Event.Nickname=Nickname; Event.Count=Count;
    return DeliverLike(Event);
}
bool UDouyinLiveSubsystem::SimulateShare(const FString& UserId,const FString& Nickname)
{
    if(bRelayMode) return false;
    FDouyinShare Event; Event.MessageId=FGuid::NewGuid().ToString(); Event.UserId=UserId; Event.Nickname=Nickname;
    return DeliverShare(Event);
}
bool UDouyinLiveSubsystem::DeliverLike(const FDouyinLike& Like)
{
    check(IsInGameThread());
    FDouyinLike Event=Like;
    Event.UserId.TrimStartAndEndInline(); Event.MessageId.TrimStartAndEndInline();
    if(Event.UserId.IsEmpty() || Event.MessageId.IsEmpty() || Event.UserId.Len()>256 || Event.MessageId.Len()>128 || Event.Nickname.Len()>64 || !douyin::IsValidLikeDelta(Event.Count)) return false;
    if(!AcceptEventId(Event.MessageId)) return false;
    OnLike.Broadcast(Event); return true;
}
bool UDouyinLiveSubsystem::DeliverShare(const FDouyinShare& Share)
{
    check(IsInGameThread());
    FDouyinShare Event=Share;
    Event.UserId.TrimStartAndEndInline(); Event.MessageId.TrimStartAndEndInline();
    if(Event.UserId.IsEmpty() || Event.MessageId.IsEmpty() || Event.UserId.Len()>256 || Event.MessageId.Len()>128 || Event.Nickname.Len()>64) return false;
    if(!AcceptEventId(Event.MessageId)) return false;
    OnShare.Broadcast(Event); return true;
}
bool UDouyinLiveSubsystem::SimulateGift(const FString& UserId,const FString& Nickname,const FString& GiftName,int32 Count)
{
    if(bRelayMode) return false;
    FDouyinGift Event; Event.MessageId=FGuid::NewGuid().ToString(); Event.UserId=UserId;
    Event.Nickname=Nickname; Event.GiftName=GiftName; Event.Count=Count;
    return DeliverGift(Event);
}
bool UDouyinLiveSubsystem::DeliverGift(const FDouyinGift& Gift)
{
    check(IsInGameThread());
    FDouyinGift Event=Gift; Event.UserId.TrimStartAndEndInline(); Event.MessageId.TrimStartAndEndInline();
    if(Event.UserId.IsEmpty() || Event.MessageId.IsEmpty() || Event.UserId.Len()>256 || Event.MessageId.Len()>128 ||
       Event.Nickname.Len()>64 || !douyin::IsSupportedGift(Event.GiftName) || !douyin::IsValidGiftCount(Event.Count)) return false;
    if(!AcceptEventId(Event.MessageId)) return false;
    OnGift.Broadcast(Event); return true;
}
bool UDouyinLiveSubsystem::AcceptEventId(const FString& MessageId)
{
    // Include room length so IDs containing ':' cannot alias another room.
    const FString Key=FString::FromInt(ExpectedRoom.Len())+TEXT(":")+ExpectedRoom+MessageId;
    if(SeenIds.Contains(Key)) return false;
    if(IdOrder.Num()>=4096) { SeenIds.Remove(IdOrder[0]); IdOrder.RemoveAt(0); }
    SeenIds.Add(Key); IdOrder.Add(Key);
    return true;
}
bool UDouyinLiveSubsystem::IsValidRelayEndpoint(const FString& Url,const FString& RoomId)
{
    return !RoomId.TrimStartAndEnd().IsEmpty() && RoomId.Len()<=128 && Url.Len()<=4096 &&
       (Url.StartsWith(TEXT("wss://")) || Url.StartsWith(TEXT("ws://127.0.0.1:")) || Url.StartsWith(TEXT("ws://localhost:")));
}
bool UDouyinLiveSubsystem::ConnectRelay(const FString& Url,const FString& RoomId)
{
    if(!IsValidRelayEndpoint(Url,RoomId)) return false;
    DisconnectRelay(); bRelayMode=true; ExpectedRoom=RoomId; ConnectionStatus=TEXT("ADAPTER CONNECTING");
    Socket=FWebSocketsModule::Get().CreateWebSocket(Url);
    const auto Weak=TWeakObjectPtr<UDouyinLiveSubsystem>(this); const int32 Generation=SessionGeneration;
    Socket->OnConnected().AddLambda([Weak,Generation] {
        AsyncTask(ENamedThreads::GameThread,[Weak,Generation] { if(Weak.IsValid() && Weak->SessionGeneration==Generation) Weak->ConnectionStatus=TEXT("ADAPTER CONNECTED"); });
    });
    Socket->OnConnectionError().AddLambda([Weak,Generation](const FString&) {
        AsyncTask(ENamedThreads::GameThread,[Weak,Generation] { if(Weak.IsValid() && Weak->SessionGeneration==Generation) Weak->ConnectionStatus=TEXT("ADAPTER ERROR / RECONNECT"); });
    });
    Socket->OnClosed().AddLambda([Weak,Generation](int32,const FString&,bool) {
        AsyncTask(ENamedThreads::GameThread,[Weak,Generation] { if(Weak.IsValid() && Weak->SessionGeneration==Generation) Weak->ConnectionStatus=TEXT("ADAPTER DISCONNECTED"); });
    });
    Socket->OnMessage().AddLambda([Weak,Generation](const FString& Json) {
        if(Json.Len()>65536) return;
        AsyncTask(ENamedThreads::GameThread,[Weak,Generation,Json] { if(Weak.IsValid() && Weak->SessionGeneration==Generation) Weak->Receive(Json); });
    });
    Socket->Connect(); return true;
}
void UDouyinLiveSubsystem::DisconnectRelay()
{
    ++SessionGeneration;
    AvatarRequests.Reset();
    for(auto& Request:Requests) { Request->OnProcessRequestComplete().Unbind(); Request->OnRequestProgress64().Unbind(); Request->CancelRequest(); }
    Requests.Empty();
    if(Socket) { Socket->OnConnected().Clear(); Socket->OnConnectionError().Clear(); Socket->OnClosed().Clear(); Socket->OnMessage().Clear(); Socket->Close(); Socket.Reset(); }
    bRelayMode=false; ExpectedRoom.Reset(); ConnectionStatus=TEXT("LOCAL SIMULATION");
}
void UDouyinLiveSubsystem::Receive(const FString& Json)
{
    if(!bRelayMode) return;
    FString Room,Error; TArray<FDouyinComment> Comments; TArray<FDouyinLike> Likes; TArray<FDouyinShare> Shares; TArray<FDouyinGift> Gifts;
    if(!FDouyinEventDecoder::DecodeEvents(Json,Room,Comments,Likes,Shares,Gifts,Error) || Room!=ExpectedRoom) return;
    for(const auto& Comment:Comments) DeliverComment(Comment);
    for(const auto& Like:Likes) DeliverLike(Like);
    for(const auto& Share:Shares) DeliverShare(Share);
    for(const auto& Gift:Gifts) DeliverGift(Gift);
}
void UDouyinLiveSubsystem::RequestAvatar(const FString& UserId,const FString& Url)
{
    if(UserId.IsEmpty() || !Url.StartsWith(TEXT("https://")) || Url.Len()>4096) return;
    if(AvatarRequests.IsPending(UserId,Url)) return;
    const uint64 Ticket=AvatarRequests.Begin(UserId,Url);
    if(auto* Cached=AvatarCache.Find(Url)) { AvatarRequests.Complete(UserId,Ticket); OnAvatarReady.Broadcast(UserId,*Cached); return; }
    if(Requests.Num()>=100) { AvatarRequests.Complete(UserId,Ticket); return; }
    auto Request=FHttpModule::Get().CreateRequest(); Request->SetURL(Url); Request->SetVerb(TEXT("GET")); Request->SetTimeout(10);
    const auto Weak=TWeakObjectPtr<UDouyinLiveSubsystem>(this);
    Request->OnRequestProgress64().BindLambda([](FHttpRequestPtr InRequest,uint64,uint64 Received) { if(Received>2*1024*1024) InRequest->CancelRequest(); });
    Request->OnProcessRequestComplete().BindLambda([Weak,UserId,Url,Ticket](FHttpRequestPtr InRequest,FHttpResponsePtr Response,bool Success) {
        if(!Weak.IsValid()) return;
        Weak->Requests.Remove(InRequest);
        if(!Weak->AvatarRequests.Complete(UserId,Ticket)) return;
        if(!Success || !Response || Response->GetResponseCode()!=200 || Response->GetContent().Num()>2*1024*1024) return;
        const auto& Bytes=Response->GetContent();
        auto& Images=FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
        auto Decoder=Images.CreateImageWrapper(Images.DetectImageFormat(Bytes.GetData(),Bytes.Num()));
        if(!Decoder || !Decoder->SetCompressed(Bytes.GetData(),Bytes.Num()) || Decoder->GetWidth()<=0 || Decoder->GetHeight()<=0 || Decoder->GetWidth()>1024 || Decoder->GetHeight()>1024) return;
        TArray64<uint8> Pixels;
        if(!Decoder->GetRaw(ERGBFormat::BGRA,8,Pixels)) return;
        TArray64<uint8> Thumbnail;
        Thumbnail.SetNumUninitialized(static_cast<int64>(douyin::AvatarBytes));
        if(!douyin::MakeAvatarThumbnailBGRA(Pixels.GetData(),static_cast<std::size_t>(Pixels.Num()),
            static_cast<int>(Decoder->GetWidth()),static_cast<int>(Decoder->GetHeight()),Thumbnail.GetData(),static_cast<std::size_t>(Thumbnail.Num()))) return;
        // Never allocate a full-resolution GPU texture: 5000 BGRA thumbnails
        // use 78.125 MiB of pixels, irrespective of the source image dimensions.
        UTexture2D* Texture=UTexture2D::CreateTransient(douyin::AvatarSide,douyin::AvatarSide,PF_B8G8R8A8,NAME_None,
            TConstArrayView64<uint8>(Thumbnail.GetData(),Thumbnail.Num()));
        if(Texture) { if(Weak->AvatarCache.Num()<128) Weak->AvatarCache.Add(Url,Texture); Weak->OnAvatarReady.Broadcast(UserId,Texture); }
    });
    Requests.Add(Request);
    if(!Request->ProcessRequest()) { Requests.Remove(Request); AvatarRequests.Complete(UserId,Ticket); }
}
void UDouyinLiveSubsystem::Deinitialize()
{
    DisconnectRelay();
    AvatarCache.Empty(); SeenIds.Empty(); IdOrder.Empty();
    Super::Deinitialize();
}
