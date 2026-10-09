#include "ArenaGameMode.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Kismet/GameplayStatics.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace {
class FFollowQualificationProvider final : public ILiveInteractionProvider {
public:
    FLiveSession Session;
    FString GetPlatformId() const override {return TEXT("follow-qualification-test");}
    bool Start(ULiveInteractionSubsystem& Host) override {Session=Host.BeginProviderSession(*this,TEXT("app"),TEXT("room"),TEXT("anchor"));return Session.Nonce.IsValid();}
    void Stop() override {}
    bool SendCommand(const FString&,const FString&,const TSharedRef<FJsonObject>&) override {return true;}
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArenaFollowQualificationTest,"GeometricWarfare.Arena.FollowQualification",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FArenaFollowQualificationTest::RunTest(const FString&) {
    auto Provider=MakeShared<FFollowQualificationProvider>();
    FLiveInteractionProviderRegistry::Register(Provider->GetPlatformId(),[Provider]() -> TSharedPtr<ILiveInteractionProvider> {return Provider;});
    auto* Instance=NewObject<UGameInstance>(GEngine);Instance->InitializeStandalone(FName(TEXT("FollowQualificationWorld")));
    auto* Bridge=Instance->GetSubsystem<ULiveInteractionSubsystem>();Bridge->StartPlatform(Provider->GetPlatformId());
    UWorld* World=Instance->GetWorld();FURL Url;Url.AddOption(TEXT("game=/Script/GeometricWarfare.ArenaGameMode"));
    World->SetGameMode(Url);World->InitializeActorsForPlay(Url);World->BeginPlay();
    auto* Game=World->GetAuthGameMode<AArenaGameMode>();
    if(!TestNotNull(TEXT("Follow fixture created"),Game))return false;
    const FString Slot=TEXT("FollowQualification_")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
    Game->ProgressSlot=Slot;Game->Progress=NewObject<UArenaProgressSave>(Game);Game->bProgressWritable=true;
    const auto Comment=[&](const FString& User,const FString& Text){FLiveComment E;E.Session=Provider->Session;E.MessageId=FGuid::NewGuid().ToString();E.UserId=User;E.Nickname=TEXT("same display name");E.Content=Text;return Bridge->DeliverComment(E);};
    const auto Presence=[&](const FString& User,int64 Time,int32 Status,int32 Direction=1){FLivePresence E;E.Session=Provider->Session;E.MessageId=FGuid::NewGuid().ToString();E.UserId=User;E.Nickname=TEXT("same display name");E.TimestampMs=Time;E.FollowStatus=Status;E.EnterType=Direction;return Bridge->DeliverPresence(E);};
    const auto Fighter=[&](const FString& User) -> const gw::Fighter* {const auto* V=Game->GetViewers().Find(User);return V?Game->GetMatch().findFighter(V->BodyId):nullptr;};
    const auto HasRifle=[&](const FString& User){const auto* F=Fighter(User);return F && (F->unlockedWeapons&gw::weaponBit(gw::WeaponKind::Rifle))!=0;};
    const auto EquipRifle=[&](const FString& User){TestTrue(TEXT("Literal 武器3 delivered"),Comment(User,TEXT("武器3")));const auto* F=Fighter(User);return F && F->weaponKind==gw::WeaponKind::Rifle;};

    Presence(TEXT("prefollowed"),100,1);
    TestFalse(TEXT("Already-following entry does not auto-admit"),Game->GetViewers().Contains(TEXT("prefollowed")));
    Comment(TEXT("prefollowed"),TEXT("1"));
    TestTrue(TEXT("Followed before entering then 武器3 equips rifle"),EquipRifle(TEXT("prefollowed")));
    if(const auto* F=Fighter(TEXT("prefollowed")))TestEqual(TEXT("Verified follow equips rifle with 45 rounds"),F->ammo,45);
    Comment(TEXT("prefollowed"),TEXT("武器1"));
    TestTrue(TEXT("Permanent follow rifle can be selected again with 武器3"),EquipRifle(TEXT("prefollowed")));
    const int32 BodyId=Game->GetViewers().FindChecked(TEXT("prefollowed")).BodyId;
    Presence(TEXT("prefollowed"),101,0,2);
    TestTrue(TEXT("Leaving preserves permanent rifle"),HasRifle(TEXT("prefollowed")));
    TestEqual(TEXT("Leaving preserves fighter identity"),Game->GetViewers().FindChecked(TEXT("prefollowed")).BodyId,BodyId);
    TestTrue(TEXT("Follow unlock stored with scoped platform identity"),(Game->Progress->WeaponUnlocks.FindRef(Bridge->GetScopedUserId(TEXT("prefollowed")))&gw::weaponBit(gw::WeaponKind::Rifle))!=0);

    // Distinct SDK messages can share a timestamp; message IDs, not timestamps,
    // define replay. The second snapshot is a verified qualification change.
    Presence(TEXT("same-timestamp"),200,0);
    Presence(TEXT("same-timestamp"),200,1);
    Comment(TEXT("same-timestamp"),TEXT("1"));
    TestTrue(TEXT("Distinct equal-timestamp follower snapshot unlocks and equips rifle"),EquipRifle(TEXT("same-timestamp")));

    Presence(TEXT("missing-timestamp"),300,0);
    Presence(TEXT("missing-timestamp"),0,2);
    Comment(TEXT("missing-timestamp"),TEXT("2"));
    TestTrue(TEXT("Unknown timestamp follower snapshot follows SDK arrival order"),EquipRifle(TEXT("missing-timestamp")));
    Presence(TEXT("missing-timestamp"),299,0,2);
    TestTrue(TEXT("Unknown timestamp does not erase the positive-time ordering watermark"),Game->GetViewers().FindChecked(TEXT("missing-timestamp")).bPresent);

    Presence(TEXT("stale-follower"),400,0);
    Presence(TEXT("stale-follower"),399,1);
    Comment(TEXT("stale-follower"),TEXT("1"));
    TestFalse(TEXT("Older timestamped follower evidence does not grant rifle"),HasRifle(TEXT("stale-follower")));
    Presence(TEXT("followed-by-anchor"),0,3);
    Comment(TEXT("followed-by-anchor"),TEXT("1"));
    TestFalse(TEXT("Being followed by anchor alone does not grant rifle"),HasRifle(TEXT("followed-by-anchor")));
    Comment(TEXT("different-id"),TEXT("1"));
    TestFalse(TEXT("Same display name with a different platform ID never inherits qualification"),HasRifle(TEXT("different-id")));
    Comment(TEXT("no-platform-evidence"),TEXT("1"));
    TestFalse(TEXT("Weapon command cannot fabricate missing follow evidence"),EquipRifle(TEXT("no-platform-evidence")));

    Presence(TEXT("canceled-before-join"),0,1);
    FLiveFollow Cancel;Cancel.Session=Provider->Session;Cancel.MessageId=FGuid::NewGuid().ToString();Cancel.UserId=TEXT("canceled-before-join");Cancel.TargetUserId=TEXT("anchor");Cancel.Action=2;
    Bridge->DeliverFollow(Cancel);Comment(Cancel.UserId,TEXT("1"));
    TestFalse(TEXT("Verified cancellation before participation clears pending qualification"),HasRifle(Cancel.UserId));
    Bridge->EndProviderSession(*Provider,TEXT("SDK_DISCONNECTED"));
    TestTrue(TEXT("Session switch clears pending qualifications"),Game->GetLiveViewerStates().IsEmpty());
    UGameplayStatics::DeleteGameInSlot(Slot,0);World->EndPlay(EEndPlayReason::Quit);World->DestroyWorld(false);Instance->Shutdown();GEngine->DestroyWorldContext(World);
    FLiveInteractionProviderRegistry::Unregister(Provider->GetPlatformId());
    return true;
}
#endif
