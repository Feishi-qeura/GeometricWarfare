#pragma once
#include "ArenaPhysics.h"
#include <array>
#include <cstdint>

namespace gw {
// Values are stable: comments weapon1..weapon6 map directly to value+1.
enum class WeaponKind { Pistol, Shotgun, Rifle, Sniper, MachineGun, RocketLauncher };
inline constexpr int WeaponCount=6;
inline constexpr uint8_t weaponBit(WeaponKind kind){return static_cast<uint8_t>(1u<<static_cast<unsigned>(kind));}
inline constexpr bool validWeapon(WeaponKind kind){return kind>=WeaponKind::Pistol&&kind<=WeaponKind::RocketLauncher;}
inline constexpr double RocketExplosionRadius=248.90158697766472; // Four times the original eight-circle-area blast radius.
inline constexpr size_t WeaponProjectileCapacity=8192;
inline constexpr size_t WeaponExplosionCapacity=128;
struct WeaponState {int ammo=0;double reloadRemaining=0,shotRemaining=0;};
struct WeaponRuntime {
    int ammo=0;
    double reloadRemaining=0,shotRemaining=0,aimRemaining=0,sniperAimDuration=0,acquisitionRemaining=0,aimAngle=0;
    int targetKind=0,targetIndex=-1;
};
// A view lets both hands run the same combat code while the permanent left
// state retains its public legacy fields. No state is swapped or copied back.
struct WeaponRuntimeView {
    WeaponKind kind;
    int& ammo;
    double& reloadRemaining;double& shotRemaining;double& aimRemaining;double& sniperAimDuration;
    double& acquisitionRemaining;double& aimAngle;
    int& targetKind;int& targetIndex;
};
inline Vec weaponMuzzle(const Body& body,Vec direction,bool rightHand,bool hasRightWeapon=true){
    return body.position+direction*(28*body.scale)+Vec{-direction.y,direction.x}*((hasRightWeapon?(rightHand?10:-10):0)*body.scale);
}
struct WeaponProjectile {
    Vec position{},previous{},velocity{};
    double travelled=0,maxDistance=0,radius=3,damage=0;
    int ownerId=-1,team=0;
    WeaponKind kind=WeaponKind::Sniper;
    bool active=true;
    bool rightHand=false;
};
struct WeaponExplosion {
    Vec position{};
    double age=0,lifetime=.45,radius=RocketExplosionRadius;
    int team=0;
    bool active=true;
};
}
