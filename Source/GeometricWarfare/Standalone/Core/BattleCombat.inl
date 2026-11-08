#pragma once
namespace gwgame
{
inline void Battle::fire(Unit& u)
{
    auto stats = weaponStats(u.weapon);
    u.cooldown = stats.interval;
    --u.ammo;
    Vec2 direction = u.aim.normalized();
    double angle = std::atan2(direction.y, direction.x);
    for (int pellet = 0; pellet < stats.pellets && projectiles.size() < 768; ++pellet)
    {
        double spread = stats.pellets > 1 ? (pellet - (stats.pellets - 1) * .5) * .075 : 0;
        Vec2 d{std::cos(angle + spread), std::sin(angle + spread)};
        Projectile p;
        p.id = nextProjectile++;
        p.owner = u.id;
        p.squad = squadHead(u);
        p.team = u.team;
        p.weapon = u.weapon;
        p.position = u.position + d * (u.radius + 9);
        p.velocity = d * stats.speed;
        p.damage = stats.damage * (1 + u.skills[2] * .15 + u.skills[8] * .1) *
                   (u.kind == UnitKind::Companion ? sideDamage(u.sides) : perk(u, Attribute::Damage));
        p.remaining = stats.range / stats.speed;
        p.radius = u.weapon == 5 ? 10 : 4;
        p.pierce = u.skills[0] + (u.weapon == 3 ? 1 : 0);
        projectiles.push_back(p);
    }
}
inline void Battle::updateWeapons(double dt)
{
    for (auto& u : units)
        if (u.alive)
        {
            u.cooldown = std::max(0., u.cooldown - dt);
            u.reload = std::max(0., u.reload - dt);
            if (u.ammo <= 0 && u.reload <= 0)
            {
                u.ammo = weaponStats(u.weapon).ammo;
                u.reload = weaponStats(u.weapon).reload;
            }
            if (u.kind == UnitKind::Monster)
            {
                // Training targets only stand there and take it.
                if (u.dummy)
                    continue;
                if (u.radius >= 100)
                {
                    // Defense bosses also sweep a laser toward the core that cuts a channel through the maze.
                    if (defenseMode() && u.castTime <= 0)
                    {
                        if (u.laserTime > 0)
                        {
                            u.laserTime -= dt;
                            if (u.laserTime <= 0)
                                fireLaser(u);
                            continue;
                        }
                        if ((u.laserCooldown -= dt) <= 0)
                        {
                            u.laserDir = (defense.core - u.position).normalized();
                            u.laserTime = 1.6;
                            u.laserCooldown = 10;
                            Effect warning{u.position, u.position + u.laserDir * 2600, 80, 1.6, 5, 2};
                            warning.style = EffectStyle::LaserTelegraph;
                            addEffect(warning);
                            continue;
                        }
                    }
                    if (u.castTime > 0)
                    {
                        u.castTime -= dt;
                        if (u.castTime <= 0)
                        {
                            const double slam = defenseMode() ? 90 * (1 + .06 * std::max(0, defense.wave - 1)) * enemyPower()
                                                              : 100 + elapsed * .08;
                            blast(u.id, 2, u.castTarget, 320, slam, .45, EffectStyle::BossSlam);
                            if (defenseMode())
                                maze.damageDisc(u.castTarget, 320, 70);
                        }
                    }
                    else if (u.cooldown <= 0)
                    {
                        // The boss slams a teammate lying as bait first, then the nearest player.
                        const Unit* target = baitFor(u, 900);
                        const bool lured = target != nullptr;
                        double nearest = 1e20;
                        for (auto& other : units)
                            if (!lured && other.player() && other.alive &&
                                (other.position - u.position).lengthSquared() < nearest)
                            {
                                target = &other;
                                nearest = (other.position - u.position).lengthSquared();
                            }
                        if (target)
                        {
                            u.castTarget = target->position;
                            u.castTime = 1.5;
                            u.cooldown = 6;
                            Effect warning{u.castTarget, u.castTarget, 320, 1.5, 5, 2};
                            warning.style = EffectStyle::Telegraph;
                            addEffect(warning);
                        }
                    }
                }
                else if (defenseMode() && u.shape == Shape::Triangle && u.target != -1)
                {
                    if (u.cooldown <= 0)
                    {
                        fireBolt(u, u.castTarget, 12 * (u.elite ? 2 : 1) * (1 + .06 * std::max(0, defense.wave - 1)) * enemyPower(), 9,
                                 950, 820);
                        u.cooldown = 1.4;
                    }
                }
                else if (defenseMode() && u.shape == Shape::Rectangle && u.target >= 0 && maze.walls[u.target] > 0)
                {
                    // A ram leaning on its wall bites a chunk out of it every swing.
                    const Vec2 lo{(u.target % maze.width) * maze.cellSize, (u.target / maze.width) * maze.cellSize};
                    const Vec2 hi = lo + Vec2{maze.cellSize, maze.cellSize};
                    const Vec2 contact{std::clamp(u.position.x, lo.x, hi.x), std::clamp(u.position.y, lo.y, hi.y)};
                    if ((contact - u.position).length() < u.radius + 16 && u.cooldown <= 0)
                    {
                        maze.damageDisc(contact, 150, 80 * (u.elite ? 2 : 1));
                        event(EffectStyle::Shove, contact, contact + (contact - u.position).normalized() * 60, 2, 160, .3);
                        u.cooldown = .9;
                    }
                }
                if (u.cooldown <= 0)
                {
                    for (auto& target : units)
                        if (target.alive && target.team != u.team &&
                            (target.position - u.position).length() < u.radius + target.radius + 12)
                        {
                            damageUnit(u.id, target.id, meleeDamage(u));
                            u.cooldown = .8;
                            break;
                        }
                }
                continue;
            }
            if (u.turret)
            {
                if (u.cooldown <= 0)
                    fireTurret(u);
                continue;
            }
            if (u.state == UnitState::Following && !u.human)
            {
                const auto* head = find(u.followerOf);
                u.input.fire = head && head->alive && head->input.fire;
                if (head)
                    u.aim = head->aim;
            }
            if (u.state == UnitState::Launched)
                continue;
            if (u.input.fire && u.cooldown <= 0 && u.reload <= 0 && u.ammo > 0)
                fire(u);
        }
}
inline void Battle::blast(int owner, int team, Vec2 position, double radius, double damage, double lifetime,
                          EffectStyle style)
{
    Effect ring{position, position, radius, lifetime, 1, team};
    ring.style = style;
    addEffect(ring);
    for (auto& u : units)
        if (u.alive && u.team != team && (u.position - position).length() < radius + u.radius)
        {
            damageUnit(owner, u.id, damage);
            u.impulse += (u.position - position).normalized() * 380;
        }
}
inline void Battle::shove(const Projectile& p, Unit& target, Vec2 at)
{
    // Momentum transfer: the heavier the shot the harder the shove; bigger bodies move less. The room's
    // trolling level makes it weak (Friendly) or strong (Chaos).
    Vec2 direction = p.velocity.normalized();
    const double strength = prankPush(config.prank);
    double amount = friendlyPush(p.damage) * 25 / std::max(10., target.radius) * strength;
    target.impulse += direction * amount;
    const double cap = MaxFriendlyImpulse * std::max(1., strength);
    if (double length = target.impulse.length(); length > cap)
        target.impulse = target.impulse * (cap / length);
    if (target.player())
        if (Unit* shooter = find(p.squad); shooter && shooter->player())
            ++shooter->stats.pushes;
    event(EffectStyle::Shove, at, at + direction * 60, target.team, amount, .3);
}
inline void Battle::updateProjectiles(double dt)
{
    for (auto& p : projectiles)
    {
        if (p.remaining <= 0)
            continue;
        Vec2 from = p.position, to = from + p.velocity * dt;
        p.remaining -= dt;
        auto* source = find(p.owner);
        bool stopped = false;
        double fraction = 1;
        Vec2 normal{};
        int wallCell = -1;
        if (maze.raycast(from, to, p.radius, fraction, normal, &wallCell))
        {
            to = from + (to - from) * fraction;
            stopped = true;
            // A highlighter's glossy wall sends any shot (either side's) back off it, twice at most.
            const int ricochets = std::max(source ? source->skills[1] : 0, maze.kind(wallCell) == WallKind::Bounce ? BounceRicochets : 0);
            if (p.weapon == 5)
            {
                // Players never blast their own drawn maze apart in Defense.
                if (!(defenseMode() && p.team == 1) && !maze.damageWalls(to, to, 110, 45).empty())
                    addEffect({to, to, 140, .8, 6, 0});
                blast(p.owner, p.team, to, 165, p.damage, .35, EffectStyle::Rocket);
            }
            else if (ricochets > p.bounces)
            {
                p.velocity = p.velocity - normal * (2 * p.velocity.dot(normal));
                to += p.velocity.normalized() * 2;
                ++p.bounces;
                stopped = false;
            }
        }
        if (defenseMode() && p.team == 2 && distanceToSegment(defense.core, from, to) < DefenseState::CoreRadius + p.radius)
        {
            hurtCore(p.damage, to);
            p.position = to;
            p.remaining = 0;
            continue;
        }
        for (auto& target : units)
        {
            if (!target.alive || distanceToSegment(target.position, from, to) >= target.radius + p.radius ||
                std::find(p.hit.begin(), p.hit.end(), target.id) != p.hit.end())
                continue;
            if (target.team == p.team)
            {
                // Friendly fire never hurts: it passes through and pushes teammates outside the shooter's squad.
                // Only the players' own guns shove; turret bolts and enemy shots just pass their friends.
                if (p.team == 1 && p.weapon != 6 && target.id != p.owner && target.state != UnitState::Launched &&
                    !target.turret && squadHead(target) != p.squad && config.prank != PrankLevel::Off)
                {
                    p.hit.push_back(target.id);
                    Vec2 delta = to - from;
                    double length = delta.lengthSquared();
                    double along = length > 1e-9 ? std::clamp((target.position - from).dot(delta) / length, 0., 1.) : 0;
                    shove(p, target, from + delta * along);
                }
                continue;
            }
            p.hit.push_back(target.id);
            if (p.weapon == 5)
                blast(p.owner, p.team, target.position, 165, p.damage, .35, EffectStyle::Rocket);
            else
                damageUnit(p.owner, target.id, p.damage);
            if (source)
            {
                if (source->skills[3] > 0)
                    blast(source->id, source->team, target.position, 120 + source->skills[3] * 20,
                          p.damage * .25 * source->skills[3], .35, EffectStyle::Fragment);
                if (source->skills[8] > 0)
                {
                    target.burnTime = 2 + source->skills[9];
                    target.burnDamage = 2 + source->skills[8] * 2;
                    target.burnOwner = source->id;
                    if (source->skills[11] > 0)
                        damageUnit(source->id, target.id, source->skills[11] * 4);
                }
                if (source->skills[12] > 0)
                    target.slowTime = 1 + source->skills[13] * .4;
                if (!target.alive && source->skills[14] > 0)
                    blast(source->id, source->team, target.position, 180, p.damage * .2 * source->skills[14], .35,
                          EffectStyle::KillBlast);
                if (source->skills[15] > 0)
                {
                    for (auto& other : units)
                        if (other.alive && other.team != source->team &&
                            (other.position - target.position).length() < 120 + source->skills[15] * 30)
                            other.slowTime = std::max(other.slowTime, 2.);
                    Effect nova{target.position, target.position, 120 + source->skills[15] * 30., .5, 2, source->team};
                    nova.style = EffectStyle::FrostNova;
                    addEffect(nova);
                }
                if (source->skills[4] > 0)
                {
                    int hits = 0;
                    for (auto& other : units)
                        if (other.alive && other.id != target.id && other.team != source->team &&
                            (other.position - target.position).length() < 220 + source->skills[5] * 60)
                        {
                            damageUnit(source->id, other.id, p.damage * .3);
                            Effect arc{target.position, other.position, 0, .14, 2, p.team};
                            arc.style = EffectStyle::Chain;
                            addEffect(arc);
                            if (++hits >= 1 + source->skills[7])
                                break;
                        }
                }
            }
            if (p.pierce-- <= 0)
            {
                stopped = true;
                break;
            }
        }
        p.position = to;
        if (stopped)
            p.remaining = 0;
    }
}
} // namespace gwgame
