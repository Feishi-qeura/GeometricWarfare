#pragma once
// Included after the complete Match definition.
namespace gw {
inline void Match::syncArenaObstacles() {
    auto& obstacles=world.obstacles;obstacles.clear();
    if(boss.active){const double r=BossDiameter*.5;
        obstacles.push_back(StaticObstacle::polygon(boss.position,
            {Vec{-.62*r,-.8*r},Vec{.62*r,-.8*r},Vec{r,.8*r},Vec{-r,.8*r}},4));}
    for(const auto& npc:npcs)if(npc.active)obstacles.push_back(StaticObstacle::hexagon(npc.position,32));
    for(const auto& pack:evolutionPacks)if(pack.active)obstacles.push_back(StaticObstacle::box(pack.position,PickupHalfExtent));
    for(const auto& crate:weaponCrates)if(crate.active)obstacles.push_back(StaticObstacle::box(crate.position,PickupHalfExtent));
    world.rebuildObstacleSpatial();
}
} // namespace gw
