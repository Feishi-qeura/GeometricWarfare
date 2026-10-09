#include "ArenaGameMode.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Kismet/GameplayStatics.h"
#include "RHI.h"
#include "ArenaLiveRounds.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "DouyinGiftDecoder.h"
namespace {
struct FGameplayLiveCommand {FString Id,Operation;TSharedPtr<FJsonObject> Payload;};
class FGameplayLiveProvider final : public ILiveInteractionProvider {
public:
    FLiveSession Session;TArray<FGameplayLiveCommand> Commands;
    FString GetPlatformId() const override {return TEXT("advanced-gameplay-test");}
    bool Start(ULiveInteractionSubsystem& Host) override {Session=Host.BeginProviderSession(*this,TEXT("app"),TEXT("room"),TEXT("anchor"));return Session.Nonce.IsValid();}
    void Stop() override {}
    bool SendCommand(const FString& Id,const FString& Op,const TSharedRef<FJsonObject>& Payload) override {Commands.Add({Id,Op,Payload});return true;}
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArenaAdvancedLiveTest,"GeometricWarfare.Arena.AdvancedLive",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FArenaAdvancedLiveTest::RunTest(const FString&) {
    auto Provider=MakeShared<FGameplayLiveProvider>();
    FLiveInteractionProviderRegistry::Register(Provider->GetPlatformId(),[Provider]() -> TSharedPtr<ILiveInteractionProvider> {return Provider;});
    auto* Instance=NewObject<UGameInstance>(GEngine);Instance->InitializeStandalone(FName(TEXT("AdvancedLiveWorld")));
    auto* Bridge=Instance->GetSubsystem<ULiveInteractionSubsystem>();Bridge->StartPlatform(Provider->GetPlatformId());
    UWorld* World=Instance->GetWorld();FURL Url;Url.AddOption(TEXT("game=/Script/GeometricWarfare.ArenaGameMode"));
    World->SetGameMode(Url);World->InitializeActorsForPlay(Url);World->BeginPlay();
    auto* Game=World->GetAuthGameMode<AArenaGameMode>();
    if(!TestNotNull(TEXT("Advanced live fixture created"),Game))return false;
    const FString Slot=TEXT("AdvancedLive_")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
    Game->ProgressSlot=Slot;Game->Progress=NewObject<UArenaProgressSave>(Game);Game->bProgressWritable=true;
    const auto Comment=[&](const FString& User,const FString& Text){FLiveComment E;E.Session=Provider->Session;E.MessageId=FGuid::NewGuid().ToString();E.UserId=User;E.Content=Text;return Bridge->DeliverComment(E);};
    const auto Rifle=[&](const FString& User){const auto* V=Game->GetViewers().Find(User);const auto* F=V?Game->GetMatch().findFighter(V->BodyId):nullptr;return F&&(F->unlockedWeapons&gw::weaponBit(gw::WeaponKind::Rifle));};
    FLivePresence Presence;Presence.Session=Provider->Session;Presence.MessageId=TEXT("enter");Presence.UserId=TEXT("viewer");Presence.TimestampMs=200;Presence.FollowStatus=1;Presence.EnterType=1;Presence.InviterId=TEXT("inviter");Presence.EnterRoomScene=2;
    TestTrue(TEXT("Authenticated presence reaches gameplay"),Bridge->DeliverPresence(Presence));
    TestTrue(TEXT("Presence and invitation evidence do not auto-admit a fighter"),Game->GetViewers().IsEmpty());
    TestEqual(TEXT("Invitation identity preserved without awarding shotgun"),Game->GetLiveViewerStates().FindChecked(TEXT("viewer")).InviterId,FString(TEXT("inviter")));
    TestTrue(TEXT("Explicit join materializes the spectator"),Comment(TEXT("viewer"),TEXT("1")));
    TestTrue(TEXT("Joined follower unlocks rifle from verified presence qualification"),Rifle(TEXT("viewer")));
    auto* Viewer=Game->Viewers.Find(TEXT("viewer"));const int32 BodyId=Viewer->BodyId;
    const TCHAR* AliasInputs[]={TEXT("y"),TEXT("z"),TEXT("c"),TEXT("s"),TEXT("Y"),TEXT("Z"),TEXT("C"),TEXT("S")};
    for(int32 I=0;I<8;++I){Game->Match.findFighter(BodyId)->shapeCooldown=0;
        TestTrue(TEXT("Official comment accepts literal shape alias"),Comment(TEXT("viewer"),AliasInputs[I]));
        TestTrue(FString::Printf(TEXT("Official literal %s changes shape without changing team"),AliasInputs[I]),Game->Match.world.find(BodyId)->shape==static_cast<gw::Shape>(I%4) && Game->Viewers.FindChecked(TEXT("viewer")).Team==1);
    }
    Comment(TEXT("viewer"),TEXT("y"));
    TestTrue(TEXT("Official letter alias retains existing shape cooldown"),Game->Match.world.find(BodyId)->shape==gw::Shape::Triangle);
    TestTrue(TEXT("Joined identity retains present state"),Viewer->bPresent);
    Presence.MessageId=TEXT("old-leave");Presence.TimestampMs=199;Presence.EnterType=2;Presence.FollowStatus=0;Bridge->DeliverPresence(Presence);
    TestTrue(TEXT("Older presence cannot overwrite current state"),Game->GetViewers().FindChecked(TEXT("viewer")).bPresent);
    Presence.MessageId=TEXT("leave");Presence.TimestampMs=201;Bridge->DeliverPresence(Presence);
    TestFalse(TEXT("Leave updates room presence"),Game->GetViewers().FindChecked(TEXT("viewer")).bPresent);
    TestEqual(TEXT("Leave retains the existing fighter identity"),Game->GetViewers().FindChecked(TEXT("viewer")).BodyId,BodyId);
    TestTrue(TEXT("Leaving or cancellation never revokes one-time permanent rifle"),Rifle(TEXT("viewer")));
    FLiveLike LargeLike;LargeLike.Session=Provider->Session;LargeLike.UserId=TEXT("viewer");LargeLike.MessageId=TEXT("likes-101");LargeLike.Count=101;
    Game->Match.findFighter(BodyId)->hp=1;
    TestTrue(TEXT("Official provider accepts 101 new likes"),Bridge->DeliverLike(LargeLike));
    TestEqual(TEXT("Large like count saturates health without overflow"),Game->Match.findFighter(BodyId)->hp,Game->Match.findFighter(BodyId)->maxHp);
    LargeLike.MessageId=TEXT("likes-long");LargeLike.Count=MAX_int64;Game->Match.findFighter(BodyId)->hp=1;
    TestTrue(TEXT("Native official envelope preserves positive int64 count"),Bridge->DeliverLike(LargeLike));
    FLiveGift LargeGift;LargeGift.Session=Provider->Session;LargeGift.UserId=TEXT("viewer");LargeGift.MessageId=TEXT("gift-101");LargeGift.GiftName=TEXT("能力药丸");LargeGift.Count=101;
    const double BeforeFortify=Game->Match.bases[1].maxHp;
    TestTrue(TEXT("Official provider accepts 101 gift units"),Bridge->DeliverGift(LargeGift));
    TestEqual(TEXT("Official gift batch applies all 101 units"),Game->Match.bases[1].maxHp,BeforeFortify+101000.0);
    Game->Match.bases[1].alive=false;Game->Match.bases[1].hp=0;
    LargeGift.MessageId=TEXT("gift-1000");LargeGift.Count=1000;
    TestTrue(TEXT("Official provider accepts 1000 gift units"),Bridge->DeliverGift(LargeGift));
    TestEqual(TEXT("Destroyed base rebuild consumes one unit then fortifies remaining 999"),Game->Match.bases[1].maxHp,BeforeFortify+1100000.0);
    FLiveFollow Follow;Follow.Session=Provider->Session;Follow.MessageId=TEXT("wrong-target");Follow.UserId=TEXT("waiting");Follow.TargetUserId=TEXT("other-anchor");Follow.Action=1;Bridge->DeliverFollow(Follow);
    Comment(TEXT("waiting"),TEXT("2"));TestFalse(TEXT("Following another target does not grant rifle"),Rifle(TEXT("waiting")));
    Follow.TargetUserId=TEXT("anchor");Follow.MessageId=TEXT("follow-now");Bridge->DeliverFollow(Follow);
    TestTrue(TEXT("Following actual session anchor grants joined viewer rifle"),Rifle(TEXT("waiting")));
    Follow.UserId=TEXT("pending-follower");Follow.MessageId=TEXT("pending-follow");Bridge->DeliverFollow(Follow);
    TestFalse(TEXT("Follow does not auto-admit pending user"),Game->Viewers.Contains(Follow.UserId));
    Follow.Action=2;Follow.MessageId=TEXT("cancel-before-join");Bridge->DeliverFollow(Follow);Comment(Follow.UserId,TEXT("1"));
    TestFalse(TEXT("Canceled pending qualification does not grant on later join"),Rifle(Follow.UserId));
    Follow.Action=3;Follow.MessageId=TEXT("reciprocal-follow");Bridge->DeliverFollow(Follow);TestTrue(TEXT("Reciprocal follow qualifies once"),Rifle(Follow.UserId));
    Follow.Action=2;Follow.MessageId=TEXT("cancel-after-reward");Bridge->DeliverFollow(Follow);TestTrue(TEXT("Canceling after reward retains permanent unlock"),Rifle(Follow.UserId));
    const uint8 BeforeShare=Game->Match.findFighter(BodyId)->unlockedWeapons;
    FLiveShare Share;Share.Session=Provider->Session;Share.MessageId=TEXT("unverified-share");Share.UserId=TEXT("viewer");Bridge->DeliverShare(Share);
    TestEqual(TEXT("Production share never adds weapon entitlement"),Game->Match.findFighter(BodyId)->unlockedWeapons,BeforeShare);
    TestTrue(TEXT("Large official personal like delta unlocks shotgun"),(BeforeShare&gw::weaponBit(gw::WeaponKind::Shotgun))!=0);
    FString OldRed,OldBlue,OldGray;const bool HadRed=GConfig->GetString(TEXT("LiveInteraction"),TEXT("RedGroupId"),OldRed,GGameIni);const bool HadBlue=GConfig->GetString(TEXT("LiveInteraction"),TEXT("BlueGroupId"),OldBlue,GGameIni);const bool HadGray=GConfig->GetString(TEXT("LiveInteraction"),TEXT("GrayGroupId"),OldGray,GGameIni);
    GConfig->RemoveKey(TEXT("LiveInteraction"),TEXT("RedGroupId"),GGameIni);GConfig->RemoveKey(TEXT("LiveInteraction"),TEXT("BlueGroupId"),GGameIni);
    FLiveTeamSelection Team;Team.Session=Provider->Session;Team.MessageId=TEXT("unconfigured-selection");Team.UserId=TEXT("quick-select");Team.GroupId=TEXT("red-config");Bridge->DeliverTeamSelection(Team);
    TestFalse(TEXT("Missing group mapping does not invent a team"),Game->Viewers.Contains(Team.UserId));
    GConfig->SetString(TEXT("LiveInteraction"),TEXT("RedGroupId"),TEXT("red-config"),GGameIni);GConfig->SetString(TEXT("LiveInteraction"),TEXT("BlueGroupId"),TEXT("blue-config"),GGameIni);
    GConfig->SetString(TEXT("LiveInteraction"),TEXT("GrayGroupId"),TEXT("gray-config"),GGameIni);
    Team.MessageId=TEXT("mapped-selection");Bridge->DeliverTeamSelection(Team);
    TestEqual(TEXT("Configured quick selection follows ordinary red admission"),Game->Viewers.FindChecked(Team.UserId).Team,1);
    TestEqual(TEXT("No user-group command before accepted round contract"),Provider->Commands.Num(),0);
    Game->LiveRoundId=10001;Team.MessageId=TEXT("mapped-selection-round");Bridge->DeliverTeamSelection(Team);
    TestEqual(TEXT("Quick selection leaves group reporting exclusively to the round reporter"),Provider->Commands.Num(),0);
    Team.MessageId=TEXT("gray-admission");Team.UserId=TEXT("quick-gray");Team.GroupId=TEXT("gray-config");Bridge->DeliverTeamSelection(Team);
    TestEqual(TEXT("Official gray button admits a neutral participant"),Game->Viewers.FindChecked(Team.UserId).Team,0);
    const int32 GrayBody=Game->Viewers.FindChecked(Team.UserId).BodyId,BeforeRepeat=Game->Viewers.Num();
    Team.MessageId=TEXT("gray-repeat");Bridge->DeliverTeamSelection(Team);
    TestEqual(TEXT("Repeated gray button preserves fighter identity"),Game->Viewers.FindChecked(Team.UserId).BodyId,GrayBody);
    TestEqual(TEXT("Repeated gray button creates no duplicate participant"),Game->Viewers.Num(),BeforeRepeat);
    Team.MessageId=TEXT("red-cannot-return-gray");Team.UserId=TEXT("quick-select");Bridge->DeliverTeamSelection(Team);
    TestEqual(TEXT("Committed red team cannot return to neutral through platform button"),Game->Viewers.FindChecked(Team.UserId).Team,1);
    Team.MessageId=TEXT("unknown-selection");Team.UserId=TEXT("unknown-selector");Team.GroupId=TEXT("unknown-group");Bridge->DeliverTeamSelection(Team);
    TestFalse(TEXT("Unknown group does not invent neutral admission"),Game->Viewers.Contains(Team.UserId));
    GConfig->RemoveKey(TEXT("LiveInteraction"),TEXT("GrayGroupId"),GGameIni);
    Team.MessageId=TEXT("missing-gray-config");Team.UserId=TEXT("missing-gray-selector");Team.GroupId=TEXT("red-config");Bridge->DeliverTeamSelection(Team);
    TestFalse(TEXT("All three configured group IDs are required even for red selection"),Game->Viewers.Contains(Team.UserId));
    GConfig->SetString(TEXT("LiveInteraction"),TEXT("GrayGroupId"),TEXT("red-config"),GGameIni);
    Team.MessageId=TEXT("duplicate-group-config");Team.UserId=TEXT("duplicate-group-selector");Bridge->DeliverTeamSelection(Team);
    TestFalse(TEXT("Ambiguous gray/red mapping cannot admit a participant"),Game->Viewers.Contains(Team.UserId));
    GConfig->SetString(TEXT("LiveInteraction"),TEXT("GrayGroupId"),TEXT("gray-config"),GGameIni);
    if(GUsingNullRHI){Game->FlushRenderedLiveEvents();TestEqual(TEXT("NullRHI cannot acknowledge rendering"),Provider->Commands.Num(),0);}
    // Exercise the post-draw submission state machine separately from renderer
    // availability; the public NullRHI guard above remains mandatory.
    Game->PendingLiveAcks.Empty();
    FLiveLike AckLike;AckLike.Session=Provider->Session;AckLike.MessageId=TEXT("ack-retry");AckLike.UserId=TEXT("viewer");
    Bridge->DeliverLike(AckLike);
    TestEqual(TEXT("Handling an event does not immediately ACK"),Provider->Commands.Num(),0);
    Game->SubmitRenderedLiveAcks();
    const auto FirstAck=Provider->Commands.Last();
    TestEqual(TEXT("Post-draw ACK uses original official type"),FirstAck.Payload->GetStringField(TEXT("msg_type")),FString(TEXT("live_like")));
    TestEqual(TEXT("Submitted ACK remains awaiting actual platform result"),Game->InFlightLiveAcks.Num(),1);
    Bridge->ReportCommandResult(*Provider,FirstAck.Id,false,42);
    TestEqual(TEXT("ACK failure retains the handled event for retry"),Game->PendingLiveAcks.Num(),1);
    Game->PendingLiveAcks[0].RetryAfter=0;Game->SubmitRenderedLiveAcks();
    const auto RetriedAck=Provider->Commands.Last();
    TestNotEqual(TEXT("ACK retry has a new request identity"),RetriedAck.Id,FirstAck.Id);
    TestEqual(TEXT("ACK retry retains original message identity"),RetriedAck.Payload->GetStringField(TEXT("msg_id")),FString(TEXT("ack-retry")));
    Bridge->ReportCommandResult(*Provider,RetriedAck.Id,true,0);
    TestTrue(TEXT("Confirmed ACK drains pending and in-flight queues"),Game->PendingLiveAcks.IsEmpty() && Game->InFlightLiveAcks.IsEmpty());
    // Exercise the production SDK-host decoder, then the authenticated delivery
    // path. Review gifts are real platform callbacks even though no purchase occurred.
    const auto SdkGift=[&](const TCHAR* Id,const TCHAR* GiftName,int64 Count,bool IsTest,const FString& User=TEXT("viewer")) {
        FString Encrypted;GConfig->GetString(TEXT("DouyinLiveProvider.Gifts"),GiftName,Encrypted,GGameIni);
        auto Message=MakeShared<FJsonObject>(),Sender=MakeShared<FJsonObject>();
        Message->SetStringField(TEXT("msg_type"),TEXT("live_gift"));Message->SetStringField(TEXT("msg_id"),Id);
        Message->SetStringField(TEXT("gift_id"),Encrypted);Message->SetStringField(TEXT("count"),LexToString(Count));Message->SetBoolField(TEXT("is_test"),IsTest);
        Sender->SetStringField(TEXT("open_id"),User);Sender->SetStringField(TEXT("nickname"),TEXT("审核礼物"));Message->SetObjectField(TEXT("user"),Sender);
        FLiveGift Gift;FString Rejection;
        if(!TestTrue(TEXT("Configured SDK gift decodes through production adapter"),FDouyinGiftDecoder::Decode(Message,Provider->Session,Gift,Rejection)))return false;
        TestEqual(TEXT("SDK test provenance survives normalization"),Gift.bIsTestData,IsTest);
        return Bridge->DeliverGift(Gift);
    };
    const double ReviewBaseBefore=Game->Match.bases[1].maxHp;
    TestTrue(TEXT("Review ability pill x66 reaches gameplay"),SdkGift(TEXT("review-pill-66"),TEXT("能力药丸"),66,true));
    TestTrue(TEXT("Review ability pill x10 reaches gameplay"),SdkGift(TEXT("review-pill-10"),TEXT("能力药丸"),10,true));
    TestEqual(TEXT("Review batches fortify with all 76 units"),Game->Match.bases[1].maxHp,ReviewBaseBefore+76000.0);
    TestFalse(TEXT("Review gift replay cannot reapply its batch"),SdkGift(TEXT("review-pill-66"),TEXT("能力药丸"),66,true));
    const FString ReviewUser=TEXT("review-weapon-user"),ReviewKey=Game->ProgressKey(ReviewUser);
    TestTrue(TEXT("Review battery before joining is retained"),SdkGift(TEXT("review-battery-1"),TEXT("能量电池"),1,true,ReviewUser));
    TestTrue(TEXT("Review gift does not invent a participant"),!Game->Viewers.Contains(ReviewUser));
    Comment(ReviewUser,TEXT("2"));
    const auto& ReviewViewer=Game->Viewers.FindChecked(ReviewUser);
    const double WandMaxBefore=Game->Match.findFighter(ReviewViewer.BodyId)->maxHp;
    TestTrue(TEXT("Review living wands reach gameplay through the production SDK decoder"),SdkGift(TEXT("review-living-wands"),TEXT("仙女棒"),2,true,ReviewUser));
    TestEqual(TEXT("Review living wands add both units of maximum health"),Game->Match.findFighter(ReviewViewer.BodyId)->maxHp,WandMaxBefore+60.0);
    TestFalse(TEXT("SDK wand replay is rejected before increasing health"),SdkGift(TEXT("review-living-wands"),TEXT("仙女棒"),2,true,ReviewUser));
    TestTrue(TEXT("Paid living wand follows the same effect path"),SdkGift(TEXT("paid-living-wand"),TEXT("仙女棒"),1,false,ReviewUser));
    TestEqual(TEXT("Paid and review wand effects share exact unit accounting"),Game->Match.findFighter(ReviewViewer.BodyId)->maxHp,WandMaxBefore+90.0);
    TestTrue(TEXT("Review battery restores temporary rocket ownership after join"),(Game->Match.findFighter(ReviewViewer.BodyId)->unlockedWeapons&gw::weaponBit(gw::WeaponKind::RocketLauncher))!=0);
    Comment(ReviewUser,TEXT("武器6"));
    TestTrue(TEXT("Review rocket can be selected"),Game->Match.findFighter(ReviewViewer.BodyId)->weaponKind==gw::WeaponKind::RocketLauncher);
    TestFalse(TEXT("Review weapon never becomes a permanent purchase"),Game->Progress->WeaponUnlocks.Contains(ReviewKey));
    TestFalse(TEXT("Review selection never enters paid weapon save"),Game->Progress->SelectedLeftWeapons.Contains(ReviewKey));
    TestTrue(TEXT("Review callback isolates later session statistics"),Game->bPlatformTestSession);
    TestTrue(TEXT("Review gifts queue ordinary rendered fulfillment"),Game->PendingLiveAcks.ContainsByPredicate([](const auto& Ack){return Ack.MessageId==TEXT("review-battery-1")&&Ack.MessageType==TEXT("live_gift");}));
    if(GUsingNullRHI){const int32 BeforeNull=Provider->Commands.Num();Game->FlushRenderedLiveEvents();TestEqual(TEXT("Review gifts do not bypass NullRHI rendering guard"),Provider->Commands.Num(),BeforeNull);}
    Game->SubmitRenderedLiveAcks();
    TestTrue(TEXT("Post-render review ACK retains official gift ID and type"),Provider->Commands.ContainsByPredicate([](const auto& Command){return Command.Operation==TEXT("ack")&&Command.Payload->GetStringField(TEXT("msg_id"))==TEXT("review-battery-1")&&Command.Payload->GetStringField(TEXT("msg_type"))==TEXT("live_gift");}));
    TestTrue(TEXT("Paid battery in the same session still persists"),SdkGift(TEXT("paid-battery-1"),TEXT("能量电池"),1,false,ReviewUser));
    TestTrue(TEXT("Paid battery creates permanent ownership"),(Game->Progress->WeaponUnlocks.FindRef(ReviewKey)&gw::weaponBit(gw::WeaponKind::RocketLauncher))!=0);
    const auto SavedReview=Cast<UArenaProgressSave>(UGameplayStatics::LoadGameFromSlot(Slot,0));
    TestTrue(TEXT("Paid battery ownership survives disk save"),SavedReview&&(SavedReview->WeaponUnlocks.FindRef(ReviewKey)&gw::weaponBit(gw::WeaponKind::RocketLauncher))!=0);
    Comment(ReviewUser,TEXT("武器1"));
    const uint8 BeforeReviewChoice=Game->Progress->SelectedLeftWeapons.FindChecked(ReviewKey);
    TestTrue(TEXT("Review gift for already paid weapon still executes"),SdkGift(TEXT("review-owned-battery"),TEXT("能量电池"),1,true,ReviewUser));
    TestEqual(TEXT("Review auto-equip never replaces a saved paid selection"),Game->Progress->SelectedLeftWeapons.FindChecked(ReviewKey),BeforeReviewChoice);
    Game->Match.startNextRound();
    TestTrue(TEXT("Review weapon and statistics isolation persist across local round transitions"),Game->bPlatformTestSession&&(Game->Match.findFighter(ReviewViewer.BodyId)->unlockedWeapons&gw::weaponBit(gw::WeaponKind::RocketLauncher))!=0);
    Game->TickLiveRound();const auto ReviewStart=Provider->Commands.Last();
    TestEqual(TEXT("Test session still starts the official round lifecycle"),ReviewStart.Operation,FString(TEXT("round")));
    Bridge->ReportCommandResult(*Provider,ReviewStart.Id,true,0);
    Game->Match.phase=gw::Phase::Results;Game->Match.winnerTeam=1;Game->TickLiveRound();
    TestFalse(TEXT("Test-affected round has no backend or personal leaderboard uploads"),Game->LiveRoundReporter->Steps.ContainsByPredicate([](const auto& Step){return Step.Operation==TEXT("backend_round")||Step.Operation==TEXT("user_results")||Step.Operation==TEXT("room_rank");}));
    bool ReviewComplete=false;
    for(int32 Step=0;Step<32&&!ReviewComplete;++Step) {
        const auto Command=Provider->Commands.Last();
        TestTrue(TEXT("Review round submits only group and lifecycle operations"),Command.Operation==TEXT("user_group")||Command.Operation==TEXT("round")||Command.Operation==TEXT("complete"));
        if(Command.Operation==TEXT("round")&&Command.Payload->GetNumberField(TEXT("status"))==2)
            for(const auto& Group:Command.Payload->GetArrayField(TEXT("group_results")))TestEqual(TEXT("Review round cannot report a competitive group win"),Group->AsObject()->GetNumberField(TEXT("result")),3.0);
        ReviewComplete=Command.Operation==TEXT("complete");Bridge->ReportCommandResult(*Provider,Command.Id,true,0);
        if(!ReviewComplete)Game->TickLiveRound();
    }
    TestTrue(TEXT("Review round completion resumes normal intermission"),ReviewComplete&&Game->LiveRoundId>0);
    if(HadRed)GConfig->SetString(TEXT("LiveInteraction"),TEXT("RedGroupId"),*OldRed,GGameIni);else GConfig->RemoveKey(TEXT("LiveInteraction"),TEXT("RedGroupId"),GGameIni);
    if(HadBlue)GConfig->SetString(TEXT("LiveInteraction"),TEXT("BlueGroupId"),*OldBlue,GGameIni);else GConfig->RemoveKey(TEXT("LiveInteraction"),TEXT("BlueGroupId"),GGameIni);
    if(HadGray)GConfig->SetString(TEXT("LiveInteraction"),TEXT("GrayGroupId"),*OldGray,GGameIni);else GConfig->RemoveKey(TEXT("LiveInteraction"),TEXT("GrayGroupId"),GGameIni);
    Bridge->EndProviderSession(*Provider,TEXT("SDK_DISCONNECTED"));TestTrue(TEXT("Session switch clears spectator qualification cache"),Game->GetLiveViewerStates().IsEmpty());TestEqual(TEXT("Session invalidates platform round ID"),Game->LiveRoundId,int64(0));
    TestFalse(TEXT("Session switch clears review statistics isolation"),Game->bPlatformTestSession);
    TestTrue(TEXT("Session switch removes temporary review weapon ownership"),Game->PlatformTestWeaponUnlocks.IsEmpty());
    UGameplayStatics::DeleteGameInSlot(Slot,0);World->EndPlay(EEndPlayReason::Quit);World->DestroyWorld(false);Instance->Shutdown();GEngine->DestroyWorldContext(World);FLiveInteractionProviderRegistry::Unregister(Provider->GetPlatformId());
    return true;
}
#endif
