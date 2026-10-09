#include "LiveInteractionSubsystem.h"
#include "LiveInteractionTestAdapter.h"
#include "LiveInteractionTestConsumer.h"
#include "Engine/GameInstance.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace {
class FAutomationLiveProvider final : public ILiveInteractionProvider
{
public:
    FString Platform,App,Room;
    FLiveSession Session;
    TFunction<void()> OnStop;
    TFunction<void(ULiveInteractionSubsystem&)> OnStart;
    explicit FAutomationLiveProvider(const FString& InPlatform,const FString& InApp=TEXT("app"),const FString& InRoom=TEXT("room"))
        : Platform(InPlatform),App(InApp),Room(InRoom) {}
    FString GetPlatformId() const override { return Platform; }
    bool Start(ULiveInteractionSubsystem& Host) override { Session=Host.BeginProviderSession(*this,App,Room); if(OnStart) OnStart(Host); return Session.Nonce.IsValid(); }
    void Stop() override { if(OnStop) OnStop(); }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiveModeAdmissionTest,"GeometricWarfare.LiveInteraction.ModeAdmission",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLiveModeAdmissionTest::RunTest(const FString&)
{
    auto* Host=NewObject<ULiveInteractionSubsystem>(NewObject<UGameInstance>());
    auto* Consumer=NewObject<ULiveInteractionTestConsumer>(Host);Consumer->Bind(*Host);
    TestTrue(TEXT("Uninitialized subsystem defaults to production"),Host->IsLiveMode());
    TestFalse(TEXT("Missing provider is disconnected"),Host->IsConnected());
    TestEqual(TEXT("Missing SDK is explicit"),Host->ConnectionStatus,FString(TEXT("SDK_UNAVAILABLE")));
    FLiveComment Comment;Comment.MessageId=TEXT("message");Comment.UserId=TEXT("viewer");Comment.Content=TEXT("加入");
    TestFalse(TEXT("Unprovenanced direct event rejected by default"),Host->DeliverComment(Comment));
    TestFalse(TEXT("Default simulator comment rejected"),Host->SimulateComment(TEXT("viewer"),TEXT("Viewer"),TEXT("加入")));
    TestFalse(TEXT("Default simulator like rejected"),Host->SimulateLike(TEXT("viewer"),TEXT("Viewer"),1));
    TestFalse(TEXT("Default simulator share rejected"),Host->SimulateShare(TEXT("viewer"),TEXT("Viewer")));
    TestFalse(TEXT("Default simulator gift rejected"),Host->SimulateGift(TEXT("viewer"),TEXT("Viewer"),TEXT("魔法镜"),1));
    TestFalse(TEXT("Production cannot connect custom development relay"),Host->ConnectRelay(TEXT("ws://127.0.0.1:1"),TEXT("room")));
    TestFalse(TEXT("Absent provider cannot start"),Host->StartPlatform(TEXT("automation-provider-does-not-exist")));
    TestEqual(TEXT("Missing registry provider reports SDK_UNAVAILABLE"),Host->ConnectionStatus,FString(TEXT("SDK_UNAVAILABLE")));
    TestFalse(TEXT("Disconnecting a relay never enables production simulation"),(Host->DisconnectRelay(),Host->IsLocalTestMode()));
    TestFalse(TEXT("No flags never enable local testing"),ULiveInteractionSubsystem::ShouldEnableLocalTest(TEXT(""),false));
    TestTrue(TEXT("Explicit development flag enables local testing"),ULiveInteractionSubsystem::ShouldEnableLocalTest(TEXT("-GWLocalTest"),false));
    TestFalse(TEXT("Shipping ignores local test flag"),ULiveInteractionSubsystem::ShouldEnableLocalTest(TEXT("-GWLocalTest"),true));
    TestFalse(TEXT("Platform token overrides local flag"),ULiveInteractionSubsystem::ShouldEnableLocalTest(TEXT("-GWLocalTest -token=opaque-launch-value"),false));
    TestFalse(TEXT("Empty token still means platform launch"),ULiveInteractionSubsystem::ShouldEnableLocalTest(TEXT("-GWLocalTest -token="),false));
    TestFalse(TEXT("Token separate value overrides local flag"),ULiveInteractionSubsystem::ShouldEnableLocalTest(TEXT("-GWLocalTest -token opaque"),false));
    TestFalse(TEXT("Credential stdin launch overrides local flag"),ULiveInteractionSubsystem::ShouldEnableLocalTest(TEXT("-GWLocalTest -GWCredentialStdin"),false));
    FLiveInteractionTestAdapter::EnableLocalTest(*Host);
    TestTrue(TEXT("Automation must explicitly opt in"),Host->IsLocalTestMode());
    TestFalse(TEXT("Local mode never reports authenticated production connection"),Host->IsConnected());
    TestEqual(TEXT("Legacy local progress namespace retained"),Host->GetScopedUserId(TEXT("viewer")),FString(TEXT("local:viewer")));
    TestFalse(TEXT("Local mode still rejects unstamped direct delivery"),Host->DeliverComment(Comment));
    TestTrue(TEXT("Stamped current local event accepted"),Host->DeliverComment(FLiveInteractionTestAdapter::Stamp(*Host,Comment)));
    TestTrue(TEXT("Explicit local simulator accepted"),Host->SimulateLike(TEXT("viewer"),TEXT("Viewer"),1));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiveProviderSessionTest,"GeometricWarfare.LiveInteraction.ProviderSessions",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLiveProviderSessionTest::RunTest(const FString&)
{
    auto* Host=NewObject<ULiveInteractionSubsystem>(NewObject<UGameInstance>());
    auto* Consumer=NewObject<ULiveInteractionTestConsumer>(Host);Consumer->Bind(*Host);
    auto A=MakeShared<FAutomationLiveProvider>(TEXT("automation-a"));
    auto B=MakeShared<FAutomationLiveProvider>(TEXT("automation-b"));
    TestTrue(TEXT("First platform registers"),FLiveInteractionProviderRegistry::Register(A->Platform,[A]() -> TSharedPtr<ILiveInteractionProvider> { return A; }));
    TestFalse(TEXT("Duplicate registration cannot silently replace provider"),FLiveInteractionProviderRegistry::Register(A->Platform,[B]() -> TSharedPtr<ILiveInteractionProvider> { return B; }));
    TestTrue(TEXT("Second platform registers independently"),FLiveInteractionProviderRegistry::Register(B->Platform,[B]() -> TSharedPtr<ILiveInteractionProvider> { return B; }));
    TestTrue(TEXT("Registered authenticated test provider can start"),Host->StartPlatform(A->Platform));
    TestTrue(TEXT("Production connection requires provider room session"),Host->IsConnected());
    const FString AKey=Host->GetScopedUserId(TEXT("same-viewer"));
    FLiveComment Comment;Comment.Session=A->Session;Comment.MessageId=TEXT("shared-id");Comment.UserId=TEXT("same-viewer");Comment.Content=TEXT("加入");
    TestTrue(TEXT("Current trusted provider event accepted"),Host->DeliverComment(Comment));
    TestFalse(TEXT("Production simulator remains disabled with provider connected"),Host->SimulateComment(TEXT("same-viewer"),TEXT("Viewer"),TEXT("加入")));
    bool StoppedProviderReopenedRoom=false;
    A->OnStop=[&]() { StoppedProviderReopenedRoom=Host->BeginProviderSession(*A,TEXT("app"),TEXT("late-stop-room")).Nonce.IsValid(); };
    TestTrue(TEXT("Second platform can establish its own room"),Host->StartPlatform(B->Platform));
    TestFalse(TEXT("Provider stopping callback cannot reopen an authenticated room"),StoppedProviderReopenedRoom);
    A->OnStop=nullptr;
    const FString BKey=Host->GetScopedUserId(TEXT("same-viewer"));
    TestNotEqual(TEXT("Same raw user ID has isolated platform progress identity"),AKey,BKey);
    TestFalse(TEXT("Old provider callback rejected after platform switch"),Host->DeliverComment(Comment));
    Comment.Session=B->Session;
    TestTrue(TEXT("Other platform can use its independent same event ID"),Host->DeliverComment(Comment));
    const FLiveSession Prior=B->Session;
    TestFalse(TEXT("Inactive provider cannot establish a new room"),Host->BeginProviderSession(*A,TEXT("app"),TEXT("room")).Nonce.IsValid());
    Host->EndProviderSession(*B,TEXT("SDK_DISCONNECTED"));
    TestFalse(TEXT("Provider disconnect invalidates room admission"),Host->IsConnected());
    Comment.MessageId=TEXT("late-disconnected");TestFalse(TEXT("Disconnected provider cannot deliver old session"),Host->DeliverComment(Comment));
    B->Session=Host->BeginProviderSession(*B,TEXT("app-two"),TEXT("room"));
    TestTrue(TEXT("Reconnect receives a new session nonce"),B->Session.Nonce.IsValid() && B->Session.Nonce!=Prior.Nonce);
    TestNotEqual(TEXT("Same platform raw ID isolated across applications"),BKey,Host->GetScopedUserId(TEXT("same-viewer")));
    Comment.Session=Prior;Comment.MessageId=TEXT("late-reconnect");TestFalse(TEXT("Prior generation rejected after reconnect"),Host->DeliverComment(Comment));
    Comment.Session=B->Session;TestTrue(TEXT("Rejected stale event did not consume ID in new session"),Host->DeliverComment(Comment));
    auto Forged=Comment;Forged.MessageId=TEXT("forged");Forged.Session.RoomId=TEXT("wrong-room");
    TestFalse(TEXT("Room mismatch rejected even with current nonce"),Host->DeliverComment(Forged));
    Forged.Session=Comment.Session;Forged.Session.bLocalTest=true;
    TestFalse(TEXT("Simulation provenance cannot enter production"),Host->DeliverComment(Forged));
    FLiveInteractionTestAdapter::EnableLocalTest(*Host);
    TestFalse(TEXT("Production provenance cannot enter local test session"),Host->DeliverComment(Comment));
    Host->StartPlatform(TEXT("automation-provider-does-not-exist"));
    FLiveInteractionProviderRegistry::Unregister(A->Platform);FLiveInteractionProviderRegistry::Unregister(B->Platform);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiveConsumerReadinessTest,"GeometricWarfare.LiveInteraction.ConsumerReadiness",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLiveConsumerReadinessTest::RunTest(const FString&)
{
    auto* Host=NewObject<ULiveInteractionSubsystem>(NewObject<UGameInstance>());
    auto Provider=MakeShared<FAutomationLiveProvider>(TEXT("automation-early-delivery"));
    FLiveComment Comment;Comment.MessageId=TEXT("early-comment");Comment.UserId=TEXT("viewer");Comment.Content=TEXT("加入");
    FLiveLike Like;Like.MessageId=TEXT("early-like");Like.UserId=TEXT("viewer");
    FLiveShare Share;Share.MessageId=TEXT("early-share");Share.UserId=TEXT("viewer");
    FLiveGift Gift;Gift.MessageId=TEXT("early-gift");Gift.UserId=TEXT("viewer");Gift.GiftName=TEXT("魔法镜");
    bool EarlyComment=false,EarlyLike=false,EarlyShare=false,EarlyGift=false;
    Provider->OnStart=[&](ULiveInteractionSubsystem& Sink) {
        Comment.Session=Like.Session=Share.Session=Gift.Session=Provider->Session;
        EarlyComment=Sink.DeliverComment(Comment);EarlyLike=Sink.DeliverLike(Like);
        EarlyShare=Sink.DeliverShare(Share);EarlyGift=Sink.DeliverGift(Gift);
    };
    FLiveInteractionProviderRegistry::Register(Provider->Platform,[Provider]() -> TSharedPtr<ILiveInteractionProvider> { return Provider; });
    TestTrue(TEXT("Provider starts before gameplay subscription"),Host->StartPlatform(Provider->Platform));
    TestFalse(TEXT("Comment without consumer is not acknowledged"),EarlyComment);
    TestFalse(TEXT("Like without consumer is not acknowledged"),EarlyLike);
    TestFalse(TEXT("Share without consumer is not acknowledged"),EarlyShare);
    TestFalse(TEXT("Gift without consumer is not acknowledged"),EarlyGift);
    auto* Consumer=NewObject<ULiveInteractionTestConsumer>(Host);Consumer->Bind(*Host);
    TestTrue(TEXT("Early comment ID can be retried after subscription"),Host->DeliverComment(Comment));
    TestTrue(TEXT("Early like ID can be retried after subscription"),Host->DeliverLike(Like));
    TestTrue(TEXT("Early share ID can be retried after subscription"),Host->DeliverShare(Share));
    TestTrue(TEXT("Early gift ID can be retried after subscription"),Host->DeliverGift(Gift));
    TestEqual(TEXT("Consumer receives exactly one comment"),Consumer->Comments,1);
    TestEqual(TEXT("Consumer receives exactly one like"),Consumer->Likes,1);
    TestEqual(TEXT("Consumer receives exactly one share"),Consumer->Shares,1);
    TestEqual(TEXT("Consumer receives exactly one gift"),Consumer->Gifts,1);
    TestFalse(TEXT("Successful delivery still rejects replay"),Host->DeliverGift(Gift));
    Consumer->MarkAsGarbage();
    Gift.MessageId=TEXT("consumer-replaced-gift");
    TestFalse(TEXT("Expired consumer binding cannot acknowledge an event"),Host->DeliverGift(Gift));
    auto* Replacement=NewObject<ULiveInteractionTestConsumer>(Host);Replacement->Bind(*Host);
    TestTrue(TEXT("Replacing consumer can retry the unconsumed event ID"),Host->DeliverGift(Gift));
    TestEqual(TEXT("Replacement consumer receives exactly one gift"),Replacement->Gifts,1);
    Host->StartPlatform(TEXT("automation-provider-does-not-exist"));
    FLiveInteractionProviderRegistry::Unregister(Provider->Platform);
    return true;
}
#endif
