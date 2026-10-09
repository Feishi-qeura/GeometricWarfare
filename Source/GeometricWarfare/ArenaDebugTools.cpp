#include "ArenaGameMode.h"
#include "LiveGiftRules.h"
#include "Misc/ConfigCacheIni.h"

bool AArenaGameMode::IsGMEnabled() const {
    bool Enabled=false;GConfig->GetBool(TEXT("ArenaDebug"),TEXT("EnableGM"),Enabled,GGameIni);
    return Enabled;
}
void AArenaGameMode::UpdateHostProfile() {
    const auto* Bridge=GetBridge();const auto& Session=Bridge->GetCurrentSession();
    HostViewer.UserId=Session.AnchorUserId;HostViewer.BodyId=HostBodyId;HostViewer.Team=GetHostTeam();
    HostViewer.Name=Bridge->GetAnchorNickname().IsEmpty()?TEXT("主播 · 助战"):Bridge->GetAnchorNickname();
    if(!HostViewer.Avatar)HostViewer.Avatar=MakePlaceholder(0);
}
void AArenaGameMode::AddGMBots(int32 Team,int32 Count) {
    if(!IsGMEnabled() || Team<0 || Team>2)return;
    Count=FMath::Clamp(Count,1,100);int Added=0;
    for(int I=0;I<Count;++I){const int Id=NextBodyId;
        if(!Match.add(Id,static_cast<gw::Shape>(Id%4),Team))break;
        ++NextBodyId;FViewerState V;V.BodyId=Id;V.Team=Team;V.bDebugBot=true;
        V.UserId=TEXT("gm-bot-")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
        V.Name=FString::Printf(TEXT("人机%s%03d"),Team==1?TEXT("红"):Team==2?TEXT("蓝"):TEXT("灰"),Id);V.Avatar=MakePlaceholder(Id);
        Identities.Add(Id,V.UserId);Viewers.Add(V.UserId,V);EnqueueJoinNotice(V);++Added;
    }
    LastEvent=FString::Printf(TEXT("GM：添加 %d 个%s方人机"),Added,Team==1?TEXT("红"):Team==2?TEXT("蓝"):TEXT("灰"));
}
bool AArenaGameMode::BindGMIdentity(const FString& DouyinId,const FString& LiveUserId) {
    const FString Key=DouyinId.TrimStartAndEnd();const auto* V=Viewers.Find(LiveUserId);
    if(!IsGMEnabled() || Key.IsEmpty() || Key.Len()>256 || !V || V->bDebugBot || Viewers.Contains(Key)){LastEvent=TEXT("请选择真实入场观众，填写抖音号；SDK ID 可直接使用");return false;}
    GMIdentityBindings.Add(Key,LiveUserId);LastEvent=TEXT("GM身份已绑定：")+Key+TEXT(" → ")+V->Name;return true;
}
bool AArenaGameMode::SendGMGift(const FString& Identity,const FString& GiftName,int32 Count) {
    const FString Key=Identity.TrimStartAndEnd();const FString Id=GMIdentityBindings.Contains(Key)?GMIdentityBindings.FindChecked(Key):Key;
    const auto* V=Viewers.Find(Id);
    if(!IsGMEnabled() || !liveinteraction::IsSupportedGift(GiftName) || Count<1 || Count>100)return false;
    if(!V || V->bDebugBot || !GetBridge()->GetCurrentSession().Nonce.IsValid()){LastEvent=TEXT("GM礼物：请先用真实账号发送“加入”，再绑定抖音号或输入SDK OpenID");return false;}
    FLiveGift Gift;Gift.Session=GetBridge()->GetCurrentSession();Gift.UserId=Id;Gift.Nickname=V->Name;Gift.GiftName=GiftName;Gift.Count=Count;
    TGuardValue<bool> Guard(bApplyingGMGift,true);ApplyGift(Gift);return true;
}
bool AArenaGameMode::PrepareGMGiftTarget(const FString& Identity,bool DestroyBase) {
    const FString Key=Identity.TrimStartAndEnd();const auto* V=Viewers.Find(GMIdentityBindings.Contains(Key)?GMIdentityBindings.FindChecked(Key):Key);
    if(!IsGMEnabled() || !V || V->bDebugBot)return false;
    if(DestroyBase){if(V->Team<1 || V->Team>2)return false;auto& Base=Match.bases[V->Team];Base.hp=0;Base.alive=false;}
    else {auto* F=Match.findFighter(V->BodyId);if(!F)return false;Match.damageEnvironment(F->id,F->maxHp+F->armor+10000);}
    ConsumeEvents();LastEvent=DestroyBase?TEXT("GM：基地已摧毁，可测试能力药丸"):TEXT("GM：角色已阵亡，可测试仙女棒");return true;
}
