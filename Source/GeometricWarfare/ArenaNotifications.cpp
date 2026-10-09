#include "ArenaGameMode.h"

namespace {
bool SameNotice(const FGiftNotice& A,const FGiftNotice& B) {
    return A.UserId==B.UserId && A.Team==B.Team && A.GiftName==B.GiftName && A.Visual==B.Visual
        && A.WeaponKind==B.WeaponKind && A.Success==B.Success && A.bIsTestData==B.bIsTestData && A.Detail==B.Detail;
}
void MergeNotice(FGiftNotice& A,const FGiftNotice& B) {
    A.CountCapped=A.CountCapped || B.CountCapped || B.Count>MAX_int64-A.Count;
    A.Count+=FMath::Min<int64>(B.Count,MAX_int64-A.Count);
    // Never reset Age: a continuous combo must not starve other senders.
}
void ResizeNoticeLanes(TArray<FGiftNotice>& Active,TArray<FGiftNotice>& Queue,int32 Lanes) {
    if(Active.Num()==Lanes)return;
    // Displaced visible senders are older than the queue. Give them their full
    // display interval again; retaining the first lane's age prevents starvation.
    if(Active.Num()>Lanes){
        TArray<FGiftNotice> Pending;
        for(int32 I=Lanes;I<Active.Num();++I)if(Active[I].Active){Active[I].Age=0;Pending.Add(MoveTemp(Active[I]));}
        Pending.Append(MoveTemp(Queue));Queue=MoveTemp(Pending);
    }
    Active.SetNum(Lanes);
    for(auto& N:Active)if(!N.Active&&!Queue.IsEmpty()){N=MoveTemp(Queue[0]);Queue.RemoveAt(0);N.Age=0;}
}
}
void AArenaGameMode::SetNoticeCompactLayout(bool Compact) {
    if(bCompactNoticeLayout==Compact)return;
    bCompactNoticeLayout=Compact;
    ResizeNoticeLanes(GiftNotices,GiftNoticeQueue,Compact?1:3);
    ResizeNoticeLanes(RewardNotices,RewardNoticeQueue,Compact?1:2);
    ResizeNoticeLanes(JoinNotices,JoinNoticeQueue,Compact?1:3);
}
void AArenaGameMode::QueueNotice(TArray<FGiftNotice>& Active,TArray<FGiftNotice>& Queue,FGiftNotice Notice,int32 Lanes,float Lifetime) {
    Notice.Active=true;Notice.Age=0;
    ResizeNoticeLanes(Active,Queue,bCompactNoticeLayout?1:Lanes);
    for(auto& N:Active)if(N.Active && N.Age<Lifetime-.6f && SameNotice(N,Notice)){MergeNotice(N,Notice);return;}
    for(auto& N:Queue)if(SameNotice(N,Notice)){MergeNotice(N,Notice);return;}
    // FIFO fairness: a free lane takes the oldest waiting item first.
    for(auto& N:Active)if(!N.Active && Queue.IsEmpty()){N=MoveTemp(Notice);return;}
    if(Queue.Num()>=256) {
        // Extreme pressure becomes explicitly labelled collective announcements,
        // grouped by effect and outcome. Never silently delete paid units.
        Notice.UserId.Empty();Notice.ViewerName=TEXT("多位观众");
        if(!Notice.GiftName.IsEmpty())Notice.Detail=Notice.Success?
            (Notice.Visual==EArenaNoticeVisual::Weapon?(Notice.bIsTestData?TEXT("平台测试：本会话解锁左手"):TEXT("永久解锁左手；重复送礼不补弹")):Notice.Visual==EArenaNoticeVisual::Base?TEXT("基地重建 / 强化成功，按送出时基地状态生效"):TEXT("阵亡先复活，其余每件基础生命 +30；死亡清零")):
            TEXT("未触发：未入场或当前状态不满足使用条件");
        else if(Notice.Visual==EArenaNoticeVisual::TeamSelected || Notice.Visual==EArenaNoticeVisual::WeaponSwitch) {
            // Preserve the actual action, camp and weapon in collective notices.
        }
        else if(Notice.Visual==EArenaNoticeVisual::Gather)Notice.Detail=TEXT("响应了一键摇人召集");
        else if(Notice.Visual==EArenaNoticeVisual::TemporaryWeapon)Notice.Detail=TEXT("武器箱奖励：右手临时60秒，阵亡或到期消失");
        else Notice.Detail=Notice.Visual==EArenaNoticeVisual::People?
            (Notice.Detail.StartsWith(TEXT("加入了"))?Notice.Detail:TEXT("发生击败 / 连续击败")):TEXT("获得对应道具或增益");
        for(auto& N:Queue)if(SameNotice(N,Notice)){MergeNotice(N,Notice);return;}
    }
    Queue.Add(MoveTemp(Notice));
}
void AArenaGameMode::TickNoticeQueue(TArray<FGiftNotice>& Active,TArray<FGiftNotice>& Queue,float Dt,float Lifetime) {
    if(!FMath::IsFinite(Dt) || Dt<=0)return;
    for(auto& N:Active) {
        if(N.Active){N.Age+=Dt;if(N.Age>=Lifetime)N=FGiftNotice{};}
        if(!N.Active && !Queue.IsEmpty()){N=MoveTemp(Queue[0]);Queue.RemoveAt(0);N.Age=0;}
    }
}
void AArenaGameMode::TickGiftNotice(float Dt) {
    TickNoticeQueue(GiftNotices,GiftNoticeQueue,Dt,2.8f);
    TickNoticeQueue(RewardNotices,RewardNoticeQueue,Dt,3.2f);
    TickNoticeQueue(JoinNotices,JoinNoticeQueue,Dt,5.5f);
}
void AArenaGameMode::EnqueueGiftNotice(FGiftNotice Notice) {
    QueueNotice(GiftNotices,GiftNoticeQueue,MoveTemp(Notice),3,2.8f);
}
void AArenaGameMode::EnqueueGiftNotice(const FString& Name,const FString& Gift,const FString& Weapon) {
    FGiftNotice N;N.ViewerName=Name;N.GiftName=Gift;N.WeaponName=Weapon;N.Elaborate=true;
    N.WeaponKind=Gift==TEXT("魔法镜")?gw::WeaponKind::Sniper:Gift==TEXT("甜甜圈")?gw::WeaponKind::MachineGun:gw::WeaponKind::RocketLauncher;
    N.Detail=TEXT("永久解锁并装备到左手");EnqueueGiftNotice(MoveTemp(N));
}
void AArenaGameMode::EnqueueRewardNotice(const FString& UserId,gw::WeaponKind Kind,const FString& Reason,EArenaNoticeVisual Visual) {
    FGiftNotice N;N.UserId=UserId;N.WeaponKind=Kind;N.Detail=Reason;N.Visual=Visual;
    static const TCHAR* Names[]={TEXT("手枪"),TEXT("霰弹枪"),TEXT("步枪"),TEXT("狙击枪·巴雷特"),TEXT("机枪·加特林"),TEXT("火箭筒")};
    N.WeaponName=gw::validWeapon(Kind)?Names[static_cast<int32>(Kind)]:TEXT("武器");
    if(const auto* V=Viewers.Find(UserId)){N.ViewerName=V->Name;N.Team=V->Team;}
    else N.ViewerName=TEXT("观众");
    QueueNotice(RewardNotices,RewardNoticeQueue,MoveTemp(N),2,3.2f);
}
void AArenaGameMode::EnqueueJoinNotice(const FViewerState& Viewer,bool TeamSelection) {
    FGiftNotice N;N.UserId=Viewer.UserId;N.ViewerName=Viewer.Name;N.Team=Viewer.Team;N.Visual=TeamSelection?EArenaNoticeVisual::TeamSelected:EArenaNoticeVisual::People;
    N.Detail=TeamSelection?(Viewer.Team==1?TEXT("选择了红队"):TEXT("选择了蓝队")):
        Viewer.Team==1?TEXT("加入了红队"):Viewer.Team==2?TEXT("加入了蓝队"):TEXT("加入了灰队");
    Feed.enqueue(N.ViewerName+TEXT(" ")+N.Detail,Viewer.Team);
    QueueNotice(JoinNotices,JoinNoticeQueue,MoveTemp(N),3,5.5f);
}
void AArenaGameMode::EnqueueWeaponSwitchNotice(const FViewerState& Viewer,gw::WeaponKind Kind) {
    if(!gw::validWeapon(Kind))return;
    static const TCHAR* Names[]={TEXT("手枪"),TEXT("霰弹枪"),TEXT("步枪"),TEXT("狙击枪·巴雷特"),TEXT("机枪·加特林"),TEXT("火箭筒")};
    FGiftNotice N;N.UserId=Viewer.UserId;N.ViewerName=Viewer.Name;N.Team=Viewer.Team;
    N.Visual=EArenaNoticeVisual::WeaponSwitch;N.WeaponKind=Kind;N.WeaponName=Names[static_cast<int32>(Kind)];N.Detail=TEXT("左手切换为")+N.WeaponName;
    LastEvent=N.ViewerName+TEXT(" ")+N.Detail;Feed.enqueue(LastEvent,Viewer.Team);
    QueueNotice(JoinNotices,JoinNoticeQueue,MoveTemp(N),3,5.5f);
}
UTexture2D* AArenaGameMode::GetNoticeAvatar(const FString& UserId) {
    if(const auto* V=Viewers.Find(UserId))return V->Avatar;
    if(const auto* State=LiveViewerStates.Find(UserId))if(State->Avatar)return State->Avatar;
    return UserId.IsEmpty()?nullptr:MakePlaceholder(static_cast<int32>(GetTypeHash(UserId)&0x7fffffff));
}
