#include "../Source/GeometricWarfare/Simulation/ArenaMatch.h"
#include <iostream>
#include <cstdlib>
using namespace gw;
int failures=0;
void check(bool value,const char* label){if(!value){std::cerr<<"FAIL: "<<label<<'\n';++failures;}}
Match quiet(){MatchConfig c;c.autoCombat=false;c.autoCollect=false;c.naturalOrbs=1;c.npcCount=1;return Match(c);}
int main(){
    Match m=quiet();m.add(1,Shape::Rectangle,1);m.add(2,Shape::Rectangle,2);
    m.audio.clear();m.fighters[1].armor=100;m.damagePlayer(1,2,10);
    check(m.audio.count(AudioKind::FighterHit)==1,"positive armored damage emits one hit");
    m.damagePlayer(1,2,100000);m.damagePlayer(1,2,10);
    check(m.audio.count(AudioKind::FighterDeath)==1,"alive to dead emits once");
    m.damageNpc(1,0,100000);m.damageNpc(1,0,1);
    check(m.audio.count(AudioKind::NpcDeath)==1,"NPC death emits once");
    m.collectOrb(1,0);m.collectOrb(1,0);check(m.audio.count(AudioKind::Orb)==1,"orb consumed once");
    m.evolutionPacks.push_back({{500,500},true,60});m.damageEvolutionPack(1,0,10000);
    check(m.audio.count(AudioKind::EvolutionBreak)==1 && m.audio.count(AudioKind::EvolutionPickup)==0,"break excludes pickup sound");
    m.evolutionPacks.push_back({{500,500},true,60});m.collectEvolutionPack(1,1);
    check(m.audio.count(AudioKind::EvolutionPickup)==1,"touch evolution pickup");
    m.weaponCrates.push_back({{500,500},WeaponKind::Sniper,true});m.damageWeaponCrate(1,0,10000);
    check(m.audio.count(AudioKind::WeaponBreak)==1 && m.audio.count(AudioKind::WeaponPickup)==0,"crate break excludes pickup");
    m.weaponCrates.push_back({{500,500},WeaponKind::RocketLauncher,true});m.collectWeaponCrate(1,1);
    check(m.audio.count(AudioKind::WeaponPickup)==1,"touch crate pickup");
    for(auto kind:{WeaponKind::Pistol,WeaponKind::Shotgun,WeaponKind::Rifle,WeaponKind::Sniper,WeaponKind::MachineGun,WeaponKind::RocketLauncher}){
        Match gun=quiet();gun.add(1,Shape::Rectangle,1);gun.add(2,Shape::Rectangle,2);gun.grantWeapon(1,kind);
        gun.world.bodies[0].position={1000,1000};gun.world.bodies[1].position={1150,1000};gun.world.bodies[0].velocity=gun.world.bodies[1].velocity={0,0};gun.world.rebuildSpatial();
        auto& f=gun.fighters[0];f.targetKind=1;f.targetIndex=1;f.acquisitionRemaining=100;f.aimRemaining=0;f.sniperAimDuration=10;gun.fighters[1].shotRemaining=100;gun.config.autoCombat=true;
        gun.step(.001);if(gun.audio.count(static_cast<AudioKind>(kind))!=1)std::cerr<<"kind="<<static_cast<int>(kind)<<" ammo="<<f.ammo<<" aim="<<f.aimRemaining<<"\n";check(gun.audio.count(static_cast<AudioKind>(kind))==1,"one audio event per real weapon trigger, including multi pellet");
        gun.audio.clear();f.ammo=0;f.reloadRemaining=20;gun.step(.001);check(gun.audio.count(static_cast<AudioKind>(kind))==0,"no audio during empty reload");
    }
    Match boss=quiet();boss.add(1,Shape::Rectangle,1);boss.world.bodies[0].position={4400,4000};boss.world.rebuildSpatial();boss.elapsed=BossSpawnSeconds;boss.config.autoCombat=true;
    boss.boss.active=boss.boss.spawned=true;boss.boss.attack=BossAttack::LaserWindup;boss.step(.001);
    check(boss.audio.count(AudioKind::BossLaserWindup)==1,"laser telegraph begins once");boss.step(.001);check(boss.audio.count(AudioKind::BossLaserWindup)==1,"no telegraph each frame");
    boss.audio.clear();boss.boss.attack=BossAttack::StompJump;boss.boss.attackInitialized=false;boss.step(.001);
    check(boss.audio.count(AudioKind::BossStompJump)==1,"stomp jump cue");boss.step(1);
    check(boss.audio.count(AudioKind::BossStompImpact)==1,"stomp landing cue");
    boss.audio.clear();boss.boss.attack=BossAttack::Bullet;boss.boss.attackInitialized=false;boss.boss.hp=boss.boss.maxHp*.4;boss.step(2.5);
    check(boss.audio.count(AudioKind::BossBullet)==1,"rage five bullets are one salvo sound");
    AudioEvents events;events.focusId=7;for(int i=0;i<100000;++i)events.emit(AudioKind::Pistol,i%100);check(events.slots[0].important.actor==7,"focused candidate survives ordinary events");
    check(events.count(AudioKind::Pistol)==100000,"aggregation preserves count without allocation");events.clear();check(events.count(AudioKind::Pistol)==0,"frame drain");
    AudioBudget budget;int starts=0;for(int frame=0;frame<60;++frame){budget.beginFrame(frame/60.0);for(int i=0;i<21;++i)starts+=budget.allow(static_cast<AudioKind>(i),3,frame/60.0);check(budget.frameStarts<=4,"four starts frame cap");}
    check(starts<=67,"60/sec plus initial burst cap");
    budget=AudioBudget{};budget.beginFrame(0);check(budget.allow(AudioKind::Pistol,0,0),"first shot allowed");budget.beginFrame(.05);check(!budget.allow(AudioKind::Pistol,0,.05),"ordinary pistol cooldown");budget.beginFrame(.08);check(budget.allow(AudioKind::Pistol,0,.08),"ordinary pistol cooldown expires");
    std::cout<<(failures?"FAILED ":"PASS ")<<failures<<" failures\n";return failures?EXIT_FAILURE:EXIT_SUCCESS;
}


