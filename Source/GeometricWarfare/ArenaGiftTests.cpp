#include "ArenaGameMode.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArenaGiftCombatTest,"GeometricWarfare.Arena.GiftCombat",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FArenaGiftCombatTest::RunTest(const FString&)
{
    bool Passed=true;
    const auto Verify=[&](const TCHAR* Message,bool Condition){Passed=TestTrue(Message,Condition)&&Passed;return Condition;};
    const FString TestSlot=TEXT("ArenaGiftAutomation_")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
    auto* Instance=NewObject<UGameInstance>(GEngine);
    Instance->InitializeStandalone(FName(*TestSlot));
    UWorld* World=Instance->GetWorld();
    const auto Cleanup=[&](){World->EndPlay(EEndPlayReason::Quit);World->DestroyWorld(false);Instance->Shutdown();GEngine->DestroyWorldContext(World);};
    FURL Url;Url.AddOption(TEXT("game=/Script/GeometricWarfare.ArenaGameMode"));
    World->SetGameMode(Url);World->InitializeActorsForPlay(Url);World->BeginPlay();
    auto* Game=World->GetAuthGameMode<AArenaGameMode>();
    if(!Verify(TEXT("Gift fixture starts the real Arena GameMode"),Game!=nullptr)){Cleanup();return false;}
    // BeginPlay may read the user's normal slot. Replace both references before
    // any operation that can write, and only delete this GUID-owned slot.
    Game->ProgressSlot=TestSlot;
    Game->Progress=NewObject<UArenaProgressSave>(Game);
    Game->bProgressWritable=true;
    Game->bProgressDirty=false;Game->ProgressRetryRemaining=5;
    if(!Verify(TEXT("Gift fixture owns a new unique save slot"),!UGameplayStatics::DoesSaveGameExist(TestSlot,0))){Cleanup();return false;}
    Game->ResetArena();Game->Match.config.autoCombat=false;Game->Match.config.autoCollect=false;
    auto* Bridge=Game->GetBridge();
    if(!Verify(TEXT("Gift fixture uses the real local bridge"),Bridge!=nullptr&&!Bridge->IsRelayMode())){Cleanup();return false;}
    const FString Name=TEXT("礼物测试"),UserA=TEXT("local-")+Name,UserB=TEXT("gift-independent-id");
    const auto NewGift=[&](const FString& User,const TCHAR* Gift,int32 Count=1){
        FDouyinGift Event;Event.MessageId=FGuid::NewGuid().ToString();Event.UserId=User;Event.Nickname=Name;Event.GiftName=Gift;Event.Count=Count;return Event;
    };
    const auto Comment=[&](const FString& User,const FString& Text){return Bridge->SimulateComment(User,Name,Text);};
    const auto Equipped=[&](int32 Id,gw::WeaponKind Kind){const auto* F=Game->Match.findFighter(Id);return F&&F->weaponKind==Kind;};
    const auto Unlocked=[&](int32 Id,gw::WeaponKind Kind){const auto* F=Game->Match.findFighter(Id);return F&&(F->unlockedWeapons&gw::weaponBit(Kind))!=0;};
    const auto Near=[](double A,double B){return FMath::IsNearlyEqual(A,B,.00001);};

    const FDouyinGift PendingMirror=NewGift(UserB,TEXT("魔法镜"));
    Verify(TEXT("Magic mirror is delivered before the sender joins"),Bridge->DeliverGift(PendingMirror));
    Verify(TEXT("A pending weapon gift does not invent a viewer or fighter"),Game->Viewers.Num()==0&&Game->Match.fighters.empty());
    Verify(TEXT("Pending unlock is keyed by stable sender ID"),(Game->Progress->WeaponUnlocks.FindRef(Game->ProgressKey(UserB))&gw::weaponBit(gw::WeaponKind::Sniper))!=0);
    Verify(TEXT("Same nickname never grants the other stable identity a pending weapon"),Game->Progress->WeaponUnlocks.FindRef(Game->ProgressKey(UserA))==0);
    Verify(TEXT("First gift commits a real save file"),UGameplayStatics::DoesSaveGameExist(TestSlot,0));
    Verify(TEXT("Pending gift starts a named weapon notice"),Game->GetGiftNotice().Active&&Game->GetGiftNotice().ViewerName==Name&&Game->GetGiftNotice().GiftName==TEXT("魔法镜"));
    Verify(TEXT("Identical gift event ID is rejected on replay"),!Bridge->DeliverGift(PendingMirror));
    Verify(TEXT("Replayed pending gift adds no duplicate notice"),Game->GiftNoticeQueue.IsEmpty());
    Verify(TEXT("First identity can join red"),Comment(UserA,TEXT("1")));
    Verify(TEXT("Pending gift identity can join blue under the same nickname"),Comment(UserB,TEXT("2")));
    if(!Verify(TEXT("Both stable viewer identities materialize independently"),Game->Viewers.Contains(UserA)&&Game->Viewers.Contains(UserB))){Cleanup();return false;}
    const int32 IdA=Game->Viewers.FindChecked(UserA).BodyId,IdB=Game->Viewers.FindChecked(UserB).BodyId;
    Verify(TEXT("New viewer begins with pistol only"),Game->Match.findFighter(IdA)->unlockedWeapons==gw::weaponBit(gw::WeaponKind::Pistol));
    Verify(TEXT("Joining restores the sender's previously saved sniper unlock"),Unlocked(IdB,gw::WeaponKind::Sniper)&&!Unlocked(IdA,gw::WeaponKind::Sniper));
    Comment(UserB,TEXT("武器+4"));Verify(TEXT("Restored pending sniper can be selected by comment"),Equipped(IdB,gw::WeaponKind::Sniper));

    Game->Match.weaponCrates.push_back({{4000,4000},gw::WeaponKind::MachineGun,true,0});
    Verify(TEXT("Destroying a500HP weapon crate equips its last attacker's temporary gun without unlocking it"),Game->Match.damageWeaponCrate(IdA,0,500)&&!Game->Match.weaponCrates[0].active&&Equipped(IdA,gw::WeaponKind::MachineGun)&&!Unlocked(IdA,gw::WeaponKind::MachineGun));
    Comment(UserA,TEXT("武器+1"));Comment(UserA,TEXT("武器+5"));
    Verify(TEXT("Comment switching can return to an active temporary gun"),Equipped(IdA,gw::WeaponKind::MachineGun));
    Verify(TEXT("Saving while a temporary gun is equipped succeeds"),Game->SaveProgress());
    auto* LeaseSave=Cast<UArenaProgressSave>(UGameplayStatics::LoadGameFromSlot(TestSlot,0));
    Verify(TEXT("Temporary gun is excluded from persisted weapon entitlements"),LeaseSave&&(LeaseSave->WeaponUnlocks.FindRef(Game->ProgressKey(UserA))&gw::weaponBit(gw::WeaponKind::MachineGun))==0);
    Game->Match.step(gw::TemporaryWeaponLifetime);
    Verify(TEXT("Expired temporary gun returns to owned pistol"),Equipped(IdA,gw::WeaponKind::Pistol));
    Comment(UserA,TEXT("武器+5"));
    Verify(TEXT("An expired crate no longer permits comment selection"),Equipped(IdA,gw::WeaponKind::Pistol));

    Verify(TEXT("Magic mirror equips sniper through actual gift delegate"),Bridge->DeliverGift(NewGift(UserA,TEXT("魔法镜")))&&Equipped(IdA,gw::WeaponKind::Sniper));
    Verify(TEXT("Donut equips machinegun through actual gift delegate"),Bridge->DeliverGift(NewGift(UserA,TEXT("甜甜圈")))&&Equipped(IdA,gw::WeaponKind::MachineGun));
    const FDouyinGift Battery=NewGift(UserA,TEXT("能量电池"));
    Verify(TEXT("Energy battery equips rocket launcher through actual gift delegate"),Bridge->DeliverGift(Battery)&&Equipped(IdA,gw::WeaponKind::RocketLauncher));
    auto* Fighter=Game->Match.findFighter(IdA);Fighter->ammo=0;Fighter->reloadRemaining=4;
    const int32 QueuedBeforeReplay=Game->GiftNoticeQueue.Num();
    Verify(TEXT("Equipped weapon gift replay is rejected"),!Bridge->DeliverGift(Battery));
    Verify(TEXT("Replayed battery cannot refill ammo, skip reload, or repeat its notice"),Fighter->ammo==0&&Near(Fighter->reloadRemaining,4)&&Game->GiftNoticeQueue.Num()==QueuedBeforeReplay);
    Bridge->DeliverGift(NewGift(UserA,TEXT("能量电池")));
    Verify(TEXT("A different gift event for an already owned weapon still cannot refill ammo"),Fighter->ammo==0&&Near(Fighter->reloadRemaining,4));
    Verify(TEXT("Weapon gifts keep the unrelated identity unchanged"),!Unlocked(IdB,gw::WeaponKind::MachineGun)&&!Unlocked(IdB,gw::WeaponKind::RocketLauncher));
    for(const TCHAR* Gift:{TEXT("仙女棒"),TEXT("能力药丸"),TEXT("魔法镜"),TEXT("甜甜圈"),TEXT("能量电池")}){
        auto* Icon=Game->GetGiftIcon(Gift);
        Verify(*FString::Printf(TEXT("Real gift icon is available: %s"),Gift),Icon!=nullptr);
        Verify(*FString::Printf(TEXT("Gift icon reuses its cached texture: %s"),Gift),Icon==Game->GetGiftIcon(Gift));
    }
    Verify(TEXT("Unknown gift cannot load an arbitrary icon path"),Game->GetGiftIcon(TEXT("不支持的礼物"))==nullptr);

    auto& RedBase=Game->Match.bases[1];RedBase.hp=1800;
    const FDouyinGift Pills=NewGift(UserA,TEXT("能力药丸"),2);
    Verify(TEXT("Ability pills are delivered as two new units"),Bridge->DeliverGift(Pills));
    Verify(TEXT("Two pills add2000 to both living base current and maximum health"),Near(RedBase.hp,3800)&&Near(RedBase.maxHp,4500));
    Verify(TEXT("Ability pill event cannot be replayed"),!Bridge->DeliverGift(Pills));
    Verify(TEXT("Pill replay does not stack extra fortification"),Near(RedBase.hp,3800)&&Near(RedBase.maxHp,4500));
    Game->Match.damageBase(IdB,1,100000);
    Verify(TEXT("Test attack destroys the fortified base"),!RedBase.alive&&Near(RedBase.hp,0));
    Bridge->DeliverGift(NewGift(UserA,TEXT("能力药丸")));
    Verify(TEXT("One pill rebuilds a destroyed base to its retained maximum"),RedBase.alive&&Near(RedBase.hp,4500)&&Near(RedBase.maxHp,4500));
    Game->Match.damageEnvironment(IdA,100000);
    Game->Match.step(Game->Match.config.sprintSeconds-Game->Match.elapsed);
    Verify(TEXT("Sprint entry revives a dead top-ranked viewer and grants hero health and armor"),Game->Match.findFighter(IdA)->alive&&Game->Match.findFighter(IdA)->heroBuff&&Near(Game->Match.findFighter(IdA)->hp,2100)&&Near(Game->Match.findFighter(IdA)->armor,300));
    Game->Match.damageEnvironment(IdA,100000);Game->Match.step(20);
    Verify(TEXT("Sprint leaves a dead viewer waiting and blocks ordinary revival"),!Game->Match.findFighter(IdA)->alive&&!Game->Match.revive(IdA));
    const FDouyinGift Wand=NewGift(UserA,TEXT("仙女棒"));
    Verify(TEXT("Fairy wand gift explicitly revives during Sprint"),Bridge->DeliverGift(Wand)&&Game->Match.findFighter(IdA)->alive);
    Verify(TEXT("Fairy wand preserves the revived viewer's hero stats"),Game->Match.findFighter(IdA)->heroBuff&&Near(Game->Match.findFighter(IdA)->hp,2100)&&Near(Game->Match.findFighter(IdA)->armor,300));
    Game->Match.damageEnvironment(IdA,100000);
    Verify(TEXT("Replayed wand cannot revive a second death"),!Bridge->DeliverGift(Wand)&&!Game->Match.findFighter(IdA)->alive);
    Bridge->DeliverGift(NewGift(UserA,TEXT("仙女棒")));
    Verify(TEXT("A distinct wand event can revive the new death"),Game->Match.findFighter(IdA)->alive);
    Bridge->DeliverGift(NewGift(UserA,TEXT("能力药丸"),3));
    Verify(TEXT("Pills neither rebuild nor fortify during Sprint"),!RedBase.alive&&Near(RedBase.hp,0)&&Near(RedBase.maxHp,4500));
    const uint8 MaskBeforeRound=Game->Match.findFighter(IdA)->unlockedWeapons;
    Game->Match.startNextRound();
    Verify(TEXT("Next round preserves all gifted weapon entitlements and selected weapon"),Game->Match.findFighter(IdA)->unlockedWeapons==MaskBeforeRound&&Equipped(IdA,gw::WeaponKind::RocketLauncher));
    Verify(TEXT("Next round refreshes selected rocket ammo and reload"),Game->Match.findFighter(IdA)->ammo==1&&Near(Game->Match.findFighter(IdA)->reloadRemaining,0));

    const FString Code=Game->GenerateRifleCode();
    Verify(TEXT("Local streamer creates a nonempty saved single-use rifle code"),!Code.IsEmpty()&&Game->Progress->UnusedRifleCodes.Contains(Code));
    Comment(UserA,TEXT("步枪+INVALID"));
    Verify(TEXT("Invalid code neither unlocks rifle nor consumes the valid code"),!Unlocked(IdA,gw::WeaponKind::Rifle)&&Game->Progress->UnusedRifleCodes.Contains(Code));
    Comment(UserA,TEXT("步枪+")+Code.ToLower());
    Verify(TEXT("Valid rifle code unlocks and equips rifle through comment parsing"),Unlocked(IdA,gw::WeaponKind::Rifle)&&Equipped(IdA,gw::WeaponKind::Rifle));
    Verify(TEXT("Rifle redemption consumes its saved code"),!Game->Progress->UnusedRifleCodes.Contains(Code));
    Comment(UserB,TEXT("步枪+")+Code);
    Verify(TEXT("A consumed code cannot unlock another viewer"),!Unlocked(IdB,gw::WeaponKind::Rifle));
    const FString SpareCode=Game->GenerateRifleCode();
    Comment(UserA,TEXT("步枪+")+SpareCode);
    Verify(TEXT("Already unlocked viewer does not consume another person's usable code"),!SpareCode.IsEmpty()&&Game->Progress->UnusedRifleCodes.Contains(SpareCode));
    Comment(UserB,TEXT("步枪＋")+SpareCode);
    Verify(TEXT("The preserved spare code remains redeemable by an eligible viewer"),Unlocked(IdB,gw::WeaponKind::Rifle)&&!Game->Progress->UnusedRifleCodes.Contains(SpareCode));

    // Follow the same GM generation -> bridge comment -> disk unlock path for
    // human-entered spellings, including the reported "步枪64CD3B1E" form.
    const TCHAR* RiflePrefixes[]={TEXT("步枪"),TEXT("步枪 "),TEXT("步枪 + "),TEXT("步枪 ＋ ")};
    for(int32 Variant=0;Variant<UE_ARRAY_COUNT(RiflePrefixes);++Variant) {
        const FString Redeemer=FString::Printf(TEXT("rifle-format-%d"),Variant);
        Comment(Redeemer,TEXT("加入"));
        if(!Verify(TEXT("Rifle format fixture joins through the real comment bridge"),Game->Viewers.Contains(Redeemer)))continue;
        const int32 RedeemerId=Game->Viewers.FindChecked(Redeemer).BodyId;
        const FString FormatCode=Game->GenerateRifleCode();
        Verify(TEXT("GM creates a saved code for each comment spelling"),!FormatCode.IsEmpty()&&Game->Progress->UnusedRifleCodes.Contains(FormatCode));
        Comment(Redeemer,TEXT("步枪"));
        Verify(TEXT("Missing rifle code explains the command format without consuming a code"),Game->LastEvent.Contains(TEXT("兑换格式"))&&!Unlocked(RedeemerId,gw::WeaponKind::Rifle)&&Game->Progress->UnusedRifleCodes.Contains(FormatCode));
        Comment(Redeemer,TEXT("步枪++")+FormatCode);
        Verify(TEXT("Repeated separators cannot redeem or consume a valid code"),!Unlocked(RedeemerId,gw::WeaponKind::Rifle)&&Game->Progress->UnusedRifleCodes.Contains(FormatCode));
        Comment(Redeemer,FString(TEXT("  "))+RiflePrefixes[Variant]+FormatCode.ToLower()+TEXT("  "));
        Verify(*FString::Printf(TEXT("Rifle spelling%d permanently unlocks and immediately equips45 rounds"),Variant),Unlocked(RedeemerId,gw::WeaponKind::Rifle)&&Equipped(RedeemerId,gw::WeaponKind::Rifle)&&Game->Match.findFighter(RedeemerId)->ammo==45);
        Verify(TEXT("A successful variant consumes its code exactly once"),!Game->Progress->UnusedRifleCodes.Contains(FormatCode));
        auto* RedeemedSave=Cast<UArenaProgressSave>(UGameplayStatics::LoadGameFromSlot(TestSlot,0));
        Verify(TEXT("Variant entitlement and code consumption persist together on disk"),RedeemedSave&&(RedeemedSave->WeaponUnlocks.FindRef(Game->ProgressKey(Redeemer))&gw::weaponBit(gw::WeaponKind::Rifle))!=0&&!RedeemedSave->UnusedRifleCodes.Contains(FormatCode));
        Comment(Redeemer,TEXT("武器1"));Comment(Redeemer,TEXT("武器3"));
        Verify(TEXT("Weapon3 can re-equip the rifle redeemed without a mandatory plus"),Equipped(RedeemerId,gw::WeaponKind::Rifle));
    }
    Comment(UserA,TEXT("武器+1"));Game->Feed.reset();Game->Match.events.clear();
    Comment(UserA,TEXT("武器+3"));
    Verify(TEXT("Weapon3 selects an already redeemed rifle without another code"),Equipped(IdA,gw::WeaponKind::Rifle));
    Verify(TEXT("Successful weapon switch enters the real scrolling feed once"),Game->GetFeed().pending()==1);
    Game->Feed.step(.001);
    bool VisibleSwitch=false;
    for(const auto& Slot:Game->GetFeed().slots)VisibleSwitch|=Slot.active&&Slot.text.Contains(Name)&&Slot.text.Contains(TEXT("切换"))&&Slot.text.Contains(TEXT("步枪"));
    Verify(TEXT("Scrolling feed exposes the viewer and selected weapon"),VisibleSwitch);
    Comment(UserA,TEXT("武器+3"));
    Verify(TEXT("Selecting the equipped weapon cannot enqueue another switch message"),Game->GetFeed().pending()==0);
    Bridge->SimulateShare(UserA,Name);
    Verify(TEXT("Sharing adds the sixth permanent weapon to gift and code unlocks"),Game->Match.findFighter(IdA)->unlockedWeapons==0x3f);

    Game->GiftNotice=FGiftNotice{};Game->GiftNoticeQueue.Reset();Game->bSimulationPaused=true;Game->SetDemoSpeed(4);
    Bridge->DeliverGift(NewGift(UserA,TEXT("魔法镜")));Bridge->DeliverGift(NewGift(UserA,TEXT("甜甜圈")));Bridge->DeliverGift(NewGift(UserA,TEXT("能量电池")));
    Verify(TEXT("Three weapon gifts show the first notice and queue two"),Game->GetGiftNotice().GiftName==TEXT("魔法镜")&&Game->GiftNoticeQueue.Num()==2);
    for(int32 I=0;I<7;++I)Game->Tick(.25f);
    Verify(TEXT("Gift notice remains on screen through1.75 display seconds despite4x simulation speed"),Game->GetGiftNotice().Active&&Game->GetGiftNotice().GiftName==TEXT("魔法镜")&&Near(Game->GetGiftNotice().Age,1.75));
    Game->Tick(.25f);
    Verify(TEXT("First notice hands off at exactly two display seconds"),Game->GetGiftNotice().GiftName==TEXT("甜甜圈")&&Near(Game->GetGiftNotice().Age,0)&&Game->GiftNoticeQueue.Num()==1);
    for(int32 I=0;I<8;++I)Game->Tick(.25f);
    Verify(TEXT("Second notice hands off to the battery in FIFO order"),Game->GetGiftNotice().GiftName==TEXT("能量电池")&&Game->GiftNoticeQueue.IsEmpty());
    for(int32 I=0;I<8;++I)Game->Tick(.25f);
    Verify(TEXT("Last notice clears after its own full two second lifetime"),!Game->GetGiftNotice().Active&&Game->GiftNoticeQueue.IsEmpty());
    Game->CameraCenter=FVector2D::ZeroVector;Game->FocusBodyId=-1;
    Game->FocusLocalViewer(TEXT(" ")+Name+TEXT(" "));
    const auto* FocusBody=Game->Match.world.find(IdA);
    Verify(TEXT("Local focus resolves local nickname identity, not another viewer with the same display name"),Game->FocusBodyId==IdA&&Game->CameraZoom==6&&FocusBody&&Game->CameraCenter.Equals(FVector2D(FocusBody->position.x,FocusBody->position.y),.00001));

    // Block the writer flag instead of using an invalid filesystem path. The
    // bridge must still retain this paid entitlement after consuming the ID.
    const FString RetryUser=TEXT("gift-storage-retry-id"),RetryKey=Game->ProgressKey(RetryUser);
    const FDouyinGift RetriedGift=NewGift(RetryUser,TEXT("魔法镜"));
    Game->bProgressWritable=false;
    AddExpectedError(TEXT("Weapon progress save failed in slot"),EAutomationExpectedErrorFlags::Contains,1);
    Verify(TEXT("A storage failure still accepts a new real gift event"),Bridge->DeliverGift(RetriedGift));
    Verify(TEXT("Failed save retains the pending sniper entitlement in memory and marks progress dirty"),Game->bProgressDirty&&(Game->Progress->WeaponUnlocks.FindRef(RetryKey)&gw::weaponBit(gw::WeaponKind::Sniper))!=0);
    const int32 RetryNotices=Game->GiftNoticeQueue.Num();
    Verify(TEXT("Storage failure does not reopen a consumed bridge event ID"),!Bridge->DeliverGift(RetriedGift));
    Verify(TEXT("Rejected retry event cannot duplicate its notice or discard its retained entitlement"),Game->GiftNoticeQueue.Num()==RetryNotices&&Game->bProgressDirty&&(Game->Progress->WeaponUnlocks.FindRef(RetryKey)&gw::weaponBit(gw::WeaponKind::Sniper))!=0);
    auto* BeforeRetry=Cast<UArenaProgressSave>(UGameplayStatics::LoadGameFromSlot(TestSlot,0));
    Verify(TEXT("Disabled writer leaves the previous save file unchanged"),BeforeRetry!=nullptr&&BeforeRetry->WeaponUnlocks.FindRef(RetryKey)==0);
    Game->bProgressWritable=true;
    Game->TickProgressSave(5);
    auto* AfterRetry=Cast<UArenaProgressSave>(UGameplayStatics::LoadGameFromSlot(TestSlot,0));
    Verify(TEXT("Five second save retry commits the retained gift to the real slot"),AfterRetry!=nullptr&&(AfterRetry->WeaponUnlocks.FindRef(RetryKey)&gw::weaponBit(gw::WeaponKind::Sniper))!=0);
    Verify(TEXT("Successful retry clears dirty state without re-delivering the gift"),!Game->bProgressDirty&&!Game->Viewers.Contains(RetryUser));

    const FString UnusedSavedCode=Game->GenerateRifleCode();
    const uint8 SavedMaskA=Game->Match.findFighter(IdA)->unlockedWeapons,SavedMaskB=Game->Match.findFighter(IdB)->unlockedWeapons;
    Game->ResetArena();Game->Progress=NewObject<UArenaProgressSave>(Game);
    Verify(TEXT("Reset fixture has no in-memory identity or progress to mask a failed load"),Game->Viewers.IsEmpty()&&Game->Progress->WeaponUnlocks.IsEmpty());
    auto* Loaded=Cast<UArenaProgressSave>(UGameplayStatics::LoadGameFromSlot(TestSlot,0));
    if(Verify(TEXT("Actual SaveGame file reloads with the expected schema"),Loaded!=nullptr&&Loaded->Version==1)){
        Game->Progress=Loaded;
        Verify(TEXT("Disk round trip retains unused codes and excludes both consumed codes"),Loaded->UnusedRifleCodes.Contains(UnusedSavedCode)&&!Loaded->UnusedRifleCodes.Contains(Code)&&!Loaded->UnusedRifleCodes.Contains(SpareCode));
        Comment(UserB,TEXT("2"));Comment(UserA,TEXT("1"));
        if(Verify(TEXT("Both original stable identities can rejoin after reload"),Game->Viewers.Contains(UserA)&&Game->Viewers.Contains(UserB))){
            const int32 ReloadA=Game->Viewers.FindChecked(UserA).BodyId,ReloadB=Game->Viewers.FindChecked(UserB).BodyId;
            Verify(TEXT("Rejoined identities receive new transient body IDs"),ReloadA!=IdA&&ReloadB!=IdB);
            Verify(TEXT("All six unlocks restore from disk for their stable owner"),Game->Match.findFighter(ReloadA)->unlockedWeapons==SavedMaskA&&SavedMaskA==0x3f);
            Verify(TEXT("Independent same-name viewer restores only their own weapons"),Game->Match.findFighter(ReloadB)->unlockedWeapons==SavedMaskB&&SavedMaskB!=SavedMaskA);
            Comment(UserA,TEXT("武器+6"));Verify(TEXT("Reloaded entitlement permits rocket selection without another gift"),Equipped(ReloadA,gw::WeaponKind::RocketLauncher));
            Game->Match.startNextRound();Verify(TEXT("Disk-restored weapons remain owned through another round"),Game->Match.findFighter(ReloadA)->unlockedWeapons==0x3f&&Equipped(ReloadA,gw::WeaponKind::RocketLauncher));
        }
    }
    // Preserve failed fixture evidence for diagnosis. Never delete the normal
    // player slot or a slot supplied by the application's command line.
    Verify(TEXT("The storage failure emitted exactly the expected warning"),HasMetExpectedErrors());
    if(Passed&&!HasAnyErrors()){Verify(TEXT("Successful gift fixture removes only its own GUID slot"),UGameplayStatics::DeleteGameInSlot(TestSlot,0));Verify(TEXT("GUID fixture save was removed"),!UGameplayStatics::DoesSaveGameExist(TestSlot,0));}
    else AddInfo(TEXT("Gift fixture save retained for diagnosis: ")+TestSlot);
    Cleanup();return Passed;
}
#endif
