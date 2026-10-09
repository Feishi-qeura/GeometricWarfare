#include "ArenaGameMode.h"
#include "LiveInteractionTestAdapter.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArenaDualHandIntegrationTest,"GeometricWarfare.Arena.DualHandIntegration",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FArenaDualHandIntegrationTest::RunTest(const FString&) {
    const FString Slot=TEXT("DualHand_")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
    auto* Instance=NewObject<UGameInstance>(GEngine);Instance->InitializeStandalone(FName(*Slot));
    FLiveInteractionTestAdapter::EnableLocalTest(*Instance->GetSubsystem<ULiveInteractionSubsystem>());
    auto* World=Instance->GetWorld();FURL Url;Url.AddOption(TEXT("game=/Script/GeometricWarfare.ArenaGameMode"));
    World->SetGameMode(Url);World->InitializeActorsForPlay(Url);World->BeginPlay();
    auto* Game=World->GetAuthGameMode<AArenaGameMode>();
    if(!TestNotNull(TEXT("Dual-hand fixture uses real GameMode"),Game))return false;
    Game->ProgressSlot=Slot;Game->Progress=NewObject<UArenaProgressSave>(Game);Game->bProgressWritable=true;Game->bProgressDirty=false;
    Game->ResetArena();Game->Match.config.autoCombat=false;Game->Match.config.autoCollect=false;
    auto* Bridge=Game->GetBridge();const FString User=TEXT("dual-hand-user"),Name=TEXT("双持观众");
    Bridge->SimulateComment(User,Name,TEXT("1"));const int32 Id=Game->Viewers.FindChecked(User).BodyId;
    Bridge->SimulateGift(User,Name,TEXT("魔法镜"));Bridge->SimulateComment(User,Name,TEXT("武器1"));
    auto* Fighter=Game->Match.findFighter(Id);Fighter->ammo=7;Fighter->reloadRemaining=1.25;
    Game->Match.events.clear();Game->RewardNotices.Reset();Game->RewardNoticeQueue.Reset();
    Game->Match.weaponCrates.push_back({{2000,2000},gw::WeaponKind::Sniper,true,0});
    TestTrue(TEXT("Owned sniper crate is destroyed by real damage path"),Game->Match.damageWeaponCrate(Id,0,500));
    TestTrue(TEXT("Owned sniper crate keeps permanently chosen pistol in left hand"),Fighter->weaponKind==gw::WeaponKind::Pistol);
    TestTrue(TEXT("Owned sniper still grants a sixty-second right-hand lease"),Fighter->temporaryWeaponKind==gw::WeaponKind::Sniper&&FMath::IsNearlyEqual(Fighter->temporaryWeaponRemaining,60.0));
    TestTrue(TEXT("Right crate cannot refill or cancel left pistol reload"),Fighter->ammo==7&&FMath::IsNearlyEqual(Fighter->reloadRemaining,1.25));
    Game->Match.step(60);Game->ConsumeEvents();
    const auto* CrateNotice=Game->RewardNotices.FindByPredicate([](const FGiftNotice& N){return N.Active&&N.WeaponKind==gw::WeaponKind::Sniper;});
    TestTrue(TEXT("Delayed crate notice retains pickup-time right temporary duration and death expiry"),CrateNotice&&CrateNotice->Detail.Contains(TEXT("右手"))&&CrateNotice->Detail.Contains(TEXT("60"))&&CrateNotice->Detail.Contains(TEXT("阵亡")));
    TestTrue(TEXT("Expired crate keeps a distinct temporary reward visual"),CrateNotice&&CrateNotice->Visual==EArenaNoticeVisual::TemporaryWeapon);
    Game->Match.weaponCrates.push_back({{2000,2000},gw::WeaponKind::MachineGun,true,0});
    TestTrue(TEXT("Unowned machinegun crate grants only right eligibility"),Game->Match.damageWeaponCrate(Id,static_cast<int>(Game->Match.weaponCrates.size())-1,500));
    Bridge->SimulateComment(User,Name,TEXT("武器5"));
    TestTrue(TEXT("Literal weapon5 cannot promote right-only machinegun to left"),Fighter->weaponKind==gw::WeaponKind::Pistol&&Fighter->temporaryWeaponKind==gw::WeaponKind::MachineGun&&Fighter->temporaryWeaponRemaining==60);
    Fighter->rightWeapon.ammo=11;Fighter->rightWeapon.reloadRemaining=2.2;Fighter->rightWeapon.shotRemaining=.4;Fighter->rightWeapon.aimRemaining=.9;Fighter->temporaryWeaponRemaining=12.3;
    Game->Match.events.clear();Game->Feed.reset();Game->GiftNotices.Reset();Game->GiftNoticeQueue.Reset();Game->JoinNotices.Reset();Game->JoinNoticeQueue.Reset();
    FLiveGift Gift;Gift.Session=Bridge->GetCurrentSession();Gift.MessageId=FGuid::NewGuid().ToString();Gift.UserId=User;Gift.Nickname=Name;Gift.GiftName=TEXT("甜甜圈");
    TestTrue(TEXT("Real gift grants permanent left machinegun"),Bridge->DeliverGift(Gift));Game->ConsumeEvents();
    TestTrue(TEXT("Gift neither promotes nor resets matching right lease"),Fighter->weaponKind==gw::WeaponKind::MachineGun&&Fighter->temporaryWeaponKind==gw::WeaponKind::MachineGun&&FMath::IsNearlyEqual(Fighter->temporaryWeaponRemaining,12.3)&&Fighter->rightWeapon.ammo==11&&FMath::IsNearlyEqual(Fighter->rightWeapon.reloadRemaining,2.2)&&FMath::IsNearlyEqual(Fighter->rightWeapon.shotRemaining,.4)&&FMath::IsNearlyEqual(Fighter->rightWeapon.aimRemaining,.9));
    TestTrue(TEXT("Gift has one paid notice without an extra manual switch announcement"),Game->GetGiftNotice().Active&&Game->GetGiftNotice().Count==1&&Game->GiftNoticeQueue.IsEmpty()&&Game->JoinNoticeQueue.IsEmpty()&&Game->Feed.pending()==0);
    Fighter->ammo=3;Fighter->reloadRemaining=1.4;
    TestFalse(TEXT("Duplicate gift SDK ID is rejected"),Bridge->DeliverGift(Gift));
    TestTrue(TEXT("Duplicate gift changes neither hand ammo nor paid count"),Fighter->ammo==3&&FMath::IsNearlyEqual(Fighter->reloadRemaining,1.4)&&Fighter->rightWeapon.ammo==11&&Game->GetGiftNotice().Count==1);
    Bridge->SimulateComment(User,Name,TEXT("武器5"));
    TestTrue(TEXT("Already selected permanent weapon preserves current ammo and reload"),Fighter->ammo==3&&FMath::IsNearlyEqual(Fighter->reloadRemaining,1.4));
    Bridge->SimulateComment(User,Name,TEXT("武器4"));TestTrue(TEXT("Literal weapon4 chooses owned left sniper"),Fighter->weaponKind==gw::WeaponKind::Sniper);
    TestTrue(TEXT("Chosen permanent weapon saves"),Game->SaveProgress());
    auto* Disk=Cast<UArenaProgressSave>(UGameplayStatics::LoadGameFromSlot(Slot,0));
    TestTrue(TEXT("Permanent sniper entitlement remains on disk"),Disk&&(Disk->WeaponUnlocks.FindRef(Game->ProgressKey(User))&gw::weaponBit(gw::WeaponKind::Sniper))!=0);
    TestTrue(TEXT("Disk stores sniper choice independently of active right machinegun"),Disk&&Disk->SelectedLeftWeapons.FindRef(Game->ProgressKey(User))==3);
    Game->Progress=Disk;Game->ResetArena();Game->Match.config.autoCombat=false;Game->Match.config.autoCollect=false;
    Bridge->SimulateComment(User,Name,TEXT("1"));const int32 RestoredId=Game->Viewers.FindChecked(User).BodyId;
    TestTrue(TEXT("Disk restoration restores chosen permanent left sniper"),Game->Match.findFighter(RestoredId)->weaponKind==gw::WeaponKind::Sniper);
    TestTrue(TEXT("Disk restoration never restores a right temporary weapon"),Game->Match.findFighter(RestoredId)->temporaryWeaponRemaining==0);
    const FString Key=Game->ProgressKey(User);
    // Version 1 remains writable: an old save with no selection entry defaults
    // to pistol; corrupt or no-longer-owned selections have the same fallback.
    Game->Progress->SelectedLeftWeapons.Remove(Key);Game->SaveProgress();
    Game->Progress=Cast<UArenaProgressSave>(UGameplayStatics::LoadGameFromSlot(Slot,0));
    TestTrue(TEXT("Version1 save without a selection remains readable"),Game->Progress&&Game->Progress->Version==1&&!Game->Progress->SelectedLeftWeapons.Contains(Key));
    for(int32 Variant=0;Variant<3;++Variant){
        if(Variant==1)Game->Progress->SelectedLeftWeapons.Add(Key,255);
        if(Variant==2)Game->Progress->SelectedLeftWeapons.Add(Key,5);
        Game->ResetArena();Game->Match.config.autoCombat=false;Game->Match.config.autoCollect=false;Bridge->SimulateComment(User,Name,TEXT("1"));
        const auto* Restored=Game->Match.findFighter(Game->Viewers.FindChecked(User).BodyId);
        TestTrue(TEXT("Missing invalid or locked saved choice defaults to permanent pistol"),Restored&&Restored->weaponKind==gw::WeaponKind::Pistol&&Restored->temporaryWeaponRemaining==0);
    }
    Fighter=Game->Match.findFighter(Game->Viewers.FindChecked(User).BodyId);
    Game->bProgressWritable=false;AddExpectedError(TEXT("Weapon progress save failed in slot"),EAutomationExpectedErrorFlags::Contains,1);
    Bridge->SimulateComment(User,Name,TEXT("武器4"));
    TestTrue(TEXT("Save failure retains changed left choice in memory for retry"),Game->bProgressDirty&&Fighter->weaponKind==gw::WeaponKind::Sniper&&Game->Progress->SelectedLeftWeapons.FindRef(Key)==3);
    Bridge->SimulateComment(User,Name,TEXT("武器4"));Game->bProgressWritable=true;Game->TickProgressSave(5);
    Disk=Cast<UArenaProgressSave>(UGameplayStatics::LoadGameFromSlot(Slot,0));
    TestTrue(TEXT("Existing five-second retry commits left choice without replaying command"),Disk&&Disk->SelectedLeftWeapons.FindRef(Key)==3&&!Game->bProgressDirty);
    Fighter->hp=Fighter->maxHp-1;Fighter->hitFlash=0;
    FLiveLike Like;Like.Session=Bridge->GetCurrentSession();Like.MessageId=FGuid::NewGuid().ToString();Like.UserId=User;Like.Nickname=Name;Like.Count=1;
    TestTrue(TEXT("Validated bridge like triggers healing green flash"),Bridge->DeliverLike(Like)&&Fighter->healFlash>.69&&Fighter->hp==Fighter->maxHp);
    Game->Match.step(.2);const double FlashAfterTick=Fighter->healFlash;
    TestFalse(TEXT("Duplicate like SDK ID is rejected"),Bridge->DeliverLike(Like));
    TestTrue(TEXT("Duplicate like cannot restart healing flash"),Fighter->healFlash==FlashAfterTick&&FlashAfterTick>0&&FlashAfterTick<.7);
    Game->Match.step(.6);TestTrue(TEXT("Healing flash expires under real simulation ticking"),Fighter->healFlash==0);
    Like.MessageId=FGuid::NewGuid().ToString();Bridge->DeliverLike(Like);TestTrue(TEXT("Full-health like does not show false healing flash"),Fighter->healFlash==0);
    Game->RewardNotices.Reset();Game->RewardNoticeQueue.Reset();
    for(int32 I=0;I<300;++I){const FString Crowd=TEXT("crate-crowd-")+FString::FromInt(I);Game->EnqueueRewardNotice(Crowd,gw::WeaponKind::Sniper,TEXT("武器箱奖励：右手临时60秒，阵亡或到期消失"),EArenaNoticeVisual::TemporaryWeapon);}
    for(int32 I=0;I<300;++I){const FString Crowd=TEXT("permanent-crowd-")+FString::FromInt(I);Game->EnqueueRewardNotice(Crowd,gw::WeaponKind::Sniper,TEXT("永久解锁左手"));}
    const auto* CollectiveTemporary=Game->RewardNoticeQueue.FindByPredicate([](const FGiftNotice& N){return N.UserId.IsEmpty()&&N.Visual==EArenaNoticeVisual::TemporaryWeapon;});
    const auto* CollectivePermanent=Game->RewardNoticeQueue.FindByPredicate([](const FGiftNotice& N){return N.UserId.IsEmpty()&&N.Visual==EArenaNoticeVisual::Weapon;});
    TestTrue(TEXT("Overload never combines temporary crates with permanent unlocks"),CollectiveTemporary&&CollectivePermanent&&CollectiveTemporary->Detail.Contains(TEXT("右手"))&&CollectiveTemporary->Detail.Contains(TEXT("60"))&&CollectiveTemporary->Detail.Contains(TEXT("阵亡")));
    int64 TemporaryUnits=0,PermanentUnits=0;
    const auto CountReward=[&](const FGiftNotice& N){if(N.Visual==EArenaNoticeVisual::TemporaryWeapon)TemporaryUnits+=N.Count;else if(N.Visual==EArenaNoticeVisual::Weapon)PermanentUnits+=N.Count;};
    for(const auto& N:Game->RewardNotices)if(N.Active)CountReward(N);for(const auto& N:Game->RewardNoticeQueue)CountReward(N);
    TestEqual(TEXT("Overload preserves all right crate units"),TemporaryUnits,int64(300));TestEqual(TEXT("Overload preserves all permanent unlock units"),PermanentUnits,int64(300));
    Game->bProgressDirty=false;UGameplayStatics::DeleteGameInSlot(Slot,0);
    World->EndPlay(EEndPlayReason::Quit);World->DestroyWorld(false);Instance->Shutdown();GEngine->DestroyWorldContext(World);
    return true;
}
#endif
