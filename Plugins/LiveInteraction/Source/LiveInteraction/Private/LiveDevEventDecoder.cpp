#include "LiveInteractionSubsystem.h"
#include "LiveInteractionRules.h"
#include "LiveGiftRules.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

namespace {
bool ReadString(const TSharedPtr<FJsonObject>& Object,const TCHAR* Key,FString& Out,bool Required=true)
{
    const auto Value=Object->TryGetField(Key);
    if(!Value.IsValid()) return !Required;
    return Value->Type==EJson::String && Value->TryGetString(Out);
}
template<typename EventType>
bool ReadIdentity(const TSharedPtr<FJsonObject>& Object,EventType& Event)
{
    if(!ReadString(Object,TEXT("msg_id"),Event.MessageId) || !ReadString(Object,TEXT("sec_openid"),Event.UserId) ||
       !ReadString(Object,TEXT("nickname"),Event.Nickname,false)) return false;
    Event.UserId.TrimStartAndEndInline(); Event.MessageId.TrimStartAndEndInline();
    return !Event.UserId.IsEmpty() && !Event.MessageId.IsEmpty() && Event.UserId.Len()<=256 && Event.MessageId.Len()<=128 && Event.Nickname.Len()<=64;
}
bool DecodeEnvelope(const FString& Json,FString& Room,TArray<FLiveComment>& Comments,
    TArray<FLiveLike>& Likes,TArray<FLiveShare>& Shares,TArray<FLiveGift>& Gifts,FString& Error,bool CommentsOnly)
{
    Comments.Reset(); Likes.Reset(); Shares.Reset(); Gifts.Reset(); Room.Reset(); Error.Reset();
    const auto Fail=[&](const TCHAR* Message) {
        Comments.Reset(); Likes.Reset(); Shares.Reset(); Gifts.Reset(); Room.Reset(); Error=Message; return false;
    };
    if(Json.Len()>65536) return Fail(TEXT("Envelope exceeds 64K characters"));
    TSharedPtr<FJsonObject> Root;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root) || !Root) return Fail(TEXT("Invalid JSON"));
    FString Type;
    if(!ReadString(Root,TEXT("room_id"),Room) || Room.TrimStartAndEnd().IsEmpty() || Room.Len()>128 ||
       !ReadString(Root,TEXT("type"),Type)) return Fail(TEXT("Expected room_id and event type"));
    if(Type!=TEXT("live_comment") && (CommentsOnly || (Type!=TEXT("live_like") && Type!=TEXT("live_share") && Type!=TEXT("live_gift"))))
        return Fail(TEXT("Unsupported relay event type"));
    const TArray<TSharedPtr<FJsonValue>>* Payload=nullptr;
    if(!Root->TryGetArrayField(TEXT("payload"),Payload) || Payload->Num()>100) return Fail(TEXT("Invalid event batch"));
    for(const auto& Value:*Payload) {
        if(!Value || Value->Type!=EJson::Object) return Fail(TEXT("Invalid event record"));
        const auto Object=Value->AsObject();
        if(!Object) return Fail(TEXT("Invalid event object"));
        if(Type==TEXT("live_comment")) {
            FLiveComment Event;
            if(!ReadIdentity(Object,Event) || !ReadString(Object,TEXT("content"),Event.Content) ||
               !ReadString(Object,TEXT("avatar_url"),Event.AvatarUrl,false)) return Fail(TEXT("Invalid comment fields"));
            Event.Content.TrimStartAndEndInline();
            if(Event.Content.IsEmpty() || Event.Content.Len()>128 || Event.AvatarUrl.Len()>4096) return Fail(TEXT("Invalid comment field length"));
            Comments.Add(MoveTemp(Event));
        } else if(Type==TEXT("live_like")) {
            FLiveLike Event;
            const auto Count=Object->TryGetField(TEXT("count")); double Delta=0;
            if(!ReadIdentity(Object,Event) || !Count.IsValid() || Count->Type!=EJson::Number ||
               !Count->TryGetNumber(Delta) || !liveinteraction::IsValidLikeDelta(Delta)) return Fail(TEXT("Like count must be a delta integer from 1 to 100"));
            Event.Count=static_cast<int32>(Delta); Likes.Add(MoveTemp(Event));
        } else if(Type==TEXT("live_gift")) {
            FLiveGift Event;
            const auto Count=Object->TryGetField(TEXT("count")); double Delta=0;
            if(!ReadIdentity(Object,Event) || !ReadString(Object,TEXT("gift_name"),Event.GiftName) ||
               !liveinteraction::IsSupportedGift(Event.GiftName) || !Count.IsValid() || Count->Type!=EJson::Number ||
               !Count->TryGetNumber(Delta) || !liveinteraction::IsValidGiftCount(Delta)) return Fail(TEXT("Invalid gift name or delta count"));
            Event.Count=static_cast<int32>(Delta); Gifts.Add(MoveTemp(Event));
        } else {
            FLiveShare Event;
            if(!ReadIdentity(Object,Event)) return Fail(TEXT("Invalid share fields"));
            Shares.Add(MoveTemp(Event));
        }
    }
    return true;
}
}

bool FLiveDevEventDecoder::Decode(const FString& Json,FString& Room,TArray<FLiveComment>& Comments,FString& Error)
{
    TArray<FLiveLike> Likes; TArray<FLiveShare> Shares; TArray<FLiveGift> Gifts;
    return DecodeEnvelope(Json,Room,Comments,Likes,Shares,Gifts,Error,true);
}
bool FLiveDevEventDecoder::DecodeEvents(const FString& Json,FString& Room,TArray<FLiveComment>& Comments,
    TArray<FLiveLike>& Likes,TArray<FLiveShare>& Shares,FString& Error)
{
    TArray<FLiveGift> Gifts;
    if(!DecodeEnvelope(Json,Room,Comments,Likes,Shares,Gifts,Error,false)) return false;
    if(!Gifts.IsEmpty()) { Room.Reset(); Error=TEXT("Use the gift-aware decoder for gift events"); return false; }
    return true;
}
bool FLiveDevEventDecoder::DecodeEvents(const FString& Json,FString& Room,TArray<FLiveComment>& Comments,
    TArray<FLiveLike>& Likes,TArray<FLiveShare>& Shares,TArray<FLiveGift>& Gifts,FString& Error)
{
    return DecodeEnvelope(Json,Room,Comments,Likes,Shares,Gifts,Error,false);
}
