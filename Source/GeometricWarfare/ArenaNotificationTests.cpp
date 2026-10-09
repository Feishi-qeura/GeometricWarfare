#include "ArenaGameMode.h"
#include "LiveInteractionTestAdapter.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArenaNotificationTest,"GeometricWarfare.Arena.Notifications",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FArenaNotificationTest::RunTest(const FString&) {
    auto* Instance=NewObject<UGameInstance>(GEngine);Instance->InitializeStandalone(FName(TEXT("NoticeWorld")));
    FLiveInteractionTestAdapter::EnableLocalTest(*Instance->GetSubsystem<ULiveInteractionSubsystem>());
    auto* World=Instance->GetWorld();FURL Url;Url.AddOption(TEXT("game=/Script/GeometricWarfare.ArenaGameMode"));
    World->SetGameMode(Url);World->InitializeActorsForPlay(Url);World->BeginPlay();
    auto* Game=World->GetAuthGameMode<AArenaGameMode>();if(!TestNotNull(TEXT("Real notification fixture"),Game))return false;
    const FString Slot=TEXT("Notification_")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
    Game->ProgressSlot=Slot;Game->Progress=NewObject<UArenaProgressSave>(Game);Game->bProgressWritable=true;
    Game->ResetArena();Game->Match.config.autoCombat=false;Game->Match.config.autoCollect=false;
    auto* Bridge=Game->GetBridge();Bridge->SimulateComment(TEXT("notice-user"),TEXT("播报观众"),TEXT("1"));
    const int32 Id=Game->Viewers.FindChecked(TEXT("notice-user")).BodyId;
    Bridge->SimulateShare(TEXT("notice-user"),TEXT("播报观众"));
    TestFalse(TEXT("Sharing no longer grants shotgun"),(Game->Match.findFighter(Id)->unlockedWeapons&gw::weaponBit(gw::WeaponKind::Shotgun))!=0);
    for(int32 N=0;N<9;++N)Bridge->SimulateLike(TEXT("notice-user"),TEXT("播报观众"),100);
    Bridge->SimulateLike(TEXT("notice-user"),TEXT("播报观众"),99);
    TestFalse(TEXT("999 personal likes do not unlock shotgun"),(Game->Match.findFighter(Id)->unlockedWeapons&gw::weaponBit(gw::WeaponKind::Shotgun))!=0);
    Bridge->SimulateLike(TEXT("notice-user"),TEXT("播报观众"),1);
    TestTrue(TEXT("1000 personal likes auto unlock and equip shotgun"),Game->Match.findFighter(Id)->weaponKind==gw::WeaponKind::Shotgun);
    TestTrue(TEXT("Shotgun unlock has avatar weapon reward notice"),!Game->RewardNotices.IsEmpty()&&Game->RewardNotices[0].Active&&Game->RewardNotices[0].UserId==TEXT("notice-user")&&Game->RewardNotices[0].WeaponKind==gw::WeaponKind::Shotgun);
    auto* Fighter=Game->Match.findFighter(Id);Fighter->ammo=0;Fighter->reloadRemaining=4;
    Bridge->SimulateLike(TEXT("notice-user"),TEXT("播报观众"),100);
    TestTrue(TEXT("Post-threshold likes cannot refill or reset weapon"),Fighter->ammo==0&&Fighter->reloadRemaining==4);
    TestTrue(TEXT("Unlock was actually saved"),(Game->Progress->WeaponUnlocks.FindRef(Game->ProgressKey(TEXT("notice-user")))&gw::weaponBit(gw::WeaponKind::Shotgun))!=0);
    Game->Match.damageEnvironment(Id,100000);Bridge->SimulateGift(TEXT("notice-user"),TEXT("播报观众"),TEXT("仙女棒"));
    TestTrue(TEXT("Fairy wand has a visible paid gift notice"),Game->GetGiftNotice().Active&&Game->GetGiftNotice().GiftName==TEXT("仙女棒"));
    Game->TickGiftNotice(3);Bridge->SimulateGift(TEXT("notice-user"),TEXT("播报观众"),TEXT("能力药丸"));
    TestTrue(TEXT("Ability pill has a visible paid gift notice"),Game->GetGiftNotice().Active&&Game->GetGiftNotice().GiftName==TEXT("能力药丸"));
    TestTrue(TEXT("Join banner has stable identity, team and readable text"),!Game->JoinNotices.IsEmpty()&&Game->JoinNotices[0].Team==1&&Game->JoinNotices[0].Detail==TEXT("加入了红队"));
    Game->TickGiftNotice(6);
    const auto Units=[&](){int64 Total=0;for(const auto& N:Game->GiftNotices)if(N.Active)Total+=N.Count;for(const auto& N:Game->GiftNoticeQueue)Total+=N.Count;return Total;};
    FLiveGift Last;
    // Twelve distinct stable IDs deliberately share one nickname. Deliver
    // 100 individual callbacks each, not just one synthetic count=100 DTO.
    for(int32 U=0;U<12;++U)for(int32 N=0;N<100;++N){
        FLiveGift Gift;Gift.Session=Bridge->GetCurrentSession();Gift.UserId=FString::Printf(TEXT("combo-%d"),U);Gift.Nickname=TEXT("同名观众");Gift.GiftName=TEXT("能量电池");Gift.MessageId=FGuid::NewGuid().ToString();
        Bridge->DeliverGift(Gift);Last=Gift;
    }
    TestEqual(TEXT("Twelve viewers times100 gifts preserve all1200 units"),Units(),int64(1200));
    TestEqual(TEXT("Three senders display on independent gift lanes"),Game->GiftNotices.Num(),3);
    TestEqual(TEXT("Different identities with same nickname remain independent queued notices"),Game->GiftNoticeQueue.Num(),9);
    Bridge->DeliverGift(Last);TestEqual(TEXT("Replayed callback never increments combo count"),Units(),int64(1200));
    int64 Displayed=0;
    for(int32 Batch=0;Batch<8;++Batch){for(const auto& N:Game->GiftNotices)if(N.Active)Displayed+=N.Count;Game->TickGiftNotice(3);}
    TestEqual(TEXT("Every queued sender eventually displays exactly its counted contribution"),Displayed,int64(1200));
    TestTrue(TEXT("Combo queue fully drains, no infinite age extension"),Game->GiftNoticeQueue.IsEmpty()&&!Game->GetGiftNotice().Active);
    for(int32 U=0;U<600;++U){FLiveGift Gift;Gift.Session=Bridge->GetCurrentSession();Gift.UserId=FString::Printf(TEXT("crowd-%d"),U);Gift.Nickname=TEXT("压力观众");Gift.GiftName=TEXT("仙女棒");Gift.Count=100;Gift.MessageId=FGuid::NewGuid().ToString();Bridge->DeliverGift(Gift);}
    TestEqual(TEXT("600 senders times100 preserve60000 paid units under overload"),Units(),int64(60000));
    TestTrue(TEXT("Overflow combines explicit collective notices with bounded queue"),Game->GiftNoticeQueue.Num()<300);
    Game->TickGiftNotice(3);Game->GiftNotices.Reset();Game->GiftNoticeQueue.Reset();
    int64 ExpiredUnits=0;TSet<FString> ShownSenders;
    const auto Advance=[&](){
        for(const auto& N:Game->GiftNotices)if(N.Active){ShownSenders.Add(N.UserId);if(N.Age+.1f>=2.8f)ExpiredUnits+=N.Count;}
        Game->TickGiftNotice(.1f);
    };
    for(int32 Step=0;Step<100;++Step){
        for(int32 U=0;U<4;++U){FLiveGift Gift;Gift.Session=Bridge->GetCurrentSession();Gift.UserId=FString::Printf(TEXT("timed-combo-%d"),U);Gift.Nickname=TEXT("持续送礼");Gift.GiftName=TEXT("能量电池");Gift.MessageId=FGuid::NewGuid().ToString();Bridge->DeliverGift(Gift);}
        Advance();
    }
    TestEqual(TEXT("Interleaved100 gifts per4 viewers across10 display seconds preserve400 units"),ExpiredUnits+Units(),int64(400));
    TestEqual(TEXT("Continuous sender cannot starve any of the four identities"),ShownSenders.Num(),4);
    for(int32 Step=0;Step<100 && (Game->GetGiftNotice().Active||!Game->GiftNoticeQueue.IsEmpty());++Step)Advance();
    TestEqual(TEXT("Timed continuous combos drain all400 units without restarting ages forever"),ExpiredUnits,int64(400));
    TestTrue(TEXT("No timed combo remains stuck"),!Game->GetGiftNotice().Active&&Game->GiftNoticeQueue.IsEmpty());
    Game->Match.boss.active=true;Game->Match.boss.hp=1;Game->Match.boss.maxHp=32000;
    Game->Match.damageBoss(Id,1000);Game->ConsumeEvents();
    bool BossShown=false;for(const auto& N:Game->RewardNotices)if(N.Active&&N.Visual==EArenaNoticeVisual::Boss)BossShown=N.Team==1&&N.WeaponName==TEXT("强化");
    for(const auto& N:Game->RewardNoticeQueue)if(N.Visual==EArenaNoticeVisual::Boss)BossShown=N.Team==1;
    TestTrue(TEXT("Actual boss death produces team coloured boss reward notice"),BossShown);
    Game->JoinNotices.Reset();Game->JoinNoticeQueue.Reset();
    const auto ActionUnits=[&](){int64 Total=0;for(const auto& N:Game->JoinNotices)if(N.Active)Total+=N.Count;for(const auto& N:Game->JoinNoticeQueue)Total+=N.Count;return Total;};
    Bridge->SimulateComment(TEXT("action-user"),TEXT("播报测试"),TEXT("加入"));
    const auto JoinVisual=Game->JoinNotices[0].Visual;
    TestTrue(TEXT("Joining keeps avatar identity and grey camp announcement"),Game->JoinNotices[0].UserId==TEXT("action-user")&&Game->JoinNotices[0].Team==0&&Game->JoinNotices[0].Detail==TEXT("加入了灰队"));
    Bridge->SimulateComment(TEXT("action-user"),TEXT("播报测试"),TEXT("1"));
    const auto* TeamNotice=Game->JoinNotices.FindByPredicate([](const FGiftNotice& N){return N.Active&&N.UserId==TEXT("action-user")&&N.Detail==TEXT("选择了红队");});
    FGiftNotice TeamTemplate;if(TeamNotice)TeamTemplate=*TeamNotice;
    TestTrue(TEXT("Actual team selection is a distinct rolling announcement"),TeamNotice&&TeamNotice->Team==1&&TeamNotice->Visual!=JoinVisual);
    const int64 AfterTeam=ActionUnits();
    Bridge->SimulateComment(TEXT("action-user"),TEXT("播报测试"),TEXT("加入"));
    TestEqual(TEXT("Repeated participation cannot create another join announcement"),ActionUnits(),AfterTeam);
    Bridge->SimulateComment(TEXT("action-user"),TEXT("播报测试"),TEXT("武器3"));
    TestEqual(TEXT("Locked weapon command has no success announcement"),ActionUnits(),AfterTeam);
    const int32 ActionId=Game->Viewers.FindChecked(TEXT("action-user")).BodyId;
    Game->Match.findFighter(ActionId)->unlockedWeapons|=gw::weaponBit(gw::WeaponKind::Rifle);
    Bridge->SimulateComment(TEXT("action-user"),TEXT("播报测试"),TEXT("武器3"));
    const auto* WeaponNotice=Game->JoinNotices.FindByPredicate([](const FGiftNotice& N){return N.Active&&N.UserId==TEXT("action-user")&&N.Detail==TEXT("左手切换为步枪");});
    TestTrue(TEXT("Literal weapon3 produces identity camp and rifle-icon rolling announcement"),WeaponNotice&&WeaponNotice->Team==1&&WeaponNotice->WeaponKind==gw::WeaponKind::Rifle&&WeaponNotice->WeaponName==TEXT("步枪"));
    TestEqual(TEXT("Successful weapon command produces one visible announcement"),ActionUnits(),AfterTeam+1);
    Bridge->SimulateComment(TEXT("action-user"),TEXT("播报测试"),TEXT("武器3"));
    TestEqual(TEXT("Already-equipped weapon command cannot spam announcements"),ActionUnits(),AfterTeam+1);
    FLiveComment Replayed;Replayed.Session=Bridge->GetCurrentSession();Replayed.MessageId=FGuid::NewGuid().ToString();Replayed.UserId=TEXT("action-user");Replayed.Content=TEXT("武器1");
    Bridge->DeliverComment(Replayed);const int64 AfterSwitch=ActionUnits();Bridge->DeliverComment(Replayed);
    TestEqual(TEXT("Replayed SDK event cannot duplicate a switch announcement"),ActionUnits(),AfterSwitch);
    if(WeaponNotice && TeamTemplate.Active) {
        const FGiftNotice WeaponTemplate=*WeaponNotice;
        Game->JoinNotices.Reset();Game->JoinNoticeQueue.Reset();
        for(int32 I=0;I<300;++I){auto N=WeaponTemplate;N.UserId=TEXT("switch-crowd-")+FString::FromInt(I);N.ViewerName=TEXT("同名观众");N.Count=1;Game->QueueNotice(Game->JoinNotices,Game->JoinNoticeQueue,MoveTemp(N),3,5.5f);}
        TestEqual(TEXT("Switch overload preserves all successful action counts"),ActionUnits(),int64(300));
        const auto* CollectiveSwitch=Game->JoinNoticeQueue.FindByPredicate([](const FGiftNotice& N){return N.UserId.IsEmpty();});
        TestTrue(TEXT("Collective switch remains left rifle switching instead of a reward or kill"),CollectiveSwitch&&CollectiveSwitch->Detail==TEXT("左手切换为步枪")&&CollectiveSwitch->WeaponKind==gw::WeaponKind::Rifle);
        for(int32 I=0;I<300;++I){auto N=TeamTemplate;N.UserId=TEXT("team-crowd-")+FString::FromInt(I);N.ViewerName=TEXT("同名观众");N.Count=1;Game->QueueNotice(Game->JoinNotices,Game->JoinNoticeQueue,MoveTemp(N),3,5.5f);}
        TestEqual(TEXT("Team and switch overload preserve counts independently"),ActionUnits(),int64(600));
        const auto* CollectiveTeam=Game->JoinNoticeQueue.FindByPredicate([](const FGiftNotice& N){return N.UserId.IsEmpty()&&N.Detail==TEXT("选择了红队");});
        TestTrue(TEXT("Collective camp selection preserves the selected camp"),CollectiveTeam&&CollectiveTeam->Team==1);
        TestTrue(TEXT("Action overload has bounded collective backlog"),Game->JoinNoticeQueue.Num()<270);
    }
    Game->GiftNotices.Reset();Game->GiftNoticeQueue.Reset();
    FGiftNotice Provenance;Provenance.UserId=TEXT("same-gift-user");Provenance.ViewerName=TEXT("同名观众");Provenance.GiftName=TEXT("能量电池");Provenance.Visual=EArenaNoticeVisual::Weapon;Provenance.Detail=TEXT("同文案测试");
    Game->EnqueueGiftNotice(Provenance);Provenance.bIsTestData=true;Game->EnqueueGiftNotice(Provenance);
    TestTrue(TEXT("Test and paid callbacks never merge despite identical sender gift and detail"),Game->GiftNotices[0].Active&&Game->GiftNotices[1].Active&&!Game->GiftNotices[0].bIsTestData&&Game->GiftNotices[1].bIsTestData&&Game->GiftNotices[0].Count==1&&Game->GiftNotices[1].Count==1);
    Game->GiftNotices.Reset();Game->GiftNoticeQueue.Reset();
    for(int32 I=0;I<600;++I){Provenance.UserId=TEXT("provenance-crowd-")+FString::FromInt(I);Provenance.bIsTestData=I%2==0;Game->EnqueueGiftNotice(Provenance);}
    TestEqual(TEXT("Mixed paid and platform test overload preserves all 600 units"),Units(),int64(600));
    const auto* TestCollective=Game->GiftNoticeQueue.FindByPredicate([](const FGiftNotice& N){return N.UserId.IsEmpty()&&N.bIsTestData;});
    const auto* PaidCollective=Game->GiftNoticeQueue.FindByPredicate([](const FGiftNotice& N){return N.UserId.IsEmpty()&&!N.bIsTestData;});
    TestTrue(TEXT("Test gift collective never promises permanent ownership"),TestCollective&&TestCollective->Detail==TEXT("平台测试：本会话解锁左手"));
    TestTrue(TEXT("Paid collective retains permanent entitlement independently"),PaidCollective&&PaidCollective->Detail==TEXT("永久解锁左手；重复送礼不补弹"));
    Game->GiftNotices.Reset();Game->GiftNoticeQueue.Reset();Game->RewardNotices.Reset();Game->RewardNoticeQueue.Reset();Game->JoinNotices.Reset();Game->JoinNoticeQueue.Reset();
    for(int32 I=0;I<6;++I){FGiftNotice N;N.UserId=TEXT("layout-gift-")+FString::FromInt(I);N.ViewerName=N.UserId;N.GiftName=TEXT("仙女棒");N.Count=I+1;Game->EnqueueGiftNotice(N);
        N.GiftName.Empty();Game->QueueNotice(Game->RewardNotices,Game->RewardNoticeQueue,N,2,3.2f);Game->QueueNotice(Game->JoinNotices,Game->JoinNoticeQueue,N,3,5.5f);}
    Game->TickGiftNotice(.8f);Game->SetNoticeCompactLayout(true);
    TestTrue(TEXT("Compact layout reserves exactly one active lane for each category"),Game->GiftNotices.Num()==1&&Game->RewardNotices.Num()==1&&Game->JoinNotices.Num()==1);
    TestEqual(TEXT("Shrinking active lanes preserves every gift unit"),Units(),int64(21));
    TestEqual(TEXT("Shrinking active lanes preserves every join unit"),ActionUnits(),int64(21));
    TestTrue(TEXT("Removed active sender is requeued ahead of waiting gifts with a full display lifetime"),Game->GiftNoticeQueue[0].UserId==TEXT("layout-gift-1")&&Game->GiftNoticeQueue[0].Age==0);
    Game->TickGiftNotice(.3f);const float RetainedAge=Game->GiftNotices[0].Age;Game->SetNoticeCompactLayout(true);
    TestEqual(TEXT("Repeated layout configuration cannot restart the active sender age"),Game->GiftNotices[0].Age,RetainedAge);
    int64 CompactDisplayed=0;TSet<FString> CompactSenders;
    for(int32 I=0;I<6;++I){if(Game->GiftNotices[0].Active){CompactDisplayed+=Game->GiftNotices[0].Count;CompactSenders.Add(Game->GiftNotices[0].UserId);}Game->TickGiftNotice(2.8f);}
    TestTrue(TEXT("One compact gift lane drains all six senders without dropping or duplicating units"),CompactDisplayed==21&&CompactSenders.Num()==6&&Game->GiftNoticeQueue.IsEmpty()&&!Game->GiftNotices[0].Active);
    Game->SetNoticeCompactLayout(false);
    TestTrue(TEXT("Restoring wide layout restores all independent lane budgets"),Game->GiftNotices.Num()==3&&Game->RewardNotices.Num()==2&&Game->JoinNotices.Num()==3);
    TestTrue(TEXT("Restoring wide layout fills free slots from waiting notices"),Game->JoinNotices[0].Active&&Game->JoinNotices[1].Active&&Game->JoinNotices[2].Active);
    Game->SetNoticeCompactLayout(true);Game->SetNoticeCompactLayout(false);
    TestEqual(TEXT("Repeated narrow-wide transitions preserve remaining joined viewer units"),ActionUnits(),int64(15));
    Game->HandleSessionChanged();TestTrue(TEXT("New live session clears all combo queues and personal partial likes"),Game->GiftNoticeQueue.IsEmpty()&&Game->GiftNotices.IsEmpty()&&Game->PersonalLikes.IsEmpty());
    UGameplayStatics::DeleteGameInSlot(Slot,0);World->EndPlay(EEndPlayReason::Quit);World->DestroyWorld(false);Instance->Shutdown();GEngine->DestroyWorldContext(World);
    return true;
}
#endif
