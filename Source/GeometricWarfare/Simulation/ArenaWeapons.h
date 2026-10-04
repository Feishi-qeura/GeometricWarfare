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
struct WeaponProjectile {
    Vec position{},previous{},velocity{};
    double travelled=0,maxDistance=0,radius=3,damage=0;
    int ownerId=-1,team=0;
    WeaponKind kind=WeaponKind::Sniper;
    bool active=true;
};
struct WeaponExplosion {
    Vec position{};
    double age=0,lifetime=.45,radius=RocketExplosionRadius;
    int team=0;
    bool active=true;
};
}
