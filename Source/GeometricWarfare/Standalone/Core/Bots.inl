#pragma once
namespace gwgame
{
inline int Battle::addBot()
{
    if (finished || units.size() >= 500)
        return -1;
    int index = 0;
    for (const auto& u : units)
        index += u.bot;
    Unit u;
    u.id = nextId++;
    u.team = 1;
    u.bot = true;
    u.position = defenseMode() ? coreSpawn(index + 4) : spawnPosition(index + 6);
    u.seat = freeSeat();
    defense.botsUsed |= defenseMode();
    u.level = sharedLevel;
    u.pendingUpgrades = sharedLevel - 1;
    u.think = index * .1;
    makeChoices(u);
    units.push_back(u);
    botChoose(units.back());
    return u.id;
}
// Rank up what the bot already owns first, then open core skills, so each bot grows a coherent build.
inline void Battle::botChoose(Unit& u)
{
    const int id = u.id;
    for (int guard = 0; guard < 64; ++guard)
    {
        Unit* bot = find(id);
        if (!bot || bot->pendingUpgrades <= 0)
            return;
        int pick = 0;
        double best = -1;
        for (int i = 0; i < 3; ++i)
        {
            const int skill = bot->choices[i];
            const double score = skill < 0 ? 0 : 1 + bot->skills[skill] * 2 + (skillPrerequisite(skill) < 0 ? 1 : 0) +
                                                     ((static_cast<uint64_t>(id) * 7 + skill * 13 + bot->level) % 5) * .1;
            if (score > best)
            {
                best = score;
                pick = i;
            }
        }
        if (!chooseUpgrade(id, pick))
            return;
    }
}
inline bool Battle::lineOfFireBlocked(const Unit& shooter, Vec2 target) const
{
    const double reach = (target - shooter.position).length();
    for (const auto& u : units)
        if (u.alive && u.team == shooter.team && u.id != shooter.id && u.state != UnitState::Launched &&
            (u.position - shooter.position).length() < reach &&
            distanceToSegment(u.position, shooter.position, target) < u.radius + 6)
            return true;
    return false;
}
// AI teammates hold a loose triangle around the nearest human, shoot the closest visible monster
// (never through a friend), back off when one gets too close and detour for weapon crates.
inline void Battle::updateBot(Unit& u, double dt)
{
    u.input.fire = false;
    u.state = UnitState::Free;
    if (!config.aiEnabled)
    {
        u.velocity = {};
        return;
    }
    int index = 0;
    for (const auto& other : units)
        index += other.bot && other.id < u.id;
    const Unit* leader = nullptr;
    const Unit* enemy = nullptr;
    double leaderDistance = std::numeric_limits<double>::infinity(), enemyDistance = leaderDistance;
    for (const auto& v : units)
    {
        if (!v.alive)
            continue;
        const double d = (v.position - u.position).lengthSquared();
        if (v.human && d < leaderDistance)
        {
            leaderDistance = d;
            leader = &v;
        }
        else if (v.kind == UnitKind::Monster && d < enemyDistance)
        {
            enemyDistance = d;
            enemy = &v;
        }
    }
    if (enemy && enemyDistance < 850 * 850 && maze.clear(u.position, enemy->position, 4))
    {
        u.aim = (enemy->position - u.position).normalized();
        u.input.fire = !lineOfFireBlocked(u, enemy->position);
    }
    Vec2 goal = u.position;
    if (leader)
    {
        const double angle = Pi * .5 + index * 2 * Pi / 3;
        const Vec2 slot = leader->position + Vec2{std::cos(angle), std::sin(angle)} * 170;
        goal = maze.walkable(slot, u.radius + 4) ? slot : leader->position;
    }
    if (!u.hasPickupWeapon)
        for (const auto& c : crates)
            if (c.active && c.dropTime <= 0 && (c.position - u.position).length() < 700 &&
                (!leader || (c.position - leader->position).length() < 900))
            {
                goal = c.position;
                break;
            }
    if (enemy && enemyDistance < 230 * 230)
        goal = u.position + (u.position - enemy->position).normalized() * 220;
    const double distance = (goal - u.position).length();
    Vec2 direction{};
    if (distance > 30)
    {
        if (maze.clear(u.position, goal, u.radius))
        {
            direction = (goal - u.position) * (1 / distance);
            u.path.clear();
        }
        else
        {
            u.think -= dt;
            if (u.think <= 0 || u.path.empty())
            {
                u.path = maze.path(u.position, goal, u.radius + 2);
                u.waypoint = 0;
                u.think = .5 + index * .05;
            }
            while (u.waypoint < u.path.size() && (u.path[u.waypoint] - u.position).length() < 18)
                ++u.waypoint;
            if (u.waypoint < u.path.size())
                direction = (u.path[u.waypoint] - u.position).normalized();
        }
    }
    u.velocity = direction * std::min(config.playerSpeed, distance * 4);
    if (!u.input.fire && direction.lengthSquared() > .5)
        u.aim = direction;
}
} // namespace gwgame
