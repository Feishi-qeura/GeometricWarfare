#pragma once
#include "ArenaPhysics.h"
#include <bitset>
namespace gw {
inline constexpr double PickupHalfExtent=18;
inline bool pickupTouches(const Body& body,Vec position){
    const Vec delta=body.position-position;
    if(delta.dot(delta)<=50*50)return true;
    Body box;box.shape=Shape::Square;box.position=position;box.scale=(PickupHalfExtent+.75)/21;
    Vec normal;double depth;
    return overlap(body,box,normal,depth);
}
struct EvolutionPack {Vec position;bool active=true;double spawnedAt=0;double hp=500,maxHp=500,hitFlash=0;};
struct SwordWave {
    Vec from,position,direction;
    double traveled=0,maxDistance=1100,speed=440,damage=60,life=2.5;
    // A sword born within the current simulation step advances only after birth.
    double stepBirthDelay=0;
    int ownerId=-1,team=0;
    bool active=true,hero=false;
    std::bitset<World::Capacity> hitPlayers;
    std::bitset<128> hitNpcs;
    std::bitset<4> hitBases;
    bool hitBoss=false;
    // Index plus spawn time distinguishes a new crate in a recycled slot.
    std::vector<std::pair<size_t,double>> hitPickups;
};
}
