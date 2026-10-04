#include "DouyinLiveSubsystem.h"
#include "Misc/AutomationTest.h"
#include "Engine/GameInstance.h"
#include "DouyinAvatarRequestTracker.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDouyinDecodeTest, "GeometricWarfare.Bridge.Decode", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDouyinDecodeTest::RunTest(const FString&)
{
    FString Room, Error; TArray<FDouyinComment> Events;
    const FString Valid=TEXT("{\"room_id\":\"test-room\",\"type\":\"live_comment\",\"payload\":[{\"msg_id\":\"1\",\"sec_openid\":\"u1\",\"nickname\":\"Alice\",\"content\":\"加入\"},{\"msg_id\":\"2\",\"sec_openid\":\"u2\",\"nickname\":\"Bob\",\"content\":\"2\"}]}");
    TestTrue(TEXT("Batch of valid comments accepted"),FDouyinEventDecoder::Decode(Valid,Room,Events,Error));
    TestEqual(TEXT("Two events"),Events.Num(),2);
    if(Events.Num()==2) { TestEqual(TEXT("User identity kept"),Events[0].UserId,FString(TEXT("u1"))); TestEqual(TEXT("Chinese command preserved"),Events[0].Content,FString(TEXT("加入"))); }
    TestFalse(TEXT("Invalid JSON rejected"),FDouyinEventDecoder::Decode(TEXT("{broken"),Room,Events,Error));
    TestTrue(TEXT("Failed batch clears previous events"),Events.IsEmpty());
    TestFalse(TEXT("Missing user rejected"),FDouyinEventDecoder::Decode(TEXT("{\"room_id\":\"r\",\"type\":\"live_comment\",\"payload\":[{\"msg_id\":\"x\",\"content\":\"加入\"}]}"),Room,Events,Error));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDouyinDedupTest, "GeometricWarfare.Bridge.Deduplicate", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDouyinDedupTest::RunTest(const FString&)
{
    auto* Instance=NewObject<UGameInstance>();
    auto* Bridge=NewObject<UDouyinLiveSubsystem>(Instance);
    FDouyinComment Comment; Comment.MessageId=TEXT("id-1"); Comment.UserId=TEXT("u1"); Comment.Content=TEXT("加入");
    TestTrue(TEXT("First event accepted"),Bridge->DeliverComment(Comment));
    TestFalse(TEXT("Replay rejected"),Bridge->DeliverComment(Comment));
    Comment.MessageId=TEXT("id-2"); TestTrue(TEXT("Distinct message accepted"),Bridge->DeliverComment(Comment));
    Comment.UserId=TEXT(" "); TestFalse(TEXT("Blank identity rejected"),Bridge->DeliverComment(Comment));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDouyinAvatarRaceTest,"GeometricWarfare.Bridge.AvatarOrdering",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDouyinAvatarRaceTest::RunTest(const FString&)
{
    FDouyinAvatarRequestTracker Tracker;
    const auto Old=Tracker.Begin(TEXT("viewer"),TEXT("https://avatar/a"));
    TestTrue(TEXT("Pending request can be coalesced"),Tracker.IsPending(TEXT("viewer"),TEXT("https://avatar/a")));
    const auto Cached=Tracker.Begin(TEXT("viewer"),TEXT("https://avatar/cached"));
    TestTrue(TEXT("New cached request wins"),Tracker.Complete(TEXT("viewer"),Cached));
    TestFalse(TEXT("Late download cannot overwrite cache hit"),Tracker.Complete(TEXT("viewer"),Old));
    const auto PriorRoom=Tracker.Begin(TEXT("viewer"),TEXT("https://avatar/b"));
    Tracker.Reset();
    const auto NextRoom=Tracker.Begin(TEXT("viewer"),TEXT("https://avatar/b"));
    TestFalse(TEXT("Prior room completion cannot update reused identity"),Tracker.Complete(TEXT("viewer"),PriorRoom));
    TestTrue(TEXT("Current room completion accepted"),Tracker.Complete(TEXT("viewer"),NextRoom));
    TestFalse(TEXT("Completion only delivered once"),Tracker.Complete(TEXT("viewer"),NextRoom));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDouyinInteractionDecodeTest,"GeometricWarfare.Bridge.InteractionDecode",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDouyinInteractionDecodeTest::RunTest(const FString&)
{
    FString Room,Error; TArray<FDouyinComment> Comments; TArray<FDouyinLike> Likes; TArray<FDouyinShare> Shares;
    const auto Decode=[&](const FString& Json) { return FDouyinEventDecoder::DecodeEvents(Json,Room,Comments,Likes,Shares,Error); };
    const FString LikePrefix=TEXT("{\"room_id\":\"test-room\",\"type\":\"live_like\",\"payload\":[{\"msg_id\":\"like-1\",\"sec_openid\":\"u1\",\"nickname\":\"Alice\",\"count\":");
    TestTrue(TEXT("Normalized delta like envelope accepted"),Decode(LikePrefix+TEXT("3}]}")));
    TestEqual(TEXT("Like room preserved"),Room,FString(TEXT("test-room")));
    TestEqual(TEXT("One like event"),Likes.Num(),1);
    if(Likes.Num()==1) { TestEqual(TEXT("Like stable identity preserved"),Likes[0].UserId,FString(TEXT("u1"))); TestEqual(TEXT("Like count means three new likes"),Likes[0].Count,3); }
    TestTrue(TEXT("Like does not invent a comment or share"),Comments.IsEmpty() && Shares.IsEmpty());
    TestTrue(TEXT("Maximum bounded delta accepted"),Decode(LikePrefix+TEXT("100}]}")));
    for(const TCHAR* Invalid:{TEXT("0"),TEXT("-1"),TEXT("101"),TEXT("1.5"),TEXT("\"3\""),TEXT("null"),TEXT("true")}) {
        TestFalse(FString::Printf(TEXT("Invalid like count %s rejected"),Invalid),Decode(LikePrefix+Invalid+TEXT("}]}")));
        TestTrue(TEXT("Invalid like clears every output"),Room.IsEmpty() && Comments.IsEmpty() && Likes.IsEmpty() && Shares.IsEmpty());
    }
    TestFalse(TEXT("Missing like delta rejected"),Decode(TEXT("{\"room_id\":\"r\",\"type\":\"live_like\",\"payload\":[{\"msg_id\":\"x\",\"sec_openid\":\"u\"}]}")));
    TestFalse(TEXT("One invalid item rejects the whole like batch"),Decode(TEXT("{\"room_id\":\"r\",\"type\":\"live_like\",\"payload\":[{\"msg_id\":\"x\",\"sec_openid\":\"u\",\"count\":1},{\"msg_id\":\"y\",\"sec_openid\":\"u\",\"count\":101}]}")));
    TestTrue(TEXT("No partial valid likes survive a failed batch"),Likes.IsEmpty());
    const FString Share=TEXT("{\"room_id\":\"r\",\"type\":\"live_share\",\"payload\":[{\"msg_id\":\"share-1\",\"sec_openid\":\" u1 \",\"nickname\":\"Alice\"}]}");
    TestTrue(TEXT("Normalized share envelope accepted"),Decode(Share));
    TestEqual(TEXT("One share event"),Shares.Num(),1);
    if(Shares.Num()==1) { TestEqual(TEXT("Share ID preserved"),Shares[0].MessageId,FString(TEXT("share-1"))); TestEqual(TEXT("Share user normalized"),Shares[0].UserId,FString(TEXT("u1"))); }
    TestFalse(TEXT("Legacy comment-only decoder rejects share envelope"),FDouyinEventDecoder::Decode(Share,Room,Comments,Error));
    TestFalse(TEXT("Share without event ID rejected"),Decode(TEXT("{\"room_id\":\"r\",\"type\":\"live_share\",\"payload\":[{\"sec_openid\":\"u\"}]}")));
    TestFalse(TEXT("Unknown event type rejected"),Decode(TEXT("{\"room_id\":\"r\",\"type\":\"official_guess\",\"payload\":[]}")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDouyinInteractionDedupTest,"GeometricWarfare.Bridge.InteractionDeduplicate",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDouyinInteractionDedupTest::RunTest(const FString&)
{
    auto* Bridge=NewObject<UDouyinLiveSubsystem>(NewObject<UGameInstance>());
    FDouyinLike Like; Like.MessageId=TEXT("like-id"); Like.UserId=TEXT("u1"); Like.Count=3;
    TestTrue(TEXT("First like accepted"),Bridge->DeliverLike(Like));
    TestFalse(TEXT("Same like event cannot heal twice"),Bridge->DeliverLike(Like));
    FDouyinShare Share; Share.MessageId=Like.MessageId; Share.UserId=Like.UserId;
    TestFalse(TEXT("Changing event type cannot bypass shared event-ID dedup"),Bridge->DeliverShare(Share));
    Share.MessageId=TEXT("share-id");
    TestTrue(TEXT("First share accepted"),Bridge->DeliverShare(Share));
    TestFalse(TEXT("Same share cannot grant twice"),Bridge->DeliverShare(Share));
    FDouyinComment Comment; Comment.MessageId=Share.MessageId; Comment.UserId=Share.UserId; Comment.Content=TEXT("1");
    TestFalse(TEXT("Comments share the same event-ID window"),Bridge->DeliverComment(Comment));
    Like.MessageId=TEXT("repairable"); Like.Count=101;
    TestFalse(TEXT("Oversized direct like delivery rejected"),Bridge->DeliverLike(Like));
    Like.Count=1;
    TestTrue(TEXT("Rejected input does not consume its ID"),Bridge->DeliverLike(Like));
    Like.MessageId=TEXT(" "); TestFalse(TEXT("Blank like event ID rejected"),Bridge->DeliverLike(Like));
    Share.MessageId=TEXT("fresh"); Share.UserId=TEXT(" "); TestFalse(TEXT("Blank share identity rejected"),Bridge->DeliverShare(Share));
    TestTrue(TEXT("Mock like uses validated delivery"),Bridge->SimulateLike(TEXT("u1"),TEXT("Alice"),5));
    TestFalse(TEXT("Mock like cannot submit a cumulative oversized count"),Bridge->SimulateLike(TEXT("u1"),TEXT("Alice"),101));
    TestTrue(TEXT("Mock share uses validated delivery"),Bridge->SimulateShare(TEXT("u1"),TEXT("Alice")));
    TestFalse(TEXT("Mock share requires identity"),Bridge->SimulateShare(TEXT(""),TEXT("Alice")));
    return true;
}
#endif
