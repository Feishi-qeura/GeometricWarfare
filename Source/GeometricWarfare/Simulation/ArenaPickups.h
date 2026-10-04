#pragma once
#include "ArenaWeapons.h"

namespace gw {
inline constexpr double WeaponCrateInterval=90;
inline constexpr double TemporaryWeaponLifetime=60;
inline constexpr size_t WeaponCrateCapacity=64;
struct WeaponCrate {
    Vec position{};
    WeaponKind weaponKind=WeaponKind::Sniper;
    bool active=true;
    double spawnedAt=0;
    double hp=500,maxHp=500,hitFlash=0;
};
}
