#include "../Source/GeometricWarfare/Simulation/ArenaMatch.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace gw;
static int assertions=0;
static void check(bool ok,const char* message){++assertions;if(!ok)throw std::runtime_error(message);}
static bool eq(double a,double b){return std::abs(a-b)<1e-6;}
static Match quiet(){MatchConfig c;c.naturalOrbs=c.npcCount=0;c.autoCombat=c.autoCollect=false;Match m(c);m.fighters.reserve(3);return m;}
static int crate(Match& m,WeaponKind kind,Vec position={2000,2000}){m.weaponCrates.push_back({position,kind,true,m.elapsed});return static_cast<int>(m.weaponCrates.size()-1);}
static void permanentSelection(){
    Match m=quiet();m.add(1,Shape::Rectangle,1);m.grantWeapon(1,WeaponKind::Rifle);auto& f=m.fighters[0];f.ammo=7;f.reloadRemaining=2;const auto owned=f.unlockedWeapons;
    check(m.collectWeaponCrate(1,crate(m,WeaponKind::Sniper)),"collect temporary sniper");
    check(f.weaponKind==WeaponKind::Rifle&&f.ammo==7&&eq(f.reloadRemaining,2)&&f.unlockedWeapons==owned,"crate preserves permanent left rifle, magazine, reload and unlocks");
    check(!m.switchWeapon(1,WeaponKind::Sniper),"temporary right sniper cannot be selected by permanent left command");
    check(m.switchWeapon(1,WeaponKind::Pistol)&&f.temporaryWeaponKind==WeaponKind::Sniper,"left permanent switch preserves right lease");
}
static void movement(){
    Match m=quiet();m.add(1,Shape::Rectangle,1);m.grantWeapon(1,WeaponKind::Rifle);auto& b=m.world.bodies[0];b.position={1000,1000};b.velocity={100,0};m.world.rebuildSpatial();m.step(.001);
    check(eq(b.speedScale,1.5),"left rifle gives its existing movement speed");const double speed=b.velocity.length();
    m.collectWeaponCrate(1,crate(m,WeaponKind::Sniper));m.step(.001);
    check(eq(b.speedScale,1.5)&&eq(b.velocity.length(),speed),"right sniper cannot slow permanent rifle movement");
}
template<class F> static void rightRuntime(F& f,Match& m){
    if constexpr(requires{f.rightWeapon.ammo;}){
        m.grantWeapon(f.id,WeaponKind::MachineGun);f.ammo=17;f.reloadRemaining=5;
        m.collectWeaponCrate(f.id,crate(m,WeaponKind::MachineGun));
        check(f.weaponKind==WeaponKind::MachineGun&&f.ammo==17&&eq(f.reloadRemaining,5)&&f.rightWeapon.ammo==150,"owned machine gun crate creates independent full right magazine");
        f.rightWeapon.ammo=9;f.rightWeapon.reloadRemaining=4;f.rightWeapon.shotRemaining=.08;f.rightWeapon.aimRemaining=.17;f.rightWeapon.targetKind=3;f.rightWeapon.targetIndex=2;m.step(1);
        m.collectWeaponCrate(f.id,crate(m,WeaponKind::MachineGun));
        check(eq(f.temporaryWeaponRemaining,60)&&f.rightWeapon.ammo==9&&eq(f.rightWeapon.reloadRemaining,4)&&eq(f.rightWeapon.shotRemaining,.08)&&eq(f.rightWeapon.aimRemaining,.17)&&f.rightWeapon.targetKind==3,"same owned right crate refreshes lease without resetting any combat state");
        m.grantWeapon(f.id,WeaponKind::MachineGun);
        check(f.rightWeapon.ammo==9&&eq(f.rightWeapon.reloadRemaining,4)&&eq(f.temporaryWeaponRemaining,60),"permanent gift preserves right runtime and lease");
        m.collectWeaponCrate(f.id,crate(m,WeaponKind::RocketLauncher));
        check(f.weaponKind==WeaponKind::MachineGun&&f.ammo==17&&f.rightWeapon.ammo==1&&eq(f.rightWeapon.reloadRemaining,0)&&f.rightWeapon.targetKind==0,"different right kind replaces runtime with fresh magazine only");
    }else check(false,"independent right weapon runtime is missing");
}
static void runtime(){Match m=quiet();m.add(1,Shape::Rectangle,1);rightRuntime(m.fighters[0],m);}
template<class F> static void dualFire(F& f,Match& m){
    if constexpr(requires{f.rightWeapon.ammo;}){
        m.grantWeapon(f.id,WeaponKind::MachineGun);m.collectWeaponCrate(f.id,crate(m,WeaponKind::MachineGun));
        m.add(2,Shape::Square,2);auto& enemy=m.fighters[1];enemy.hp=enemy.maxHp=100000;enemy.shotRemaining=1000;
        m.world.bodies[0].position={1000,1000};m.world.bodies[1].position={1300,1000};for(auto& b:m.world.bodies)b.velocity={};m.world.rebuildSpatial();
        f.targetKind=f.rightWeapon.targetKind=1;f.targetIndex=f.rightWeapon.targetIndex=1;f.acquisitionRemaining=f.rightWeapon.acquisitionRemaining=10;f.aimRemaining=f.rightWeapon.aimRemaining=0;
        m.config.autoCombat=true;m.step(.001);
        check(f.ammo==149&&f.rightWeapon.ammo==149&&m.projectiles.size()==14,"same-kind hands shoot simultaneously using separate magazines");
        bool left=false,right=false;for(const auto& p:m.projectiles){if constexpr(requires{p.rightHand;}){if(p.rightHand){right=true;check(p.previous.y>1000,"right muzzle is on its own side");}else{left=true;check(p.previous.y<1000,"left muzzle is on its own side");}}}
        check(left&&right,"projectiles carry both hand markers");
        f.reloadRemaining=1;f.ammo=0;f.rightWeapon.shotRemaining=0;f.rightWeapon.aimRemaining=0;const int before=f.rightWeapon.ammo;m.step(.001);
        check(f.ammo==0&&f.reloadRemaining>0&&f.rightWeapon.ammo==before-1,"right hand fires while left hand reloads");
        f.reloadRemaining=0;f.ammo=10;f.shotRemaining=0;f.aimRemaining=0;f.rightWeapon.ammo=0;f.rightWeapon.reloadRemaining=1;m.step(.001);
        check(f.ammo==9&&f.rightWeapon.ammo==0&&f.rightWeapon.reloadRemaining>0,"left hand fires while right hand reloads");
    }else check(false,"independent simultaneous fire is missing");
}
static void simultaneous(){Match m=quiet();m.add(1,Shape::Rectangle,1);dualFire(m.fighters[0],m);}
template<class F> static void lifetime(F& f,Match& m){
    if constexpr(requires{f.rightWeapon.ammo;}){
        m.grantWeapon(f.id,WeaponKind::Rifle);f.ammo=7;f.reloadRemaining=2;const auto owned=f.unlockedWeapons;
        m.collectWeaponCrate(f.id,crate(m,WeaponKind::Sniper));m.step(59.999);check(f.temporaryWeaponRemaining>0&&f.rightWeapon.ammo==5,"right lease survives until expiration");m.step(.001);
        check(eq(f.temporaryWeaponRemaining,0)&&f.rightWeapon.ammo==0&&f.rightWeapon.targetKind==0&&f.weaponKind==WeaponKind::Rifle&&f.ammo==7&&eq(f.reloadRemaining,2),"exact expiration clears only right runtime");
        m.collectWeaponCrate(f.id,crate(m,WeaponKind::MachineGun));m.damageEnvironment(f.id,100000);
        check(!f.alive&&f.weaponKind==WeaponKind::Rifle&&f.unlockedWeapons==owned&&eq(f.temporaryWeaponRemaining,0)&&f.rightWeapon.ammo==0,"death clears right and keeps selected permanent gun and unlocks");
        m.revive(f.id);check(f.weaponKind==WeaponKind::Rifle&&f.rightWeapon.ammo==0,"revive retains left and no right weapon");
        const int id=f.id;m.collectWeaponCrate(id,crate(m,WeaponKind::RocketLauncher));m.phase=Phase::Results;m.startNextRound();const auto& next=*m.findFighter(id);
        check(next.weaponKind==WeaponKind::Rifle&&next.unlockedWeapons==owned&&eq(next.temporaryWeaponRemaining,0)&&next.rightWeapon.ammo==0,"new round preserves left choice and clears right");
    }else check(false,"right lease lifetime runtime is missing");
}
static void leases(){Match m=quiet();m.add(1,Shape::Rectangle,1);lifetime(m.fighters[0],m);}
static void battleResultsBoundary(){
    Match m=quiet();m.config.battleSeconds=.2;m.config.sprintSeconds=.1;m.add(1,Shape::Rectangle,1);
    m.grantWeapon(1,WeaponKind::Shotgun);m.grantWeapon(1,WeaponKind::Rifle);auto& f=m.fighters[0];
    f.ammo=7;f.reloadRemaining=2;f.shotRemaining=.17;const auto owned=f.unlockedWeapons;
    m.collectWeaponCrate(1,crate(m,WeaponKind::Sniper));f.rightWeapon.ammo=2;f.rightWeapon.reloadRemaining=4;f.rightWeapon.shotRemaining=3;
    f.rightWeapon.aimRemaining=1;f.rightWeapon.sniperAimDuration=2;f.rightWeapon.acquisitionRemaining=.3;f.rightWeapon.aimAngle=.9;f.rightWeapon.targetKind=3;f.rightWeapon.targetIndex=2;
    m.step(.199);check(m.phase==Phase::Sprint&&f.temporaryWeaponRemaining>0&&f.rightWeapon.ammo==2,"right weapon stays live immediately before actual battle time boundary");
    m.step(.001);check(m.phase==Phase::Results,"actual battle time boundary enters Results");
    check(f.temporaryWeaponRemaining==0&&f.temporaryWeaponKind==WeaponKind::Pistol&&f.rightWeapon.ammo==0&&f.rightWeapon.reloadRemaining==0&&f.rightWeapon.shotRemaining==0&&f.rightWeapon.aimRemaining==0&&f.rightWeapon.sniperAimDuration==0&&f.rightWeapon.acquisitionRemaining==0&&f.rightWeapon.aimAngle==0&&f.rightWeapon.targetKind==0&&f.rightWeapon.targetIndex==-1,"battle end immediately clears right lease and every runtime field");
    check(f.weaponKind==WeaponKind::Rifle&&f.ammo==7&&eq(f.reloadRemaining,2)&&eq(f.shotRemaining,.17)&&f.unlockedWeapons==owned&&f.weaponStates[static_cast<size_t>(WeaponKind::Shotgun)].ammo==5,"battle end preserves permanent left selection, current magazine, timers and saved owned magazines");
    m.step(15);check(m.phase==Phase::Results&&f.temporaryWeaponRemaining==0&&f.rightWeapon.ammo==0&&f.weaponKind==WeaponKind::Rifle&&f.ammo==7,"right gun remains cleared throughout results while left state remains frozen");
}
template<class F> static void rightBox(F& f,Match& m){
    if constexpr(requires{f.rightWeapon.ammo;}){
        m.world.bodies[0].position={1000,1000};m.world.bodies[0].velocity={};m.world.rebuildSpatial();m.collectWeaponCrate(f.id,crate(m,WeaponKind::RocketLauncher));
        const int box=crate(m,WeaponKind::Sniper,{1300,1000});m.weaponCrates[box].hp=1;m.bases[1].alive=m.bases[2].alive=false;
        f.reloadRemaining=100;f.rightWeapon.targetKind=6;f.rightWeapon.targetIndex=box;f.rightWeapon.acquisitionRemaining=10;
        m.config.autoCombat=true;m.step(.001);check(m.projectiles.size()==1,"right rocket launches toward crate while left reloads");m.step(.2);
        check(!m.weaponCrates[box].active&&f.temporaryWeaponKind==WeaponKind::Sniper&&f.rightWeapon.ammo==5&&f.rightWeapon.reloadRemaining==0&&f.weaponKind==WeaponKind::Pistol,"right rocket breaks crate and replacement survives its firing side effects");
        m.add(2,Shape::Square,2);m.world.bodies[1].position={1500,1000};m.world.bodies[1].velocity={};m.fighters[1].shotRemaining=100;m.world.rebuildSpatial();m.step(.001);
        check(f.rightWeapon.targetKind==1&&f.rightWeapon.targetIndex==1&&f.rightWeapon.aimRemaining>0,"replaced right sniper independently reacquires with its own aim delay");
    }else check(false,"right crate destruction replacement runtime is missing");
}
static void crateReplacement(){Match m=quiet();m.add(1,Shape::Rectangle,1);rightBox(m.fighters[0],m);}
template<class F> static void heal(F& f,Match& m){
    if constexpr(requires{f.healFlash;}){
        m.healLike(f.id,1);check(f.healFlash==0&&m.damageEvents.empty(),"full HP like produces neither heal flash nor zero healing number");
        f.hp-=20;m.healLike(f.id,1);check(eq(f.hp,190)&&eq(f.healFlash,.7)&&m.damageEvents.back().kind==NumberKind::Healing&&eq(m.damageEvents.back().amount,10),"actual like restores existing five-percent HP and starts green flash");
        m.step(.3);check(eq(f.healFlash,.4),"green flash decays with simulation");m.step(.4);check(eq(f.healFlash,0),"green flash expires");
        f.hp-=10;m.healLike(f.id,1);m.damageEnvironment(f.id,100000);check(f.healFlash==0,"death clears healing flash immediately");
    }else check(false,"heal flash runtime is missing");
}
static void healing(){Match m=quiet();m.add(1,Shape::Rectangle,1);heal(m.fighters[0],m);}
static void host(){Match m=quiet();m.addHost(1,1);check(!m.collectWeaponCrate(1,crate(m,WeaponKind::Sniper))&&m.fighters[0].weaponKind==WeaponKind::Rifle&&m.fighters[0].temporaryWeaponRemaining==0,"host keeps rifle and rejects right crate");}
int main(){int failures=0;const std::pair<const char*,void(*)()> tests[]={{"permanent left selection",permanentSelection},{"left movement",movement},{"independent right runtime",runtime},{"simultaneous fire",simultaneous},{"lease death round",leases},{"battle Results boundary",battleResultsBoundary},{"right crate replacement",crateReplacement},{"healing flash",healing},{"host",host}};
    for(const auto& test:tests)try{test.second();std::cout<<"PASS "<<test.first<<'\n';}catch(const std::exception& e){++failures;std::cerr<<"FAIL "<<test.first<<": "<<e.what()<<'\n';}
    std::cout<<assertions<<" assertions, "<<failures<<" failed groups\n";return failures?1:0;
}
