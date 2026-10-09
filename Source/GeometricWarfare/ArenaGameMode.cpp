#include "ArenaGameMode.h"
#include "ArenaHUD.h"
#include "ArenaAudioSubsystem.h"
#include "AudioMixerBlueprintLibrary.h"
#include "ArenaLiveRounds.h"
#include "ArenaPlayerController.h"
#include "Engine/Texture2D.h"
#include "Engine/GameInstance.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"

AArenaGameMode::AArenaGameMode() {
    PrimaryActorTick.bCanEverTick=true;
    HUDClass=AArenaHUD::StaticClass(); PlayerControllerClass=AArenaPlayerController::StaticClass(); DefaultPawnClass=nullptr;
}
ULiveInteractionSubsystem* AArenaGameMode::GetBridge() const { return GetGameInstance()->GetSubsystem<ULiveInteractionSubsystem>(); }
void AArenaGameMode::BeginPlay() {
    Super::BeginPlay(); LastWall=RecordingStartWall=FPlatformTime::Seconds();
    GameAudio=GetWorld()->GetSubsystem<UArenaAudioSubsystem>();GameAudio->PrepareAudio();
    auto* Bridge=GetBridge(); Bridge->OnComment.AddDynamic(this,&AArenaGameMode::HandleComment);
    Bridge->OnLike.AddDynamic(this,&AArenaGameMode::HandleLike);
    Bridge->OnShare.AddDynamic(this,&AArenaGameMode::HandleShare);
    Bridge->OnGift.AddDynamic(this,&AArenaGameMode::HandleGift);
    Bridge->OnFollow.AddDynamic(this,&AArenaGameMode::HandleFollow);
    Bridge->OnPresence.AddDynamic(this,&AArenaGameMode::HandlePresence);
    Bridge->OnTeamSelection.AddDynamic(this,&AArenaGameMode::HandleTeamSelection);
    Bridge->OnAvatarReady.AddDynamic(this,&AArenaGameMode::HandleAvatar);
    Bridge->OnSessionChanged.AddDynamic(this,&AArenaGameMode::HandleSessionChanged);
    LiveAckResultHandle=Bridge->OnCommandResult.AddUObject(this,&AArenaGameMode::HandleLiveAckResult);
    LoadProgress();
    if(!Bridge->IsLocalTestMode()) {
        bShowControls=false;
        LastEvent=TEXT("等待平台 SDK 连接；当前没有模拟观众");
        return;
    }
    bVisualTest=FParse::Param(FCommandLine::Get(),TEXT("GWVisualTest"));
    bCompactOverviewPreview=FParse::Param(FCommandLine::Get(),TEXT("GWCompactOverviewPreview"));
    if(bCompactOverviewPreview){bVisualTest=true;VisualCaptureAt=6;Match.config.autoCombat=false;Match.config.autoCollect=false;}
    bStressTest=FParse::Param(FCommandLine::Get(),TEXT("GWStressTest"));
    bAudioStress=FParse::Param(FCommandLine::Get(),TEXT("GWAudioStress"));
    bAudioDisabled=FParse::Param(FCommandLine::Get(),TEXT("GWAudioDisabled"));
    if(bAudioStress)FMath::RandInit(1052026);
    bCombatStress=FParse::Param(FCommandLine::Get(),TEXT("GWCombatStress"));
    bRecording=FParse::Param(FCommandLine::Get(),TEXT("GWRecordDemo"));
    bShowControls=!bRecording;
    if(Bridge->IsLocalTestMode()) { AddMockUsers(bStressTest||bCompactOverviewPreview?gw::Match::ViewerCapacity:96); PumpMockUsers(bStressTest||bCompactOverviewPreview?128:96); }
    FocusBodyId=-1;Overview();
    if(bRecording) {
        IFileManager::Get().MakeDirectory(*(FPaths::ProjectSavedDir()/TEXT("DemoFrames")),true);
        DemoSpeed=4;
        FParse::Value(FCommandLine::Get(),TEXT("GWRecordSpeed="),DemoSpeed);
        DemoSpeed=FMath::Clamp(DemoSpeed,1.f,4.f);
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("GWResultsTest"))) { DemoAction(TEXT("end")); AddMockUsers(50); }
    if(FParse::Value(FCommandLine::Get(),TEXT("GWCombatTest="),CombatVisualMode)) {
        DemoAction(TEXT("combat-showcase"));bVisualTest=true;
        if(CombatVisualMode==TEXT("rage")){Match.boss.hp=Match.boss.maxHp*.4;Match.boss.rage=true;Match.boss.attack=gw::BossAttack::LaserWindup;Match.boss.attackInitialized=false;VisualCaptureAt=3.5f;}
        else if(CombatVisualMode==TEXT("laser")){Match.boss.attack=gw::BossAttack::LaserWindup;Match.boss.attackInitialized=false;VisualCaptureAt=5.5f;}
        else if(CombatVisualMode==TEXT("wave")){Match.boss.attack=gw::BossAttack::StompJump;Match.boss.attackInitialized=false;VisualCaptureAt=1.5f;}
        else if(CombatVisualMode==TEXT("cannon")){Match.boss.attack=gw::BossAttack::Bullet;Match.boss.attackInitialized=false;VisualCaptureAt=5.25f;}
        else if(CombatVisualMode==TEXT("impact")) {
            // Put two ordinary live bodies in the first missile's splash area.
            if(Match.fighters.size()>=2) {
                Match.world.bodies[0].position={gw::World::Size*.5+480,gw::World::Size*.5};Match.world.bodies[1].position={gw::World::Size*.5+480,gw::World::Size*.5+80};
                Match.world.bodies[0].velocity=Match.world.bodies[1].velocity={90,0};Match.world.rebuildSpatial();
                Match.boss.projectiles[0]={{gw::World::Size*.5+400,gw::World::Size*.5},{gw::BossBulletSpeed,0},gw::BossBulletRadius,0,100,true};
            }
            Match.boss.attack=gw::BossAttack::Bullet;Match.boss.attackInitialized=false;VisualCaptureAt=.22f;
        }
        else if(CombatVisualMode==TEXT("evolution")){for(auto& F:Match.fighters)if(F.evolutionRemaining>0)F.swordRemaining=.05;VisualCaptureAt=.25f;}
        else if(CombatVisualMode==TEXT("buffs") || CombatVisualMode==TEXT("gray-buffs")) {
            // Exercise the same kill reward and bridge-healing paths as gameplay.
            const int RewardTeam=CombatVisualMode==TEXT("gray-buffs")?0:1;
            for(const auto& F:Match.fighters)if(F.alive&&F.team==RewardTeam&&!F.isHost){Match.damageBoss(F.id,100000);break;}
            for(size_t i=0;i<Match.fighters.size()&&i<4;++i) {
                auto& F=Match.fighters[i];F.score=400-static_cast<int64_t>(i)*50;
                F.hp=F.maxHp*.6;
                Match.damageEnvironment(F.id,10);
                if(const auto* V=FindViewer(F.id))GetBridge()->SimulateLike(V->UserId,V->Name,1);
            }
            Match.refreshStandings();bSimulationPaused=true;VisualCaptureAt=.2f;
        }
        else VisualCaptureAt=4.9f;
        Match.boss.attackElapsed=0;
        // Isolate BOSS telegraph captures from the spectators' gunfire. The
        // evolution capture keeps normal weapons active to show impact feedback.
        if(CombatVisualMode==TEXT("rage")||CombatVisualMode==TEXT("laser")||CombatVisualMode==TEXT("wave")||CombatVisualMode==TEXT("cannon")||CombatVisualMode==TEXT("impact"))for(auto& F:Match.fighters)F.shotRemaining=20;
        PrepareGiftVisualTest();
        FParse::Value(FCommandLine::Get(),TEXT("GWCaptureAt="),VisualCaptureAt);
    }
#if !UE_BUILD_SHIPPING
    if(FParse::Param(FCommandLine::Get(),TEXT("GWHostAssistPreview")))HostAssist(1);
#endif
}
void AArenaGameMode::EndPlay(const EEndPlayReason::Type Reason) {
    ResetLiveRound();if(GameAudio)GameAudio->ResetAudio();
    if(bProgressDirty) SaveProgress();
    if(auto* Bridge=GetBridge()) { Bridge->OnComment.RemoveAll(this); Bridge->OnLike.RemoveAll(this); Bridge->OnShare.RemoveAll(this); Bridge->OnGift.RemoveAll(this); Bridge->OnFollow.RemoveAll(this); Bridge->OnPresence.RemoveAll(this); Bridge->OnTeamSelection.RemoveAll(this); Bridge->OnAvatarReady.RemoveAll(this); Bridge->OnSessionChanged.RemoveAll(this); Bridge->OnCommandResult.Remove(LiveAckResultHandle); }
    Super::EndPlay(Reason);
}
void AArenaGameMode::Tick(float DeltaSeconds) {
    Super::Tick(DeltaSeconds);
    const double Wall=FPlatformTime::Seconds(); FrameMs=(Wall-LastWall)*1000; LastWall=Wall;
    if(bRecording && Wall-RecordingStartWall<3) return;
    const double DisplayDt=bRecording?1.0/12:FMath::Min(DeltaSeconds,.25f);
    RunningTime+=DisplayDt; Feed.step(DisplayDt); DamageNumbers.step(DisplayDt); PumpMockUsers(64);
    TickGiftNotice(DisplayDt);
    if(bCompactOverviewPreview && Viewers.Num()==gw::Match::ViewerCapacity){JoinNotices.Reset();JoinNoticeQueue.Reset();Feed.reset();LastEvent=TEXT("500名观众 · 全图镜头 · 点击角色或榜单查看详情");}
    TickProgressSave(DisplayDt);
    UpdateHostProfile();
    if(!GetBridge()->IsLocalTestMode() && !GetBridge()->IsConnected() && !IsGMEnabled()){GameAudio->TickAudio(Match,FocusBodyId,HostBodyId,false);return;}
    if(!GetBridge()->IsLocalTestMode() && GetBridge()->IsConnected()) {TickLiveRound();if(LiveRoundId<=0 && !IsGMEnabled()){GameAudio->TickAudio(Match,FocusBodyId,HostBodyId,false);return;}}
    if(bCombatStress && bStressTest && !bCombatStressPrepared && Viewers.Num()==gw::Match::ViewerCapacity) {
        bCombatStressPrepared=true;Match.elapsed=gw::BossSpawnSeconds;Match.step(.001);HostAssist(1);
        const bool DualStress=GetBridge()->IsLocalTestMode()&&FParse::Param(FCommandLine::Get(),TEXT("GWDualStress"));
        // A worst-case fixture keeps all 500 dual wielders and the BOSS alive
        // throughout measurement; normal match balance is unchanged.
        if(DualStress){Match.boss.hp=Match.boss.maxHp=10000000;for(int Team=1;Team<=2;++Team)Match.bases[Team].hp=Match.bases[Team].maxHp=1000000;}
        for(size_t i=0;i<Match.fighters.size();++i)if(!Match.fighters[i].isHost){
            if(DualStress){
                const int32 Id=Match.fighters[i].id;
                Match.fighters[i].hp=Match.fighters[i].maxHp=1000000;
                Match.grantWeapon(Id,i%2?gw::WeaponKind::Rifle:gw::WeaponKind::MachineGun);
                gw::WeaponCrate Crate;Crate.position=Match.world.bodies[i].position;Crate.weaponKind=static_cast<gw::WeaponKind>(3+i%3);
                Match.weaponCrates={Crate};Match.collectWeaponCrate(Id,0);
            }
            else if(bAudioStress&&i%10==0)Match.grantWeapon(Match.fighters[i].id,static_cast<gw::WeaponKind>((i/10)%6));
            else if(i%10==0)Match.grantShotgun(Match.fighters[i].id);
        }
        if(DualStress)Match.events.clear();
        for(size_t i=0;i<5 && i<Match.evolutionPacks.size() && i<Match.fighters.size();++i)Match.collectEvolutionPack(Match.fighters[i].id,static_cast<int>(i));
        FocusBodyId=-1;Overview();
    }
    Match.audio.focusId=FocusBodyId;Match.audio.hostId=HostBodyId;
    const double Start=FPlatformTime::Seconds();
    if(!bSimulationPaused) {
        double Remaining=DisplayDt*DemoSpeed;
        while(Remaining>1e-7) { const double Slice=FMath::Min(Remaining,1.0/30); Match.step(Slice); ConsumeDamage(); Remaining-=Slice; }
    }
    SimulationMs=(FPlatformTime::Seconds()-Start)*1000; ConsumeEvents();
    if(bAudioDisabled){Match.audio.clear();GameAudio->ResetAudio();GameAudio->SchedulingMs=0;}else GameAudio->TickAudio(Match,FocusBodyId,HostBodyId,true);
    if(FocusBodyId>=0) if(const auto* B=Match.world.find(FocusBodyId)) CameraCenter=FVector2D(B->position.x,B->position.y);
    if(bRecording) TickRecording();
    if(bVisualTest && RunningTime>VisualCaptureAt && !bScreenshotTaken) {
        bScreenshotTaken=true;
        FString Name=CombatVisualMode.IsEmpty()?TEXT("FullDemo"):TEXT("Combat-")+CombatVisualMode;
        FString CaptureName;
        if(FParse::Value(FCommandLine::Get(),TEXT("GWCaptureName="),CaptureName))Name=FPaths::GetCleanFilename(CaptureName);
        FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/")+Name+TEXT(".png"),true,false);
    }
    if(bVisualTest && RunningTime>VisualCaptureAt+3) FPlatformMisc::RequestExit(false);
    if(bStressTest && Viewers.Num()==gw::Match::ViewerCapacity) {
        if(StressStart==0) StressStart=Wall;
        if(Wall-StressStart>5) {
            if(bAudioStress&&!bAudioDisabled&&StressFrames.IsEmpty())UAudioMixerBlueprintLibrary::StartRecordingOutput(this,30);
            StressFrames.Add(FrameMs); StressSimulation.Add(SimulationMs); StressRender.Add(RenderMs);StressAudio.Add(GameAudio->SchedulingMs);
            StressBossFrames+=Match.boss.active?1:0;StressMaxShots=FMath::Max(StressMaxShots,static_cast<int32>(Match.shots.size()));StressMaxSwords=FMath::Max(StressMaxSwords,static_cast<int32>(Match.swordWaves.size()));
            if(StressFrames.Num()%30==1) { int32 Alive=0; for(const auto& F:Match.fighters)Alive+=F.alive&&!F.isHost?1:0; StressMinAlive=FMath::Min(StressMinAlive,Alive);StressMaxAlive=FMath::Max(StressMaxAlive,Alive); }
        }
        if(Wall-StressStart>(bAudioStress?35:25)) {
            if(bAudioStress&&!bAudioDisabled)UAudioMixerBlueprintLibrary::StopRecordingOutput(this,EAudioRecordingExportType::WavFile,TEXT("AudioStress-Rendered"),FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()));
            SaveStressReport();
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/(bCombatStress?TEXT("Screenshots/Combat-500.png"):TEXT("Screenshots/Stress500.png")),true,false);
            bStressTest=false; bVisualTest=true; bScreenshotTaken=true; RunningTime=8;
        }
    }
}
void AArenaGameMode::ConsumeEvents() {
    for(const auto& E:Match.events) {
        switch(E.kind) {
        case gw::EventKind::BaseDestroyed: LastEvent=(E.team==1?FString(TEXT("红方")):FString(TEXT("蓝方")))+TEXT("基地被摧毁，暂停自动复活"); break;
        case gw::EventKind::BaseRebuilt: LastEvent=E.value>0?TEXT("基地强化成功，当前及最大生命增加"):TEXT("基地重建成功，阵营恢复自动复活"); break;
        case gw::EventKind::SprintStarted: LastEvent=FString::Printf(TEXT("最后 %d 秒！前十名复活并成为英雄，双方基地崩塌"),FMath::CeilToInt(Match.config.battleSeconds-Match.config.sprintSeconds)); Feed.enqueue(LastEvent); break;
        case gw::EventKind::RoundEnded: LastEvent=TEXT("本局结束，30 秒后自动开启下一局"); break;
        case gw::EventKind::RoundStarted: KillStreaks.Reset();RewardNotices.Reset();RewardNoticeQueue.Reset();DamageNumbers.reset(); LastEvent=TEXT("新一局开始！身份、阵营和形状保留，分数重新计算"); break;
        case gw::EventKind::Kill: {
            KillStreaks.Remove(E.targetId);
            if(E.actorId<0){LastEvent=ViewerName(E.targetId)+TEXT(" 阵亡");break;}
            const int32 Streak=++KillStreaks.FindOrAdd(E.actorId);
            FGiftNotice N;N.Team=E.team;N.Visual=EArenaNoticeVisual::People;N.ViewerName=ViewerName(E.actorId);
            if(const auto* V=FindViewer(E.actorId))N.UserId=V->UserId;
            N.Detail=TEXT("击败了 ")+ViewerName(E.targetId)+(Streak>=2?FString::Printf(TEXT(" · %d 次连续击败"),Streak):TEXT(""));
            LastEvent=N.ViewerName+TEXT(" ")+N.Detail;Feed.enqueue(LastEvent,E.team);
            QueueNotice(JoinNotices,JoinNoticeQueue,MoveTemp(N),3,5.5f);break;
        }
        case gw::EventKind::WeaponObtained: {
            if(const auto* V=FindViewer(E.actorId)) {
                const auto Kind=static_cast<gw::WeaponKind>(static_cast<int32>(E.value));
                // WeaponObtained is a crate snapshot, even if the lease ended
                // before the UI consumed it or this weapon is permanently owned.
                EnqueueRewardNotice(V->UserId,Kind,TEXT("武器箱奖励：右手临时60秒，阵亡或到期消失"),EArenaNoticeVisual::TemporaryWeapon);
            }break;
        }
        case gw::EventKind::EvolutionObtained: {
            FGiftNotice N;N.Team=E.team;N.Visual=EArenaNoticeVisual::Evolution;N.ViewerName=ViewerName(E.actorId);
            if(const auto* V=FindViewer(E.actorId))N.UserId=V->UserId;
            N.WeaponName=TEXT("进化");N.Detail=TEXT("进化 40 秒，生命强化、护甲与穿透剑气");
            QueueNotice(RewardNotices,RewardNoticeQueue,MoveTemp(N),2,3.2f);break;
        }
        case gw::EventKind::BossReward: {
            FGiftNotice N;N.Team=E.team;N.Visual=EArenaNoticeVisual::Boss;
            N.ViewerName=E.team==1?TEXT("红队"):E.team==2?TEXT("蓝队"):TEXT("灰队");N.WeaponName=TEXT("强化");
            N.Detail=TEXT("当时存活队员：伤害 / 新产出积分 +20% · 60 秒，进化 40 秒");
            LastEvent=N.ViewerName+TEXT(" 获得了 BOSS 强化");
            QueueNotice(RewardNotices,RewardNoticeQueue,MoveTemp(N),2,3.2f);break;
        }
        case gw::EventKind::WeaponSwitched: {
            // Only a manual comment creates a switch announcement. Gifts and
            // qualification rewards already have their own visible notice.
            break;
        }
        default: break;
        }
    }
    Match.events.clear();
}
void AArenaGameMode::ConsumeDamage() {
    for(const auto& Hit:Match.damageEvents) DamageNumbers.add(Hit);
    Match.damageEvents.clear();
}
const FViewerState* AArenaGameMode::FindViewer(int32 BodyId) const { if(BodyId>=0 && BodyId==HostBodyId)return &HostViewer;const FString* Key=Identities.Find(BodyId); return Key?Viewers.Find(*Key):nullptr; }
FString AArenaGameMode::ViewerName(int32 BodyId) const { const auto* V=FindViewer(BodyId);if(V)return V->Name;const auto* F=Match.findFighter(BodyId);return F&&F->isHost?TEXT("主播 · 助战"):TEXT("暂无获奖者"); }
void AArenaGameMode::HandleComment(const FLiveComment& Comment) {
    if(!GetBridge()->IsEventFromCurrentSession(Comment.Session))return;
    ApplyComment(Comment);
    QueueHandledLiveEvent(Comment.Session,Comment.MessageId,TEXT("live_comment"));
}
void AArenaGameMode::ApplyComment(const FLiveComment& Comment) {
    if(!GetBridge()->IsEventFromCurrentSession(Comment.Session)) return;
    const FString Command=Comment.Content.TrimStartAndEnd(); auto* Viewer=Viewers.Find(Comment.UserId);
    if(Command==TEXT("加入") || (!Viewer && (Command==TEXT("1") || Command==TEXT("2")))) {
        if(Viewer) { Viewer->HighlightUntil=RunningTime+3; LastEvent=Viewer->Name+TEXT(" 已在场中，保持当前镜头"); return; }
        const int32 Team=Command==TEXT("1")?1:(Command==TEXT("2")?2:0);
        const int32 Id=NextBodyId;
        if(!Match.add(Id,static_cast<gw::Shape>(FMath::RandRange(0,3)),Team)) {
            LastEvent=Team==0?FString::Printf(TEXT("灰色已满 %d 人；可发送 1 / 2 直接加入未满的红蓝阵营"),gw::Match::TeamCapacity(0)):
                FString::Printf(TEXT("%s已满 %d 人，请选择其他未满阵营"),Team==1?TEXT("红方"):TEXT("蓝方"),gw::Match::TeamCapacity(Team)); return;
        }
        ++NextBodyId;
        FViewerState State; State.UserId=Comment.UserId; State.Name=Comment.Nickname.IsEmpty()?TEXT("观众"):Comment.Nickname.Left(20);
        State.BodyId=Id; State.Team=Team; State.Avatar=MakePlaceholder(Id); State.HighlightUntil=RunningTime+3;
        Viewers.Add(State.UserId,State); Identities.Add(Id,State.UserId);
        RestoreWeapons(State,true);
        UpdateLiveViewer(State.UserId);
        ApplyFollowQualification(State.UserId);
        if(PersonalLikes.FindRef(State.UserId)>=1000) {
            const auto* F=Match.findFighter(Id);
            if(F && !(F->unlockedWeapons&gw::weaponBit(gw::WeaponKind::Shotgun)) && RememberWeapon(State.UserId,gw::WeaponKind::Shotgun)) {
                RestoreWeapons(State);Match.grantShotgun(Id);RememberSelectedLeftWeapon(State);EnqueueRewardNotice(State.UserId,gw::WeaponKind::Shotgun,TEXT("个人累计点赞达到 1000，永久解锁左手"));
            }
        }
        EnqueueJoinNotice(Viewers.FindChecked(State.UserId));
        if(!Comment.AvatarUrl.IsEmpty()) GetBridge()->RequestAvatar(Comment.UserId,Comment.AvatarUrl);
        LastEvent=State.Name+TEXT(" 加入了竞技场"); return;
    }
    if(!Viewer) { LastEvent=TEXT("请先发送“加入”"); return; }
    if(!Comment.Nickname.IsEmpty()) Viewer->Name=Comment.Nickname.Left(20);
    ApplyFollowQualification(Viewer->UserId);
    if(HandleWeaponCommand(Command,*Viewer)) return;
    if(Match.phase==gw::Phase::Results) { LastEvent=TEXT("结算中，已保留你的身份；下一局开始后可选队换形"); return; }
    if(Viewer->Team==0) {
        if(Command==TEXT("1") || Command==TEXT("2")) {
            const int32 Team=Command==TEXT("1")?1:2;
            if(Match.chooseTeam(Viewer->BodyId,Team)) { Viewer->Team=Team; Viewer->HighlightUntil=RunningTime+3; EnqueueJoinNotice(*Viewer,true); LastEvent=Viewer->Name+(Team==1?TEXT(" 选择了红方"):TEXT(" 选择了蓝方")); }
            else LastEvent=FString::Printf(TEXT("%s已满 %d 人，当前保留灰色身份"),Team==1?TEXT("红方"):TEXT("蓝方"),gw::Match::TeamCapacity(Team));
        } else LastEvent=TEXT("先选择阵营：1 红方 / 2 蓝方");
        return;
    }
    int32 ShapeIndex=INDEX_NONE;
    if(Command.Len()==1) {
        const TCHAR Letter=FChar::ToLower(Command[0]);
        if(Letter>=TEXT('1') && Letter<=TEXT('4'))ShapeIndex=Letter-TEXT('1');
        else if(Letter==TEXT('y'))ShapeIndex=0;
        else if(Letter==TEXT('z'))ShapeIndex=1;
        else if(Letter==TEXT('c'))ShapeIndex=2;
        else if(Letter==TEXT('s'))ShapeIndex=3;
    }
    if(ShapeIndex!=INDEX_NONE) {
        if(Match.changeShape(Viewer->BodyId,static_cast<gw::Shape>(ShapeIndex))) { Viewer->HighlightUntil=RunningTime+2; LastEvent=Viewer->Name+TEXT(" 更换了形状"); }
        else LastEvent=TEXT("换形需要间隔 2 秒，生命比例与武器状态保留");
    }
}
void AArenaGameMode::HandleLike(const FLiveLike& Like) {
    if(!GetBridge()->IsEventFromCurrentSession(Like.Session) || Like.Count<=0) return;
    QueueHandledLiveEvent(Like.Session,Like.MessageId,TEXT("live_like"));
    // Stable identity, validated incremental units; saturate at the unlock
    // threshold rather than adding int64 counts that can overflow.
    auto& Total=PersonalLikes.FindOrAdd(Like.UserId);Total+=FMath::Min<int64>(Like.Count,1000-Total);
    const auto* V=Viewers.Find(Like.UserId);
    if(!V){LastEvent=TEXT("个人点赞已累计；加入战局后领取达到 1000 的霰弹枪权益");return;}
    const auto* F=Match.findFighter(V->BodyId);
    if(Total>=1000 && F && !(F->unlockedWeapons&gw::weaponBit(gw::WeaponKind::Shotgun)) && RememberWeapon(V->UserId,gw::WeaponKind::Shotgun)) {
        RestoreWeapons(*V);Match.grantShotgun(V->BodyId);
        RememberSelectedLeftWeapon(*V);
        EnqueueRewardNotice(V->UserId,gw::WeaponKind::Shotgun,TEXT("个人累计点赞达到 1000，永久解锁左手"));
    }
    const double BeforeHp=F?F->hp:0;
    const bool HealingAllowed=Match.healLike(V->BodyId,FMath::Min<int64>(Like.Count,20));
    LastEvent=HealingAllowed?(F&&F->hp>BeforeHp?V->Name+FString::Printf(TEXT(" 点赞 ×%lld，恢复生命"),Like.Count):TEXT("点赞已累计；生命已满")):TEXT("点赞已累计；阵亡或结算期间不恢复生命");
    ConsumeDamage();
}
void AArenaGameMode::HandleShare(const FLiveShare& Share) {
    if(!GetBridge()->IsEventFromCurrentSession(Share.Session)) return;
    LastEvent=TEXT("分享已收到；个人累计点赞 1000 解锁霰弹枪，关注主播解锁步枪");
}
bool AArenaGameMode::CanHostAssist() const {
    const auto* Bridge=GetBridge();
    return Match.phase!=gw::Phase::Results && Bridge && (Bridge->IsLocalTestMode() || IsGMEnabled()
        || (Bridge->IsConnected() && LiveRoundReporter.IsValid() && LiveRoundReporter->IsCurrentRoundActive()));
}
int32 AArenaGameMode::GetHostTeam() const {const auto* F=Match.findFighter(HostBodyId);return F && F->isHost?F->team:-1;}
void AArenaGameMode::HostAssist(int32 Team) {
    if(!CanHostAssist()){LastEvent=Match.phase==gw::Phase::Results?TEXT("结算期间无法调整主播阵营"):TEXT("等待直播对局就绪后可助战");return;}
    if(Team<0||Team>2||Match.phase==gw::Phase::Results){LastEvent=TEXT("结算期间无法调整主播阵营");return;}
    const int32 PreviousTeam=GetHostTeam();
    if(HostBodyId<0){const int32 Id=NextBodyId;if(!Match.addHost(Id,Team))return;++NextBodyId;HostBodyId=Id;Feed.enqueue(TEXT("主播以五角星身份进入竞技场"));}
    else if(!Match.setHostTeam(HostBodyId,Team))return;
    UpdateHostProfile();
    if(PreviousTeam!=Team&&GameAudio)GameAudio->HostAssist();
    FocusViewer(HostBodyId);
    LastEvent=Team==1?TEXT("主播加入红方助战 · 不获取积分或奖项"):Team==2?TEXT("主播加入蓝方助战 · 不获取积分或奖项"):TEXT("主播以中立身份下场 · 不获取积分或奖项");
}
void AArenaGameMode::HandleAvatar(const FString& UserId,UTexture2D* Texture) {
    if(!UserId.IsEmpty() && UserId==GetBridge()->GetCurrentSession().AnchorUserId)HostViewer.Avatar=Texture;
    if(auto* State=LiveViewerStates.Find(UserId))State->Avatar=Texture;
    if(auto* V=Viewers.Find(UserId)) V->Avatar=Texture;
}
void AArenaGameMode::AddMockUsers(int32 Count) {
    if(!GetBridge()->IsLocalTestMode()) { LastEvent=TEXT("正式平台模式不可添加模拟观众"); return; }
    PendingMockUsers=FMath::Clamp(PendingMockUsers+FMath::Max(0,Count),0,gw::Match::ViewerCapacity-Viewers.Num());
}
void AArenaGameMode::PumpMockUsers(int32 Budget) {
    if(!GetBridge()->IsLocalTestMode()) { PendingMockUsers=0; return; }
    static const TCHAR* Names[]={TEXT("小橘"),TEXT("阿白"),TEXT("月亮"),TEXT("小雨"),TEXT("星河"),TEXT("北风"),TEXT("团子"),TEXT("青柠"),TEXT("栗子"),TEXT("南山"),TEXT("泡泡"),TEXT("山海"),TEXT("可乐"),TEXT("小鹿"),TEXT("云朵"),TEXT("麦芽")};
    for(int32 i=0;i<Budget && PendingMockUsers>0;++i) {
        --PendingMockUsers; ++DemoCounter;
        const FString Id=FString::Printf(TEXT("demo-%d"),DemoCounter);
        const FString Name=FString(Names[(DemoCounter-1)%16])+FString::FromInt((DemoCounter-1)/16+1);
        int32 Team=FMath::RandRange(0,2);
        if(Match.teamCounts[Team]>=gw::Match::TeamCapacity(Team)) {
            for(int32 T=0;T<3;++T) if(Match.teamCounts[T]<gw::Match::TeamCapacity(T)) { Team=T; break; }
        }
        GetBridge()->SimulateComment(Id,Name,Team==0?TEXT("加入"):(Team==1?TEXT("1"):TEXT("2")));
    }
}
void AArenaGameMode::ResetArena() {
    if(!GetBridge()->IsLocalTestMode()) { LastEvent=TEXT("正式平台模式不可重置演示"); return; }
    HandleSessionChanged();
    LastEvent=TEXT("场地已重置，发送“加入”重新开始");
}
void AArenaGameMode::HandleSessionChanged() {
    ResetLiveRound();
    if(GameAudio)GameAudio->ResetAudio();
    Match.reset(); Viewers.Empty(); Identities.Empty(); Feed.reset(); DamageNumbers.reset(); PendingMockUsers=0;HostBodyId=-1;FocusBodyId=-1;
    GiftNotice=FGiftNotice{}; GiftNoticeQueue.Reset();GiftNotices.Reset();RewardNotices.Reset();RewardNoticeQueue.Reset();JoinNotices.Reset();JoinNoticeQueue.Reset();PersonalLikes.Reset();KillStreaks.Reset();
    LiveViewerStates.Empty();PendingLiveAcks.Empty();InFlightLiveAcks.Empty();LiveRoundId=0;
    HostViewer=FViewerState{};GMIdentityBindings.Reset();
    bSimulationPaused=false; bRecording=false; bVisualTest=false; bStressTest=false; bCombatStress=false;bCompactOverviewPreview=false;
    DemoSpeed=1; Overview(); LastEvent=TEXT("平台会话已切换，等待观众参与");
}
void AArenaGameMode::ZoomBy(float Factor) { CameraZoom=FMath::Clamp(CameraZoom*Factor,1.f,12.f); }
void AArenaGameMode::FocusViewer(int32 Id) { if(const auto* Body=Match.world.find(Id)) { FocusBodyId=Id; CameraZoom=6; CameraCenter={Body->position.x,Body->position.y}; } }
void AArenaGameMode::Overview() { if(FocusBodyId>=0)return;CameraZoom=1; CameraCenter={gw::World::Size*.5,gw::World::Size*.5}; }
void AArenaGameMode::MoveCamera(float X,float Y) { if(FocusBodyId>=0)return;CameraCenter+=FVector2D(X,Y)*(700/CameraZoom); }
void AArenaGameMode::SetDemoSpeed(float Speed) { if(GetBridge()->IsLocalTestMode()) DemoSpeed=FMath::Clamp(Speed,1.f,8.f); }
void AArenaGameMode::DemoAction(const FString& Action) {
    if(Action.StartsWith(TEXT("gm-add-"))){AddGMBots(Action==TEXT("gm-add-red")?1:Action==TEXT("gm-add-blue")?2:0);return;}
    if(!GetBridge()->IsLocalTestMode()) { LastEvent=TEXT("此操作仅用于本地演示"); return; }
    if(Action==TEXT("host-red")||Action==TEXT("host-blue")||Action==TEXT("host-gray")) {HostAssist(Action==TEXT("host-red")?1:Action==TEXT("host-blue")?2:0);}
    else if(Action==TEXT("combat-showcase")) {
        if(Match.phase!=gw::Phase::Battle || Match.elapsed>gw::BossSpawnSeconds)Match.startNextRound();
        if(Viewers.Num()<12){AddMockUsers(24);PumpMockUsers(24);}
        // A local demonstration preset: advance resource timers and arrange a
        // readable encounter. Live sessions never enter this branch.
        Match.elapsed=gw::BossSpawnSeconds;Match.boss=gw::BossState{};Match.step(.001);HostAssist(1);
        int32 Index=0;
        for(auto& F:Match.fighters){
            if(!F.alive)Match.revive(F.id);
            auto* B=Match.world.find(F.id);
            const double Center=gw::World::Size*.5;
            if(F.isHost){B->position={Center-310,Center-50};B->velocity={55,30};continue;}
            if(Index<12){const double A=Index*PI/6;B->position={Center+FMath::Cos(A)*(Index<6?360:610),Center+FMath::Sin(A)*(Index<6?360:610)};B->velocity={-FMath::Sin(A)*95,FMath::Cos(A)*95};}
            if(Index<3){Match.grantShotgun(F.id);if(!Match.evolutionPacks.empty()){Match.evolutionPacks[0].active=true;Match.collectEvolutionPack(F.id,0);F.swordRemaining=4.8;}}
            ++Index;
        }
        Match.world.rebuildSpatial();FocusBodyId=-1;Overview();
        LastEvent=TEXT("本地战斗展示：主播 / 霰弹枪 / 进化 / 中央 BOSS");
    } else if(Action==TEXT("sprint")) {
        while(Match.phase==gw::Phase::Battle && Match.elapsed<Match.config.sprintSeconds) Match.step(FMath::Min(1.0/30,Match.config.sprintSeconds-Match.elapsed));
        LastEvent=TEXT("本地演示已快进至冲刺阶段");
    } else if(Action==TEXT("end")) {
        while(Match.phase!=gw::Phase::Results) Match.step(1.0/30);
    } else if(Action==TEXT("rebuild-red") || Action==TEXT("rebuild-blue")) {
        const int32 Team=Action==TEXT("rebuild-red")?1:2;
        LastEvent=Match.rebuildBase(Team)?TEXT("模拟礼物：基地重建成功"):TEXT("无法重建：基地尚存 / 冲刺阶段 / 本局已结束");
    } else if(Action==TEXT("revive")) {
        int32 Id=FocusBodyId;
        if(Id<0) for(const auto& F:Match.fighters) if(!F.alive && F.team!=0) { Id=F.id; break; }
        const auto* Recipient=Match.findFighter(Id);const bool WasAlive=Recipient&&Recipient->alive;
        LastEvent=Match.applyFairyWand(Id,1)?(WasAlive?TEXT("模拟仙女棒：基础生命 +30，死亡清零"):TEXT("模拟仙女棒：个人复活成功")):TEXT("请先选择观众；结算期间无法复活或强化");
    } else if(Action==TEXT("destroy-blue")) {
        for(const auto& F:Match.fighters) if(F.team==1 && F.alive) { Match.damageBase(F.id,2,100000); break; }
        LastEvent=TEXT("本地演示：蓝方基地受击摧毁");
    }
}
void AArenaGameMode::TickRecording() {
    DemoCaption=FString::Printf(TEXT("本地模拟演示 · %d 倍速 · 无真实礼物消费"),FMath::RoundToInt(DemoSpeed));
    if(Match.elapsed>100 && Match.elapsed<112) DemoCaption=TEXT("模拟攻击：蓝方基地被摧毁，停止自动复活");
    if(Match.elapsed>=112 && Match.elapsed<125) DemoCaption=TEXT("模拟礼物：蓝方基地重建，恢复自动复活资格");
    if(Match.elapsed>=Match.config.sprintSeconds && Match.elapsed<Match.config.sprintSeconds+12) DemoCaption=FString::Printf(TEXT("最后 %d 秒：前十成为英雄，基地永久摧毁"),FMath::CeilToInt(Match.config.battleSeconds-Match.config.sprintSeconds));
    if(Match.elapsed>8 && DemoStage==0) {HostAssist(1);if(const auto* V=FindViewer(1)){for(int N=0;N<10;++N)GetBridge()->SimulateLike(V->UserId,V->Name,100);}FocusBodyId=-1;Overview();DemoStage=1;}
    if(Match.elapsed>35 && DemoStage==1) { FocusBodyId=-1;Overview();DemoStage=2; }
    if(Match.elapsed>65 && DemoStage==2) { if(!Match.leaderboard.empty()) FocusViewer(Match.fighters[Match.leaderboard[0]].id); DemoStage=3; }
    if(Match.elapsed>100 && DemoStage==3) { DemoAction(TEXT("destroy-blue")); FocusBodyId=-1; CameraCenter={Match.bases[2].position.x,Match.bases[2].position.y}; CameraZoom=4; DemoStage=4; }
    if(Match.elapsed>112 && DemoStage==4) { DemoAction(TEXT("rebuild-blue")); DemoStage=5; }
    if(Match.elapsed>125 && DemoStage==5) { FocusBodyId=-1;Overview(); DemoStage=6; }
    if(Match.phase==gw::Phase::Sprint && DemoStage==6) { DemoAction(TEXT("revive")); DemoStage=7; }
    if(Match.phase==gw::Phase::Results){FocusBodyId=-1;Overview();}
    const FString Path=FPaths::ProjectSavedDir()/FString::Printf(TEXT("DemoFrames/Frame_%05d.png"),RecordFrame++);
    FScreenshotRequest::RequestScreenshot(Path,true,false);
    if(Match.round>=2 && Match.elapsed>8) FPlatformMisc::RequestExit(false);
}
void AArenaGameMode::SaveStressReport() {
    auto Stats=[](TArray<double> Values) {
        if(Values.IsEmpty()) return FString(TEXT("{\"mean_ms\":0,\"p95_ms\":0}"));
        double Sum=0; for(double V:Values) Sum+=V; Values.Sort();
        return FString::Printf(TEXT("{\"mean_ms\":%.3f,\"p95_ms\":%.3f}"),Sum/Values.Num(),Values[FMath::Min(Values.Num()-1,FMath::FloorToInt(Values.Num()*.95))]);
    };
    const bool DualStress=GetBridge()->IsLocalTestMode()&&FParse::Param(FCommandLine::Get(),TEXT("GWDualStress"));
    const TCHAR* Mode=DualStress?TEXT("UE offscreen worst-case; 500 live dual wielders plus host and BOSS; artificial fixture HP preserves simultaneous combat during measurement"):bCombatStress?TEXT("UE offscreen overview; 500 viewers plus host; dead participants included; preset starts at 150s; audio fixture uses six weapons across 50 viewers and five evolved viewers"):TEXT("UE offscreen overview, local auto-combat; player count includes dead participants");
    const FString Report=FString::Printf(TEXT("{\"players\":%d,\"host\":%d,\"sampled_alive_min\":%d,\"sampled_alive_max\":%d,\"samples\":%d,\"boss_active_samples\":%d,\"max_visible_shots\":%d,\"max_sword_waves\":%d,\"frame\":%s,\"simulation\":%s,\"hud\":%s,\"mode\":\"%s\"}"),Viewers.Num(),HostBodyId>=0?1:0,StressMinAlive,StressMaxAlive,StressFrames.Num(),StressBossFrames,StressMaxShots,StressMaxSwords,*Stats(StressFrames),*Stats(StressSimulation),*Stats(StressRender),Mode);
    FString FinalReport=Report;FinalReport.RemoveAt(FinalReport.Len()-1);
    FinalReport+=FString::Printf(TEXT(",\"audio\":%s,\"audio_enabled\":%s,\"audio_device\":%s,\"audio_events\":%llu,\"audio_starts\":%llu,\"audio_dropped\":%llu,\"sfx_peak\":%d,\"audio_missing\":%d}"),*Stats(StressAudio),bAudioDisabled?TEXT("false"):TEXT("true"),GameAudio->HasAudioDevice()?TEXT("true"):TEXT("false"),static_cast<unsigned long long>(GameAudio->Events),static_cast<unsigned long long>(GameAudio->Starts),static_cast<unsigned long long>(GameAudio->Dropped),GameAudio->ActivePeak,GameAudio->MissingAssets);
    FFileHelper::SaveStringToFile(FinalReport,*(FPaths::ProjectSavedDir()/(bCombatStress?TEXT("CombatStress500.json"):TEXT("Stress500.json"))));
}
UTexture2D* AArenaGameMode::MakePlaceholder(int32 Seed) {
    const int32 Index=Seed%32; if(PlaceholderPool.Num()<32) PlaceholderPool.SetNum(32);
    if(PlaceholderPool[Index]) return PlaceholderPool[Index];
    constexpr int32 Size=32;
    auto* Texture=UTexture2D::CreateTransient(Size,Size,PF_B8G8R8A8); if(!Texture) return nullptr;
    const FColor Background=FLinearColor::MakeFromHSV8(static_cast<uint8>(Index*37),110,238).ToFColor(true);
    auto* Pixels=static_cast<FColor*>(Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
    for(int32 y=0;y<Size;++y) for(int32 x=0;x<Size;++x) {
        const bool Head=FMath::Square(x-16)+FMath::Square(y-15)<10*10;
        const bool Eye=(y>=13 && y<=15 && ((x>=11 && x<=12)||(x>=20 && x<=21)));
        const bool Smile=(y==20 && x>=14 && x<=18);
        Pixels[y*Size+x]=(Eye||Smile)?FColor(36,42,45):(Head?FColor(251,246,235):Background);
    }
    Texture->GetPlatformData()->Mips[0].BulkData.Unlock(); Texture->UpdateResource(); PlaceholderPool[Index]=Texture; return Texture;
}
