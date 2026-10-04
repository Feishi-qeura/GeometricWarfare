#include "../Source/GeometricWarfare/Simulation/ArenaMatch.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace gw;
static int assertions=0;
static void check(bool ok,const char* text){++assertions;if(!ok)throw std::runtime_error(text);}
static bool eq(double a,double b){return std::abs(a-b)<1e-5;}
static Match quiet(int npcs=0){MatchConfig c;c.naturalOrbs=0;c.npcCount=npcs;c.autoCombat=c.autoCollect=false;Match m(c);for(int team=1;team<=2;++team)m.bases[team].alive=false;return m;}
static void place(Match& m,int i,Vec p){auto& b=m.world.bodies[i];b.position=p;b.velocity={};b.spin=b.angle=0;m.world.rebuildSpatial();}
static void acquire(Match& m){m.fighters[0].acquisitionRemaining=0;m.config.autoCombat=true;m.step(.00001);}
static WeaponProjectile shot(int owner,WeaponKind kind,Vec from,double damage){WeaponProjectile p;p.ownerId=owner;p.team=1;p.kind=kind;p.position=p.previous=from;p.velocity={200000,0};p.damage=damage;p.radius=kind==WeaponKind::RocketLauncher?8:3;p.maxDistance=10000;return p;}
static void strictTiers(){
    Match m=quiet(1);m.add(1,Shape::Square,1);m.add(2,Shape::Square,2);
    const Vec axis{m.npcs[0].position.x<4000?1.0:-1.0,0},origin=m.npcs[0].position+axis*200;
    place(m,0,origin);place(m,1,origin+axis*400);for(auto& f:m.fighters)f.shotRemaining=100;
    m.evolutionPacks.push_back({origin+axis*80,true,0});m.weaponCrates.push_back({origin+axis*60,WeaponKind::Sniper,true,0});
    m.bases[2].alive=true;m.bases[2].position=origin+axis*300;
    acquire(m);check(m.fighters[0].targetKind==1,"enemy geometry outranks closer base, pickups and neutral NPC");
    m.boss.active=m.boss.spawned=true;m.boss.fireCenter=m.boss.position=origin+axis*600;m.boss.attack=BossAttack::StompRest;m.boss.attackInitialized=true;
    m.fighters[0].acquisitionRemaining=100;m.step(.00001);check(m.fighters[0].targetKind==4,"Boss immediately preempts a closer locked enemy without waiting for acquisition timer");
    m.boss.active=false;m.fighters[1].alive=false;m.fighters[1].respawnRemaining=100;m.world.bodies[1].active=false;m.world.rebuildSpatial();acquire(m);
    check(m.fighters[0].targetKind==3,"enemy base is preferred when no hostile participant is in range");
    m.bases[2].alive=false;acquire(m);check(m.fighters[0].targetKind==6,"nearest weapon crate outranks evolution pack and neutral NPC");
    m.weaponCrates[0].active=false;acquire(m);check(m.fighters[0].targetKind==5,"evolution pack outranks closer neutral NPC");
    m.evolutionPacks[0].active=false;acquire(m);check(m.fighters[0].targetKind==2,"neutral NPC remains a fallback when higher tiers are unavailable");
}
static void alliesDoNotHideEnemy(){
    Match m=quiet();m.add(1,Shape::Square,1);m.add(2,Shape::Square,2);place(m,0,{4000,4000});place(m,1,{4200,4000});
    for(int i=0;i<600;++i){m.add(100+i,Shape::Circle,1);place(m,i+2,{4000,4000});}
    for(auto& f:m.fighters)f.shotRemaining=100;acquire(m);
    check(m.fighters[0].targetKind==1&&m.fighters[0].targetIndex==1,"six hundred nearby allies cannot fill the bounded candidate list and hide an enemy");
    m.addHost(9999,1);const int host=static_cast<int>(m.fighters.size())-1;place(m,host,{1000,1000});m.fighters[host].shotRemaining=100;
    m.evolutionPacks.push_back({{1100,1000},true,0});m.weaponCrates.push_back({{1200,1000},WeaponKind::Sniper,true,0});m.step(.00001);
    check(m.fighters[host].targetKind==0,"host does not waste attacks on viewer-only pickups");
}
static void prefirePriorityRecheck(){
    for(int lowerKind:{2,3,5,6}){
        Match m=quiet(1);m.add(1,Shape::Square,1);m.add(2,Shape::Square,2);
        const Vec axis{m.npcs[0].position.x<4000?1.0:-1.0,0},origin=m.npcs[0].position+axis*200;
        place(m,0,origin);place(m,1,origin+axis*800);m.fighters[1].shotRemaining=100;
        if(lowerKind==3){m.bases[2].alive=true;m.bases[2].position=origin+axis*150;}
        if(lowerKind==5)m.evolutionPacks.push_back({origin+axis*150,true,0});
        if(lowerKind==6)m.weaponCrates.push_back({origin+axis*150,WeaponKind::Sniper,true,0});
        auto& f=m.fighters[0];f.targetKind=lowerKind;f.targetIndex=lowerKind==3?2:0;f.acquisitionRemaining=100;f.aimRemaining=0;f.shotRemaining=.1;m.config.autoCombat=true;
        m.step(.001);check(f.targetKind==lowerKind&&f.ammo==10,"low tier target stays locked while weapon is still cooling down");
        place(m,1,origin+axis*400);f.shotRemaining=0;m.step(.00001);
        check(f.targetKind==1&&f.targetIndex==1&&f.ammo==10&&m.shots.empty(),"new in-range enemy preempts a locked NPC, base or pickup before the next shot");
        check(eq(f.aimRemaining,.28),"prefire switch starts a full fresh target aim without consuming the elapsed low-target frame");
        check(eq(m.npcs[0].hp,200)&&(lowerKind!=3||eq(m.bases[2].hp,2500))&&(lowerKind!=5||eq(m.evolutionPacks[0].hp,500))&&(lowerKind!=6||eq(m.weaponCrates[0].hp,500)),"the preempted low priority target takes no last stray hit");
        m.step(.279);check(f.ammo==10&&f.aimRemaining>0,"new hostile geometry cannot be fired on before its full aim completes");
        m.step(.001);check(f.ammo==9&&f.targetKind==1,"attack proceeds against the enemy after the complete new aim");
    }
    Match m=quiet(1);m.add(1,Shape::Square,1);const Vec axis{m.npcs[0].position.x<4000?1.0:-1.0,0},origin=m.npcs[0].position+axis*200;
    place(m,0,origin);auto& f=m.fighters[0];f.targetKind=2;f.targetIndex=0;f.acquisitionRemaining=100;f.shotRemaining=100;
    m.boss.active=m.boss.spawned=true;m.boss.fireCenter=m.boss.position=origin+axis*600;m.boss.attack=BossAttack::StompRest;m.boss.attackInitialized=true;m.config.autoCombat=true;m.step(.00001);
    check(f.targetKind==4&&f.ammo==10,"Boss still preempts a locked NPC immediately even while firing cooldown is long");
}
static void pickupBallistics(){
    Match m=quiet();m.add(1,Shape::Rectangle,1);place(m,0,{100,100});m.evolutionPacks.push_back({{1000,1000},true,0});m.weaponCrates.push_back({{1200,1000},WeaponKind::Sniper,true,0});
    m.projectiles.push_back(shot(1,WeaponKind::Sniper,{800,1000},200));m.step(.003);
    check(eq(m.evolutionPacks[0].hp,300)&&eq(m.weaponCrates[0].hp,500)&&m.projectiles.empty(),"swept bullet hits the first pickup without tunneling into the next one");
    m.projectiles.push_back(shot(1,WeaponKind::MachineGun,{800,1022},3));m.step(.003);
    check(eq(m.evolutionPacks[0].hp,300)&&eq(m.weaponCrates[0].hp,500),"bullet outside square plus projectile radius does not falsely hit pickup bounding circle");m.projectiles.clear();
    m.projectiles.push_back(shot(1,WeaponKind::Sniper,{800,1000},300));m.step(.003);
    check(!m.evolutionPacks[0].active&&m.fighters[0].evolutionRemaining>0,"last ranged hit grants evolution without requiring contact pickup");
    m.projectiles.push_back(shot(1,WeaponKind::Sniper,{1000,1000},500));m.step(.002);
    check(!m.weaponCrates[0].active&&m.fighters[0].weaponKind==WeaponKind::Sniper&&eq(m.fighters[0].temporaryWeaponRemaining,60),"last ranged hit grants the crate lease immediately");
    Match blast=quiet();blast.add(1,Shape::Rectangle,1);place(blast,0,{100,100});blast.evolutionPacks.push_back({{1000,1000},true,0});blast.weaponCrates.push_back({{1130,1000},WeaponKind::MachineGun,true,0});
    blast.projectiles.push_back(shot(1,WeaponKind::RocketLauncher,{800,1000},1000));blast.step(.001);
    check(!blast.evolutionPacks[0].active&&!blast.weaponCrates[0].active,"rocket AoE damages and awards every pickup inside its blast");
    check(blast.fighters[0].evolutionRemaining>0&&blast.fighters[0].weaponKind==WeaponKind::MachineGun&&blast.explosions.size()==1,"one rocket can grant both buffs while producing one explosion");
}
static void hitscanRewardSwitch(){
    Match m=quiet();m.add(1,Shape::Square,1);place(m,0,{1000,1000});m.grantWeapon(1,WeaponKind::Shotgun);m.weaponCrates.push_back({{1070,1000},WeaponKind::RocketLauncher,true,0});m.weaponCrates[0].hp=1;
    auto& f=m.fighters[0];f.targetKind=6;f.targetIndex=0;f.acquisitionRemaining=100;f.aimRemaining=0;f.ammo=1;m.config.autoCombat=true;m.step(.00001);
    check(!m.weaponCrates[0].active&&f.weaponKind==WeaponKind::RocketLauncher,"shotgun last hit can safely equip a crate weapon in the middle of a volley");
    check(m.shots.size()>=15&&m.shots.size()<=25&&m.projectiles.empty(),"all remaining pellets retain the original hitscan shotgun kind after reward switches weapon");
    for(const auto& s:m.shots)check(s.kind==WeaponKind::Shotgun,"volley visual never changes into newly awarded rocket");
    check(f.ammo==1&&eq(f.reloadRemaining,0)&&eq(f.weaponStates[static_cast<int>(WeaponKind::Shotgun)].reloadRemaining,3),"new rocket starts full while the empty holstered shotgun retains its reload");
}
int main(){int failures=0;const std::pair<const char*,void(*)()> tests[]={{"strict target tiers",strictTiers},{"ally crowd filtering",alliesDoNotHideEnemy},{"prefire priority recheck",prefirePriorityRecheck},{"pickup ballistics",pickupBallistics},{"hitscan reward switch",hitscanRewardSwitch}};
    for(const auto& t:tests)try{t.second();std::cout<<"PASS "<<t.first<<'\n';}catch(const std::exception& e){++failures;std::cerr<<"FAIL "<<t.first<<": "<<e.what()<<'\n';}
    std::cout<<assertions<<" assertions, "<<failures<<" failed groups\n";return failures?1:0;}
