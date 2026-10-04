#include "ArenaGameMode.h"
#include "ArenaHUD.h"
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
UDouyinLiveSubsystem* AArenaGameMode::GetBridge() const { return GetGameInstance()->GetSubsystem<UDouyinLiveSubsystem>(); }
void AArenaGameMode::BeginPlay() {
    Super::BeginPlay(); LastWall=RecordingStartWall=FPlatformTime::Seconds();
    auto* Bridge=GetBridge(); Bridge->OnComment.AddDynamic(this,&AArenaGameMode::HandleComment);
    Bridge->OnLike.AddDynamic(this,&AArenaGameMode::HandleLike);
    Bridge->OnShare.AddDynamic(this,&AArenaGameMode::HandleShare);
    Bridge->OnGift.AddDynamic(this,&AArenaGameMode::HandleGift);
    Bridge->OnAvatarReady.AddDynamic(this,&AArenaGameMode::HandleAvatar);
    bVisualTest=FParse::Param(FCommandLine::Get(),TEXT("GWVisualTest"));
    bStressTest=FParse::Param(FCommandLine::Get(),TEXT("GWStressTest"));
    bCombatStress=FParse::Param(FCommandLine::Get(),TEXT("GWCombatStress"));
    bRecording=FParse::Param(FCommandLine::Get(),TEXT("GWRecordDemo"));
    bShowControls=!bRecording;
    LoadProgress();
    if(!Bridge->IsRelayMode()) { AddMockUsers(bStressTest?5000:96); PumpMockUsers(bStressTest?128:96); }
    if(!bStressTest && !Bridge->IsRelayMode()) {
        int32 i=0; for(auto& B:Match.world.bodies) { B.position={3200.0+(i%12)*145,3350.0+(i/12)*145}; ++i; }
        Match.world.rebuildSpatial(); CameraZoom=4.2f;
    }
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
                Match.world.bodies[0].position={4480,4000};Match.world.bodies[1].position={4480,4080};
                Match.world.bodies[0].velocity=Match.world.bodies[1].velocity={90,0};Match.world.rebuildSpatial();
                Match.boss.projectiles[0]={{4400,4000},{gw::BossBulletSpeed,0},gw::BossBulletRadius,0,100,true};
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
}
void AArenaGameMode::EndPlay(const EEndPlayReason::Type Reason) {
    if(bProgressDirty) SaveProgress();
    if(auto* Bridge=GetBridge()) { Bridge->OnComment.RemoveAll(this); Bridge->OnLike.RemoveAll(this); Bridge->OnShare.RemoveAll(this); Bridge->OnGift.RemoveAll(this); Bridge->OnAvatarReady.RemoveAll(this); }
    Super::EndPlay(Reason);
}
void AArenaGameMode::Tick(float DeltaSeconds) {
    Super::Tick(DeltaSeconds);
    const double Wall=FPlatformTime::Seconds(); FrameMs=(Wall-LastWall)*1000; LastWall=Wall;
    if(bRecording && Wall-RecordingStartWall<3) return;
    const double DisplayDt=bRecording?1.0/12:FMath::Min(DeltaSeconds,.25f);
    RunningTime+=DisplayDt; Feed.step(DisplayDt); DamageNumbers.step(DisplayDt); PumpMockUsers(64);
    TickGiftNotice(DisplayDt);
    TickProgressSave(DisplayDt);
    if(bCombatStress && bStressTest && !bCombatStressPrepared && Viewers.Num()==5000) {
        bCombatStressPrepared=true;Match.elapsed=gw::BossSpawnSeconds;Match.step(.001);HostAssist(1);
        for(size_t i=0;i<Match.fighters.size();++i)if(!Match.fighters[i].isHost && i%10==0)Match.grantShotgun(Match.fighters[i].id);
        for(size_t i=0;i<5 && i<Match.evolutionPacks.size() && i<Match.fighters.size();++i)Match.collectEvolutionPack(Match.fighters[i].id,static_cast<int>(i));
        FocusBodyId=-1;Overview();
    }
    const double Start=FPlatformTime::Seconds();
    if(!bSimulationPaused) {
        double Remaining=DisplayDt*DemoSpeed;
        while(Remaining>1e-7) { const double Slice=FMath::Min(Remaining,1.0/30); Match.step(Slice); ConsumeDamage(); Remaining-=Slice; }
    }
    SimulationMs=(FPlatformTime::Seconds()-Start)*1000; ConsumeEvents();
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
    if(bStressTest && Viewers.Num()==5000) {
        if(StressStart==0) StressStart=Wall;
        if(Wall-StressStart>5) {
            StressFrames.Add(FrameMs); StressSimulation.Add(SimulationMs); StressRender.Add(RenderMs);
            StressBossFrames+=Match.boss.active?1:0;StressMaxShots=FMath::Max(StressMaxShots,static_cast<int32>(Match.shots.size()));StressMaxSwords=FMath::Max(StressMaxSwords,static_cast<int32>(Match.swordWaves.size()));
            if(StressFrames.Num()%30==1) { int32 Alive=0; for(const auto& F:Match.fighters)Alive+=F.alive&&!F.isHost?1:0; StressMinAlive=FMath::Min(StressMinAlive,Alive);StressMaxAlive=FMath::Max(StressMaxAlive,Alive); }
        }
        if(Wall-StressStart>25) {
            SaveStressReport();
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/(bCombatStress?TEXT("Screenshots/Combat-5000.png"):TEXT("Screenshots/Stress5000.png")),true,false);
            bStressTest=false; bVisualTest=true; bScreenshotTaken=true; RunningTime=8;
        }
    }
}
void AArenaGameMode::ConsumeEvents() {
    for(const auto& E:Match.events) {
        switch(E.kind) {
        case gw::EventKind::BaseDestroyed: LastEvent=(E.team==1?FString(TEXT("红方")):FString(TEXT("蓝方")))+TEXT("基地被摧毁，暂停自动复活"); break;
        case gw::EventKind::BaseRebuilt: LastEvent=TEXT("模拟重建生效：基地恢复，阵营可再次自动复活"); break;
        case gw::EventKind::SprintStarted: LastEvent=FString::Printf(TEXT("最后 %d 秒！前十名复活并成为英雄，双方基地崩塌"),FMath::CeilToInt(Match.config.battleSeconds-Match.config.sprintSeconds)); Feed.enqueue(LastEvent); break;
        case gw::EventKind::RoundEnded: LastEvent=TEXT("本局结束，30 秒后自动开启下一局"); break;
        case gw::EventKind::RoundStarted: DamageNumbers.reset(); LastEvent=TEXT("新一局开始！身份、阵营和形状保留，分数重新计算"); break;
        case gw::EventKind::WeaponSwitched: {
            static const TCHAR* Names[]={TEXT("手枪"),TEXT("霰弹枪"),TEXT("步枪"),TEXT("狙击枪·巴雷特"),TEXT("机枪·加特林"),TEXT("火箭筒")};
            const int32 Kind=static_cast<int32>(E.value);
            if(Kind>=0 && Kind<gw::WeaponCount) { LastEvent=ViewerName(E.actorId)+TEXT(" 切换了枪械“")+Names[Kind]+TEXT("”"); Feed.enqueue(LastEvent); }
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
const FViewerState* AArenaGameMode::FindViewer(int32 BodyId) const { const FString* Key=Identities.Find(BodyId); return Key?Viewers.Find(*Key):nullptr; }
FString AArenaGameMode::ViewerName(int32 BodyId) const { const auto* V=FindViewer(BodyId);if(V)return V->Name;const auto* F=Match.findFighter(BodyId);return F&&F->isHost?TEXT("主播 · 助战"):TEXT("暂无获奖者"); }
void AArenaGameMode::HandleComment(const FDouyinComment& Comment) {
    const FString Command=Comment.Content.TrimStartAndEnd(); auto* Viewer=Viewers.Find(Comment.UserId);
    if(Command==TEXT("加入") || (!Viewer && (Command==TEXT("1") || Command==TEXT("2")))) {
        if(Viewer) { Viewer->HighlightUntil=RunningTime+3; FocusViewer(Viewer->BodyId); LastEvent=Viewer->Name+TEXT(" 已在场中，镜头已定位"); return; }
        const int32 Team=Command==TEXT("1")?1:(Command==TEXT("2")?2:0);
        const int32 Id=NextBodyId;
        if(!Match.add(Id,static_cast<gw::Shape>(FMath::RandRange(0,3)),Team)) {
            LastEvent=Team==0?TEXT("灰色已满 1000 人；可发送 1 / 2 直接加入未满的红蓝阵营"):
                FString(Team==1?TEXT("红方"):TEXT("蓝方"))+TEXT("已满 2000 人，请选择其他未满阵营"); return;
        }
        ++NextBodyId;
        FViewerState State; State.UserId=Comment.UserId; State.Name=Comment.Nickname.IsEmpty()?TEXT("观众"):Comment.Nickname.Left(20);
        State.BodyId=Id; State.Team=Team; State.Avatar=MakePlaceholder(Id); State.HighlightUntil=RunningTime+3;
        Viewers.Add(State.UserId,State); Identities.Add(Id,State.UserId);
        RestoreWeapons(State);
        Feed.enqueue(State.Name+TEXT(" 加入了竞技场"));
        if(!Comment.AvatarUrl.IsEmpty()) GetBridge()->RequestAvatar(Comment.UserId,Comment.AvatarUrl);
        LastEvent=State.Name+TEXT(" 加入了竞技场"); return;
    }
    if(!Viewer) { LastEvent=TEXT("请先发送“加入”"); return; }
    if(!Comment.Nickname.IsEmpty()) Viewer->Name=Comment.Nickname.Left(20);
    if(HandleWeaponCommand(Command,*Viewer)) return;
    if(Match.phase==gw::Phase::Results) { LastEvent=TEXT("结算中，已保留你的身份；下一局开始后可选队换形"); return; }
    if(Viewer->Team==0) {
        if(Command==TEXT("1") || Command==TEXT("2")) {
            const int32 Team=Command==TEXT("1")?1:2;
            if(Match.chooseTeam(Viewer->BodyId,Team)) { Viewer->Team=Team; Viewer->HighlightUntil=RunningTime+3; LastEvent=Viewer->Name+(Team==1?TEXT(" 加入红方"):TEXT(" 加入蓝方")); }
            else LastEvent=FString(Team==1?TEXT("红方"):TEXT("蓝方"))+TEXT("已满 2000 人，当前保留灰色身份");
        } else LastEvent=TEXT("先选择阵营：1 红方 / 2 蓝方");
        return;
    }
    if(Command.Len()==1 && Command[0]>=TEXT('1') && Command[0]<=TEXT('4')) {
        if(Match.changeShape(Viewer->BodyId,static_cast<gw::Shape>(Command[0]-TEXT('1')))) { Viewer->HighlightUntil=RunningTime+2; LastEvent=Viewer->Name+TEXT(" 更换了形状"); }
        else LastEvent=TEXT("换形需要间隔 2 秒，生命比例与武器状态保留");
    }
}
void AArenaGameMode::HandleLike(const FDouyinLike& Like) {
    const auto* V=Viewers.Find(Like.UserId);
    if(!V){LastEvent=TEXT("点赞已收到；先加入战局才能恢复生命");return;}
    LastEvent=Match.healLike(V->BodyId,Like.Count)?V->Name+FString::Printf(TEXT(" 点赞 ×%d，恢复生命"),Like.Count):TEXT("点赞不能复活阵亡角色，结算期间不恢复生命");
    ConsumeDamage();
}
void AArenaGameMode::HandleShare(const FDouyinShare& Share) {
    const auto* V=Viewers.Find(Share.UserId);
    if(!V){LastEvent=TEXT("分享已收到；先加入战局才能领取霰弹枪");return;}
    const auto* F=Match.findFighter(V->BodyId);const bool Had=F&&(F->unlockedWeapons&gw::weaponBit(gw::WeaponKind::Shotgun));
    if(RememberWeapon(V->UserId,gw::WeaponKind::Shotgun)) {
        RestoreWeapons(*V);
        Match.grantShotgun(V->BodyId);
        LastEvent=V->Name+(Had?TEXT(" 已解锁霰弹枪，可用武器+2切换"):TEXT(" 分享直播间，永久解锁霰弹枪"));
    }
}
void AArenaGameMode::HostAssist(int32 Team) {
    if(Team<0||Team>2||Match.phase==gw::Phase::Results){LastEvent=TEXT("结算期间无法调整主播阵营");return;}
    if(HostBodyId<0){const int32 Id=NextBodyId;if(!Match.addHost(Id,Team))return;++NextBodyId;HostBodyId=Id;Feed.enqueue(TEXT("主播以五角星身份进入竞技场"));}
    else if(!Match.setHostTeam(HostBodyId,Team))return;
    FocusViewer(HostBodyId);
    LastEvent=Team==1?TEXT("主播加入红方助战 · 不获取积分或奖项"):Team==2?TEXT("主播加入蓝方助战 · 不获取积分或奖项"):TEXT("主播以中立身份下场 · 不获取积分或奖项");
}
void AArenaGameMode::HandleAvatar(const FString& UserId,UTexture2D* Texture) { if(auto* V=Viewers.Find(UserId)) V->Avatar=Texture; }
void AArenaGameMode::AddMockUsers(int32 Count) {
    if(GetBridge()->IsRelayMode()) { LastEvent=TEXT("连接平台适配服务时不可添加模拟观众"); return; }
    PendingMockUsers=FMath::Clamp(PendingMockUsers+FMath::Max(0,Count),0,gw::Match::ViewerCapacity-Viewers.Num());
}
void AArenaGameMode::PumpMockUsers(int32 Budget) {
    if(GetBridge()->IsRelayMode()) { PendingMockUsers=0; return; }
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
    if(GetBridge()->IsRelayMode()) { LastEvent=TEXT("请先断开接入再重置演示"); return; }
    Match.reset(); Viewers.Empty(); Identities.Empty(); Feed.reset(); DamageNumbers.reset(); PendingMockUsers=0;HostBodyId=-1;FocusBodyId=-1;
    GiftNotice=FGiftNotice{}; GiftNoticeQueue.Reset();
    bSimulationPaused=false; DemoSpeed=1; Overview(); LastEvent=TEXT("场地已重置，发送“加入”重新开始");
}
void AArenaGameMode::ZoomBy(float Factor) { CameraZoom=FMath::Clamp(CameraZoom*Factor,1.f,12.f); }
void AArenaGameMode::FocusViewer(int32 Id) { if(const auto* Body=Match.world.find(Id)) { FocusBodyId=Id; CameraZoom=6; CameraCenter={Body->position.x,Body->position.y}; } }
void AArenaGameMode::Overview() { if(FocusBodyId>=0)return;CameraZoom=1; CameraCenter={4000,4000}; }
void AArenaGameMode::MoveCamera(float X,float Y) { if(FocusBodyId>=0)return;CameraCenter+=FVector2D(X,Y)*(700/CameraZoom); }
void AArenaGameMode::SetDemoSpeed(float Speed) { if(!GetBridge()->IsRelayMode()) DemoSpeed=FMath::Clamp(Speed,1.f,8.f); }
void AArenaGameMode::DemoAction(const FString& Action) {
    if(GetBridge()->IsRelayMode()) { LastEvent=TEXT("此操作仅用于本地演示"); return; }
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
            if(F.isHost){B->position={3690,3950};B->velocity={55,30};continue;}
            if(Index<12){const double A=Index*PI/6;B->position={4000+FMath::Cos(A)*(Index<6?360:610),4000+FMath::Sin(A)*(Index<6?360:610)};B->velocity={-FMath::Sin(A)*95,FMath::Cos(A)*95};}
            else {B->position={650.+(Index%12)*140.,750.+(Index/12)*140.};B->velocity={60,35};}
            if(Index<3){Match.grantShotgun(F.id);if(!Match.evolutionPacks.empty()){Match.evolutionPacks[0].active=true;Match.collectEvolutionPack(F.id,0);F.swordRemaining=4.8;}}
            ++Index;
        }
        for(size_t i=1;i<Match.evolutionPacks.size();++i)Match.evolutionPacks[i].position={3520+(i%2)*900.,3450+(i/2)*520.};
        Match.world.rebuildSpatial();FocusBodyId=-1;CameraCenter={4000,4000};CameraZoom=4.5;
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
        LastEvent=Match.revive(Id,true)?TEXT("模拟仙女棒：个人复活成功"):TEXT("请选择已阵亡的玩家；结算期间无法复活");
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
    if(Match.elapsed>8 && DemoStage==0) {HostAssist(1);if(const auto* V=FindViewer(1)){GetBridge()->SimulateShare(V->UserId,V->Name);GetBridge()->SimulateLike(V->UserId,V->Name,1);}FocusBodyId=-1;CameraCenter={4000,4000};CameraZoom=4.2f;DemoStage=1;}
    if(Match.elapsed>35 && DemoStage==1) { FocusBodyId=-1;CameraCenter={4000,4000};CameraZoom=5;DemoStage=2; }
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
    const TCHAR* Mode=bCombatStress?TEXT("UE offscreen overview; 5000 viewers plus host; dead participants included; preset starts at 150s with 500 shotguns and five evolved viewers"):TEXT("UE offscreen overview, local auto-combat; player count includes dead participants");
    const FString Report=FString::Printf(TEXT("{\"players\":%d,\"host\":%d,\"sampled_alive_min\":%d,\"sampled_alive_max\":%d,\"samples\":%d,\"boss_active_samples\":%d,\"max_visible_shots\":%d,\"max_sword_waves\":%d,\"frame\":%s,\"simulation\":%s,\"hud\":%s,\"mode\":\"%s\"}"),Viewers.Num(),HostBodyId>=0?1:0,StressMinAlive,StressMaxAlive,StressFrames.Num(),StressBossFrames,StressMaxShots,StressMaxSwords,*Stats(StressFrames),*Stats(StressSimulation),*Stats(StressRender),Mode);
    FFileHelper::SaveStringToFile(Report,*(FPaths::ProjectSavedDir()/(bCombatStress?TEXT("CombatStress5000.json"):TEXT("Stress5000.json"))));
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
