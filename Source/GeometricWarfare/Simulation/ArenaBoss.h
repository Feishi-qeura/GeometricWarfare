#pragma once
#include "ArenaPhysics.h"
#include <array>
#include <cstdint>
#include <vector>

namespace gw {
inline constexpr double BossSpawnSeconds=150;
inline constexpr double BossDiameter=220;
inline constexpr double BossFireRadius=600;
inline constexpr double BossWaveRadius=900;
inline constexpr double BossBulletRadius=17.6;
inline constexpr double BossExplosionRadius=140;
inline constexpr double BossExplosionLifetime=.45;
inline constexpr double BossLaserWarningWidth=17.6;
inline constexpr double BossLaserWidth=52.8;
inline constexpr double BossLaserGrowthSeconds=.65;
inline constexpr double BossAttackRange=4000;
inline constexpr double BossBulletSpeed=1000;
inline constexpr double BossSpawnWaveSeconds=2;
inline constexpr double BossAttractionSteering=3;

inline double BossSpawnWaveRadiusAt(double elapsed) {
    const double t=std::clamp(elapsed/BossSpawnWaveSeconds,0.0,1.0);
    return World::Size*std::sqrt(.5)*t*t*(3-2*t);
}
inline double BossSpawnWaveOpacityAt(double elapsed) {
    const double t=std::clamp(elapsed/BossSpawnWaveSeconds,0.0,1.0);
    return 1-t*t*(3-2*t);
}

inline double BossLaserWidthAt(double elapsed) {
    const double t=std::clamp(elapsed/BossLaserGrowthSeconds,0.0,1.0);
    return BossLaserWarningWidth+(BossLaserWidth-BossLaserWarningWidth)*t*t*(3-2*t);
}
inline double BossScorchWidthAt(double elapsed) {return BossLaserWidthAt(elapsed);}
inline double BossScorchOpacityAt(double elapsed,double lifetime=5) {
    const double t=std::clamp(lifetime-elapsed,0.0,1.0);
    return t*t*(3-2*t);
}
enum class BossAttack { Idle, Bullet, LaserWindup, LaserActive, StompJump, StompWave, StompRest };
struct BossProjectile {
    Vec position{},velocity{};
    double radius=BossBulletRadius,travelled=0,damage=100;
    bool active=false;
};
struct BossExplosion {
    Vec position{};
    double age=0,lifetime=BossExplosionLifetime,radius=BossExplosionRadius;
    bool active=false;
};
struct BossScorch {
    Vec from{},to{},direction{1,0};
    double age=0,lifetime=5;
    int ticks=0;
    bool active=false;
};
struct BossState {
    bool active=false,spawned=false,rage=false;
    Vec position{World::Size*.5,World::Size*.5};
    // The boss and fire stay at the spawn center; jumping changes height only.
    Vec fireCenter{World::Size*.5,World::Size*.5};
    double hp=32000,maxHp=32000,armor=0,maxArmor=0,hitFlash=0,spawnAge=0;
    BossAttack attack=BossAttack::Idle;
    double attackElapsed=0,attackRemaining=0;
    bool attackInitialized=false;
    int targetId=-1,lastHitTeam=0;
    Vec laserFrom{},laserTo{},laserDirection{1,0};
    Vec jumpFrom{},landingPosition{},waveCenter{};
    double jumpHeight=0,waveRadius=0;
    int laserTicks=0;
    uint32_t waveSerial=0,randomSeed=0xB055C0DEu;
    uint64_t attacksCompleted=0,projectilesFired=0;
    std::array<BossProjectile,32> projectiles{};
    size_t projectileCursor=0;
    std::array<BossExplosion,16> explosions{};
    size_t explosionCursor=0;
    std::array<BossScorch,8> scorches{};
    size_t scorchCursor=0;
    // Fixed maximum player storage lives on the heap to keep the UE object small.
    std::vector<double> fireExposure=std::vector<double>(World::Capacity+1,0);
    std::vector<uint32_t> waveHits=std::vector<uint32_t>(World::Capacity+1,0);
    std::vector<uint32_t> npcWaveHits;
    std::vector<int> queryScratch;

    bool isInFire(Vec p) const {
        const Vec d=p-fireCenter;
        return active&&d.dot(d)<=BossFireRadius*BossFireRadius;
    }
};
} // namespace gw
