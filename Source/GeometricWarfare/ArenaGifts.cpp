#include "ArenaGameMode.h"
#include "DouyinGiftRules.h"
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
    if(SaveProgress()) LastEvent=TEXT("武器解锁已补存，重启后仍保留");
}
FString AArenaGameMode::ProgressKey(const FString& UserId) const {
    return (GetBridge()->IsRelayMode()?TEXT("live:"):TEXT("local:"))+UserId;
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
void AArenaGameMode::RestoreWeapons(const FViewerState& Viewer) {
    if(Progress) if(auto* Fighter=Match.findFighter(Viewer.BodyId))
        Fighter->unlockedWeapons|=Progress->WeaponUnlocks.FindRef(ProgressKey(Viewer.UserId))&0x3f;
}
FString AArenaGameMode::GenerateRifleCode() {
    if(GetBridge()->IsRelayMode()) { LastEvent=TEXT("兑换码生成仅用于本地演示台"); return FString(); }
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
        FString Code=Command.Mid(2).TrimStartAndEnd();
        if(Code.StartsWith(TEXT("+")) || Code.StartsWith(TEXT("＋"))) Code=Code.Mid(1).TrimStartAndEnd();
        Code=Code.ToUpper();
        if(Code.IsEmpty()) {LastEvent=TEXT("兑换格式：在“步枪”后接兑换码，加号可省略；请复制 GM 生成的完整评论");return true;}
        const auto* Fighter=Match.findFighter(Viewer.BodyId);
        if(Fighter && (Fighter->unlockedWeapons&gw::weaponBit(gw::WeaponKind::Rifle))) {LastEvent=TEXT("已解锁步枪，评论“武器+3”切换；兑换码未消耗");return true;}
        if(!Progress || !Progress->UnusedRifleCodes.Contains(Code)) {LastEvent=TEXT("步枪兑换码无效或已使用，请关注后向主播领取兑换码");return true;}
        // Commit code consumption and the entitlement together in one save.
        const FString Key=ProgressKey(Viewer.UserId); const uint8 Old=Progress->WeaponUnlocks.FindRef(Key);
        Progress->UnusedRifleCodes.Remove(Code);
        Progress->WeaponUnlocks.Add(Key,Old|gw::weaponBit(gw::WeaponKind::Pistol)|gw::weaponBit(gw::WeaponKind::Rifle));
        if(!SaveProgress()) {Progress->UnusedRifleCodes.Add(Code);if(Old)Progress->WeaponUnlocks.Add(Key,Old);else Progress->WeaponUnlocks.Remove(Key);return true;}
        RestoreWeapons(Viewer); Match.switchWeapon(Viewer.BodyId,gw::WeaponKind::Rifle);
        LastEvent=Viewer.Name+TEXT(" 兑换成功，永久解锁步枪"); Feed.enqueue(LastEvent); return true;
    }
    if(!Command.StartsWith(TEXT("武器"))) return false;
    FString Number=Command.Mid(2).TrimStartAndEnd();
    if(Number.StartsWith(TEXT("+")) || Number.StartsWith(TEXT("＋"))) Number=Number.Mid(1).TrimStartAndEnd();
    if(Number.Len()!=1 || Number[0]<TEXT('1') || Number[0]>TEXT('6')) {LastEvent=TEXT("切换格式：武器+1 至 武器+6");return true;}
    const auto Kind=static_cast<gw::WeaponKind>(Number[0]-TEXT('1'));
    const auto* Fighter=Match.findFighter(Viewer.BodyId);
    if(!Fighter || (!(Fighter->unlockedWeapons&gw::weaponBit(Kind)) && !(Fighter->temporaryWeaponRemaining>0 && Fighter->temporaryWeaponKind==Kind))) {LastEvent=TEXT("尚未解锁“")+WeaponName(Kind)+TEXT("”");return true;}
    if(Match.phase==gw::Phase::Results) {LastEvent=TEXT("结算期间不可切换；下一局保留已解锁武器");return true;}
    if(Fighter->weaponKind==Kind) {LastEvent=TEXT("当前已装备“")+WeaponName(Kind)+TEXT("”");return true;}
    Match.switchWeapon(Viewer.BodyId,Kind); ConsumeEvents(); return true;
}
void AArenaGameMode::FocusLocalViewer(const FString& Nickname) {
    if(GetBridge()->IsRelayMode()) return;
    const auto* Viewer=Viewers.Find(TEXT("local-")+Nickname.TrimStartAndEnd());
    if(!Viewer) {LastEvent=TEXT("先用这个昵称发送“加入”，即可锁定我的视角");return;}
    FocusViewer(Viewer->BodyId); LastEvent=TEXT("已锁定 ")+Viewer->Name+TEXT(" 的视角");
}
void AArenaGameMode::SimulateGift(const FString& Nickname,const FString& GiftName) {
    if(GetBridge()->IsRelayMode()) return;
    const FString Name=Nickname.TrimStartAndEnd();
    if(Name.IsEmpty()) {LastEvent=TEXT("先填写测试昵称");return;}
    GetBridge()->SimulateGift(TEXT("local-")+Name,Name,GiftName);
}
void AArenaGameMode::HandleGift(const FDouyinGift& Gift) {
    if(!douyin::IsSupportedGift(Gift.GiftName) || !douyin::IsValidGiftCount(Gift.Count)) return;
    auto* Viewer=Viewers.Find(Gift.UserId);
    if(Viewer && !Gift.Nickname.IsEmpty()) Viewer->Name=Gift.Nickname.Left(20);
    const FString Name=Viewer?Viewer->Name:(Gift.Nickname.IsEmpty()?TEXT("观众"):Gift.Nickname.Left(20));
    if(Gift.GiftName==TEXT("仙女棒") || Gift.GiftName==TEXT("能力药丸")) {
        bool Applied=false;
        if(Viewer) {
            if(Gift.GiftName==TEXT("仙女棒")) Applied=Match.revive(Viewer->BodyId,true);
            else for(int32 N=0;N<Gift.Count;++N) Applied=Match.rebuildOrFortifyBase(Viewer->Team)||Applied;
        }
        LastEvent=Name+TEXT(" 送出“")+Gift.GiftName+TEXT("”")+FString::Printf(TEXT(" ×%d · "),Gift.Count);
        LastEvent+=Applied?(Gift.GiftName==TEXT("仙女棒")?TEXT("个人复活成功"):TEXT("基地重建 / 强化成功")):
            (!Viewer?TEXT("请先加入战局"):Gift.GiftName==TEXT("仙女棒")?TEXT("角色存活或结算中，无需复活"):TEXT("需要红蓝阵营，冲刺 / 结算期间无法重建或强化"));
        Feed.enqueue(LastEvent); return;
    }
    const auto Kind=Gift.GiftName==TEXT("魔法镜")?gw::WeaponKind::Sniper:
        Gift.GiftName==TEXT("甜甜圈")?gw::WeaponKind::MachineGun:gw::WeaponKind::RocketLauncher;
    if(!RememberWeapon(Gift.UserId,Kind)) return;
    if(Viewer) {RestoreWeapons(*Viewer);Match.grantWeapon(Viewer->BodyId,Kind);}
    LastEvent=Name+TEXT(" 送出“")+Gift.GiftName+TEXT("”，永久解锁“")+WeaponName(Kind)+TEXT("”");
    if(!Viewer) LastEvent+=TEXT("，加入战局后可装备");
    if(bProgressDirty) LastEvent+=TEXT("（本地存档待重试，请暂勿退出）");
    EnqueueGiftNotice(Name,Gift.GiftName,WeaponName(Kind));
}
UTexture2D* AArenaGameMode::GetGiftIcon(const FString& GiftName) {
    if(!douyin::IsSupportedGift(GiftName)) return nullptr;
    if(const auto* Cached=GiftIcons.Find(GiftName)) return *Cached;
    auto* Texture=FImageUtils::ImportFileAsTexture2D(FPaths::ProjectContentDir()/TEXT("GiftIcons")/(GiftName+TEXT(".png")));
    GiftIcons.Add(GiftName,Texture); return Texture;
}
void AArenaGameMode::EnqueueGiftNotice(const FString& Name,const FString& Gift,const FString& Weapon) {
    FGiftNotice Notice;Notice.ViewerName=Name;Notice.GiftName=Gift;Notice.WeaponName=Weapon;Notice.Active=true;
    if(!GiftNotice.Active) GiftNotice=MoveTemp(Notice);
    else {if(GiftNoticeQueue.Num()>=64)GiftNoticeQueue.RemoveAt(0);GiftNoticeQueue.Add(MoveTemp(Notice));}
}
void AArenaGameMode::TickGiftNotice(float Dt) {
    if(!GiftNotice.Active) return;
    GiftNotice.Age+=Dt;
    while(GiftNotice.Active && GiftNotice.Age>=2) {
        const float Remainder=GiftNotice.Age-2;
        if(GiftNoticeQueue.IsEmpty()) GiftNotice=FGiftNotice{};
        else {GiftNotice=GiftNoticeQueue[0];GiftNoticeQueue.RemoveAt(0);GiftNotice.Age=Remainder;}
    }
}
