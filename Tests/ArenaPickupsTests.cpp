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
    for(const auto& box:m.weaponCrates)check(box.active&&box.spawnedAt==90&&box.weaponKind>=WeaponKind::Sniper&&box.weaponKind<=WeaponKind::RocketLauncher&&box.position.x>=50&&box.position.x<=7950&&box.position.y>=50&&box.position.y<=7950,"crate wave contains only valid random temporary weapons inside world walls");
    m.step(90);check(m.weaponCrates.size()==20,"second wave adds ten at180 seconds");m.step(90);check(m.weaponCrates.size()==30,"third wave spawns at270 seconds");m.step(90);check(m.weaponCrates.size()==40&&m.phase==Phase::Sprint,"fourth wave continues during Sprint at360 seconds");
    m.startNextRound();check(m.weaponCrates.empty(),"new round clears old ground crates");m.step(90);check(m.weaponCrates.size()==10,"new round restarts crate schedule fromzero");
}
static void leaseAndOwnedSwitches(){
    Match m=quiet();m.add(1,Shape::Rectangle,1);auto& f=m.fighters[0];m.grantWeapon(1,WeaponKind::Rifle);f.ammo=7;f.reloadRemaining=2;const uint8_t owned=f.unlockedWeapons;
    crate(m,WeaponKind::Sniper);check(m.collectWeaponCrate(1,0)&&!m.weaponCrates[0].active,"viewer consumes a weapon crate");
    check(f.weaponKind==WeaponKind::Sniper&&f.unlockedWeapons==owned&&eq(f.temporaryWeaponRemaining,60),"crate grants sixty second use without creating a permanent unlock");
    f.ammo=2;f.reloadRemaining=4;m.step(59.999);check(f.weaponKind==WeaponKind::Sniper&&f.temporaryWeaponRemaining>0,"temporary weapon remains until its exact expiration");m.step(.001);
    check(f.weaponKind==WeaponKind::Rifle&&f.ammo==7&&eq(f.reloadRemaining,2)&&eq(f.temporaryWeaponRemaining,0),"expiry restores previous owned rifle and preserves its magazine and timer");
    check(!m.switchWeapon(1,WeaponKind::Sniper),"expired temporary unlock can no longer be selected");
    crate(m,WeaponKind::MachineGun);m.collectWeaponCrate(1,1);check(m.switchWeapon(1,WeaponKind::Pistol)&&m.switchWeapon(1,WeaponKind::MachineGun),"owned weapons and the current leased weapon remain switchable");
    m.switchWeapon(1,WeaponKind::Pistol);m.step(60);check(f.weaponKind==WeaponKind::Pistol&&!m.switchWeapon(1,WeaponKind::MachineGun),"expiration preserves an already selected owned weapon and removes leased access");
}
static void deathPromotionAndReplacement(){
    Match m=quiet();m.add(1,Shape::Rectangle,1);auto& f=m.fighters[0];crate(m,WeaponKind::Sniper);m.collectWeaponCrate(1,0);m.damageEnvironment(1,100000);
    check(!f.alive&&f.weaponKind==WeaponKind::Pistol&&eq(f.temporaryWeaponRemaining,0)&&f.unlockedWeapons==1,"death cancels a crate lease and returns to pistol without saving the temporary gun");
    m.revive(1);crate(m,WeaponKind::MachineGun);m.collectWeaponCrate(1,1);f.ammo=17;f.reloadRemaining=5;crate(m,WeaponKind::MachineGun);m.collectWeaponCrate(1,2);
    check(f.ammo==17&&eq(f.reloadRemaining,5)&&eq(f.temporaryWeaponRemaining,60),"same temporary crate refreshes lifetime without refilling ammo or clearing reload");
    crate(m,WeaponKind::RocketLauncher);m.collectWeaponCrate(1,3);check(!m.switchWeapon(1,WeaponKind::MachineGun)&&f.weaponKind==WeaponKind::RocketLauncher,"different crate replaces the one active temporary entitlement");
    m.grantWeapon(1,WeaponKind::RocketLauncher);check(eq(f.temporaryWeaponRemaining,0)&&(f.unlockedWeapons&weaponBit(WeaponKind::RocketLauncher)),"permanent gift promotes an active lease immediately");m.step(61);
    check(f.weaponKind==WeaponKind::RocketLauncher&&m.switchWeapon(1,WeaponKind::RocketLauncher),"lease expiration cannot revoke a promoted permanent gun");
    crate(m,WeaponKind::Sniper);m.collectWeaponCrate(1,4);m.phase=Phase::Results;m.startNextRound();const auto* restored=m.findFighter(1);
    check(restored->weaponKind==WeaponKind::RocketLauncher&&eq(restored->temporaryWeaponRemaining,0)&&!(restored->unlockedWeapons&weaponBit(WeaponKind::Sniper)),"round transition discards a live lease and restores the previous permanent weapon");
}
static void pickupEligibility(){
    Match m=quiet();m.add(1,Shape::Circle,0);m.addHost(2,1);crate(m,WeaponKind::Sniper);
    check(!m.collectWeaponCrate(2,0)&&m.weaponCrates[0].active,"host cannot consume viewer weapon crates");
    m.world.find(1)->position={1000,1000};m.world.find(1)->velocity={90,0};m.world.find(2)->position={5000,5000};m.world.rebuildSpatial();m.config.autoCollect=true;m.step(.001);
    check(!m.weaponCrates[0].active&&m.findFighter(1)->weaponKind==WeaponKind::Sniper,"living gray viewer automatically collects a nearby temporary weapon");
    m.damageEnvironment(1,100000);crate(m,WeaponKind::MachineGun);check(!m.collectWeaponCrate(1,1)&&m.weaponCrates[1].active,"dead viewer cannot consume a crate");
    m.phase=Phase::Results;m.intermissionRemaining=30;check(!m.collectWeaponCrate(2,1)&&m.weaponCrates[1].active,"results never consume crates");
    check(!m.collectWeaponCrate(999,0)&&!m.collectWeaponCrate(1,-1),"invalid collector and index are rejected");
}
static void holsteredLeaseAndBoundedPool(){
    Match m=quiet();m.add(1,Shape::Rectangle,1);auto& f=m.fighters[0];m.grantWeapon(1,WeaponKind::Rifle);crate(m,WeaponKind::Sniper);m.collectWeaponCrate(1,0);
    m.switchWeapon(1,WeaponKind::Pistol);m.grantWeapon(1,WeaponKind::Sniper);f.ammo=2;f.reloadRemaining=4;
    check(eq(f.temporaryWeaponRemaining,0)&&f.weaponKind==WeaponKind::Sniper,"permanent gift promotes even a holstered lease and equips its weapon");
    m.grantWeapon(1,WeaponKind::Sniper);m.step(60);check(f.ammo==2&&eq(f.reloadRemaining,4)&&m.switchWeapon(1,WeaponKind::Sniper),"duplicate permanent grant and former lease deadline cannot refill or revoke promoted weapon");
    crate(m,WeaponKind::MachineGun);m.collectWeaponCrate(1,1);m.switchWeapon(1,WeaponKind::Pistol);m.damageEnvironment(1,100000);
    check(f.weaponKind==WeaponKind::Pistol&&eq(f.temporaryWeaponRemaining,0)&&!m.switchWeapon(1,WeaponKind::MachineGun),"death cancels holstered temporary access while retaining selected owned weapon");
    Match longRound=quiet();longRound.config.battleSeconds=1500;longRound.config.sprintSeconds=1400;longRound.step(900);
    check(longRound.weaponCrates.size()==WeaponCrateCapacity,"ten waves of uncollected crates remain in a bounded pool");
    int kinds=0;for(const auto& box:longRound.weaponCrates){kinds|=weaponBit(box.weaponKind);check(box.active&&box.spawnedAt>=360,"bounded crate pool replaces oldest ground pickups");}
    check(kinds==(weaponBit(WeaponKind::Sniper)|weaponBit(WeaponKind::MachineGun)|weaponBit(WeaponKind::RocketLauncher)),"repeated deterministic waves contain all three temporary weapon types");
}
int main(){int failures=0;const std::pair<const char*,void(*)()> tests[]={{"timed spawns",timedSpawns},{"lease and owned switches",leaseAndOwnedSwitches},{"death promotion replacement",deathPromotionAndReplacement},{"pickup eligibility",pickupEligibility},{"holstered lease and bounded pool",holsteredLeaseAndBoundedPool}};
    for(const auto& test:tests)try{test.second();std::cout<<"PASS "<<test.first<<'\n';}catch(const std::exception& e){++failures;std::cerr<<"FAIL "<<test.first<<": "<<e.what()<<'\n';}
    std::cout<<assertions<<" assertions, "<<failures<<" failed groups\n";return failures?1:0;
}
