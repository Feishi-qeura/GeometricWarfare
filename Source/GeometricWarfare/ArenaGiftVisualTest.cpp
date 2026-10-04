#include "ArenaGameMode.h"

// Reproducible local capture presets. Activated only by explicit GWCombatTest.
void AArenaGameMode::PrepareGiftVisualTest() {
    if(CombatVisualMode==TEXT("awards-v4") || CombatVisualMode==TEXT("awards-v4-enter")) {
        // Match-only fixture: use real damage and death paths to prepare both
        // honors, then cross the normal results boundary to select recipients.
        bSimulationPaused=true;bShowControls=false;FocusBodyId=-1;CameraZoom=1;CameraCenter={4000,4000};
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
        VisualCaptureAt=CombatVisualMode==TEXT("awards-v4-enter")?.4f:1.3f;
    } else if(CombatVisualMode==TEXT("supplies") || CombatVisualMode==TEXT("hero-wave") || CombatVisualMode==TEXT("hero-wave-fade")) {
        // Match-only changes keep captures away from permanent weapon progress.
        bSimulationPaused=true;bShowControls=false;FocusBodyId=-1;
        CameraCenter={4000,4000};CameraZoom=CombatVisualMode==TEXT("supplies")?7:5;
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
            Match.boss.active=true;Match.boss.position=Match.boss.fireCenter={4220,3820};Match.boss.attack=gw::BossAttack::Idle;
            Match.world.bodies[0].position={4140,3820}; // Starts inside BOSS; actual simulation pushes it to the rim.
            Match.world.bodies[0].shape=gw::Shape::Circle;
            Match.world.bodies[1].position={3670,4130};Match.world.bodies[1].shape=gw::Shape::Square;
            Match.world.bodies[2].position={3650,3820};Match.world.bodies[2].shape=gw::Shape::Circle;
            Match.world.bodies[3].position={4030,4180};Match.world.bodies[3].shape=gw::Shape::Circle;
            Match.fighters[0].team=Match.fighters[1].team=Match.fighters[2].team=Match.fighters[3].team=1;
            Match.npcs[0].active=true;Match.npcs[0].position={3710,4130};Match.npcs[0].hp=135;
            Match.evolutionPacks.push_back({{3970,4070},true,Match.elapsed});
            Match.evolutionPacks.push_back({{3670,3820},true,Match.elapsed}); // Collision fixture with collection disabled.
            Match.weaponCrates.push_back({{4240,4180},gw::WeaponKind::RocketLauncher,true,Match.elapsed});
            Match.weaponCrates.push_back({{4050,4180},gw::WeaponKind::Sniper,true,Match.elapsed});
            Match.world.rebuildSpatial();Match.step(1.0/30.0);
            Match.evolutionPacks[0].hp=185;Match.evolutionPacks[0].hitFlash=.13;
            Match.weaponCrates[0].hp=320;Match.weaponCrates[0].hitFlash=.10;
            // Both new target kinds use the same sniper warning renderer.
            Match.grantWeapon(Match.fighters[1].id,gw::WeaponKind::Sniper);
            auto& Aim=Match.fighters[1];Aim.targetKind=5;Aim.targetIndex=0;Aim.sniperAimDuration=1;Aim.aimRemaining=.6;
            const auto Delta=Match.evolutionPacks[0].position-Match.world.bodies[1].position;Aim.aimAngle=std::atan2(Delta.y,Delta.x);
        } else {
            auto& F=Match.fighters[0];auto& B=Match.world.bodies[0];
            B.position={3470,4000};B.shape=gw::Shape::Circle;F.team=1;F.heroBuff=true;F.hp=F.maxHp=2100;F.armor=F.maxArmor=300;
            F.heroActivatedAt=Match.elapsed-1;F.heroSwordRemaining=.001;
            Match.world.bodies[1].position={4510,3970};Match.world.bodies[2].position={4510,4030};
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
        CameraCenter={4000,4000};CameraZoom=CombatVisualMode==TEXT("gravity")?1:6;
        for(size_t I=0;I<Match.fighters.size();++I) {
            auto& F=Match.fighters[I];auto& B=Match.world.bodies[I];
            F.score=1000-static_cast<int64_t>(I)*5;F.shotRemaining=30;B.velocity={0,0};
            B.position={1000,1000};
            if(I<20){F.team=I%2+1;Match.grantWeapon(F.id,static_cast<gw::WeaponKind>(I%gw::WeaponCount));}
            if(I<6){B.position={3710+(I/3)*540.,3730+(I%3)*250.};F.aimAngle=0;}
        }
        if(CombatVisualMode==TEXT("heroes")) {
            Match.boss.active=false;Match.boss.spawned=true;Match.config.autoCombat=false;Match.config.autoCollect=false;
            Match.elapsed=Match.config.sprintSeconds-.001;Match.step(.001);
            Match.teamBuffRemaining[1]=Match.teamBuffRemaining[2]=60;
            for(size_t I=0;I<6;++I){auto& F=Match.fighters[I];F.bossBuffEligible=true;Match.grantEvolution(F.id);}
            Match.weaponCrates.push_back({{4000,3880},gw::WeaponKind::Sniper,true,Match.elapsed});
            Match.weaponCrates.push_back({{4000,4120},gw::WeaponKind::MachineGun,true,Match.elapsed});
            Match.weaponCrates.push_back({{4000,4360},gw::WeaponKind::RocketLauncher,true,Match.elapsed});
            // The pistol owner leases a rocket while retaining the three buffs.
            auto& F=Match.fighters[0];Match.weaponCrates.push_back({{3710,3730},gw::WeaponKind::RocketLauncher,true,Match.elapsed});
            Match.collectWeaponCrate(F.id,static_cast<int>(Match.weaponCrates.size())-1);F.temporaryWeaponRemaining=47;
            if(const auto* V=FindViewer(F.id))if(auto* Mutable=Viewers.Find(V->UserId))Mutable->Name=TEXT("英雄与临时武器测试");
            Match.fighters[7].score=1200;Match.damageEnvironment(Match.fighters[7].id,100000);
            for(int Ray=0;Ray<6;++Ray){gw::SwordWave Wave;Wave.hero=true;Wave.ownerId=F.id;Wave.team=F.team;Wave.damage=30;
                Wave.direction={1,0};Wave.from=Match.world.bodies[0].position+gw::Vec{0,(Ray/5.-.5)*.4*44};Wave.traveled=180;Wave.life=(Wave.maxDistance-Wave.traveled)/Wave.speed;
                Wave.position=Wave.from+Wave.direction*Wave.traveled;Match.swordWaves.push_back(Wave);}
        } else {
            Match.boss.spawnAge=.9;Match.boss.attack=gw::BossAttack::Idle;
            for(size_t I=0;I<Match.fighters.size();++I){const double A=I*gw::detail::Tau/Match.fighters.size();Match.world.bodies[I].position={4000+std::cos(A)*2700,4000+std::sin(A)*2700};}
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
            auto* B=Match.world.find(V->BodyId); B->position={4000,4200}; B->velocity={0,0};
        }
        EnqueueGiftNotice(TEXT("我"),TEXT("魔法镜"),TEXT("狙击枪·巴雷特"));
        LastRifleCode=TEXT("DEMO2026");VisualCaptureAt=.6f;
    } else if(CombatVisualMode==TEXT("edge")) {
        bSimulationPaused=true;bShowControls=false;FocusBodyId=-1;CameraZoom=12;CameraCenter={4000,4000};
        Match.boss.active=false;
        const double Edge=8000.0/12/2;
        for(size_t I=0;I<Match.fighters.size();++I) {
            auto& F=Match.fighters[I];auto& B=Match.world.bodies[I];
            B.position={1000,1000};B.velocity={0,0};
            if(I<8) {const int Side=static_cast<int>(I)%4;const double Along=I<4?-110:110;
                B.position=Side==0?gw::Vec{4000-Edge+5,4000+Along}:Side==1?gw::Vec{4000+Edge-5,4000+Along}:
                    Side==2?gw::Vec{4000+Along,4000-Edge+5}:gw::Vec{4000+Along,4000+Edge-5};
                B.shape=static_cast<gw::Shape>(I%4);B.angle=.35;F.aimAngle=.5;F.score=200-I;
            }
        }
        Match.refreshStandings();Match.world.rebuildSpatial();VisualCaptureAt=.3f;
    } else if(CombatVisualMode==TEXT("weapons")) {
        bSimulationPaused=true;bShowControls=false;Match.boss.active=false;
        FocusBodyId=-1;CameraZoom=8;CameraCenter={4000,4000};
        for(size_t I=0;I<Match.fighters.size();++I) {
            auto& F=Match.fighters[I];auto& B=Match.world.bodies[I];B.position={1000,1000};
            if(I<6) {B.position={3750+(I/3)*460.,3820+(I%3)*175.};F.aimAngle=0;F.team=I<3?1:2;F.score=300-I;
                if(I<3) {Match.grantWeapon(F.id,static_cast<gw::WeaponKind>(3+I));F.targetKind=1;F.targetIndex=I+3;F.aimRemaining=I==0?1.6:0;}}
        }
        for(int I=0;I<3;++I) {gw::WeaponProjectile P;P.position={4010.,3820+I*175.};P.previous={3960.,P.position.y};P.velocity={1000,0};P.team=1;P.kind=static_cast<gw::WeaponKind>(3+I);P.radius=I==2?8:3;Match.projectiles.push_back(P);}
        Match.explosions.push_back({{4210,4170},.18,.45,gw::RocketExplosionRadius,1,true});
        Match.refreshStandings();Match.world.rebuildSpatial();VisualCaptureAt=.3f;
    } else if(CombatVisualMode==TEXT("scorch")) {
        bSimulationPaused=true;bShowControls=false;
        Match.boss.active=false;
        Match.boss.scorches[0]={{3500,3900},{8000,4300},{.996,.088},4.35,5,43,true};
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
