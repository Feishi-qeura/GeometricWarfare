#include "LiveInteractionSubsystem.h"
#include "LiveInteractionTestAdapter.h"
#include "LiveInteractionTestConsumer.h"
#include "Misc/AutomationTest.h"
#include "Engine/GameInstance.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace {
FString GiftEnvelope(const FString& Records) {
    return TEXT("{\"room_id\":\"gift-room\",\"type\":\"live_gift\",\"payload\":[")+Records+TEXT("]}");
}
FString GiftRecord(const FString& Id,const FString& Name,const FString& Count=TEXT("1")) {
    return FString::Printf(TEXT("{\"msg_id\":\"%s\",\"sec_openid\":\" viewer-1 \",\"nickname\":\"阿白\",\"gift_name\":\"%s\",\"count\":%s}"),*Id,*Name,*Count);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiveGiftDecodeTest,"GeometricWarfare.Bridge.GiftDecode",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLiveGiftDecodeTest::RunTest(const FString&)
{
    FString Room,Error;TArray<FLiveComment> Comments;TArray<FLiveLike> Likes;TArray<FLiveShare> Shares;TArray<FLiveGift> Gifts;
    const auto Decode=[&](const FString& Json){return FLiveDevEventDecoder::DecodeEvents(Json,Room,Comments,Likes,Shares,Gifts,Error);};
    const auto Empty=[&](){return Room.IsEmpty()&&Comments.IsEmpty()&&Likes.IsEmpty()&&Shares.IsEmpty()&&Gifts.IsEmpty();};
    const TCHAR* Names[]={TEXT("仙女棒"),TEXT("能力药丸"),TEXT("魔法镜"),TEXT("甜甜圈"),TEXT("能量电池")};
    FString Records;
    for(int32 I=0;I<5;++I){if(I)Records+=TEXT(",");Records+=GiftRecord(FString::Printf(TEXT(" gift-%d "),I),Names[I],FString::FromInt(I+1));}
    Comments.AddDefaulted();Likes.AddDefaulted();Shares.AddDefaulted();Gifts.AddDefaulted();Error=TEXT("old-error");
    TestTrue(TEXT("All five normalized gift names decode in one batch"),Decode(GiftEnvelope(Records)));
    TestEqual(TEXT("Gift room preserved"),Room,FString(TEXT("gift-room")));
    TestEqual(TEXT("Exactly five gifts decoded"),Gifts.Num(),5);
    TestTrue(TEXT("Gift decoding clears old output and unrelated event arrays"),Comments.IsEmpty()&&Likes.IsEmpty()&&Shares.IsEmpty()&&Error.IsEmpty());
    if(Gifts.Num()==5)for(int32 I=0;I<5;++I){
        TestEqual(TEXT("Gift event ID is trimmed"),Gifts[I].MessageId,FString::Printf(TEXT("gift-%d"),I));
        TestEqual(TEXT("Gift identity is the normalized stable ID"),Gifts[I].UserId,FString(TEXT("viewer-1")));
        TestEqual(TEXT("Gift nickname preserves Chinese text"),Gifts[I].Nickname,FString(TEXT("阿白")));
        TestEqual(TEXT("Gift name remains the exact normalized adapter name"),Gifts[I].GiftName,FString(Names[I]));
        TestEqual(TEXT("Gift count preserves the new-unit delta"),Gifts[I].Count,int64(I+1));
    }
    TestTrue(TEXT("One hundred new gift units are accepted"),Decode(GiftEnvelope(GiftRecord(TEXT("max"),TEXT("魔法镜"),TEXT("100")))));
    if(Gifts.Num()==1)TestEqual(TEXT("Maximum count is not truncated"),Gifts[0].Count,int64(100));
    for(const TCHAR* Invalid:{TEXT("0"),TEXT("-1"),TEXT("101"),TEXT("1.5"),TEXT("\"3\""),TEXT("null"),TEXT("true"),TEXT("[]"),TEXT("{}"),TEXT("1e100")}){
        TestFalse(FString::Printf(TEXT("Invalid gift delta %s rejected"),Invalid),Decode(GiftEnvelope(GiftRecord(TEXT("bad"),TEXT("魔法镜"),Invalid))));
        TestTrue(TEXT("Invalid count clears every output and supplies a diagnostic"),Empty()&&!Error.IsEmpty());
    }
    const FString MissingCount=TEXT("{\"msg_id\":\"bad\",\"sec_openid\":\"u\",\"gift_name\":\"仙女棒\"}");
    TestFalse(TEXT("Gift without delta count is rejected"),Decode(GiftEnvelope(MissingCount)));
    TestFalse(TEXT("Gift without normalized name is rejected"),Decode(GiftEnvelope(TEXT("{\"msg_id\":\"bad\",\"sec_openid\":\"u\",\"count\":1}"))));
    TestFalse(TEXT("Gift without stable identity is rejected"),Decode(GiftEnvelope(TEXT("{\"msg_id\":\"bad\",\"sec_openid\":\" \",\"gift_name\":\"魔法镜\",\"count\":1}"))));
    TestFalse(TEXT("Gift without message ID is rejected"),Decode(GiftEnvelope(TEXT("{\"sec_openid\":\"u\",\"gift_name\":\"魔法镜\",\"count\":1}"))));
    for(const TCHAR* InvalidName:{TEXT("玫瑰"),TEXT("魔法镜+1"),TEXT(" 魔法镜 ")})
        TestFalse(TEXT("Unsupported or unnormalized gift names cannot select a reward"),Decode(GiftEnvelope(GiftRecord(TEXT("bad-name"),InvalidName))));
    TestFalse(TEXT("One bad trailing gift rejects the whole batch"),Decode(GiftEnvelope(GiftRecord(TEXT("valid-first"),TEXT("能量电池"))+TEXT(",")+MissingCount)));
    TestTrue(TEXT("The earlier valid gift cannot survive an invalid batch"),Empty());
    TestFalse(TEXT("A nonobject trailing record rejects the whole batch"),Decode(GiftEnvelope(GiftRecord(TEXT("valid-first"),TEXT("甜甜圈"))+TEXT(",false"))));
    TestTrue(TEXT("Malformed trailing record also clears the accepted prefix"),Empty());
    Records.Reset();for(int32 I=0;I<100;++I){if(I)Records+=TEXT(",");Records+=GiftRecord(FString::FromInt(I),TEXT("仙女棒"));}
    TestTrue(TEXT("Bounded batch of one hundred gifts accepted"),Decode(GiftEnvelope(Records)));
    TestEqual(TEXT("All hundred gift records retained"),Gifts.Num(),100);
    TestFalse(TEXT("A batch above one hundred gifts is rejected atomically"),Decode(GiftEnvelope(Records+TEXT(",")+GiftRecord(TEXT("100"),TEXT("仙女棒")))));
    TestTrue(TEXT("Oversized batch exposes no partial rewards"),Empty());
    const FString Valid=GiftEnvelope(GiftRecord(TEXT("legacy"),TEXT("魔法镜")));
    TestFalse(TEXT("Comment-only decoder cannot silently accept a gift"),FLiveDevEventDecoder::Decode(Valid,Room,Comments,Error));
    TestFalse(TEXT("Legacy interaction decoder requires the gift-aware overload"),FLiveDevEventDecoder::DecodeEvents(Valid,Room,Comments,Likes,Shares,Error));
    TestTrue(TEXT("Legacy rejection does not expose stale unrelated events"),Room.IsEmpty()&&Comments.IsEmpty()&&Likes.IsEmpty()&&Shares.IsEmpty());
    TestTrue(TEXT("Gift name in a viewer comment remains only a comment"),Decode(TEXT("{\"room_id\":\"gift-room\",\"type\":\"live_comment\",\"payload\":[{\"msg_id\":\"comment\",\"sec_openid\":\"u\",\"content\":\"魔法镜\"}]}")));
    TestTrue(TEXT("Viewer text never creates a paid gift event"),Comments.Num()==1&&Gifts.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiveGiftDeliveryTest,"GeometricWarfare.Bridge.GiftDelivery",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLiveGiftDeliveryTest::RunTest(const FString&)
{
    auto* Bridge=NewObject<ULiveInteractionSubsystem>(NewObject<UGameInstance>());
    FLiveInteractionTestAdapter::EnableLocalTest(*Bridge);
    auto* Consumer=NewObject<ULiveInteractionTestConsumer>(Bridge);Consumer->Bind(*Bridge);
    FLiveGift Gift;Gift.MessageId=TEXT(" gift-id ");Gift.UserId=TEXT(" viewer-1 ");Gift.Nickname=TEXT("阿白");Gift.GiftName=TEXT("魔法镜");Gift.Count=3;
    TestTrue(TEXT("First validated gift delivery accepted"),Bridge->DeliverGift(FLiveInteractionTestAdapter::Stamp(*Bridge,Gift)));
    Gift.MessageId=TEXT("gift-id");
    TestFalse(TEXT("Whitespace cannot bypass gift event deduplication"),Bridge->DeliverGift(FLiveInteractionTestAdapter::Stamp(*Bridge,Gift)));
    Gift.GiftName=TEXT("甜甜圈");Gift.Count=4;Gift.UserId=TEXT("viewer-2");
    TestFalse(TEXT("Changing gift name count or user cannot reuse the event ID"),Bridge->DeliverGift(FLiveInteractionTestAdapter::Stamp(*Bridge,Gift)));
    FLiveComment Comment;Comment.MessageId=Gift.MessageId;Comment.UserId=Gift.UserId;Comment.Content=TEXT("加入");
    FLiveLike Like;Like.MessageId=Gift.MessageId;Like.UserId=Gift.UserId;Like.Count=1;
    FLiveShare Share;Share.MessageId=Gift.MessageId;Share.UserId=Gift.UserId;
    TestFalse(TEXT("A gift ID cannot be replayed as a comment"),Bridge->DeliverComment(FLiveInteractionTestAdapter::Stamp(*Bridge,Comment)));
    TestFalse(TEXT("A gift ID cannot be replayed as a like"),Bridge->DeliverLike(FLiveInteractionTestAdapter::Stamp(*Bridge,Like)));
    TestFalse(TEXT("A gift ID cannot be replayed as a share"),Bridge->DeliverShare(FLiveInteractionTestAdapter::Stamp(*Bridge,Share)));
    Comment.MessageId=TEXT("comment-id");TestTrue(TEXT("A fresh comment ID is accepted"),Bridge->DeliverComment(FLiveInteractionTestAdapter::Stamp(*Bridge,Comment)));Gift.MessageId=Comment.MessageId;
    TestFalse(TEXT("Comment and gift deliveries share one deduplication window"),Bridge->DeliverGift(FLiveInteractionTestAdapter::Stamp(*Bridge,Gift)));
    Like.MessageId=TEXT("like-id");TestTrue(TEXT("A fresh like ID is accepted"),Bridge->DeliverLike(FLiveInteractionTestAdapter::Stamp(*Bridge,Like)));Gift.MessageId=Like.MessageId;
    TestFalse(TEXT("Like and gift deliveries share one deduplication window"),Bridge->DeliverGift(FLiveInteractionTestAdapter::Stamp(*Bridge,Gift)));
    Share.MessageId=TEXT("share-id");TestTrue(TEXT("A fresh share ID is accepted"),Bridge->DeliverShare(FLiveInteractionTestAdapter::Stamp(*Bridge,Share)));Gift.MessageId=Share.MessageId;
    TestFalse(TEXT("Share and gift deliveries share one deduplication window"),Bridge->DeliverGift(FLiveInteractionTestAdapter::Stamp(*Bridge,Gift)));
    for(int32 Invalid:{0,-1,101,MAX_int32}){
        Gift.MessageId=FString::Printf(TEXT("invalid-count-%d"),Invalid);Gift.Count=Invalid;
        TestFalse(TEXT("Direct delivery rejects an invalid count"),Bridge->DeliverGift(FLiveInteractionTestAdapter::Stamp(*Bridge,Gift)));Gift.Count=1;
        TestTrue(TEXT("Rejected count does not consume its event ID"),Bridge->DeliverGift(FLiveInteractionTestAdapter::Stamp(*Bridge,Gift)));
    }
    Gift.MessageId=TEXT("invalid-name");Gift.GiftName=TEXT("玫瑰");
    TestFalse(TEXT("Direct delivery rejects unknown gifts"),Bridge->DeliverGift(FLiveInteractionTestAdapter::Stamp(*Bridge,Gift)));Gift.GiftName=TEXT("能量电池");
    TestTrue(TEXT("Rejected name does not consume its event ID"),Bridge->DeliverGift(FLiveInteractionTestAdapter::Stamp(*Bridge,Gift)));
    Gift.MessageId=TEXT("invalid-user");Gift.UserId=TEXT(" ");TestFalse(TEXT("Blank gift identity rejected"),Bridge->DeliverGift(FLiveInteractionTestAdapter::Stamp(*Bridge,Gift)));
    Gift.UserId=TEXT("viewer-1");TestTrue(TEXT("Corrected identity can reuse an unconsumed ID"),Bridge->DeliverGift(FLiveInteractionTestAdapter::Stamp(*Bridge,Gift)));
    Gift.MessageId=TEXT(" ");TestFalse(TEXT("Blank gift event ID rejected"),Bridge->DeliverGift(FLiveInteractionTestAdapter::Stamp(*Bridge,Gift)));
    Gift.MessageId=FString::ChrN(129,TEXT('x'));TestFalse(TEXT("Oversized gift event ID rejected"),Bridge->DeliverGift(FLiveInteractionTestAdapter::Stamp(*Bridge,Gift)));
    Gift.MessageId=TEXT("long-user");Gift.UserId=FString::ChrN(257,TEXT('u'));TestFalse(TEXT("Oversized gift identity rejected"),Bridge->DeliverGift(FLiveInteractionTestAdapter::Stamp(*Bridge,Gift)));
    Gift.UserId=TEXT("viewer-1");Gift.Nickname=FString::ChrN(65,TEXT('n'));TestFalse(TEXT("Oversized gift nickname rejected"),Bridge->DeliverGift(FLiveInteractionTestAdapter::Stamp(*Bridge,Gift)));
    Gift.Nickname=TEXT("阿白");TestTrue(TEXT("Corrected length fields do not lose the event ID"),Bridge->DeliverGift(FLiveInteractionTestAdapter::Stamp(*Bridge,Gift)));
    const TCHAR* Names[]={TEXT("仙女棒"),TEXT("能力药丸"),TEXT("魔法镜"),TEXT("甜甜圈"),TEXT("能量电池")};
    for(const TCHAR* Name:Names)TestTrue(TEXT("Local simulation uses validated delivery for every gift"),Bridge->SimulateGift(TEXT("local-viewer"),TEXT("本地观众"),Name,1));
    TestFalse(TEXT("Local gift simulation requires identity"),Bridge->SimulateGift(TEXT(""),TEXT("本地观众"),TEXT("魔法镜"),1));
    TestFalse(TEXT("Local gift simulation rejects oversized delta"),Bridge->SimulateGift(TEXT("local-viewer"),TEXT("本地观众"),TEXT("魔法镜"),101));
    TestFalse(TEXT("Local gift simulation rejects unknown gifts"),Bridge->SimulateGift(TEXT("local-viewer"),TEXT("本地观众"),TEXT("玫瑰"),1));
    // Set relay state directly through test friendship: no socket or network is opened.
    FLiveInteractionTestAdapter::BeginDevRelay(*Bridge,TEXT("room-a"));
    for(const TCHAR* Name:Names)TestFalse(TEXT("Connected relay mode disables every mock gift"),Bridge->SimulateGift(TEXT("viewer-1"),TEXT("阿白"),Name,1));
    TestFalse(TEXT("Relay mode also disables mock comments"),Bridge->SimulateComment(TEXT("viewer-1"),TEXT("阿白"),TEXT("加入")));
    TestFalse(TEXT("Relay mode also disables mock likes"),Bridge->SimulateLike(TEXT("viewer-1"),TEXT("阿白"),1));
    TestFalse(TEXT("Relay mode also disables mock shares"),Bridge->SimulateShare(TEXT("viewer-1"),TEXT("阿白")));
    FLiveInteractionTestAdapter::BeginDevRelay(*Bridge,TEXT("gift-room"));
    FLiveInteractionTestAdapter::Receive(*Bridge,GiftEnvelope(GiftRecord(TEXT("atomic-first"),TEXT("魔法镜"))+TEXT(",")+GiftRecord(TEXT("atomic-bad"),TEXT("仙女棒"),TEXT("0"))));
    Gift.MessageId=TEXT("atomic-first");
    TestTrue(TEXT("Invalid relay batch never delivers or consumes its valid prefix"),Bridge->DeliverGift(FLiveInteractionTestAdapter::Stamp(*Bridge,Gift)));
    FString WrongRoom=GiftEnvelope(GiftRecord(TEXT("wrong-room"),TEXT("魔法镜")));WrongRoom.ReplaceInline(TEXT("gift-room"),TEXT("other-room"));
    FLiveInteractionTestAdapter::Receive(*Bridge,WrongRoom);Gift.MessageId=TEXT("wrong-room");
    TestTrue(TEXT("Relay ignores another room before consuming gift IDs"),Bridge->DeliverGift(FLiveInteractionTestAdapter::Stamp(*Bridge,Gift)));
    FLiveInteractionTestAdapter::Receive(*Bridge,GiftEnvelope(GiftRecord(TEXT("received-gift"),TEXT("魔法镜"))));Gift.MessageId=TEXT("received-gift");
    TestFalse(TEXT("Valid normalized relay gift uses the shared delivery deduplication path"),Bridge->DeliverGift(FLiveInteractionTestAdapter::Stamp(*Bridge,Gift)));
    FLiveInteractionTestAdapter::BeginDevRelay(*Bridge,TEXT("room-a"));
    Gift.MessageId=TEXT("room-event");
    TestTrue(TEXT("Trusted gift delivery still works in relay mode"),Bridge->DeliverGift(FLiveInteractionTestAdapter::Stamp(*Bridge,Gift)));
    TestFalse(TEXT("Relay gift replay is rejected within its room"),Bridge->DeliverGift(FLiveInteractionTestAdapter::Stamp(*Bridge,Gift)));
    FLiveInteractionTestAdapter::BeginDevRelay(*Bridge,TEXT("room-b"));TestTrue(TEXT("Another room can use its own identical event ID"),Bridge->DeliverGift(FLiveInteractionTestAdapter::Stamp(*Bridge,Gift)));
    FLiveInteractionTestAdapter::BeginDevRelay(*Bridge,TEXT("room-a"));TestFalse(TEXT("Returning to a room retains its replay protection"),Bridge->DeliverGift(FLiveInteractionTestAdapter::Stamp(*Bridge,Gift)));
    Bridge->DisconnectRelay();
    return true;
}
#endif
