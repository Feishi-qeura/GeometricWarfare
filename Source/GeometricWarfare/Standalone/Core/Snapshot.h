#pragma once
#include "Maze.h"
namespace gwgame
{
// Everything a run needs to resume. Transient presentation (projectiles, effects, damage numbers) is
// dropped; units keep their ids so squads, burn owners and player reservations stay linked.
struct SavedUnit
{
    // owner is the player's identity for human characters (alive or dead), -1 for everyone else.
    int id = -1, team = 1, owner = -1, followerOf = -1, burnOwner = -1;
    UnitKind kind = UnitKind::Fighter;
    UnitState state = UnitState::Free;
    Shape shape = Shape::Circle;
    bool alive = true, bot = false, hasPickupWeapon = false;
    Vec2 position{}, aim{1, 0};
    uint32_t tailOrder = 0;
    double hp = 300, maxHp = 300, armor = 0, radius = 25, cooldown = 0, reload = 0;
    double burnTime = 0, burnDamage = 0, slowTime = 0, skillTime = 0, emergencyTime = 0;
    int weapon = 0, ammo = 10, kills = 0, pendingUpgrades = 0, level = 1;
    std::array<int, 20> skills{};
    std::array<int, 3> choices{{0, 4, 8}};
};
struct SavedCrate
{
    int id = 0, weapon = 1;
    Vec2 position{};
    double dropTime = 0, remaining = 30;
};
struct RunSnapshot
{
    uint64_t seed = 1;
    double elapsed = 0, sharedXP = 0;
    int sharedLevel = 1, bossWave = 0;
    uint32_t mazeVersion = 0;
    std::vector<double> walls;
    std::vector<std::pair<int, Maze::WallPatch>> fragments;
    std::vector<SavedUnit> units;
    std::vector<SavedCrate> crates;
    // Identities of the human characters in this run, in save order.
    std::vector<int> owners() const
    {
        std::vector<int> result;
        for (const auto& u : units)
            if (u.owner >= 0)
                result.push_back(u.owner);
        return result;
    }
};
} // namespace gwgame
