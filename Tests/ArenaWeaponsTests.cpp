#include "../Source/GeometricWarfare/Simulation/ArenaMatch.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace gw;
static int assertions=0;
static void check(bool condition,const char* message){++assertions;if(!condition)throw std::runtime_error(message);}
static bool eq(double a,double b){return std::abs(a-b)<1e-6;}
static Match quiet(){MatchConfig c;c.autoCombat=false;c.autoCollect=false;c.naturalOrbs=0;c.npcCount=0;return Match(c);}
static void locate(Match& m,int index,Vec position){auto& b=m.world.bodies[index];b.position=position;b.angle=b.spin=0;b.velocity={90,0};m.world.rebuildSpatial();}
static Vec diagonalTarget(double distance){return Vec{100,100}+Vec{.7071067811865475,.7071067811865475}*distance;}
static void pinnedDuel(Match& m,double seconds,double distance=700){while(seconds>1e-8){locate(m,0,{100,100});locate(m,1,diagonalTarget(distance));m.world.bodies[0].velocity=m.world.bodies[1].velocity={};const double dt=std::min(.005,seconds);m.step(dt);seconds-=dt;}}
static WeaponProjectile projectile(int owner,int team,WeaponKind kind,Vec position,Vec velocity,double damage){WeaponProjectile p;p.ownerId=owner;p.team=team;p.kind=kind;p.position=p.previous=position;p.velocity=velocity;p.damage=damage;p.radius=kind==WeaponKind::RocketLauncher?8:3;p.maxDistance=kind==WeaponKind::MachineGun?4400:std::numeric_limits<double>::infinity();return p;}
static void balanceBehavior(){
    Match pistol=quiet();pistol.add(1,Shape::Square,1);pistol.add(2,Shape::Rectangle,2);locate(pistol,0,{1000,1000});locate(pistol,1,{1100,1000});
    auto& p=pistol.fighters[0];p.targetKind=1;p.targetIndex=1;p.acquisitionRemaining=100;p.aimRemaining=0;pistol.fighters[1].shotRemaining=100;pistol.config.autoCombat=true;pistol.step(.001);
    check(p.ammo==9&&eq(pistol.fighters[1].hp,190),"pistol starts with10 rounds and its first shot deals10 damage");
    pistol.step(.999);check(p.ammo==9,"pistol cannot fire again before one second");pistol.step(.001);check(p.ammo==8,"pistol fires its next round at one second");
    check(eq(pistol.world.bodies[0].speedScale,1.2),"pistol equipment grants twenty percent movement speed");
    Match rifle=quiet();rifle.add(1,Shape::Square,1);rifle.add(2,Shape::Rectangle,2);locate(rifle,0,{100,500});locate(rifle,1,{1800,500});rifle.grantWeapon(1,WeaponKind::Rifle);
    auto& r=rifle.fighters[0];r.targetKind=1;r.targetIndex=1;r.acquisitionRemaining=100;r.aimRemaining=0;rifle.fighters[1].shotRemaining=100;rifle.config.autoCombat=true;rifle.step(.001);
    check(r.ammo==44&&eq(rifle.world.bodies[0].speedScale,1.5),"rifle starts with45 rounds, reaches1700 and grants fifty percent movement speed");
    Match host=quiet();host.addHost(9,1);host.step(.001);check(eq(host.fighters[0].maxHp,3000)&&eq(host.world.bodies[0].speedScale,.75),"host has3000 hp and rifle speed stacks with its half-speed baseline");
    Match shotgun=quiet();shotgun.add(1,Shape::Square,1);shotgun.add(2,Shape::Rectangle,2);locate(shotgun,0,{1000,1000});locate(shotgun,1,{1100,1000});shotgun.grantShotgun(1);shotgun.fighters[1].hp=1e9;shotgun.fighters[1].shotRemaining=100;shotgun.config.autoCombat=true;
    int smallest=99,largest=0;double least=100,most=0;
    for(int i=0;i<40;++i){auto& s=shotgun.fighters[0];s.targetKind=1;s.targetIndex=1;s.acquisitionRemaining=100;s.aimRemaining=s.shotRemaining=s.reloadRemaining=0;s.ammo=5;shotgun.shots.clear();shotgun.damageEvents.clear();shotgun.step(.001);
        check(s.ammo==4&&shotgun.shots.size()>=15&&shotgun.shots.size()<=25,"shotgun consumes one of five shells for15..25 pellets");
        smallest=std::min(smallest,static_cast<int>(shotgun.shots.size()));largest=std::max(largest,static_cast<int>(shotgun.shots.size()));
        for(const auto& hit:shotgun.damageEvents){check(hit.amount>=3&&hit.amount<=5,"each shotgun pellet independently rolls3..5 damage");least=std::min(least,hit.amount);most=std::max(most,hit.amount);}
    }
    check(smallest==15&&largest==25&&least<most,"deterministic shotgun sample exercises both pellet count limits and variable damage");
    auto& s=shotgun.fighters[0];s.ammo=1;s.shotRemaining=0;shotgun.step(.001);check(s.ammo==0&&eq(s.reloadRemaining,3),"shotgun last shell starts three second reload");
}
static void inventoryAndTimers(){
    Match m=quiet();m.add(1,Shape::Rectangle,1);auto& f=m.fighters[0];
    check(f.unlockedWeapons==1&&!m.switchWeapon(1,WeaponKind::Rifle),"viewers begin with pistol only and cannot select locked rifle");
    f.ammo=3;f.shotRemaining=.3;f.reloadRemaining=1.4;
    check(m.grantWeapon(1,WeaponKind::Sniper)&&f.ammo==5,"first sniper grant equips full five shot magazine");
    f.ammo=1;f.reloadRemaining=4.2;f.shotRemaining=2.7;
    check(m.switchWeapon(1,WeaponKind::Pistol)&&f.ammo==3&&eq(f.reloadRemaining,1.4)&&eq(f.shotRemaining,.3),"returning pistol preserves its ammunition and all cooldowns");
    check(m.grantWeapon(1,WeaponKind::Sniper)&&f.ammo==1&&eq(f.reloadRemaining,4.2)&&eq(f.shotRemaining,2.7),"repeated grant never refills or cancels sniper timers");
    const auto event=m.events.back();check(event.kind==EventKind::WeaponSwitched&&event.actorId==1&&eq(event.value,3),"switch event gives the stable zero based weapon value");
    const size_t events=m.events.size();check(m.switchWeapon(1,WeaponKind::Sniper)&&m.events.size()==events,"selecting current weapon is an idempotent no-op");
    check(!m.grantWeapon(1,static_cast<WeaponKind>(99))&&!m.switchWeapon(1,static_cast<WeaponKind>(-1)),"invalid weapon enum rejected before bit shifts or slot access");
    m.damageEnvironment(1,10000);check(m.revive(1)&&f.weaponKind==WeaponKind::Sniper&&f.unlockedWeapons==9&&f.ammo==5,"revival retains unlock and equipped weapon with fresh magazine");
    f.ammo=0;f.reloadRemaining=5;m.startNextRound();const auto* restored=m.findFighter(1);
    check(restored->weaponKind==WeaponKind::Sniper&&restored->unlockedWeapons==9&&restored->ammo==5&&eq(restored->reloadRemaining,0),"new round retains unlock and equipment while refreshing ammo");
    m.addHost(2,2);check(!m.grantWeapon(2,WeaponKind::MachineGun)&&!m.switchWeapon(2,WeaponKind::Pistol)&&m.switchWeapon(2,WeaponKind::Rifle),"host remains restricted to its rifle");
}
static void sprintAndBaseGifts(){
    Match m=quiet();m.add(1,Shape::Rectangle,0);m.add(2,Shape::Rectangle,1);m.addHost(3,2);
    m.bases[1].hp=1800;check(m.rebuildOrFortifyBase(1)&&eq(m.bases[1].hp,2800)&&eq(m.bases[1].maxHp,3500),"living base gains1000 current and maximum health");
    m.damageBase(1,1,100000);check(m.rebuildOrFortifyBase(1)&&eq(m.bases[1].hp,3500),"destroyed fortified base rebuilds to retained maximum");
    m.step(m.config.sprintSeconds);check(!m.rebuildOrFortifyBase(1),"sprint blocks fortification and reconstruction");
    for(int id:{1,2,3})m.damageEnvironment(id,100000);
    m.step(14.99);check(!m.findFighter(3)->alive&&!m.findFighter(2)->alive,"colored host and viewer wait before the15second respawn deadline");
    m.step(.01);check(m.findFighter(3)->alive&&!m.findFighter(2)->alive,"colored host revives at15seconds despite destroyed base while Sprint blocks viewer");
    check(!m.findFighter(1)->alive&&!m.revive(1)&&!m.revive(2),"all viewers stay dead during sprint and ordinary revival is blocked");
    check(m.revive(1,true)&&m.revive(2,true),"explicit fairy wand exception revives either viewer faction during sprint");
    m.setHostTeam(3,0);m.damageEnvironment(3,100000);m.step(19.99);check(!m.findFighter(3)->alive,"gray host waits twenty seconds");m.step(.01);check(m.findFighter(3)->alive,"gray host revives during sprint after twenty seconds");
    m.step(m.config.battleSeconds-m.elapsed);check(!m.revive(2,true),"results do not allow gifted revival");
}
static void sweptGeometryAndTargets(){
    Match m=quiet();m.add(1,Shape::Rectangle,1);m.add(2,Shape::Rectangle,2);m.add(3,Shape::Rectangle,2);
    locate(m,0,{200,200});locate(m,1,{1000,1000});locate(m,2,{1200,1000});
    m.fighters[1].hp=m.fighters[2].hp=1000;
    m.projectiles.push_back(projectile(1,1,WeaponKind::Sniper,{800,1000},{200000,0},200));m.step(.003);
    check(eq(m.fighters[1].hp,800)&&eq(m.fighters[2].hp,1000)&&m.projectiles.empty(),"swept shot hits closest intervening body exactly once without tunneling");
    locate(m,2,{1200,1020});m.projectiles.push_back(projectile(1,1,WeaponKind::Sniper,{800,1020},{200000,0},200));m.step(.003);
    check(eq(m.fighters[1].hp,800)&&eq(m.fighters[2].hp,800),"rectangle corner bounding circle cannot cause a false hit outside actual polygon");
    m.world.bodies[1].angle=detail::Tau/4;m.projectiles.push_back(projectile(1,1,WeaponKind::Sniper,{800,1020},{200000,0},200));m.step(.001);
    check(eq(m.fighters[1].hp,600),"rotated long rectangle is hit using its transformed polygon");
    MatchConfig cfg;cfg.autoCombat=false;cfg.autoCollect=false;cfg.npcCount=1;cfg.naturalOrbs=0;Match npc(cfg);npc.add(4,Shape::Square,1);locate(npc,0,{100,100});const Vec p=npc.npcs[0].position;
    npc.projectiles.push_back(projectile(4,1,WeaponKind::MachineGun,p-Vec{100,0},{200000,0},7));npc.step(.001);
    check(eq(npc.npcs[0].hp,182.5),"swept NPC collision applies square neutral multiplier exactly once");
    Match b=quiet();b.add(7,Shape::Square,1);locate(b,0,{100,100});b.boss.active=b.boss.spawned=true;b.fighters[0].bossBuffEligible=true;b.teamBuffRemaining[1]=60;
    const double bossHp=b.boss.hp;b.projectiles.push_back(projectile(7,1,WeaponKind::Sniper,{World::Size*.5-200,World::Size*.5},{200000,0},200));b.step(.001);
    check(eq(b.boss.hp,bossHp-600),"boss collision applies square and boss reward multipliers exactly once");
}
static void rocketsAndLifetimes(){
    Match m=quiet();m.add(1,Shape::Rectangle,1);m.add(2,Shape::Rectangle,2);m.add(3,Shape::Rectangle,2);m.add(4,Shape::Rectangle,1);
    locate(m,0,{100,100});locate(m,1,{1000,1000});locate(m,2,{970,1263.9015869776647});locate(m,3,{962,930});
    for(auto& f:m.fighters)f.hp=f.maxHp=2000;
    m.projectiles.push_back(projectile(1,1,WeaponKind::RocketLauncher,{800,1000},{200000,0},1000));m.step(.001);
    check(eq(m.fighters[1].hp,1000)&&eq(m.fighters[2].hp,1900),"rocket impact deals1000 at contact and100 at blast edge with linear falloff");
    check(eq(m.fighters[3].hp,2000)&&m.explosions.size()==1&&m.projectiles.empty(),"rocket AoE ignores allies and emits one bounded impact visual");
    check(eq(m.explosions[0].position.x,970)&&eq(m.explosions[0].radius,248.90158697766472),"rocket blast originates at actual surface contact with doubled blast radius");
    m.step(.46);check(m.explosions.empty(),"rocket explosion expires after its animation");
    Match base=quiet();base.add(1,Shape::Rectangle,1);locate(base,0,{100,100});base.fighters[0].bossBuffEligible=true;base.teamBuffRemaining[1]=60;base.bases[2].hp=base.bases[2].maxHp=5000;
    base.projectiles.push_back(projectile(1,1,WeaponKind::RocketLauncher,base.bases[2].position-Vec{200,0},{200000,0},1000));base.step(.001);
    check(eq(base.bases[2].hp,2000)&&eq(base.fighters[0].baseDamage,3000),"rocket base hit combines2.5base bonus and1.2boss buff once");
    Match wall=quiet();wall.add(1,Shape::Rectangle,1);locate(wall,0,{100,100});
    // Infinite sniper flight is bounded by actual compact world walls. A captured
    // distance beyond acquisition range must not expire the projectile mid-map.
    auto longFlight=projectile(1,1,WeaponKind::Sniper,{1000,1000},{1000,0},200);longFlight.travelled=4300;
    wall.projectiles.push_back(longFlight);wall.step(.01);
    check(wall.projectiles.size()==1&&wall.projectiles[0].travelled>4300,"sniper projectile continues after acquisition range");wall.step((World::Size-1010)/1000+.001);check(wall.projectiles.empty(),"sniper projectile disappears at the world wall");
    // Seed accumulated travel so the unchanged4400 expiry boundary can be
    // exercised without the compact wall ending the flight first.
    auto nearExpiry=projectile(1,1,WeaponKind::MachineGun,{1000,1000},{1000,0},7);nearExpiry.travelled=4390;
    wall.projectiles.push_back(nearExpiry);wall.step(.009);check(wall.projectiles.size()==1,"machinegun projectile remains just before4400 travel");wall.step(.001);check(wall.projectiles.empty(),"machinegun projectile expires at100 normal diameters");
    wall.projectiles.push_back(projectile(1,1,WeaponKind::RocketLauncher,{World::Size-10,World::Size*.5},{1000,0},500));wall.step(.01);check(wall.projectiles.empty(),"rocket without a target also terminates at world wall");
}
static void machineGunAndReload(){
    Match m=quiet();m.add(1,Shape::Square,1);m.add(2,Shape::Square,2);locate(m,0,{200,700});locate(m,1,{1700,700});m.grantWeapon(1,WeaponKind::MachineGun);
    auto& f=m.fighters[0];f.targetKind=1;f.targetIndex=1;f.acquisitionRemaining=100;f.aimRemaining=0;f.ammo=1;m.fighters[1].acquisitionRemaining=100;m.config.autoCombat=true;m.step(.001);
    check(m.projectiles.size()==7&&f.ammo==0&&eq(f.reloadRemaining,8),"machinegun consumes one trigger ammo for seven real projectiles and starts8second reload");
    for(const auto& p:m.projectiles)check(std::abs(std::atan2(p.velocity.y,p.velocity.x))<=.2617993878&&eq(p.damage,3),"each machinegun pellet stays within15degree half-angle and carries3base damage");
    check(eq(m.world.bodies[0].speedScale,.8),"equipping machinegun reduces movement by twenty percent");
    m.switchWeapon(1,WeaponKind::Pistol);m.step(.1);m.switchWeapon(1,WeaponKind::MachineGun);check(f.ammo==0&&eq(f.reloadRemaining,8),"holstering pauses the independent reload without bypassing it");
    m.fighters[1].hp=m.fighters[1].maxHp=1e9;m.step(7.99);check(f.ammo==0&&f.reloadRemaining>0,"ordinary machinegun reload does not finish early");m.step(.01);check(f.ammo==149||f.ammo==150,"eight second reload restores full150 shot magazine before next trigger");
    Match fire=quiet();fire.add(1,Shape::Square,1);locate(fire,0,{World::Size*.5+200,World::Size*.5});fire.grantWeapon(1,WeaponKind::MachineGun);fire.boss.active=fire.boss.spawned=true;fire.boss.attack=BossAttack::StompRest;fire.boss.attackInitialized=true;fire.config.autoCombat=true;fire.step(.001);
    check(eq(fire.world.bodies[0].speedScale,.64),"boss fire and machinegun movement penalties multiply");
    Match rocket=quiet();rocket.add(1,Shape::Circle,1);rocket.add(2,Shape::Rectangle,2);locate(rocket,0,{500,700});locate(rocket,1,{1500,700});rocket.fighters[1].hp=1e9;rocket.fighters[1].shotRemaining=100;
    rocket.grantWeapon(1,WeaponKind::RocketLauncher);auto& r=rocket.fighters[0];r.targetKind=1;r.targetIndex=1;r.aimRemaining=0;r.acquisitionRemaining=100;rocket.config.autoCombat=true;rocket.step(.001);
    check(r.ammo==0&&eq(r.reloadRemaining,2.5),"circle rocket starts a half-length reload after its single shot");
    rocket.step(2.5);check(r.ammo==0&&r.reloadRemaining>2.49,"circle may fire again when its2.5second reload completes");
}
static void boundedBurstPools(){
    Match m=quiet();m.boss.active=m.boss.spawned=true;m.boss.attack=BossAttack::LaserWindup;m.boss.attackInitialized=true;
    // Preexisting in-flight projectiles plus the full legal red team overflow the
    // same8192-slot pool without exceeding compact viewer admission limits.
    for(int i=0;i<7000;++i)m.projectiles.push_back(projectile(0,1,WeaponKind::MachineGun,{100,100},{0,1},3));
    for(int i=0;i<Match::TeamCapacity(1);++i){check(m.add(i,Shape::Square,1),"burst participant joins");m.grantWeapon(i,WeaponKind::MachineGun);auto& f=m.fighters.back();f.targetKind=4;f.targetIndex=0;f.aimRemaining=0;f.acquisitionRemaining=999;auto& b=m.world.bodies.back();b.position={World::Size*.5-700,World::Size*.5};b.velocity={90,0};b.spin=0;}
    m.world.rebuildSpatial();m.config.autoCombat=true;m.step(.001);
    check(m.projectiles.size()==8192,"existing7000 projectiles plus1400 pellet launch stays within8192 projectile slots");
    check(m.events.size()<=512&&m.damageEvents.size()<=512,"large weapon gift burst keeps public feedback bounded");
    Match blast=quiet();blast.add(1,Shape::Rectangle,1);blast.add(2,Shape::Rectangle,2);locate(blast,0,{100,100});locate(blast,1,{1000,1000});blast.fighters[1].hp=1e9;
    for(int i=0;i<200;++i)blast.projectiles.push_back(projectile(1,1,WeaponKind::RocketLauncher,{800,1000},{200000,0},1000));blast.step(.001);
    check(blast.explosions.size()==128&&blast.projectiles.empty()&&eq(blast.fighters[1].hp,999800000),"two hundred rocket hits apply once each while impact visuals stay capped128");
}
static void newWeaponBehavior(){
    Match m=quiet();m.add(1,Shape::Square,1);m.add(2,Shape::Square,2);
    auto& shooter=m.fighters[0];shooter.weaponKind=static_cast<WeaponKind>(3);shooter.ammo=5;
    m.fighters[1].hp=m.fighters[1].maxHp=10000;m.fighters[1].shotRemaining=100;m.config.autoCombat=true;
    pinnedDuel(m,1.049,2520);check(shooter.ammo==5,"sniper at2520 must aim for1.05seconds before firing");
    pinnedDuel(m,.001,2520);check(shooter.ammo==4,"sniper fires exactly at its interpolated1.05second aim endpoint");
    check(eq(m.fighters[1].hp,10000),"sniper bullet cannot apply distant damage at launch");
    check(!m.projectiles.empty()&&eq(m.projectiles[0].velocity.length(),10000)&&eq(m.projectiles[0].damage,300),"sniper launches at10000 speed and captures its far-band300damage");
    pinnedDuel(m,.4,2520);check(eq(m.fighters[1].hp,9700),"traveling far sniper applies exactly300 damage on collision");
}
static void sniperReloadThenAim(){
    for(Shape shape:{Shape::Square,Shape::Rectangle}){
        Match m=quiet();m.add(1,shape,1);m.add(2,Shape::Square,2);locate(m,0,{100,100});locate(m,1,diagonalTarget(700));
        m.fighters[1].hp=m.fighters[1].maxHp=1e9;m.fighters[1].shotRemaining=1000;m.grantWeapon(1,WeaponKind::Sniper);
        auto& sniper=m.fighters[0];sniper.targetKind=1;sniper.targetIndex=1;sniper.acquisitionRemaining=1000;
        const double aim=shape==Shape::Rectangle?.5:1;sniper.aimRemaining=aim;m.config.autoCombat=true;
        pinnedDuel(m,aim+12);
        check(sniper.ammo==0&&eq(sniper.reloadRemaining,5),"fifth sniper shot empties its five round magazine and starts a full5second reload");
        pinnedDuel(m,4.999);
        check(sniper.ammo==0&&sniper.reloadRemaining>0&&eq(sniper.aimRemaining,aim),"sniper cannot consume its next visible aim while the magazine is reloading");
        pinnedDuel(m,.001);
        check(sniper.ammo==5&&eq(sniper.reloadRemaining,0)&&eq(sniper.aimRemaining,aim)&&m.projectiles.empty(),"reload endpoint restores ammunition without firing or consuming a frame of aim");
        pinnedDuel(m,aim-.001);
        check(sniper.ammo==5&&sniper.aimRemaining>0&&m.projectiles.empty(),"post reload sniper waits the entire aim duration including Rectangle's faster passive");
        pinnedDuel(m,.001);
        check(sniper.ammo==4&&m.projectiles.size()==1&&m.projectiles.front().kind==WeaponKind::Sniper,"sixth sniper projectile is born exactly when post reload aiming completes");
    }
}
static void sniperDistanceBands(){
    for(Shape shape:{Shape::Square,Shape::Rectangle}){
        Match m=quiet();m.add(1,shape,1);m.add(2,Shape::Square,2);m.grantWeapon(1,WeaponKind::Sniper);
        auto& sniper=m.fighters[0];sniper.targetKind=1;sniper.targetIndex=1;sniper.acquisitionRemaining=1000;
        m.fighters[1].hp=m.fighters[1].maxHp=10000;m.fighters[1].shotRemaining=1000;
        locate(m,0,{100,100});locate(m,1,diagonalTarget(2399.999));check(eq(m.weaponFor(sniper).aimTime,1)&&eq(m.weaponFor(sniper).damage,200),"sniper below2400 uses one second base aim and200damage");
        locate(m,1,diagonalTarget(2400));check(eq(m.weaponFor(sniper).aimTime,1)&&eq(m.weaponFor(sniper).damage,300),"sniper exactly2400 retains one second aim and gains far damage");
        locate(m,1,diagonalTarget(3600));check(eq(m.weaponFor(sniper).aimTime,1.5),"sniper aim interpolates linearly to1.5seconds at3600");
        locate(m,1,diagonalTarget(4800));check(eq(m.weaponFor(sniper).range,4800)&&eq(m.weaponFor(sniper).aimTime,2),"sniper at its4800 maximum has two second base aim");
        //3600/4800 above are synthetic formula checks; live motion stays in bounds.
        const double nearAim=shape==Shape::Rectangle?.5:1,farAim=nearAim*1.05;m.config.autoCombat=true;
        pinnedDuel(m,nearAim-.1,2300);check(sniper.ammo==5,"near sniper cannot fire before its shape-adjusted aim completes");
        pinnedDuel(m,.1,2520);check(sniper.ammo==5&&eq(sniper.aimRemaining,farAim-nearAim),"moving farther extends aim smoothly while retaining elapsed tracking time");
        pinnedDuel(m,farAim-nearAim-.001,2520);check(sniper.ammo==5&&sniper.aimRemaining>0,"far distance does not inherit a prematurely completed near aim");
        pinnedDuel(m,.001,2520);check(sniper.ammo==4&&m.projectiles.size()==1&&eq(m.projectiles[0].damage,300),"far shot starts exactly after the full interpolated aim with300 captured damage");
        locate(m,1,diagonalTarget(300));m.step(.08);check(eq(m.fighters[1].hp,9700),"a far shot keeps300 damage when its target moves into the near band during flight");
        check(eq(m.weaponFor(sniper).damage,200)&&eq(sniper.sniperAimDuration,nearAim),"next sniper aim follows the current near band");
        check(eq(m.world.bodies[0].speedScale,.4),"sniper movement is forty percent of normal speed");
    }
}
static void batchedBaseGifts(){
    for(bool destroyed:{false,true})for(int64_t count:{1LL,2LL,101LL,1000LL}){
        Match batch=quiet(),sequential=quiet();batch.bases[1].hp=sequential.bases[1].hp=1800;
        if(destroyed){batch.bases[1].alive=sequential.bases[1].alive=false;batch.bases[1].hp=sequential.bases[1].hp=0;}
        check(batch.rebuildOrFortifyBase(1,count),"positive batch accepted");
        for(int64_t i=0;i<count;++i)check(sequential.rebuildOrFortifyBase(1),"reference single unit accepted");
        check(eq(batch.bases[1].hp,sequential.bases[1].hp)&&eq(batch.bases[1].maxHp,sequential.bases[1].maxHp)&&batch.bases[1].alive==sequential.bases[1].alive,"batch exactly matches single unit hp/max/rebuild semantics");
        check(batch.events.size()<=2,"batch emits constant number of base events");
    }
    Match m=quiet();check(!m.rebuildOrFortifyBase(1,0)&&!m.rebuildOrFortifyBase(1,-1)&&!m.rebuildOrFortifyBase(0,1000),"invalid batch or gray base rejected");
    check(m.rebuildOrFortifyBase(1,std::numeric_limits<int64_t>::max())&&std::isfinite(m.bases[1].hp),"maximum int64 batch applies without integer overflow or iteration");
    m.phase=Phase::Sprint;check(!m.rebuildOrFortifyBase(1,1000),"sprint still rejects batched gifts");
}
int main(){int failures=0;
    const std::pair<const char*,void(*)()> tests[]={{"balance behavior",balanceBehavior},{"new weapon behavior",newWeaponBehavior},{"sniper reload then aim",sniperReloadThenAim},{"sniper distance bands",sniperDistanceBands},{"inventory and timers",inventoryAndTimers},{"sprint and base gifts",sprintAndBaseGifts},{"swept geometry and targets",sweptGeometryAndTargets},{"rockets and lifetimes",rocketsAndLifetimes},{"machinegun and reload",machineGunAndReload},{"bounded burst pools",boundedBurstPools}};
    try{batchedBaseGifts();std::cout<<"PASS batched base gifts\n";}catch(const std::exception& e){++failures;std::cerr<<"FAIL batched base gifts: "<<e.what()<<'\n';}
    for(const auto& test:tests)try{test.second();std::cout<<"PASS "<<test.first<<'\n';}catch(const std::exception& e){++failures;std::cerr<<"FAIL "<<test.first<<": "<<e.what()<<'\n';}
    std::cout<<assertions<<" assertions, "<<failures<<" failed groups\n";return failures?1:0;
}
