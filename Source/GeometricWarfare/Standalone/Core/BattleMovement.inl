#pragma once
namespace gwgame
{
// Monsters hunt the nearest teammate (player, AI teammate or companion) along the maze path.
inline void Battle::updateAI(Unit& u, double dt)
{
    u.input.fire = false;
    if (!config.aiEnabled)
        return;
    if (u.castTime > 0)
    {
        u.velocity = {};
        return;
    }
    Vec2 goal = u.position;
    double nearest = std::numeric_limits<double>::infinity();
    // A teammate lying as bait pulls every monster that can see it; otherwise the nearest teammate.
    const Unit* lure = baitFor(u, config.baitRadius * 1.5);
    chase(u, lure);
    for (auto& v : units)
        if (v.alive && v.team == 1 && (!lure || v.id == lure->id))
        {
            double d = (v.position - u.position).lengthSquared();
            if (d < nearest)
            {
                nearest = d;
                goal = v.position;
            }
        }
    const bool sees = nearest < 900 * 900 && maze.clear(u.position, goal, 4);
    if (sees)
        u.aim = (goal - u.position).normalized();
    u.think -= dt;
    if (u.think <= 0)
    {
        u.path = maze.path(u.position, goal, u.radius + 2);
        u.waypoint = 0;
        u.think = .65 + (u.id % 17) * .035;
    }
    while (u.waypoint < u.path.size() && (u.path[u.waypoint] - u.position).length() < 18)
        ++u.waypoint;
    if (u.waypoint < u.path.size())
    {
        Vec2 d = (u.path[u.waypoint] - u.position).normalized();
        u.velocity = d * (150 + std::min(90., elapsed * .12));
        if (!sees)
            u.aim = d;
    }
    else
        u.velocity = {};
}
inline void Battle::moveUnits(double dt)
{
    for (auto& u : units)
        if (u.alive)
        {
            if (u.human)
                consumeInput(u);
            if (u.state == UnitState::Launched)
            {
                Vec2 from = u.position, proposed = u.position + (u.velocity + u.impulse) * dt;
                bool wall = !maze.clear(from, proposed, u.radius);
                u.position = maze.move(from, (u.velocity + u.impulse) * dt, u.radius);
                u.impulse = u.impulse * std::exp(-5 * dt);
                bool hit = false;
                // Throw power (research) is the thrower's, and their shards carry it too.
                const Unit* thrower = find(u.launchOwner);
                const double damage = config.launchDamage * (.5 + u.launchPower * 1.5) *
                                      std::clamp(u.velocity.length() / u.launchInitialSpeed, .35, 1.) * u.impactScale *
                                      (thrower ? perk(*thrower, Attribute::Throw) : 1);
                for (auto& target : units)
                    if (target.alive && target.team != u.team && target.id != u.id &&
                        distanceToSegment(target.position, from, u.position) < target.radius + u.radius)
                    {
                        damageUnit(u.launchOwner, target.id, damage);
                        target.impulse += u.velocity * .35;
                        hit = true;
                    }
                // A polygon that strikes an enemy bursts into triangles; one that lands on paper stays whole.
                if (hit && u.kind == UnitKind::Companion && u.sides > MinSides)
                {
                    shatter(u, u.position);
                    continue;
                }
                u.launchTime -= dt;
                u.velocity = u.velocity * std::exp(-1.6 * dt);
                if (wall || hit || u.launchTime <= 0)
                {
                    u.state = u.human ? UnitState::Free : UnitState::Idle;
                    landed(u);
                    u.launchOwner = -1;
                    u.velocity = {};
                    u.impactScale = 1;
                    // A thrown teammate lands as bait: pinned to the paper, pulling the enemies around them.
                    if (u.human)
                    {
                        u.bait = config.baitSeconds;
                        u.impulse = {};
                        u.trail.clear();
                        baits.push_back(u.id);
                        event(EffectStyle::Bait, u.position, u.position, u.team, config.baitRadius, .7);
                    }
                }
                continue;
            }
            if (u.state == UnitState::Following && !u.human)
                continue;
            if (u.human)
            {
                // A carried teammate rides in the holder's tail (placed below); bait cannot walk off.
                if (u.carried)
                {
                    u.velocity = u.impulse = {};
                    continue;
                }
                u.velocity = u.bait > 0 ? Vec2{} : u.input.move * (config.playerSpeed * perk(u, Attribute::Speed));
                if (u.trail.empty() || (u.trail.back() - u.position).length() >= 7)
                {
                    u.trail.push_back(u.position);
                    if (u.trail.size() > 4096)
                        u.trail.erase(u.trail.begin(), u.trail.begin() + 512);
                }
            }
            else if (u.bot)
                updateBot(u, dt);
            else if (u.kind == UnitKind::Companion)
            {
                u.velocity = {};
                u.state = UnitState::Idle;
                if (defenseMode() && !u.turret && u.sides > MinSides && u.impulse.lengthSquared() < 400)
                    plantTurret(u);
                if (u.turret)
                {
                    u.impulse = {};
                    continue;
                }
            }
            else if (u.dummy)
            {
                // A training target stays planted on its spot, whatever hits it.
                u.velocity = u.impulse = {};
                continue;
            }
            else
            {
                u.state = UnitState::Free;
                if (defenseMode())
                    updateSiege(u, dt);
                else
                    updateAI(u, dt);
            }
            double speedFactor = u.slowTime > 0 ? .5 : 1;
            u.position = maze.move(u.position, (u.velocity * speedFactor + u.impulse) * dt, u.radius);
            u.impulse = u.impulse * std::exp(-5 * dt);
        }
    for (auto& p : units)
        if (p.alive && p.human)
        {
            int rank = 0;
            Vec2 previous = p.position;
            // A carried teammate rides right behind the holder, then the allies in recruit order. Players in a
            // voluntary squad walk on their own and are not placed.
            std::vector<int> riders, members = tailIds(p.id);
            for (int member : members)
                if (find(member)->carried)
                    riders.push_back(member);
            for (int member : members)
                if (!find(member)->human)
                    riders.push_back(member);
            for (int followerId : riders)
            {
                auto& u = *find(followerId);
                ++rank;
                double remaining = config.companionSpacing * rank;
                Vec2 target = previous, head = p.position;
                for (auto it = p.trail.rbegin(); it != p.trail.rend(); ++it)
                {
                    double d = (*it - head).length();
                    if (d >= remaining)
                    {
                        target = head + (*it - head) * (remaining / std::max(d, 1e-9));
                        remaining = 0;
                        break;
                    }
                    remaining -= d;
                    head = *it;
                    target = head;
                }
                // Extend short histories backwards. Without this, a newly recruited
                // ally targets the head until enough path exists and fights contact resolution.
                if (remaining > 0)
                {
                    Vec2 tangent = p.input.move.lengthSquared() > .01 ? p.input.move.normalized() : p.aim;
                    if (p.trail.size() > 1)
                        tangent = (p.trail[1] - p.trail[0]).normalized();
                    target = head - tangent * remaining;
                }
                Vec2 destination = target;
                if (!maze.clear(u.position, target, u.radius))
                {
                    if (u.think <= 0 || u.path.empty())
                    {
                        u.path = maze.path(u.position, target, u.radius + 2);
                        u.waypoint = 0;
                        u.think = .3;
                    }
                    u.think -= dt;
                    while (u.waypoint < u.path.size() && (u.path[u.waypoint] - u.position).length() < 20)
                        ++u.waypoint;
                    if (u.waypoint < u.path.size())
                        destination = u.path[u.waypoint];
                    else
                        destination = u.position;
                }
                Vec2 delta = destination - u.position;
                double travel = std::min(delta.length(), config.playerSpeed * 1.7 * dt);
                u.position = maze.move(u.position, delta.normalized() * travel + u.impulse * dt, u.radius);
                u.impulse = u.impulse * std::exp(-5 * dt);
                if (u.human)
                {
                    // A carried player keeps their own aim and lays a trail for their own allies to follow.
                    if (u.trail.empty() || (u.trail.back() - u.position).length() >= 7)
                    {
                        u.trail.push_back(u.position);
                        if (u.trail.size() > 4096)
                            u.trail.erase(u.trail.begin(), u.trail.begin() + 512);
                    }
                }
                else
                    u.aim = p.aim;
                previous = u.position;
            }
        }
    // Contact impulses use a spatial hash; distance is conservative for every shape.
    std::unordered_map<int64_t, std::vector<size_t>> grid;
    auto key = [](int x, int y)
    {
        return static_cast<int64_t>((static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) | static_cast<uint32_t>(y));
    };
    for (size_t i = 0; i < units.size(); ++i)
        if (units[i].alive)
        {
            auto& a = units[i];
            int x = static_cast<int>(a.position.x / 100), y = static_cast<int>(a.position.y / 100);
            for (int yy = y - 1; yy <= y + 1; ++yy)
                for (int xx = x - 1; xx <= x + 1; ++xx)
                {
                    auto it = grid.find(key(xx, yy));
                    if (it == grid.end())
                        continue;
                    for (size_t j : it->second)
                    {
                        auto& b = units[j];
                        int chainA = a.state == UnitState::Launched    ? a.launchOwner
                                     : a.state == UnitState::Following ? a.followerOf
                                     : a.human                         ? a.id
                                                                       : -1;
                        int chainB = b.state == UnitState::Launched    ? b.launchOwner
                                     : b.state == UnitState::Following ? b.followerOf
                                     : b.human                         ? b.id
                                                                       : -1;
                        // A snake chain follows one collision-safe history. Self contact
                        // must never move its predicted player or feed back into its trail.
                        if (chainA >= 0 && chainA == chainB)
                            continue;
                        Vec2 d = a.position - b.position;
                        double min = a.radius + b.radius, len = d.length();
                        const bool fixedA = a.turret || a.dummy, fixedB = b.turret || b.dummy;
                        if (len >= min || (fixedA && fixedB))
                            continue;
                        Vec2 n = len > 1e-6 ? d * (1 / len) : Vec2{1, 0};
                        // A pinned turret (or a training target) never budges; whoever bumps into it takes the
                        // whole correction.
                        Vec2 correction = n * ((min - len) * (fixedA || fixedB ? 1. : .5));
                        if (!fixedA)
                            a.position = maze.move(a.position, correction, a.radius);
                        if (!fixedB)
                            b.position = maze.move(b.position, correction * (-1), b.radius);
                    }
                }
            grid[key(x, y)].push_back(i);
        }
    // The core is solid to the team; enemies are meant to reach it (that is how they hurt it).
    if (defenseMode())
        for (auto& u : units)
            if (u.alive && u.team == 1 && u.state != UnitState::Launched)
            {
                const Vec2 d = u.position - defense.core;
                const double reach = DefenseState::CoreRadius + u.radius, len = d.length();
                if (len < reach)
                    u.position = maze.move(u.position, (len > 1e-6 ? d * (1 / len) : Vec2{1, 0}) * (reach - len), u.radius);
            }
}
} // namespace gwgame
