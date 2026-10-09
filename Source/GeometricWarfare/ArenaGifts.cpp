#include "ArenaGameMode.h"
#include "LiveGiftRules.h"
#include "ImageUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

namespace {
FString WeaponName(gw::WeaponKind Kind) {
    static const TCHAR* Names[]={TEXT("手枪"),TEXT("霰弹枪"),TEXT("步枪"),TEXT("狙击枪·巴雷特"),TEXT("机枪·加特林"),TEXT("火箭筒")};
    return gw::validWeapon(Kind)?Names[static_cast<int>(Kind)]:TEXT("");
}
}
void AArenaGameMode::LoadProgress() {
    FString Slot;
    if(FParse::Value(FCommandLine::Get(),TEXT("GWProgressSlot="),Slot)) ProgressSlot=FPaths::GetCleanFilename(Slot);
    if(UGameplayStatics::DoesSaveGameExist(ProgressSlot,0)) {
        Progress=Cast<UArenaProgressSave>(UGameplayStatics::LoadGameFromSlot(ProgressSlot,0));
        bProgressWritable=Progress && Progress->Version==1;
    }
    if(!Progress) Progress=NewObject<UArenaProgressSave>(this);
    if(!bProgressWritable) LastEvent=TEXT("武器存档读取失败，已保留原文件；本次不覆盖存档");
}
bool AArenaGameMode::SaveProgress() {
    if(!Progress) Progress=NewObject<UArenaProgressSave>(this);
    if(bProgressWritable && UGameplayStatics::SaveGameToSlot(Progress,ProgressSlot,0)) {bProgressDirty=false;return true;}
    LastEvent=TEXT("存档保存失败，武器暂存在本次运行并将重试；兑换码操作未提交");
    UE_LOG(LogTemp,Warning,TEXT("Weapon progress save failed in slot %s"),*ProgressSlot);
    return false;
}
void AArenaGameMode::TickProgressSave(float Dt) {
    if(!bProgressDirty) return;
    ProgressRetryRemaining-=Dt;
    if(ProgressRetryRemaining>0) return;
    ProgressRetryRemaining=5;
    if(SaveProgress()) LastEvent=TEXT("武器权益与左手选择已补存，重启后仍保留");
}
FString AArenaGameMode::ProgressKey(const FString& UserId) const {
    return GetBridge()->GetScopedUserId(UserId);
}
bool AArenaGameMode::RememberWeapon(const FString& UserId,gw::WeaponKind Kind) {
    if(!gw::validWeapon(Kind)) return false;
    if(!Progress) Progress=NewObject<UArenaProgressSave>(this);
    const FString Key=ProgressKey(UserId);
    const uint8 Old=Progress->WeaponUnlocks.FindRef(Key),Mask=gw::weaponBit(Kind);
    if(Old&Mask) return true;
    Progress->WeaponUnlocks.Add(Key,Old|Mask|gw::weaponBit(gw::WeaponKind::Pistol));
    if(!SaveProgress()) {bProgressDirty=true;ProgressRetryRemaining=5;}
    // The bridge has already consumed the event ID. Keep this entitlement even
    // on disk failure and retry, so a validated gift never silently disappears.
    return true;
}
void AArenaGameMode::RestoreWeapons(const FViewerState& Viewer,bool RestoreLeftSelection) {
    if(!Progress)return;
    auto* Fighter=Match.findFighter(Viewer.BodyId);if(!Fighter)return;
    const FString Key=ProgressKey(Viewer.UserId);
    Fighter->unlockedWeapons|=Progress->WeaponUnlocks.FindRef(Key)&0x3f;
    Fighter->unlockedWeapons|=PlatformTestWeaponUnlocks.FindRef(Viewer.UserId)&0x3f;
    if(!RestoreLeftSelection)return;
    const uint8* Saved=Progress->SelectedLeftWeapons.Find(Key);
    const auto Kind=Saved?static_cast<gw::WeaponKind>(*Saved):gw::WeaponKind::Pistol;
    Match.switchWeapon(Viewer.BodyId,gw::validWeapon(Kind)&&(Fighter->unlockedWeapons&gw::weaponBit(Kind))?Kind:gw::WeaponKind::Pistol);
}
void AArenaGameMode::RememberSelectedLeftWeapon(const FViewerState& Viewer) {
    if(!Progress || bApplyingGMGift || Viewer.bDebugBot)return;
    const auto* Fighter=Match.findFighter(Viewer.BodyId);if(!Fighter||!gw::validWeapon(Fighter->weaponKind))return;
    const FString Key=ProgressKey(Viewer.UserId);
    const uint8 Owned=Progress->WeaponUnlocks.FindRef(Key)|gw::weaponBit(gw::WeaponKind::Pistol);
    if(!(Owned&gw::weaponBit(Fighter->weaponKind)))return;
    const uint8 Choice=static_cast<uint8>(Fighter->weaponKind);
    if(const auto* Saved=Progress->SelectedLeftWeapons.Find(Key))if(*Saved==Choice)return;
    Progress->SelectedLeftWeapons.Add(Key,Choice);
    // Retain a failed entitlement and its choice together for the existing retry.
    if(bProgressDirty)return;
    if(!SaveProgress()){bProgressDirty=true;ProgressRetryRemaining=5;}
}
FString AArenaGameMode::GenerateRifleCode() {
    if(!GetBridge()->IsLocalTestMode()) { LastEvent=TEXT("兑换码生成仅用于本地演示台"); return FString(); }
    if(!Progress) Progress=NewObject<UArenaProgressSave>(this);
    if(Progress->UnusedRifleCodes.Num()>=256) {LastEvent=TEXT("待领取兑换码已达 256 个，请先领取已有兑换码");return FString();}
    FString Code; do { Code=FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8).ToUpper(); } while(Progress->UnusedRifleCodes.Contains(Code));
    Progress->UnusedRifleCodes.Add(Code);
    if(!SaveProgress()) {Progress->UnusedRifleCodes.Remove(Code);return FString();}
    LastRifleCode=Code; LastEvent=TEXT("关注后向主播领取一次性兑换码，评论：步枪")+Code;
    return Code;
}
bool AArenaGameMode::HandleWeaponCommand(const FString& Command,const FViewerState& Viewer) {
    if(Command.StartsWith(TEXT("步枪"))) {
        if(!GetBridge()->IsLocalTestMode()) {LastEvent=TEXT("本地测试兑换码不能用于直播；步枪解锁等待平台关注验证");return true;}
        FString Code=Command.Mid(2).TrimStartAndEnd();
        if(Code.StartsWith(TEXT("+")) || Code.StartsWith(TEXT("＋"))) Code=Code.Mid(1).TrimStartAndEnd();
        Code=Code.ToUpper();
        if(Code.IsEmpty()) {LastEvent=TEXT("兑换格式：在“步枪”后接兑换码，加号可省略；请使用有效的完整兑换评论");return true;}
        const auto* Fighter=Match.findFighter(Viewer.BodyId);
        if(Fighter && (Fighter->unlockedWeapons&gw::weaponBit(gw::WeaponKind::Rifle))) {LastEvent=TEXT("已解锁步枪，评论“武器+3”切换；兑换码未消耗");return true;}
        if(!Progress || !Progress->UnusedRifleCodes.Contains(Code)) {LastEvent=TEXT("步枪兑换码无效或已使用，请关注后向主播领取兑换码");return true;}
        // Commit code consumption and the entitlement together in one save.
        const FString Key=ProgressKey(Viewer.UserId); const uint8 Old=Progress->WeaponUnlocks.FindRef(Key);
        const uint8* OldChoicePtr=Progress->SelectedLeftWeapons.Find(Key);const bool HadChoice=OldChoicePtr!=nullptr;const uint8 OldChoice=OldChoicePtr?*OldChoicePtr:0;
        Progress->UnusedRifleCodes.Remove(Code);
        Progress->WeaponUnlocks.Add(Key,Old|gw::weaponBit(gw::WeaponKind::Pistol)|gw::weaponBit(gw::WeaponKind::Rifle));
        Progress->SelectedLeftWeapons.Add(Key,static_cast<uint8>(gw::WeaponKind::Rifle));
        if(!SaveProgress()) {Progress->UnusedRifleCodes.Add(Code);if(Old)Progress->WeaponUnlocks.Add(Key,Old);else Progress->WeaponUnlocks.Remove(Key);if(HadChoice)Progress->SelectedLeftWeapons.Add(Key,OldChoice);else Progress->SelectedLeftWeapons.Remove(Key);return true;}
        RestoreWeapons(Viewer); Match.switchWeapon(Viewer.BodyId,gw::WeaponKind::Rifle);
        LastEvent=Viewer.Name+TEXT(" 兑换成功，永久解锁步枪"); Feed.enqueue(LastEvent); return true;
    }
    if(!Command.StartsWith(TEXT("武器"))) return false;
    FString Number=Command.Mid(2).TrimStartAndEnd();
    if(Number.StartsWith(TEXT("+")) || Number.StartsWith(TEXT("＋"))) Number=Number.Mid(1).TrimStartAndEnd();
    if(Number.Len()!=1 || Number[0]<TEXT('1') || Number[0]>TEXT('6')) {LastEvent=TEXT("左手切换格式：武器1 至 武器6（加号可省略）");return true;}
    const auto Kind=static_cast<gw::WeaponKind>(Number[0]-TEXT('1'));
    const auto* Fighter=Match.findFighter(Viewer.BodyId);
    if(!Fighter || !(Fighter->unlockedWeapons&gw::weaponBit(Kind))) {LastEvent=TEXT("左手尚未永久解锁“")+WeaponName(Kind)+TEXT("”；右手武器箱不提供左手切换资格");return true;}
    if(Match.phase==gw::Phase::Results) {LastEvent=TEXT("结算期间不可切换；下一局保留已解锁武器");return true;}
    if(Fighter->weaponKind==Kind) {LastEvent=TEXT("左手当前已装备“")+WeaponName(Kind)+TEXT("”");return true;}
    if(Match.switchWeapon(Viewer.BodyId,Kind)){RememberSelectedLeftWeapon(Viewer);EnqueueWeaponSwitchNotice(Viewer,Kind);}
    ConsumeEvents(); return true;
}
void AArenaGameMode::FocusLocalViewer(const FString& Nickname) {
    if(!GetBridge()->IsLocalTestMode()) return;
    const auto* Viewer=Viewers.Find(TEXT("local-")+Nickname.TrimStartAndEnd());
    if(!Viewer) {LastEvent=TEXT("先用这个昵称发送“加入”，即可锁定我的视角");return;}
    FocusViewer(Viewer->BodyId); LastEvent=TEXT("已锁定 ")+Viewer->Name+TEXT(" 的视角");
}
void AArenaGameMode::SimulateGift(const FString& Nickname,const FString& GiftName) {
    if(IsGMEnabled()){SendGMGift(Nickname,GiftName);return;}
    if(!GetBridge()->IsLocalTestMode()) return;
    const FString Name=Nickname.TrimStartAndEnd();
    if(Name.IsEmpty()) {LastEvent=TEXT("先填写测试昵称");return;}
    GetBridge()->SimulateGift(TEXT("local-")+Name,Name,GiftName);
}
void AArenaGameMode::HandleGift(const FLiveGift& Gift) {
    if(!GetBridge()->IsEventFromCurrentSession(Gift.Session))return;
    if(!liveinteraction::IsSupportedGift(Gift.GiftName) || Gift.Count<=0 || (GetBridge()->IsLocalTestMode() && Gift.Count>100))return;
    if(Gift.bIsTestData)bPlatformTestSession=true;
    ApplyGift(Gift);
    QueueHandledLiveEvent(Gift.Session,Gift.MessageId,TEXT("live_gift"));
}
void AArenaGameMode::ApplyGift(const FLiveGift& Gift) {
    if(!GetBridge()->IsEventFromCurrentSession(Gift.Session)) return;
    if(!liveinteraction::IsSupportedGift(Gift.GiftName) || Gift.Count<=0 || (GetBridge()->IsLocalTestMode() && Gift.Count>100)) return;
    auto* Viewer=Viewers.Find(Gift.UserId);
    if(Viewer && !Gift.Nickname.IsEmpty()) Viewer->Name=Gift.Nickname.Left(20);
    const FString Name=Viewer?Viewer->Name:(Gift.Nickname.IsEmpty()?TEXT("观众"):Gift.Nickname.Left(20));
    FGiftNotice Notice;Notice.UserId=Gift.UserId;Notice.ViewerName=Name;Notice.GiftName=Gift.GiftName;Notice.Count=Gift.Count;Notice.Team=Viewer?Viewer->Team:0;Notice.bIsTestData=Gift.bIsTestData;
    if(Gift.GiftName==TEXT("仙女棒") || Gift.GiftName==TEXT("能力药丸")) {
        const bool Wand=Gift.GiftName==TEXT("仙女棒");const bool Rebuilding=Viewer&&!Wand&&!Match.bases[Viewer->Team].alive;
        const auto* Recipient=Viewer?Match.findFighter(Viewer->BodyId):nullptr;
        const bool Reviving=Wand&&Recipient&&!Recipient->alive;
        bool Applied=false;
        if(Viewer)Applied=Wand?Match.applyFairyWand(Viewer->BodyId,Gift.Count):Match.rebuildOrFortifyBase(Viewer->Team,Gift.Count);
        Notice.Visual=Wand?EArenaNoticeVisual::Revive:EArenaNoticeVisual::Base;Notice.Success=Applied;
        Notice.WeaponName=Wand?(Reviving?TEXT("复活 / 生命强化"):TEXT("生命强化")):TEXT("阵营基地");
        if(Wand)Notice.Detail=Applied?(Reviving?(Gift.Count==1?TEXT("本人复活成功"):TEXT("首件复活；其余每件基础生命 +30，死亡清零")):TEXT("每件基础生命 +30；进化 / 英雄继续加成，死亡清零")):
            (!Viewer?TEXT("未触发：请先加入战局"):TEXT("未触发：结算期间无法复活或强化"));
        else Notice.Detail=Applied?(Rebuilding?TEXT("基地重建成功；剩余每件强化生命 +1000"):TEXT("基地强化成功；每件当前 / 最大生命 +1000")):
            (!Viewer?TEXT("未触发：请先加入战局"):TEXT("未触发：需红蓝队，冲刺 / 结算禁止重建强化"));
        LastEvent=Name+TEXT(" 送出了 ")+Gift.GiftName+FString::Printf(TEXT(" ×%lld · "),Gift.Count)+Notice.Detail;
        EnqueueGiftNotice(MoveTemp(Notice));return;
    }
    const auto Kind=Gift.GiftName==TEXT("魔法镜")?gw::WeaponKind::Sniper:
        Gift.GiftName==TEXT("甜甜圈")?gw::WeaponKind::MachineGun:gw::WeaponKind::RocketLauncher;
    if(bApplyingGMGift){if(!Viewer)return;}
    else if(Gift.bIsTestData)PlatformTestWeaponUnlocks.FindOrAdd(Gift.UserId)|=gw::weaponBit(Kind);
    else if(!RememberWeapon(Gift.UserId,Kind)) return;
    if(Viewer) {RestoreWeapons(*Viewer);Match.grantWeapon(Viewer->BodyId,Kind);if(!Gift.bIsTestData)RememberSelectedLeftWeapon(*Viewer);}
    Notice.WeaponKind=Kind;Notice.WeaponName=WeaponName(Kind);Notice.Elaborate=true;
    Notice.Detail=!Viewer?TEXT("永久解锁左手，加入后可装备"):Match.phase==gw::Phase::Results?TEXT("永久解锁左手，下一局可装备"):TEXT("永久解锁并装备到左手；重复送礼不补弹");
    if(bApplyingGMGift)Notice.Detail=TEXT("GM测试：本次运行解锁并装备到左手，未提交平台履约");
    if(Gift.bIsTestData)Notice.Detail=!Viewer?TEXT("平台测试：本会话解锁，加入后可装备"):Match.phase==gw::Phase::Results?
        TEXT("平台测试：本会话解锁，下一局可装备"):TEXT("平台测试：本会话解锁并装备到左手");
    if(bProgressDirty)Notice.Detail+=TEXT("（存档重试中）");
    LastEvent=Name+TEXT(" 送出了 ")+Gift.GiftName+TEXT("，获得了 ")+Notice.WeaponName+TEXT(" · ")+Notice.Detail;
    EnqueueGiftNotice(MoveTemp(Notice));
}
UTexture2D* AArenaGameMode::GetGiftIcon(const FString& GiftName) {
    if(!liveinteraction::IsSupportedGift(GiftName)) return nullptr;
    if(const auto* Cached=GiftIcons.Find(GiftName)) return *Cached;
    auto* Texture=FImageUtils::ImportFileAsTexture2D(FPaths::ProjectContentDir()/TEXT("GiftIcons")/(GiftName+TEXT(".png")));
    GiftIcons.Add(GiftName,Texture); return Texture;
}
