#include "ArenaGameMode.h"
#include "Dom/JsonObject.h"
#include "Misc/ConfigCacheIni.h"
#include "RHI.h"

void AArenaGameMode::QueueHandledLiveEvent(const FLiveSession& Session,const FString& MessageId,const FString& MessageType)
{
    if(GetBridge()->IsLocalTestMode() || !GetBridge()->IsEventFromCurrentSession(Session) || MessageId.IsEmpty())return;
    for(const auto& Pending:PendingLiveAcks)if(Pending.Session.Nonce==Session.Nonce && Pending.MessageId==MessageId && Pending.MessageType==MessageType)return;
    for(const auto& Pair:InFlightLiveAcks)if(Pair.Value.Session.Nonce==Session.Nonce && Pair.Value.MessageId==MessageId && Pair.Value.MessageType==MessageType)return;
    PendingLiveAcks.Add({Session,MessageId,MessageType});
}
void AArenaGameMode::FlushRenderedLiveEvents()
{
    if(GUsingNullRHI || !GetBridge()->IsConnected())return;
    SubmitRenderedLiveAcks();
}
void AArenaGameMode::SubmitRenderedLiveAcks()
{
    if(!GetBridge()->IsConnected())return;
    int32 Budget=128;
    for(int32 Index=0;Index<PendingLiveAcks.Num() && Budget>0;) {
        const auto Event=PendingLiveAcks[Index];
        if(!GetBridge()->IsEventFromCurrentSession(Event.Session)) {PendingLiveAcks.RemoveAt(Index);continue;}
        if(FPlatformTime::Seconds()<Event.RetryAfter){++Index;continue;}
        --Budget;
        auto Payload=MakeShared<FJsonObject>();Payload->SetStringField(TEXT("msg_id"),Event.MessageId);Payload->SetStringField(TEXT("msg_type"),Event.MessageType);
        const FString RequestId=GetBridge()->SubmitCommand(TEXT("ack"),Payload);
        if(!RequestId.IsEmpty()){InFlightLiveAcks.Add(RequestId,Event);PendingLiveAcks.RemoveAt(Index);}
        else ++Index;
    }
}
void AArenaGameMode::HandleLiveAckResult(const FLiveSession& Session,const FString& RequestId,bool bSuccess,int32)
{
    if(!GetBridge()->IsEventFromCurrentSession(Session))return;
    FPendingLiveAck Event;
    if(!InFlightLiveAcks.RemoveAndCopyValue(RequestId,Event) || Event.Session.Nonce!=Session.Nonce)return;
    if(!bSuccess){++Event.Attempts;Event.RetryAfter=FPlatformTime::Seconds()+FMath::Min(30.0,FMath::Pow(2.0,FMath::Min(Event.Attempts,5)));PendingLiveAcks.Add(MoveTemp(Event));}
}
void AArenaGameMode::UpdateLiveViewer(const FString& UserId)
{
    const auto* State=LiveViewerStates.Find(UserId);auto* Viewer=Viewers.Find(UserId);
    if(!State || !Viewer)return;
    Viewer->bPresent=State->bPresent;Viewer->bFollowsAnchor=State->bFollowsAnchor;Viewer->PresenceTimestampMs=State->TimestampMs;
    if(!State->Nickname.IsEmpty())Viewer->Name=State->Nickname.Left(20);
    if(State->Avatar)Viewer->Avatar=State->Avatar;
    else if(!State->AvatarUrl.IsEmpty())GetBridge()->RequestAvatar(UserId,State->AvatarUrl);
}
void AArenaGameMode::ApplyFollowQualification(const FString& UserId)
{
    if(GetBridge()->IsLocalTestMode())return;
    const auto* State=LiveViewerStates.Find(UserId);const auto* Viewer=Viewers.Find(UserId);
    if(!State || !State->bFollowsAnchor || !Viewer)return;
    const auto* Fighter=Match.findFighter(Viewer->BodyId);
    if(!Fighter || (Fighter->unlockedWeapons&gw::weaponBit(gw::WeaponKind::Rifle)))return;
    if(RememberWeapon(UserId,gw::WeaponKind::Rifle)) {
        RestoreWeapons(*Viewer);Match.grantWeapon(Viewer->BodyId,gw::WeaponKind::Rifle);RememberSelectedLeftWeapon(*Viewer);
        LastEvent=Viewer->Name+TEXT(" 关注主播，永久解锁步枪");EnqueueRewardNotice(UserId,gw::WeaponKind::Rifle,TEXT("关注主播，永久解锁"));
    }
}
void AArenaGameMode::HandleFollow(const FLiveFollow& Follow)
{
    if(!GetBridge()->IsEventFromCurrentSession(Follow.Session))return;
    if(Follow.Session.AnchorUserId.IsEmpty() || Follow.TargetUserId!=Follow.Session.AnchorUserId || Follow.Action<1 || Follow.Action>3) {
        QueueHandledLiveEvent(Follow.Session,Follow.MessageId,TEXT("live_follow"));return;
    }
    auto& State=LiveViewerStates.FindOrAdd(Follow.UserId);
    State.bFollowsAnchor=Follow.Action==1 || Follow.Action==3;
    if(!Follow.Nickname.IsEmpty())State.Nickname=Follow.Nickname;
    if(!Follow.AvatarUrl.IsEmpty())State.AvatarUrl=Follow.AvatarUrl;
    UpdateLiveViewer(Follow.UserId);ApplyFollowQualification(Follow.UserId);
    if(Follow.Action==2)LastEvent=TEXT("关注状态已更新，已取得的一次性武器权益保留");
    QueueHandledLiveEvent(Follow.Session,Follow.MessageId,TEXT("live_follow"));
}
void AArenaGameMode::HandlePresence(const FLivePresence& Presence)
{
    if(!GetBridge()->IsEventFromCurrentSession(Presence.Session))return;
    if(Presence.EnterType<1 || Presence.EnterType>2 || Presence.FollowStatus<0 || Presence.FollowStatus>3 || Presence.TimestampMs<0)return;
    auto& State=LiveViewerStates.FindOrAdd(Presence.UserId);
    // IDs already suppress replay at the bridge. Distinct SDK callbacks may
    // share a millisecond; timestamp zero carries no ordering information.
    if(Presence.TimestampMs>0 && State.TimestampMs>0 && Presence.TimestampMs<State.TimestampMs) {
        QueueHandledLiveEvent(Presence.Session,Presence.MessageId,TEXT("live_enter"));return;
    }
    // Retain the last positive timestamp so an undated callback cannot make a
    // subsequently delivered, older dated callback appear current.
    State.TimestampMs=FMath::Max(State.TimestampMs,Presence.TimestampMs);State.bPresent=Presence.EnterType==1;State.FollowStatus=Presence.FollowStatus;
    State.bFollowsAnchor=Presence.FollowStatus==1 || Presence.FollowStatus==2;
    State.InviterId=Presence.InviterId;State.EnterRoomScene=Presence.EnterRoomScene;
    if(!Presence.Nickname.IsEmpty())State.Nickname=Presence.Nickname;
    if(!Presence.AvatarUrl.IsEmpty())State.AvatarUrl=Presence.AvatarUrl;
    UpdateLiveViewer(Presence.UserId);
    if(Presence.EnterType==1 && Viewers.Contains(Presence.UserId))ApplyFollowQualification(Presence.UserId);
    // Presence caches qualification for spectators and grants it to existing
    // participants. Entering alone never admits a fighter.
    const auto* Viewer=Viewers.Find(Presence.UserId);
    LastEvent=Presence.EnterType==1?(Viewer?TEXT("观众进房状态已更新，继续参与"):TEXT("观众进房状态已更新，发送加入或选队后参与")):TEXT("观众离房状态已更新，战局角色保留");
    // Only the official gather scene proves a summon relationship. A normal
    // visit or share entry is not a gather, and no admission or reward occurs.
    if(Presence.EnterType==1 && Presence.EnterRoomScene==1 && !Presence.InviterId.IsEmpty() && Presence.InviterId!=Presence.UserId) {
        FString InviterName;
        if(const auto* Inviter=Viewers.Find(Presence.InviterId))InviterName=Inviter->Name;
        if(InviterName.IsEmpty())if(const auto* Inviter=LiveViewerStates.Find(Presence.InviterId))InviterName=Inviter->Nickname;
        if(InviterName.IsEmpty())InviterName=TEXT("好友");
        FGiftNotice Notice;Notice.UserId=Presence.UserId;Notice.Visual=EArenaNoticeVisual::Gather;
        Notice.ViewerName=Viewer?Viewer->Name:State.Nickname.Left(20);
        if(Notice.ViewerName.IsEmpty())Notice.ViewerName=TEXT("观众");
        Notice.Team=Viewer?Viewer->Team:0;
        Notice.Detail=TEXT("响应")+InviterName.Left(6)+TEXT("召集")+(Viewer?TEXT(" · 已参与"):TEXT(" · 发加入"));
        LastEvent=Notice.ViewerName+TEXT(" 响应")+InviterName.Left(20)+TEXT("的一键摇人召集")+(Viewer?TEXT("，继续参与"):TEXT("，发送加入或选队参与"));
        if(!Viewer && !State.Avatar && !State.AvatarUrl.IsEmpty())GetBridge()->RequestAvatar(Presence.UserId,State.AvatarUrl);
        Feed.enqueue(LastEvent,Notice.Team);
        QueueNotice(JoinNotices,JoinNoticeQueue,MoveTemp(Notice),3,5.5f);
    }
    QueueHandledLiveEvent(Presence.Session,Presence.MessageId,TEXT("live_enter"));
}
void AArenaGameMode::HandleTeamSelection(const FLiveTeamSelection& Selection)
{
    if(!GetBridge()->IsEventFromCurrentSession(Selection.Session))return;
    FString RedGroup,BlueGroup,GrayGroup;
    GConfig->GetString(TEXT("LiveInteraction"),TEXT("RedGroupId"),RedGroup,GGameIni);
    GConfig->GetString(TEXT("LiveInteraction"),TEXT("BlueGroupId"),BlueGroup,GGameIni);
    GConfig->GetString(TEXT("LiveInteraction"),TEXT("GrayGroupId"),GrayGroup,GGameIni);
    RedGroup.TrimStartAndEndInline();BlueGroup.TrimStartAndEndInline();GrayGroup.TrimStartAndEndInline();
    const int32 Team=Selection.GroupId==RedGroup?1:(Selection.GroupId==BlueGroup?2:(Selection.GroupId==GrayGroup?0:INDEX_NONE));
    if(Team==INDEX_NONE || RedGroup.IsEmpty() || BlueGroup.IsEmpty() || GrayGroup.IsEmpty() || RedGroup==BlueGroup || RedGroup==GrayGroup || BlueGroup==GrayGroup) {
        LastEvent=TEXT("快捷选队映射未配置或不匹配，当前未加入阵营");
        QueueHandledLiveEvent(Selection.Session,Selection.MessageId,TEXT("live_team"));return;
    }
    const auto* Before=Viewers.Find(Selection.UserId);
    if(!Before || Before->Team==0) {
        FLiveComment Join;Join.Session=Selection.Session;Join.MessageId=Selection.MessageId;Join.UserId=Selection.UserId;
        Join.Nickname=Selection.Nickname;Join.AvatarUrl=Selection.AvatarUrl;Join.Content=Team==0?TEXT("加入"):(Team==1?TEXT("1"):TEXT("2"));
        ApplyComment(Join);
    } else if(Before->Team!=Team)LastEvent=TEXT("本局阵营已确定，快捷选队不会修改角色形状或阵营");
    // The round reporter owns acknowledged, retried user_group submission.
    QueueHandledLiveEvent(Selection.Session,Selection.MessageId,TEXT("live_team"));
}
