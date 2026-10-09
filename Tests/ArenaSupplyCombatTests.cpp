#include "../Source/GeometricWarfare/Simulation/ArenaMatch.h"
#include <iostream>
#include <stdexcept>
using namespace gw;
static int assertions=0;
static void check(bool ok,const char* message){++assertions;if(!ok)throw std::runtime_error(message);}
static bool eq(double a,double b){return std::abs(a-b)<1e-5;}
static Match quiet(){MatchConfig c;c.autoCombat=false;c.autoCollect=false;c.naturalOrbs=0;c.npcCount=0;return Match(c);}
static void lastHitRewards(){
    auto m=quiet();m.add(1,Shape::Rectangle,1);m.add(2,Shape::Rectangle,2);
    m.evolutionPacks.push_back({{World::Size*.25,World::Size*.25}});
    check(eq(m.evolutionPacks[0].hp,500)&&eq(m.evolutionPacks[0].maxHp,500),"evolution starts at500");
    check(m.damageEvolutionPack(1,0,350)&&eq(m.evolutionPacks[0].hp,150),"first player damages without claiming");
    check(eq(m.fighters[0].evolutionRemaining,0)&&m.evolutionPacks[0].hitFlash>0,"damage produces feedback only");
    check(m.damageEvolutionPack(2,0,200)&&!m.evolutionPacks[0].active&&eq(m.evolutionPacks[0].hp,0),"lethal shot removes the supply");
    check(eq(m.fighters[1].evolutionRemaining,40)&&eq(m.fighters[0].evolutionRemaining,0),"last attacker receives evolution remotely");
    const auto eventCount=m.damageEvents.size();
    check(!m.damageEvolutionPack(1,0,1000)&&m.damageEvents.size()==eventCount,"broken supply cannot award or emit twice");
    check(eq(m.damageEvents.back().amount,150),"overkill feedback uses remaining hp");
    m.weaponCrates.push_back({{World::Size*.75,World::Size*.75},WeaponKind::RocketLauncher});
    check(eq(m.weaponCrates[0].hp,500)&&m.damageWeaponCrate(1,0,499)&&m.weaponCrates[0].active,"weapon box keeps one hp");
    check(m.damageWeaponCrate(2,0,1)&&m.fighters[1].weaponKind==WeaponKind::Pistol&&m.fighters[1].temporaryWeaponKind==WeaponKind::RocketLauncher&&m.fighters[1].rightWeapon.ammo==1,"last attacker equips a full temporary right rocket while retaining left pistol");
    check(eq(m.fighters[1].temporaryWeaponRemaining,60)&&!(m.fighters[1].unlockedWeapons&weaponBit(WeaponKind::RocketLauncher)),"destruction does not grant permanent ownership");
    check(m.fighters[0].score==0&&m.fighters[1].score==0,"supplies do not invent NPC points");
    check(!m.collectWeaponCrate(1,0)&&!m.damageWeaponCrate(1,0,1000),"claimed weapon cannot be stolen twice");
    m.damageEnvironment(2,100000);check(eq(m.fighters[1].temporaryWeaponRemaining,0)&&eq(m.fighters[1].evolutionRemaining,0),"both supply rewards end on death");
}
static void eligibilityAndContact(){
    auto m=quiet();m.add(1,Shape::Rectangle,0);m.addHost(9,1);m.evolutionPacks.push_back({{1000,1000}});m.weaponCrates.push_back({{1500,1500},WeaponKind::Sniper});
    check(!m.damageEvolutionPack(9,0,500)&&!m.damageWeaponCrate(9,0,500),"host retains its dedicated weapon and cannot claim supplies");
    check(!m.damageEvolutionPack(99,0,500)&&!m.damageWeaponCrate(1,-1,500),"invalid identities and indices rejected");
    check(!m.damageEvolutionPack(1,0,-1)&&!m.damageWeaponCrate(1,0,std::numeric_limits<double>::quiet_NaN()),"invalid damage rejected");
    m.phase=Phase::Results;check(!m.damageEvolutionPack(1,0,500)&&!m.damageWeaponCrate(1,0,500),"settlement cannot award supplies");m.phase=Phase::Battle;
    m.config.autoCollect=true;auto& b=m.world.bodies[0];b.position={1039.3,1039.3};b.angle=detail::Tau/8;b.velocity={0,0};m.world.rebuildSpatial();
    check((b.position-m.evolutionPacks[0].position).length()>50&&pickupTouches(b,m.evolutionPacks[0].position),"rotated rectangle touches corner outside old50radius");
    m.step(.001);check(!m.evolutionPacks[0].active&&m.fighters[0].evolutionRemaining>0,"actual corner contact awards evolution");
    b.position={1539.3,1539.3};m.world.rebuildSpatial();m.step(.001);
    check(!m.weaponCrates[0].active&&m.fighters[0].weaponKind==WeaponKind::Pistol&&m.fighters[0].temporaryWeaponKind==WeaponKind::Sniper,"actual corner contact awards right sniper without changing permanent left pistol");
    m.evolutionPacks.push_back({{500,500}});b.position={570,570};m.world.rebuildSpatial();m.step(.001);
    check(m.evolutionPacks.back().active,"a separated box is not collected");
}
static void piercingSupplies(){
    auto m=quiet();m.add(1,Shape::Rectangle,1);m.world.bodies[0].position={800,1000};m.world.bodies[0].velocity={90,0};m.world.rebuildSpatial();
    m.evolutionPacks.push_back({{1200,1000}});m.weaponCrates.push_back({{1200,1100},WeaponKind::MachineGun});
    SwordWave a;a.from=a.position={1170,1000};a.direction={1,0};a.ownerId=1;a.team=1;
    auto b=a;b.from=b.position={1170,1100};m.swordWaves={a,b};m.step(.05);
    check(eq(m.evolutionPacks[0].hp,440)&&eq(m.weaponCrates[0].hp,440),"evolution swords damage both kinds of supplies");
    m.step(.01);check(eq(m.evolutionPacks[0].hp,440)&&eq(m.weaponCrates[0].hp,440),"one sword cannot damage the same supply twice");
    m.weaponCrates[0].spawnedAt=90;m.weaponCrates[0].hp=500;m.swordWaves[1].position={1170,1100};m.step(.01);
    check(eq(m.weaponCrates[0].hp,440),"a replacement in the same crate slot is a fresh target");
}
static void concentratedHeroes(){
    auto m=quiet();m.add(1,Shape::Rectangle,1);m.add(2,Shape::Rectangle,2);m.add(3,Shape::Rectangle,2);
    for(size_t i=0;i<3;++i){auto& b=m.world.bodies[i];b.position={1000+150.0*i,1000};b.velocity={90,0};b.spin=0;b.angle=0;m.fighters[i].hp=m.fighters[i].maxHp=1000;}
    m.world.rebuildSpatial();m.weapon.range=0;m.config.autoCombat=true;m.fighters[0].heroBuff=true;m.fighters[0].heroSwordRemaining=.001;m.fighters[0].aimAngle=0;
    m.fighters[2].heroBuff=true;m.fighters[2].heroSwordRemaining=100;m.step(.001);
    check(m.swordWaves.size()==1,"hero burst begins with one sword");m.step(1);
    check(m.swordWaves.size()==6,"all six swords are in flight one second after burst start");
    for(size_t i=0;i<m.swordWaves.size();++i){const auto& wave=m.swordWaves[i];check(eq(wave.direction.x,1)&&eq(wave.direction.y,0)&&eq(wave.maxDistance,1100)&&eq(wave.speed,880)&&eq(wave.life,.25+.2*i),"parallel swords share range and have staggered flight ages");}
    check(eq(m.swordWaves.front().from.y,991.2)&&eq(m.swordWaves.back().from.y,1008.8),"spawn strip is plus-minus0.2 body widths");
    m.config.autoCombat=false;m.step(.45);
    check(eq(m.fighters[1].hp,820)&&eq(m.fighters[2].hp,280),"six penetrating swords concentrate180 ordinary or720 hero damage");
    m.step(.799);check(m.swordWaves.size()==1,"last sword retains fading flight until its own1.25second lifetime");m.step(.001);
    check(m.swordWaves.empty(),"last hero sword expires2.25seconds after burst start at evolution range");
}
int main(){int failed=0;const auto run=[&](auto fn,const char* name){try{fn();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& e){++failed;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}};
    run(lastHitRewards,"last-hit rewards and lifetime");run(eligibilityAndContact,"eligibility and corner contact");run(piercingSupplies,"piercing supply hits");run(concentratedHeroes,"concentrated hero swords");
    std::cout<<assertions<<" assertions, "<<failed<<" failed groups\n";return failed?1:0;
}
