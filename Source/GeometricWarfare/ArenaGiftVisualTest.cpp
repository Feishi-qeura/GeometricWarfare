#include "ArenaGameMode.h"

// Reproducible local capture presets. Activated only by explicit GWCombatTest.
void AArenaGameMode::PrepareGiftVisualTest() {
    constexpr double Center=gw::World::Size*.5;
    if(CombatVisualMode==TEXT("score-bars") || CombatVisualMode==TEXT("score-bars-zero") || CombatVisualMode==TEXT("score-bars-long")) {
        bSimulationPaused=true;bShowControls=false;FocusBodyId=-1;Overview();
        Match.config.autoCombat=false;Match.config.autoCollect=false;
        GiftNotices.Reset();GiftNoticeQueue.Reset();RewardNotices.Reset();RewardNoticeQueue.Reset();JoinNotices.Reset();JoinNoticeQueue.Reset();Feed.reset();Match.events.clear();
        int32 TeamIndex[3]={};
        const int64 Scores[2][4]={{9876543,4000000,2000000,500000},{7000000,2500000,1000000,250000}};
        for(auto& F:Match.fighters) {
            F.score=0;if(F.isHost||F.team<1||F.team>2)continue;
            const int32 Index=TeamIndex[F.team]++;
            if(CombatVisualMode==TEXT("score-bars")&&Index<4)F.score=Scores[F.team-1][Index];
            if(CombatVisualMode==TEXT("score-bars-long")&&Index==0)F.score=F.team==1?1234567890123456789LL:987654321012345678LL;
        }
        Match.refreshStandings();VisualCaptureAt=.85f;return;
    }
    if(CombatVisualMode==TEXT("wand-health") || CombatVisualMode==TEXT("wand-evolved") || CombatVisualMode==TEXT("wand-revive")) {
        bSimulationPaused=true;bShowControls=false;Match.config.autoCombat=false;Match.config.autoCollect=false;
        Match.boss={};Match.boss.spawned=true;Match.evolutionPacks.clear();Match.weaponCrates.clear();
        GiftNotices.Reset();GiftNoticeQueue.Reset();RewardNotices.Reset();RewardNoticeQueue.Reset();JoinNotices.Reset();JoinNoticeQueue.Reset();Feed.reset();Match.events.clear();
        for(const auto& F:Match.fighters)if(const auto* V=FindViewer(F.id)) {
            const int32 Id=F.id;const FString User=V->UserId,Name=V->Name;
            // Remove any evolution granted by the general combat preview so the
            // three captures exercise ordinary, evolved and dead states distinctly.
            Match.damageEnvironment(Id,1e12);Match.revive(Id,true);
            auto* Recipient=Match.findFighter(Id);Recipient->shapeCooldown=0;Match.changeShape(Id,gw::Shape::Circle);Recipient->hp=200;
            if(CombatVisualMode==TEXT("wand-evolved"))Match.grantEvolution(Id);
            if(CombatVisualMode==TEXT("wand-revive"))Match.damageEnvironment(Id,100000);
            GetBridge()->SimulateGift(User,Name,TEXT("仙女棒"),CombatVisualMode==TEXT("wand-revive")?3:2);
            Match.events.clear();Feed.reset();
            FocusBodyId=Id;CameraZoom=3.5f;
            for(const auto& B:Match.world.bodies)if(B.id==Id){CameraCenter={B.position.x,B.position.y};break;}
            break;
        }
        VisualCaptureAt=.85f;return;
    }
    if(CombatVisualMode==TEXT("dual-hands") || CombatVisualMode==TEXT("dual-heal") || CombatVisualMode==TEXT("dual-expired")) {
        bSimulationPaused=true;bShowControls=false;Match.config.autoCombat=false;Match.config.autoCollect=false;
        Match.boss={};Match.boss.spawned=true;Match.boss.spawnAge=10;Match.evolutionPacks.clear();Match.weaponCrates.clear();Match.swordWaves.clear();Match.projectiles.clear();Match.shots.clear();
        for(auto& N:Match.npcs)N.active=false;for(auto& O:Match.orbs)O.active=false;
        GiftNotices.Reset();GiftNoticeQueue.Reset();RewardNotices.Reset();RewardNoticeQueue.Reset();JoinNotices.Reset();JoinNoticeQueue.Reset();Feed.reset();Match.events.clear();DamageNumbers.reset();
        for(size_t I=0;I<Match.fighters.size();++I) {
            auto& F=Match.fighters[I];auto& B=Match.world.bodies[I];F.alive=I<4;F.heroBuff=false;F.evolutionRemaining=0;F.bossBuffEligible=false;F.healFlash=0;
            if(I>=4)continue;
            B.position={Center-65+(I%2)*130,Center-60+(I/2)*120};B.shape=static_cast<gw::Shape>(I);B.scale=1;B.velocity={};
            F.team=I%2+1;F.hp=F.maxHp;F.score=400-I*20;F.unlockedWeapons=0x3f;
            Match.switchWeapon(F.id,I==0?gw::WeaponKind::Pistol:I==1?gw::WeaponKind::Rifle:I==2?gw::WeaponKind::MachineGun:gw::WeaponKind::RocketLauncher);
            gw::WeaponCrate Crate;Crate.position=B.position;Crate.weaponKind=I%3==0?gw::WeaponKind::Sniper:I%3==1?gw::WeaponKind::MachineGun:gw::WeaponKind::RocketLauncher;
            Match.weaponCrates={Crate};Match.collectWeaponCrate(F.id,0);
            F.aimAngle=-.4;F.rightWeapon.aimAngle=.4;F.rightWeapon.targetKind=1;F.rightWeapon.targetIndex=(I+1)%4;
            F.rightWeapon.aimRemaining=Crate.weaponKind==gw::WeaponKind::Sniper?1.2:0;F.rightWeapon.sniperAimDuration=1.6;
            if(I==0){F.ammo=7;F.reloadRemaining=1.25;F.rightWeapon.ammo=3;F.rightWeapon.reloadRemaining=0;}
        }
        Match.weaponCrates.clear();Match.world.rebuildSpatial();FocusBodyId=Match.fighters[0].id;CameraCenter={Center,Center};CameraZoom=3.5f;
        if(CombatVisualMode==TEXT("dual-heal")) {
            auto& F=Match.fighters[0];F.hp=F.maxHp*.45;
            if(const auto* V=FindViewer(F.id))GetBridge()->SimulateLike(V->UserId,V->Name,1);
            ConsumeDamage();VisualCaptureAt=.3f;
        } else if(CombatVisualMode==TEXT("dual-expired")) {
            Match.config.autoCombat=false;Match.step(gw::TemporaryWeaponLifetime);ConsumeEvents();
            GiftNotices.Reset();GiftNoticeQueue.Reset();RewardNotices.Reset();RewardNoticeQueue.Reset();Feed.reset();VisualCaptureAt=.3f;
        } else {ConsumeEvents();VisualCaptureAt=.85f;}
        Match.refreshStandings();return;
    }
    if(CombatVisualMode==TEXT("action-notices") || CombatVisualMode==TEXT("action-notices-sniper") || CombatVisualMode==TEXT("action-notices-machinegun")) {
        bSimulationPaused=true;bShowControls=false;FocusBodyId=-1;Overview();
        Match.config.autoCombat=false;Match.config.autoCollect=false;Match.boss.active=false;Match.boss.spawned=true;
        auto* Bridge=GetBridge();
        Bridge->SimulateComment(TEXT("action-preview-team"),TEXT("小橘"),TEXT("加入"));
        const FString WeaponViewerName=CombatVisualMode==TEXT("action-notices")?TEXT("月亮"):TEXT("这是一个很长的观众昵称用于播报测试");
        const auto PreviewWeapon=CombatVisualMode==TEXT("action-notices-sniper")?gw::WeaponKind::Sniper:CombatVisualMode==TEXT("action-notices-machinegun")?gw::WeaponKind::MachineGun:gw::WeaponKind::Rifle;
        Bridge->SimulateComment(TEXT("action-preview-weapon"),WeaponViewerName,TEXT("2"));
        const auto* WeaponViewer=Viewers.Find(TEXT("action-preview-weapon"));
        if(WeaponViewer)Match.findFighter(WeaponViewer->BodyId)->unlockedWeapons|=gw::weaponBit(PreviewWeapon);
        GiftNotices.Reset();GiftNoticeQueue.Reset();RewardNotices.Reset();RewardNoticeQueue.Reset();JoinNotices.Reset();JoinNoticeQueue.Reset();Feed.reset();Match.events.clear();
        Bridge->SimulateComment(TEXT("action-preview-join"),TEXT("星河"),TEXT("加入"));
        Bridge->SimulateComment(TEXT("action-preview-team"),TEXT("小橘"),TEXT("1"));
        Bridge->SimulateComment(TEXT("action-preview-weapon"),WeaponViewerName,FString::Printf(TEXT("武器%d"),static_cast<int32>(PreviewWeapon)+1));
        VisualCaptureAt=.85f;return;
    }
    if(CombatVisualMode==TEXT("notifications") || CombatVisualMode==TEXT("notifications-basic")) {
        bSimulationPaused=true;bShowControls=false;FocusBodyId=-1;CameraZoom=3.5f;CameraCenter={Center,Center};
        Match.config.autoCombat=false;Match.config.autoCollect=false;Match.events.clear();
        GiftNotices.Reset();GiftNoticeQueue.Reset();RewardNotices.Reset();RewardNoticeQueue.Reset();JoinNotices.Reset();JoinNoticeQueue.Reset();Feed.reset();
        TArray<FViewerState> People;
        for(const auto& F:Match.fighters)if(const auto* V=FindViewer(F.id))if(People.Num()<3)People.Add(*V);
        if(People.Num()==3) {
            static const TCHAR* Names[]={TEXT("星河"),TEXT("小橘"),TEXT("月亮")};
            for(int32 I=0;I<3;++I){People[I].Name=Names[I];People[I].Team=I%2+1;EnqueueJoinNotice(People[I]);}
            const bool Basic=CombatVisualMode==TEXT("notifications-basic");
            for(int32 I=0;I<(Basic?2:3);++I){FGiftNotice N;N.UserId=People[I].UserId;N.ViewerName=People[I].Name;N.Team=People[I].Team;N.Count=100;
                if(Basic){N.GiftName=I==0?TEXT("仙女棒"):TEXT("能力药丸");N.WeaponName=I==0?TEXT("复活"):TEXT("阵营基地");N.Visual=I==0?EArenaNoticeVisual::Revive:EArenaNoticeVisual::Base;N.Detail=I==0?TEXT("首件复活 · 其余每件基础生命 +30，死亡清零"):TEXT("基地强化 · 每件当前 / 最大生命 +1000");}
                else{N.GiftName=I==0?TEXT("魔法镜"):I==1?TEXT("甜甜圈"):TEXT("能量电池");N.WeaponName=I==0?TEXT("狙击枪·巴雷特"):I==1?TEXT("机枪·加特林"):TEXT("火箭筒");N.WeaponKind=static_cast<gw::WeaponKind>(3+I);N.Elaborate=true;N.Detail=TEXT("永久解锁并装备 · 重复送礼不补弹");}
                EnqueueGiftNotice(MoveTemp(N));
            }
            FGiftNotice Reward;Reward.ViewerName=Basic?People[0].Name:TEXT("红队");Reward.UserId=Basic?People[0].UserId:FString();Reward.Team=1;Reward.Visual=Basic?EArenaNoticeVisual::Evolution:EArenaNoticeVisual::Boss;
            Reward.WeaponName=Basic?TEXT("进化"):TEXT("强化");Reward.Detail=Basic?TEXT("进化 40 秒 · 生命强化 / 护甲 / 剑气"):TEXT("伤害 / 新产出积分 +20% · 60 秒，进化 40 秒");
            QueueNotice(RewardNotices,RewardNoticeQueue,MoveTemp(Reward),2,3.2f);
            EnqueueRewardNotice(People[1].UserId,Basic?gw::WeaponKind::Rifle:gw::WeaponKind::Shotgun,Basic?TEXT("关注主播，永久解锁"):TEXT("个人累计点赞 1000，永久解锁"));
        }
        VisualCaptureAt=.85f;return;
    }
    if(CombatVisualMode==TEXT("awards-v4") || CombatVisualMode==TEXT("awards-v4-enter") || CombatVisualMode==TEXT("review-results")) {
        // Match-only fixture: use real damage and death paths to prepare both
        // honors, then cross the normal results boundary to select recipients.
        bSimulationPaused=true;bShowControls=false;FocusBodyId=-1;CameraZoom=1;CameraCenter={Center,Center};
        Match.config.autoCombat=false;Match.config.autoCollect=false;Match.boss.active=false;Match.boss.spawned=true;
        Match.teamBuffRemaining={};Match.evolutionPacks.clear();Match.weaponCrates.clear();Match.swordWaves.clear();
        int Red=-1,Blue=-1,Gray=-1;
        for(auto& F:Match.fighters) {
            F.kills=F.deaths=0;F.damageTaken=0;F.score=0;F.heroBuff=false;F.evolutionRemaining=0;F.bossBuffEligible=false;
            if(F.isHost)continue;
            if(F.team==1&&Red<0)Red=F.id;else if(F.team==2&&Blue<0)Blue=F.id;else if(F.team==0&&Gray<0)Gray=F.id;
        }
        if(Red>=0&&Blue>=0&&Gray>=0) {
            for(int Id:{Red,Blue,Gray}) {
                auto* F=Match.findFighter(Id);F->hp=F->maxHp=200;F->armor=F->maxArmor=0;
                auto* B=Match.world.find(Id);B->shape=gw::Shape::Rectangle;B->scale=1;
            }
            for(int I=0;I<12;++I){Match.damagePlayer(Gray,Red,200);Match.revive(Red,true);}
            auto* Tank=Match.findFighter(Blue);Tank->hp=Tank->maxHp=2100;Tank->armor=Tank->maxArmor=300;
            for(int I=0;I<8;++I){Match.damageEnvironment(Blue,800);if(!Tank->alive)Match.revive(Blue,true);}
            Match.findFighter(Red)->score=18640;Match.findFighter(Blue)->score=12580;
        }
        Match.phase=gw::Phase::Sprint;Match.elapsed=Match.config.battleSeconds-.001;Match.step(.001);
        if(CombatVisualMode==TEXT("review-results")) {
            GiftNotices.Reset();GiftNoticeQueue.Reset();RewardNotices.Reset();RewardNoticeQueue.Reset();JoinNotices.Reset();JoinNoticeQueue.Reset();Feed.reset();Match.events.clear();
        }
        VisualCaptureAt=CombatVisualMode==TEXT("awards-v4-enter")?.4f:1.3f;
    } else if(CombatVisualMode==TEXT("supplies") || CombatVisualMode==TEXT("hero-wave") || CombatVisualMode==TEXT("hero-wave-fade")) {
        // Match-only changes keep captures away from permanent weapon progress.
        bSimulationPaused=true;bShowControls=false;FocusBodyId=-1;
        CameraCenter={Center,Center};CameraZoom=CombatVisualMode==TEXT("supplies")?7:5;
        Match.config.autoCollect=false;Match.config.autoCombat=false;
        Match.boss.active=false;Match.boss.spawned=true;Match.boss.spawnAge=10;
        Match.evolutionPacks.clear();Match.weaponCrates.clear();Match.swordWaves.clear();Match.projectiles.clear();Match.shots.clear();
        Match.teamBuffRemaining={};
        for(auto& N:Match.npcs){N.active=false;N.respawnRemaining=30;}
        for(auto& O:Match.orbs){O.active=false;O.respawnRemaining=30;}
        for(size_t I=0;I<Match.fighters.size();++I) {
            auto& F=Match.fighters[I];auto& B=Match.world.bodies[I];
            F.score=1000-static_cast<int64_t>(I)*5;F.shotRemaining=30;F.targetKind=0;F.aimAngle=0;
            F.evolutionRemaining=0;F.heroBuff=false;F.bossBuffEligible=false;
            B.position={700+(I%12)*100.,700+(I/12)*100.};B.velocity={0,0};
        }
        if(CombatVisualMode==TEXT("supplies")) {
            Match.boss.active=true;Match.boss.position=Match.boss.fireCenter={(Center+220),(Center-180)};Match.boss.attack=gw::BossAttack::Idle;
            Match.world.bodies[0].position={(Center+140),(Center-180)}; // Starts inside BOSS; actual simulation pushes it to the rim.
            Match.world.bodies[0].shape=gw::Shape::Circle;
            Match.world.bodies[1].position={(Center-330),(Center+130)};Match.world.bodies[1].shape=gw::Shape::Square;
            Match.world.bodies[2].position={(Center-350),(Center-180)};Match.world.bodies[2].shape=gw::Shape::Circle;
            Match.world.bodies[3].position={(Center+30),(Center+180)};Match.world.bodies[3].shape=gw::Shape::Circle;
            Match.fighters[0].team=Match.fighters[1].team=Match.fighters[2].team=Match.fighters[3].team=1;
            Match.npcs[0].active=true;Match.npcs[0].position={(Center-290),(Center+130)};Match.npcs[0].hp=135;
            Match.evolutionPacks.push_back({{(Center-30),(Center+70)},true,Match.elapsed});
            Match.evolutionPacks.push_back({{(Center-330),(Center-180)},true,Match.elapsed}); // Collision fixture with collection disabled.
            Match.weaponCrates.push_back({{(Center+240),(Center+180)},gw::WeaponKind::RocketLauncher,true,Match.elapsed});
            Match.weaponCrates.push_back({{(Center+50),(Center+180)},gw::WeaponKind::Sniper,true,Match.elapsed});
            Match.world.rebuildSpatial();Match.step(1.0/30.0);
            Match.evolutionPacks[0].hp=185;Match.evolutionPacks[0].hitFlash=.13;
            Match.weaponCrates[0].hp=320;Match.weaponCrates[0].hitFlash=.10;
            // Both new target kinds use the same sniper warning renderer.
            Match.grantWeapon(Match.fighters[1].id,gw::WeaponKind::Sniper);
            auto& Aim=Match.fighters[1];Aim.targetKind=5;Aim.targetIndex=0;Aim.sniperAimDuration=1;Aim.aimRemaining=.6;
            const auto Delta=Match.evolutionPacks[0].position-Match.world.bodies[1].position;Aim.aimAngle=std::atan2(Delta.y,Delta.x);
        } else {
            auto& F=Match.fighters[0];auto& B=Match.world.bodies[0];
            B.position={(Center-530),Center};B.shape=gw::Shape::Circle;F.team=1;F.heroBuff=true;F.hp=F.maxHp=2100;F.armor=F.maxArmor=300;
            F.heroActivatedAt=Match.elapsed-1;F.heroSwordRemaining=.001;
            Match.world.bodies[1].position={(Center+510),(Center-30)};Match.world.bodies[2].position={(Center+510),(Center+30)};
            Match.fighters[1].team=Match.fighters[2].team=2;
            Match.world.rebuildSpatial();Match.config.autoCombat=true;Match.step(.001);
            // Exercise the real pulse and travel path, including the shared fade.
            const double Age=CombatVisualMode==TEXT("hero-wave-fade")?2.1:1.05;
            Match.step(Age);Match.config.autoCombat=false;
            B.velocity={0,0};
        }
        Match.refreshStandings();Match.world.rebuildSpatial();VisualCaptureAt=.3f;
    } else if(CombatVisualMode==TEXT("heroes") || CombatVisualMode==TEXT("gravity")) {
        bSimulationPaused=true;bShowControls=false;FocusBodyId=-1;
        CameraCenter={Center,Center};CameraZoom=CombatVisualMode==TEXT("gravity")?1:6;
        for(size_t I=0;I<Match.fighters.size();++I) {
            auto& F=Match.fighters[I];auto& B=Match.world.bodies[I];
            F.score=1000-static_cast<int64_t>(I)*5;F.shotRemaining=30;B.velocity={0,0};
            B.position={1000,1000};
            if(I<20){F.team=I%2+1;Match.grantWeapon(F.id,static_cast<gw::WeaponKind>(I%gw::WeaponCount));}
            if(I<6){B.position={(Center-290)+(I/3)*540.,(Center-270)+(I%3)*250.};F.aimAngle=0;}
        }
        if(CombatVisualMode==TEXT("heroes")) {
            Match.boss.active=false;Match.boss.spawned=true;Match.config.autoCombat=false;Match.config.autoCollect=false;
            Match.elapsed=Match.config.sprintSeconds-.001;Match.step(.001);
            Match.teamBuffRemaining[1]=Match.teamBuffRemaining[2]=60;
            for(size_t I=0;I<6;++I){auto& F=Match.fighters[I];F.bossBuffEligible=true;Match.grantEvolution(F.id);}
            Match.weaponCrates.push_back({{Center,(Center-120)},gw::WeaponKind::Sniper,true,Match.elapsed});
            Match.weaponCrates.push_back({{Center,(Center+120)},gw::WeaponKind::MachineGun,true,Match.elapsed});
            Match.weaponCrates.push_back({{Center,(Center+360)},gw::WeaponKind::RocketLauncher,true,Match.elapsed});
            // The pistol owner leases a rocket while retaining the three buffs.
            auto& F=Match.fighters[0];Match.weaponCrates.push_back({{(Center-290),(Center-270)},gw::WeaponKind::RocketLauncher,true,Match.elapsed});
            Match.collectWeaponCrate(F.id,static_cast<int>(Match.weaponCrates.size())-1);F.temporaryWeaponRemaining=47;
            if(const auto* V=FindViewer(F.id))if(auto* Mutable=Viewers.Find(V->UserId))Mutable->Name=TEXT("英雄与临时武器测试");
            Match.fighters[7].score=1200;Match.damageEnvironment(Match.fighters[7].id,100000);
            for(int Ray=0;Ray<6;++Ray){gw::SwordWave Wave;Wave.hero=true;Wave.ownerId=F.id;Wave.team=F.team;Wave.damage=30;
                Wave.direction={1,0};Wave.from=Match.world.bodies[0].position+gw::Vec{0,(Ray/5.-.5)*.4*44};Wave.traveled=180;Wave.life=(Wave.maxDistance-Wave.traveled)/Wave.speed;
                Wave.position=Wave.from+Wave.direction*Wave.traveled;Match.swordWaves.push_back(Wave);}
        } else {
            Match.boss.spawnAge=.9;Match.boss.attack=gw::BossAttack::Idle;
            for(size_t I=0;I<Match.fighters.size();++I){const double A=I*gw::detail::Tau/Match.fighters.size();Match.world.bodies[I].position={Center+std::cos(A)*(gw::World::Size*.3375),Center+std::sin(A)*(gw::World::Size*.3375)};}
        }
        Match.refreshStandings();Match.world.rebuildSpatial();VisualCaptureAt=.3f;
    } else if(CombatVisualMode==TEXT("gift") || CombatVisualMode==TEXT("gm")) {
        // Use a separate transient fixture; captures must not grant real unlocks.
        Progress=NewObject<UArenaProgressSave>(this);
        ProgressSlot=TEXT("VisualFixture-WeaponProgress");
        bSimulationPaused=true; bShowControls=CombatVisualMode==TEXT("gm");
        GetBridge()->SimulateComment(TEXT("local-我"),TEXT("我"),TEXT("1"));
        if(const auto* V=Viewers.Find(TEXT("local-我"))) {
            Match.grantWeapon(V->BodyId,gw::WeaponKind::Sniper);
            auto* B=Match.world.find(V->BodyId); B->position={Center,(Center+200)}; B->velocity={0,0};
        }
        EnqueueGiftNotice(TEXT("我"),TEXT("魔法镜"),TEXT("狙击枪·巴雷特"));
        LastRifleCode=TEXT("DEMO2026");VisualCaptureAt=.6f;
    } else if(CombatVisualMode==TEXT("edge")) {
        bSimulationPaused=true;bShowControls=false;FocusBodyId=-1;CameraZoom=12;CameraCenter={Center,Center};
        Match.boss.active=false;
        const double Edge=gw::World::Size/12/2;
        for(size_t I=0;I<Match.fighters.size();++I) {
            auto& F=Match.fighters[I];auto& B=Match.world.bodies[I];
            B.position={1000,1000};B.velocity={0,0};
            if(I<8) {const int Side=static_cast<int>(I)%4;const double Along=I<4?-110:110;
                B.position=Side==0?gw::Vec{Center-Edge+5,Center+Along}:Side==1?gw::Vec{Center+Edge-5,Center+Along}:
                    Side==2?gw::Vec{Center+Along,Center-Edge+5}:gw::Vec{Center+Along,Center+Edge-5};
                B.shape=static_cast<gw::Shape>(I%4);B.angle=.35;F.aimAngle=.5;F.score=200-I;
            }
        }
        Match.refreshStandings();Match.world.rebuildSpatial();VisualCaptureAt=.3f;
    } else if(CombatVisualMode==TEXT("weapons")) {
        bSimulationPaused=true;bShowControls=false;Match.boss.active=false;
        FocusBodyId=-1;CameraZoom=8;CameraCenter={Center,Center};
        for(size_t I=0;I<Match.fighters.size();++I) {
            auto& F=Match.fighters[I];auto& B=Match.world.bodies[I];B.position={1000,1000};
            if(I<6) {B.position={(Center-250)+(I/3)*460.,(Center-180)+(I%3)*175.};F.aimAngle=0;F.team=I<3?1:2;F.score=300-I;
                if(I<3) {Match.grantWeapon(F.id,static_cast<gw::WeaponKind>(3+I));F.targetKind=1;F.targetIndex=I+3;F.aimRemaining=I==0?1.6:0;}}
        }
        for(int I=0;I<3;++I) {gw::WeaponProjectile P;P.position={Center+10,Center-180+I*175.};P.previous={Center-40,P.position.y};P.velocity={1000,0};P.team=1;P.kind=static_cast<gw::WeaponKind>(3+I);P.radius=I==2?8:3;Match.projectiles.push_back(P);}
        Match.explosions.push_back({{(Center+210),(Center+170)},.18,.45,gw::RocketExplosionRadius,1,true});
        Match.refreshStandings();Match.world.rebuildSpatial();VisualCaptureAt=.3f;
    } else if(CombatVisualMode==TEXT("scorch")) {
        bSimulationPaused=true;bShowControls=false;
        Match.boss.active=false;
        Match.boss.scorches[0]={{(Center-500),(Center-100)},{gw::World::Size,(Center+300)},{.996,.088},4.35,5,43,true};
        VisualCaptureAt=.3f;
    } else if(CombatVisualMode==TEXT("sprint")) {
        bSimulationPaused=true;bShowControls=false;
        Match.elapsed=Match.config.sprintSeconds;Match.phase=gw::Phase::Sprint;Match.boss.active=false;
        for(int Team=1;Team<=2;++Team){Match.bases[Team].alive=false;Match.bases[Team].hp=0;}
        int ScoringIndex=0;
        for(size_t I=0;I<Match.fighters.size();++I) {
            auto& F=Match.fighters[I];
            if(F.isHost||F.team==0)continue;
            ++ScoringIndex;if(ScoringIndex%2==0)Match.damageEnvironment(F.id,100000);
            F.score=1000-ScoringIndex; // Keep alternating live/dead rows visible regardless of random teams.
        }
        Match.refreshStandings();VisualCaptureAt=.3f;
    }
}
