#include "ArenaGameMode.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Kismet/GameplayStatics.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace {
class FGatherNoticeProvider final : public ILiveInteractionProvider {
public:
    FLiveSession Session;
    FString GetPlatformId() const override {return TEXT("gather-notice-test");}
    bool Start(ULiveInteractionSubsystem& Host) override {Session=Host.BeginProviderSession(*this,TEXT("app"),TEXT("room"),TEXT("anchor"));return Session.Nonce.IsValid();}
    void Stop() override {}
    bool SendCommand(const FString&,const FString&,const TSharedRef<FJsonObject>&) override {return true;}
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArenaGatherNoticeTest,"GeometricWarfare.Arena.GatherNotices",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FArenaGatherNoticeTest::RunTest(const FString&) {
    auto Provider=MakeShared<FGatherNoticeProvider>();
    FLiveInteractionProviderRegistry::Register(Provider->GetPlatformId(),[Provider]() -> TSharedPtr<ILiveInteractionProvider> {return Provider;});
    auto* Instance=NewObject<UGameInstance>(GEngine);Instance->InitializeStandalone(FName(TEXT("GatherNoticeWorld")));
    auto* Bridge=Instance->GetSubsystem<ULiveInteractionSubsystem>();Bridge->StartPlatform(Provider->GetPlatformId());
    auto* World=Instance->GetWorld();FURL Url;Url.AddOption(TEXT("game=/Script/GeometricWarfare.ArenaGameMode"));
    World->SetGameMode(Url);World->InitializeActorsForPlay(Url);World->BeginPlay();
    auto* Game=World->GetAuthGameMode<AArenaGameMode>();
    if(!TestNotNull(TEXT("Gather notice fixture created"),Game))return false;
    const FString Slot=TEXT("GatherNotice_")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
    Game->ProgressSlot=Slot;Game->Progress=NewObject<UArenaProgressSave>(Game);Game->bProgressWritable=true;
    const auto Units=[&](){int64 Total=0;for(const auto& N:Game->JoinNotices)if(N.Active&&N.Visual==EArenaNoticeVisual::Gather)Total+=N.Count;for(const auto& N:Game->JoinNoticeQueue)if(N.Visual==EArenaNoticeVisual::Gather)Total+=N.Count;return Total;};
    const auto Deliver=[&](const TCHAR* Id,const TCHAR* User,int32 Scene,const TCHAR* Inviter,int32 Type=1,int64 Timestamp=100){
        FLivePresence E;E.Session=Provider->Session;E.MessageId=Id;E.UserId=User;E.Nickname=TEXT("受邀观众");E.EnterType=Type;E.TimestampMs=Timestamp;E.EnterRoomScene=Scene;E.InviterId=Inviter;return Bridge->DeliverPresence(E);
    };
    TestTrue(TEXT("Ordinary entry is accepted"),Deliver(TEXT("ordinary"),TEXT("ordinary"),0,TEXT("inviter")));
    TestTrue(TEXT("Share entry is accepted"),Deliver(TEXT("share"),TEXT("share"),2,TEXT("inviter")));
    Deliver(TEXT("missing-inviter"),TEXT("missing"),1,TEXT(""));
    Deliver(TEXT("self-inviter"),TEXT("self"),1,TEXT("self"));
    Deliver(TEXT("gather-leave"),TEXT("leaving"),1,TEXT("inviter"),2);
    TestEqual(TEXT("Ordinary, share, missing, self and leave events never claim summon fulfilment"),Units(),int64(0));
    Game->LiveViewerStates.FindOrAdd(TEXT("inviter")).Nickname=TEXT("邀请好友");
    auto* Avatar=NewObject<UTexture2D>(Game);Game->LiveViewerStates.FindOrAdd(TEXT("invited")).Avatar=Avatar;
    TestTrue(TEXT("Official summon entry is accepted"),Deliver(TEXT("gather-one"),TEXT("invited"),1,TEXT("inviter")));
    TestEqual(TEXT("Summon generates exactly one scrolling notice"),Units(),int64(1));
    const auto& Notice=Game->JoinNotices[0];
    TestTrue(TEXT("Notice keeps invitee name, identity, cached avatar and inviter name"),Notice.UserId==TEXT("invited")&&Notice.ViewerName==TEXT("受邀观众")&&Notice.Detail.Contains(TEXT("邀请好友"))&&Game->GetNoticeAvatar(Notice.UserId)==Avatar);
    TestTrue(TEXT("Spectator summon explains how to participate"),Notice.Detail.Contains(TEXT("发加入"))&&Game->LastEvent.Contains(TEXT("发送加入或选队参与")));
    TestTrue(TEXT("Summon never auto-admits or creates permanent rewards"),Game->Viewers.IsEmpty()&&Game->Progress->WeaponUnlocks.IsEmpty()&&Game->RewardNotices.IsEmpty());
    TestFalse(TEXT("Replayed official event is rejected by bridge"),Deliver(TEXT("gather-one"),TEXT("invited"),1,TEXT("inviter")));
    TestEqual(TEXT("Replay does not increment summon count"),Units(),int64(1));
    Deliver(TEXT("gather-old"),TEXT("invited"),1,TEXT("inviter"),1,99);
    TestEqual(TEXT("Older presence cannot announce a new summon"),Units(),int64(1));
    Deliver(TEXT("gather-fallback"),TEXT("unknown-invite"),1,TEXT("private-id-do-not-display"));
    TestTrue(TEXT("Unknown inviter uses friend label without exposing identifier"),Game->LastEvent.Contains(TEXT("好友"))&&!Game->LastEvent.Contains(TEXT("private-id-do-not-display")));
    FLiveComment Join;Join.Session=Provider->Session;Join.MessageId=TEXT("explicit-join");Join.UserId=TEXT("invited");Join.Nickname=TEXT("受邀观众");Join.Content=TEXT("1");Bridge->DeliverComment(Join);
    const int32 BodyId=Game->Viewers.FindChecked(TEXT("invited")).BodyId;
    const auto Weapons=Game->Match.findFighter(BodyId)->unlockedWeapons;
    Game->JoinNotices.Reset();Game->JoinNoticeQueue.Reset();
    Deliver(TEXT("gather-participant"),TEXT("invited"),1,TEXT("inviter"),1,101);
    TestTrue(TEXT("Existing participant gets accurate participation status"),Game->JoinNotices[0].Detail.Contains(TEXT("已参与"))&&!Game->LastEvent.Contains(TEXT("发送加入")));
    TestTrue(TEXT("Gather preserves participant identity, camp and weapon rights"),Game->Viewers.Num()==1&&Game->Viewers.FindChecked(TEXT("invited")).BodyId==BodyId&&Game->Viewers.FindChecked(TEXT("invited")).Team==1&&Game->Match.findFighter(BodyId)->unlockedWeapons==Weapons);
    Game->JoinNotices.Reset();Game->JoinNoticeQueue.Reset();
    for(int32 I=0;I<600;++I){const FString User=FString::Printf(TEXT("crowd-%d"),I);Deliver(*User,*User,1,TEXT("inviter"));}
    TestEqual(TEXT("Crowded summon notices retain all 600 arrivals"),Units(),int64(600));
    TestTrue(TEXT("Summon overflow remains bounded and clearly labelled"),Game->JoinNoticeQueue.Num()<300&&Game->JoinNoticeQueue.ContainsByPredicate([](const FGiftNotice& N){return N.UserId.IsEmpty()&&N.ViewerName==TEXT("多位观众")&&N.Visual==EArenaNoticeVisual::Gather&&N.Detail==TEXT("响应了一键摇人召集");}));
    TestTrue(TEXT("Summon ACK keeps official enter event type"),Game->PendingLiveAcks.ContainsByPredicate([](const FPendingLiveAck& Ack){return Ack.MessageId==TEXT("gather-one")&&Ack.MessageType==TEXT("live_enter");}));
    Bridge->EndProviderSession(*Provider,TEXT("SDK_DISCONNECTED"));
    TestTrue(TEXT("Session changes clear queued summon feedback"),Game->JoinNotices.IsEmpty()&&Game->JoinNoticeQueue.IsEmpty());
    UGameplayStatics::DeleteGameInSlot(Slot,0);World->EndPlay(EEndPlayReason::Quit);World->DestroyWorld(false);Instance->Shutdown();GEngine->DestroyWorldContext(World);FLiveInteractionProviderRegistry::Unregister(Provider->GetPlatformId());
    return true;
}
#endif
