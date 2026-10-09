#include "../Source/GeometricWarfare/Simulation/ArenaMatch.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace gw;
static int assertions=0;
static void check(bool ok,const char* message){++assertions;if(!ok)throw std::runtime_error(message);}
static bool eq(double a,double b){return std::abs(a-b)<1e-6;}
static Match quiet(){MatchConfig c;c.naturalOrbs=c.npcCount=0;c.autoCombat=c.autoCollect=false;return Match(c);}
static void crate(Match& m,WeaponKind kind,Vec position={1000,1000}){m.weaponCrates.push_back({position,kind,true,m.elapsed});}
static void timedSpawns(){
    Match m=quiet();m.step(89.999);check(m.weaponCrates.empty(),"no weapon crates spawn before90 seconds");m.step(.001);
    check(m.weaponCrates.size()==10,"first90 second boundary spawns ten weapon crates");
    for(const auto& box:m.weaponCrates)check(box.active&&box.spawnedAt==90&&box.weaponKind>=WeaponKind::Sniper&&box.weaponKind<=WeaponKind::RocketLauncher&&box.position.x>=50&&box.position.x<=World::Size-50&&box.position.y>=50&&box.position.y<=World::Size-50,"crate wave contains only valid random temporary weapons inside world walls");
    m.step(90);check(m.weaponCrates.size()==20,"second wave adds ten at180 seconds");m.step(90);check(m.weaponCrates.size()==30,"third wave spawns at270 seconds");m.step(90);check(m.weaponCrates.size()==40&&m.phase==Phase::Sprint,"fourth wave continues during Sprint at360 seconds");
    m.startNextRound();check(m.weaponCrates.empty(),"new round clears old ground crates");m.step(90);check(m.weaponCrates.size()==10,"new round restarts crate schedule fromzero");
}
static void leaseAndOwnedSwitches(){
    Match m=quiet();m.add(1,Shape::Rectangle,1);auto& f=m.fighters[0];m.grantWeapon(1,WeaponKind::Rifle);f.ammo=7;f.reloadRemaining=2;const uint8_t owned=f.unlockedWeapons;
    crate(m,WeaponKind::Sniper);check(m.collectWeaponCrate(1,0)&&!m.weaponCrates[0].active,"viewer consumes a weapon crate");
    check(f.weaponKind==WeaponKind::Rifle&&f.ammo==7&&eq(f.reloadRemaining,2)&&f.temporaryWeaponKind==WeaponKind::Sniper&&f.rightWeapon.ammo==5&&f.unlockedWeapons==owned&&eq(f.temporaryWeaponRemaining,60),"crate equips a full right sniper for sixty seconds while preserving permanent left rifle ammo and reload");
    f.rightWeapon.ammo=2;f.rightWeapon.reloadRemaining=4;m.step(59.999);check(f.weaponKind==WeaponKind::Rifle&&f.temporaryWeaponKind==WeaponKind::Sniper&&f.temporaryWeaponRemaining>0,"right temporary weapon remains until its exact expiration");m.step(.001);
    check(f.weaponKind==WeaponKind::Rifle&&f.ammo==7&&eq(f.reloadRemaining,2)&&eq(f.temporaryWeaponRemaining,0)&&f.temporaryWeaponKind==WeaponKind::Pistol&&f.rightWeapon.targetKind==0,"expiry clears the right hand while preserving left rifle magazine and timer");
    const auto left=m.weaponFor(f);check(left.magazine==45&&eq(left.damage,8)&&eq(left.range,1800)&&eq(left.fireInterval,.2)&&eq(left.reloadTime,3),"right expiration leaves the permanent left rifle's exact weapon stats intact");
    check(!m.switchWeapon(1,WeaponKind::Sniper),"expired temporary unlock can no longer be selected");
    crate(m,WeaponKind::MachineGun);m.collectWeaponCrate(1,1);check(m.switchWeapon(1,WeaponKind::Pistol)&&!m.switchWeapon(1,WeaponKind::MachineGun)&&f.temporaryWeaponKind==WeaponKind::MachineGun&&f.rightWeapon.ammo==150,"left switches only among owned weapons while the right-only machinegun stays equipped");
    m.switchWeapon(1,WeaponKind::Pistol);m.step(60);check(f.weaponKind==WeaponKind::Pistol&&!m.switchWeapon(1,WeaponKind::MachineGun),"expiration preserves an already selected owned weapon and removes leased access");
}
static void deathGiftAndReplacement(){
    Match m=quiet();m.add(1,Shape::Rectangle,1);auto& f=m.fighters[0];crate(m,WeaponKind::Sniper);m.collectWeaponCrate(1,0);m.damageEnvironment(1,100000);
    check(!f.alive&&f.weaponKind==WeaponKind::Pistol&&eq(f.temporaryWeaponRemaining,0)&&f.unlockedWeapons==1,"death cancels the right crate lease while retaining permanent left pistol without saving the temporary gun");
    m.revive(1);crate(m,WeaponKind::MachineGun);m.collectWeaponCrate(1,1);f.rightWeapon.ammo=17;f.rightWeapon.reloadRemaining=5;f.rightWeapon.shotRemaining=1.2;f.rightWeapon.aimRemaining=.8;f.rightWeapon.acquisitionRemaining=3;f.rightWeapon.aimAngle=.7;f.rightWeapon.targetKind=3;f.rightWeapon.targetIndex=2;f.temporaryWeaponRemaining=20;crate(m,WeaponKind::MachineGun);m.collectWeaponCrate(1,2);
    check(f.rightWeapon.ammo==17&&eq(f.rightWeapon.reloadRemaining,5)&&eq(f.rightWeapon.shotRemaining,1.2)&&eq(f.rightWeapon.aimRemaining,.8)&&eq(f.rightWeapon.acquisitionRemaining,3)&&eq(f.rightWeapon.aimAngle,.7)&&f.rightWeapon.targetKind==3&&f.rightWeapon.targetIndex==2&&eq(f.temporaryWeaponRemaining,60),"same right crate refreshes lifetime without refilling ammo or resetting reload, aim, shot and target state");
    crate(m,WeaponKind::RocketLauncher);m.collectWeaponCrate(1,3);check(!m.switchWeapon(1,WeaponKind::MachineGun)&&f.weaponKind==WeaponKind::Pistol&&f.temporaryWeaponKind==WeaponKind::RocketLauncher&&f.rightWeapon.ammo==1&&eq(f.rightWeapon.reloadRemaining,0)&&eq(f.rightWeapon.shotRemaining,0)&&f.rightWeapon.targetKind==0,"different crate replaces only the right weapon with a fresh magazine and target state");
    f.rightWeapon.ammo=0;f.rightWeapon.reloadRemaining=3;m.grantWeapon(1,WeaponKind::RocketLauncher);check(f.weaponKind==WeaponKind::RocketLauncher&&f.ammo==1&&eq(f.temporaryWeaponRemaining,60)&&f.rightWeapon.ammo==0&&eq(f.rightWeapon.reloadRemaining,3)&&(f.unlockedWeapons&weaponBit(WeaponKind::RocketLauncher)),"permanent gift equips left rocket without modifying the independently reloading right rocket");m.step(61);
    check(f.weaponKind==WeaponKind::RocketLauncher&&f.ammo==1&&eq(f.temporaryWeaponRemaining,0)&&m.switchWeapon(1,WeaponKind::RocketLauncher),"right lease expiration cannot revoke or refill the permanent left gun");
    crate(m,WeaponKind::Sniper);m.collectWeaponCrate(1,4);m.phase=Phase::Results;m.startNextRound();const auto* restored=m.findFighter(1);
    check(restored->weaponKind==WeaponKind::RocketLauncher&&eq(restored->temporaryWeaponRemaining,0)&&!(restored->unlockedWeapons&weaponBit(WeaponKind::Sniper)),"round transition discards the live right lease and preserves permanent left rocket");
}
static void pickupEligibility(){
    Match m=quiet();m.add(1,Shape::Circle,0);m.addHost(2,1);crate(m,WeaponKind::Sniper);
    check(!m.collectWeaponCrate(2,0)&&m.weaponCrates[0].active,"host cannot consume viewer weapon crates");
    m.world.find(1)->position={1000,1000};m.world.find(1)->velocity={90,0};m.world.find(2)->position={World::Size-200,World::Size-200};m.world.rebuildSpatial();m.config.autoCollect=true;m.step(.001);
    check(!m.weaponCrates[0].active&&m.findFighter(1)->weaponKind==WeaponKind::Pistol&&m.findFighter(1)->temporaryWeaponKind==WeaponKind::Sniper,"living gray viewer automatically collects a nearby right weapon while keeping left pistol");
    m.damageEnvironment(1,100000);crate(m,WeaponKind::MachineGun);check(!m.collectWeaponCrate(1,1)&&m.weaponCrates[1].active,"dead viewer cannot consume a crate");
    m.phase=Phase::Results;m.intermissionRemaining=30;check(!m.collectWeaponCrate(2,1)&&m.weaponCrates[1].active,"results never consume crates");
    check(!m.collectWeaponCrate(999,0)&&!m.collectWeaponCrate(1,-1),"invalid collector and index are rejected");
}
static void leftSwitchGiftAndBoundedPool(){
    Match m=quiet();m.add(1,Shape::Rectangle,1);auto& f=m.fighters[0];m.grantWeapon(1,WeaponKind::Rifle);crate(m,WeaponKind::Sniper);m.collectWeaponCrate(1,0);
    m.switchWeapon(1,WeaponKind::Pistol);m.grantWeapon(1,WeaponKind::Sniper);f.ammo=2;f.reloadRemaining=4;
    check(eq(f.temporaryWeaponRemaining,60)&&f.weaponKind==WeaponKind::Sniper&&f.temporaryWeaponKind==WeaponKind::Sniper&&f.rightWeapon.ammo==5,"permanent gift equips left sniper while retaining the active independent right sniper");
    m.grantWeapon(1,WeaponKind::Sniper);m.step(60);check(f.ammo==2&&eq(f.reloadRemaining,4)&&eq(f.temporaryWeaponRemaining,0)&&m.switchWeapon(1,WeaponKind::Sniper),"duplicate permanent grant and right lease deadline cannot refill or revoke left sniper");
    crate(m,WeaponKind::MachineGun);m.collectWeaponCrate(1,1);m.switchWeapon(1,WeaponKind::Pistol);m.damageEnvironment(1,100000);
    check(f.weaponKind==WeaponKind::Pistol&&eq(f.temporaryWeaponRemaining,0)&&!m.switchWeapon(1,WeaponKind::MachineGun),"death clears right equipment while retaining the switched permanent left pistol");
    Match longRound=quiet();longRound.config.battleSeconds=1500;longRound.config.sprintSeconds=1400;longRound.step(900);
    check(longRound.weaponCrates.size()==WeaponCrateCapacity,"ten waves of uncollected crates remain in a bounded pool");
    int kinds=0;for(const auto& box:longRound.weaponCrates){kinds|=weaponBit(box.weaponKind);check(box.active&&box.spawnedAt>=360,"bounded crate pool replaces oldest ground pickups");}
    check(kinds==(weaponBit(WeaponKind::Sniper)|weaponBit(WeaponKind::MachineGun)|weaponBit(WeaponKind::RocketLauncher)),"repeated deterministic waves contain all three temporary weapon types");
}
static void ownedCrateAndLifecycle(){
    Match m=quiet();m.add(1,Shape::Rectangle,1);auto& f=m.fighters[0];m.grantWeapon(1,WeaponKind::Sniper);f.ammo=1;f.reloadRemaining=4;f.shotRemaining=2;f.aimRemaining=.6;f.sniperAimDuration=.9;f.acquisitionRemaining=3;f.targetKind=3;f.targetIndex=2;f.aimAngle=.7;
    const auto owned=f.unlockedWeapons;crate(m,WeaponKind::Sniper);check(m.collectWeaponCrate(1,0),"owned sniper crate is still consumed");
    check(f.weaponKind==WeaponKind::Sniper&&f.temporaryWeaponKind==WeaponKind::Sniper&&eq(f.temporaryWeaponRemaining,60)&&f.rightWeapon.ammo==5&&f.unlockedWeapons==owned,"already owned kind still awards a separate sixty second right weapon without altering permanent rights");
    check(f.ammo==1&&eq(f.reloadRemaining,4)&&eq(f.shotRemaining,2)&&eq(f.aimRemaining,.6)&&eq(f.sniperAimDuration,.9)&&eq(f.acquisitionRemaining,3)&&f.targetKind==3&&f.targetIndex==2&&eq(f.aimAngle,.7),"owned-kind pickup cannot change any left ammunition, timing, aim or target state");
    f.rightWeapon.ammo=2;f.rightWeapon.reloadRemaining=3;f.temporaryWeaponRemaining=9;crate(m,WeaponKind::Sniper);m.collectWeaponCrate(1,1);
    check(f.rightWeapon.ammo==2&&eq(f.rightWeapon.reloadRemaining,3)&&eq(f.temporaryWeaponRemaining,60)&&f.ammo==1&&eq(f.reloadRemaining,4),"owned same-kind crate only refreshes right lifetime and preserves both magazines and reloads");
    m.damageEnvironment(1,100000);check(!f.alive&&f.weaponKind==WeaponKind::Sniper&&eq(f.temporaryWeaponRemaining,0)&&f.temporaryWeaponKind==WeaponKind::Pistol&&f.unlockedWeapons==owned,"death clears right equipment while retaining selected permanent left sniper");
    check(m.revive(1)&&f.weaponKind==WeaponKind::Sniper&&f.ammo==5&&eq(f.temporaryWeaponRemaining,0),"revival restores a fresh permanent left magazine without restoring right equipment");
    crate(m,WeaponKind::MachineGun);m.collectWeaponCrate(1,2);m.startNextRound();const auto* restored=m.findFighter(1);
    check(restored->weaponKind==WeaponKind::Sniper&&restored->ammo==5&&restored->unlockedWeapons==owned&&eq(restored->temporaryWeaponRemaining,0)&&restored->temporaryWeaponKind==WeaponKind::Pistol,"round restart preserves selected left and permanent rights while discarding the independent right gun");
}
int main(){int failures=0;const std::pair<const char*,void(*)()> tests[]={{"timed spawns",timedSpawns},{"lease and owned switches",leaseAndOwnedSwitches},{"death gift replacement",deathGiftAndReplacement},{"pickup eligibility",pickupEligibility},{"left switch gift and bounded pool",leftSwitchGiftAndBoundedPool},{"owned crate and lifecycle",ownedCrateAndLifecycle}};
    for(const auto& test:tests)try{test.second();std::cout<<"PASS "<<test.first<<'\n';}catch(const std::exception& e){++failures;std::cerr<<"FAIL "<<test.first<<": "<<e.what()<<'\n';}
    std::cout<<assertions<<" assertions, "<<failures<<" failed groups\n";return failures?1:0;
}
