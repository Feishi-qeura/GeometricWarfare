#pragma once
namespace gwgame
{
// Triangles are the currency of every mode: they spawn on the field, drop from enemies and fall out of
// shattered polygons. Merging adds a side; throwing a polygon into an enemy breaks it back into triangles.
constexpr double CompanionHealth = 220;
inline void Battle::queueUnit(Unit u)
{
    if (units.size() + pendingUnits.size() < 500)
        pendingUnits.push_back(std::move(u));
}
// Units are added only between update passes, so no loop over units ever sees the vector change.
inline void Battle::flushUnits()
{
    for (auto& fresh : pendingUnits)
    {
        Unit* slot = nullptr;
        for (auto& u : units)
            if (!u.alive && !u.human && !u.bot && !u.dummy && u.kind == fresh.kind && u.kind != UnitKind::Fighter &&
                u.followerOf < 0)
            {
                slot = &u;
                break;
            }
        if (slot)
            *slot = std::move(fresh);
        else if (units.size() < 500)
            units.push_back(std::move(fresh));
    }
    pendingUnits.clear();
}
inline int Battle::merge(int id)
{
    auto* p = find(id);
    if (upgradePaused() || finished || !p || !p->alive || !p->human)
        return -1;
    std::vector<int> tail;
    for (int member : tailIds(id))
        if (!find(member)->human)
            tail.push_back(member);
    if (tail.size() < 2)
        return -1;
    Unit* last = find(tail.back());
    Unit* front = find(tail[tail.size() - 2]);
    if (!last || !front || last->sides != MinSides || front->sides >= MaxSides)
        return -1;
    const double ratio = front->maxHp > 0 ? front->hp / front->maxHp : 1;
    ++front->sides;
    front->shape = polygonShape(front->sides);
    front->maxHp = CompanionHealth * sideHealth(front->sides);
    front->hp = std::min(front->maxHp, front->maxHp * ratio + CompanionHealth * .5);
    last->alive = false;
    last->hp = 0;
    last->followerOf = -1;
    last->tailOrder = 0;
    last->state = UnitState::Idle;
    last->path.clear();
    event(EffectStyle::Merge, front->position, last->position, p->team, front->sides, .5);
    return front->id;
}
// One triangle per side bursts outward, each carrying 40% of the throw's damage; shards can be recruited a
// second after they land. A triangle is the smallest piece and never bursts (see Polygons in the design doc).
inline void Battle::shatter(Unit& thrown, Vec2 at)
{
    const int pieces = thrown.sides;
    const Vec2 heading = thrown.velocity.lengthSquared() > 1 ? thrown.velocity.normalized() : thrown.aim.normalized();
    const double base = std::atan2(heading.y, heading.x);
    thrown.alive = false;
    thrown.hp = 0;
    thrown.state = UnitState::Idle;
    thrown.velocity = {};
    event(EffectStyle::Shatter, at, at + heading * 80, thrown.team, pieces, .5);
    int idle = 0;
    for (const auto& u : units)
        idle += u.alive && u.kind == UnitKind::Companion && u.followerOf < 0 && !u.turret;
    for (int i = 0; i < pieces && idle + i < config.maxIdleCompanions; ++i)
    {
        const double angle = base + (i + .5) * 2 * Pi / pieces - Pi;
        const Vec2 dir{std::cos(angle), std::sin(angle)};
        Unit shard;
        shard.id = nextId++;
        shard.kind = UnitKind::Companion;
        shard.team = thrown.team;
        shard.sides = MinSides;
        shard.shape = Shape::Triangle;
        shard.maxHp = shard.hp = CompanionHealth;
        shard.weapon = thrown.weapon;
        shard.hasPickupWeapon = thrown.hasPickupWeapon;
        shard.ammo = weaponStats(shard.weapon).ammo;
        shard.position = maze.move(at, dir * (thrown.radius * .7), shard.radius);
        shard.state = UnitState::Launched;
        shard.launchOwner = thrown.launchOwner;
        shard.launchPower = thrown.launchPower;
        shard.launchInitialSpeed = 760;
        shard.velocity = dir * 760;
        shard.launchTime = .32;
        shard.impactScale = .4 * thrown.impactScale;
        shard.shardDelay = 1.32;
        shard.aim = dir;
        queueUnit(shard);
    }
}
} // namespace gwgame
