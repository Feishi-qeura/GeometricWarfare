#pragma once
namespace gwgame
{
// The bestiary's rules (Foes.h; design v2.1 chapter 7): how each enemy, elite and boss moves and fights, what the floor's
// hazards and the biomes' terrain do, and the hooks the hit, shot and death code calls on the expedition.
namespace detail
{
inline Vec2 turned(Vec2 v, double radians)
{
    const double c = std::cos(radians), s = std::sin(radians);
    return {v.x * c - v.y * s, v.x * s + v.y * c};
}
// A zone's footprint: a disc when its ends meet, otherwise a band.
inline bool inZone(const ExpeditionState::Zone& z, Vec2 p, double pad)
{
    if ((z.to - z.from).lengthSquared() < 1)
        return (p - z.from).length() < z.radius + pad;
    return distanceToSegment(p, z.from, z.to) < z.radius + pad;
}
// Warning colours (FoeCue's pulse): danger red, ink, water, spark, cinnabar, graphite, magenta.
constexpr int CueDanger = 0, CueInk = 1, CueWater = 2, CueSpark = 3, CueSeal = 4, CueGraphite = 5, CueMagenta = 6;
} // namespace detail

// ---- Spawning ----
inline int Battle::spawnFoe(Foe foe, int room, Vec2 at)
{
    const auto& x = expedition;
    const int kind = static_cast<int>(foe);
    const FoeInfo& info = foeInfo(kind);
    const bool boss = info.rank == FoeRank::Boss;
    const CrowdScale crowd = crowdScale(std::max(1, livingParty()));
    Unit m;
    m.id = nextId++;
    m.team = 2;
    m.kind = UnitKind::Monster;
    m.breed = kind;
    m.room = room;
    m.shape = info.shape;
    m.elite = info.rank == FoeRank::Elite;
    m.radius = info.radius;
    m.parts = info.parts;
    m.hp = m.maxHp = info.health * stageHealth(x.stage, x.floor) * tierInfo(config.tier).enemyHealth * (boss ? crowd.boss : crowd.health);
    m.position = at;
    m.home = at;
    // A spawn draws itself in first (the cue's ring) before it moves or bites.
    m.stun = boss ? 1.5 : .7;
    m.think = .05 * (m.id % 12);
    m.cooldown = .8 + random01(rng) * 1.2;
    m.skillTime = boss ? 2.5 : .6 + random01(rng) * 1.4;
    if (foe == Foe::Eel)
        m.acts = 5;
    if (foe == Foe::Thief)
        m.actClock = ThiefSeconds;
    if (foe == Foe::Shield || foe == Foe::GuardDun || foe == Foe::Mirror)
        m.act = static_cast<int>(foe == Foe::Mirror ? FoeAct::Guard : FoeAct::Idle);
    if (foe == Foe::Pinwheel)
        m.act = static_cast<int>(FoeAct::Spin);
    double nearest = 1e18;
    for (const auto& u : units)
        if (u.alive && u.team == 1 && (u.position - at).lengthSquared() < nearest)
        {
            nearest = (u.position - at).lengthSquared();
            m.aim = (u.position - at).normalized();
        }
    m.actAt = m.aim;
    queueUnit(m);
    event(EffectStyle::SpawnEnemy, at, at, 2, m.radius, .5);
    return m.id;
}
// One pick for a wave: the stage's biome, the lighter kinds more often; an elite is one of the biome's two, or now and
// then the sticker thief or the mash-up from anywhere. 1-1 teaches with the first enemy and the darter alone.
inline Foe Battle::pickFoe(bool elite)
{
    const auto& x = expedition;
    const PenBiome b = x.biome();
    if (elite)
    {
        const double pick = random01(rng);
        if (pick < .08)
            return Foe::Thief;
        if (pick < .16)
            return Foe::Mashup;
        return biomeElite(b, random01(rng) < .5 ? 0 : 1);
    }
    if (x.stage == 1 && x.floor == 1)
        return biomeFoe(b, random01(rng) < .6 ? 0 : 2);
    static const double Weights[BiomeCount][5] = {{3, 2.5, 3, 2, 1.5}, {3, 2.5, 2.5, 1.5, 2}, {3, 2.5, 2, 2, 2}, {3, 1.5, 2.5, 2, 2.5}, {3, 2.5, 2, 2, 2}};
    const double* w = Weights[std::clamp(static_cast<int>(b), 0, BiomeCount - 1)];
    double total = 0;
    for (int i = 0; i < 5; ++i)
        total += w[i];
    double pick = random01(rng) * total;
    for (int i = 0; i < 5; ++i)
        if ((pick -= w[i]) < 0)
            return biomeFoe(b, i);
    return biomeFoe(b, 0);
}

// ---- Shots, warnings, hazards ----
inline void Battle::foeCue(Vec2 from, Vec2 to, double radius, double seconds, int colour)
{
    Effect e{from, to, radius, seconds, EventEffect, 2};
    e.style = EffectStyle::FoeCue;
    e.pulse = colour;
    addEffect(e);
}
inline void Battle::foeShot(int owner, Vec2 from, Vec2 dir, double damage, int kind, double speed, double range, int effect, int pierce)
{
    if (projectiles.size() >= 768)
        return;
    dir = dir.normalized();
    if (dir.lengthSquared() < .5)
        dir = {1, 0};
    Projectile p;
    p.id = nextProjectile++;
    p.owner = p.squad = owner;
    p.team = 2;
    p.weapon = kind;
    p.position = from;
    p.velocity = dir * speed;
    p.damage = damage;
    p.remaining = range / speed;
    p.effect = effect;
    p.pierce = pierce;
    p.radius = kind == HeavyShot ? 11 : kind == BlotShot ? 9 : kind == DropShot ? 8 : kind == DartShot ? 7 : 5;
    // A shot fired off carbon paper comes again half a second later (the ballpoint's terrain).
    for (const auto& z : expedition.zones)
        if (z.kind == static_cast<int>(ZoneKind::Carbon) && detail::inZone(z, from, 0) && expedition.echoes.size() < 64)
        {
            expedition.echoes.push_back({.5, p});
            break;
        }
    projectiles.push_back(p);
}
inline void Battle::foeFan(Unit& u, Vec2 at, int count, double spread, double damage, int kind, double speed, double range, int effect)
{
    Vec2 dir = (at - u.position).normalized();
    if (dir.lengthSquared() < .5)
        dir = u.aim.normalized();
    // Graphite fog round the target throws the aim off.
    for (const auto& v : units)
        if (v.player() && v.alive && (v.position - at).length() < 60 && v.kit.fogged)
            dir = detail::turned(dir, (random01(rng) - .5) * .7);
    for (int k = 0; k < count; ++k)
    {
        const double a = count > 1 ? (k / static_cast<double>(count - 1) - .5) * spread : 0;
        const Vec2 d = detail::turned(dir, a);
        foeShot(u.id, u.position + d * (u.radius + 8), d, damage, kind, speed, range, effect);
    }
}
inline void Battle::foeRing(int owner, Vec2 at, double from, int count, double offset, double damage, int kind, double speed, double range, int effect)
{
    for (int k = 0; k < count; ++k)
    {
        const double a = offset + 2 * Pi * k / count;
        const Vec2 d{std::cos(a), std::sin(a)};
        foeShot(owner, at + d * from, d, damage, kind, speed, range, effect);
    }
}
inline void Battle::foeLob(Unit& u, Vec2 at, double damage, double flight, int effect)
{
    if (projectiles.size() >= 768)
        return;
    const Vec2 from = u.position + u.aim.normalized() * (u.radius * .6);
    Projectile p;
    p.id = nextProjectile++;
    p.owner = p.squad = u.id;
    p.team = 2;
    p.weapon = FoeLob;
    p.position = from;
    flight = std::max(.3, flight);
    p.velocity = (at - from) * (1 / flight);
    p.remaining = flight;
    p.damage = damage;
    p.effect = effect;
    p.radius = 10;
    p.high = true;
    projectiles.push_back(p);
}
inline int Battle::addHazard(ZoneKind kind, Vec2 from, Vec2 to, double radius, double seconds, int what, double dps, int owner)
{
    auto& zones = expedition.zones;
    if (hazardZone(static_cast<int>(kind)))
    {
        int hazards = 0;
        for (const auto& z : zones)
            hazards += hazardZone(z.kind);
        if (hazards >= ExpeditionState::MaxHazards)
            for (auto it = zones.begin(); it != zones.end(); ++it)
                if (hazardZone(it->kind))
                {
                    zones.erase(it);
                    break;
                }
    }
    ExpeditionState::Zone z;
    z.kind = static_cast<int>(kind);
    z.owner = owner;
    z.what = what;
    z.from = from;
    z.to = to;
    z.radius = radius;
    z.remaining = z.life = seconds;
    z.dps = dps;
    zones.push_back(z);
    return static_cast<int>(zones.size()) - 1;
}
inline void Battle::foeVanish(Unit& u)
{
    u.alive = false;
    u.hp = 0;
    u.velocity = u.impulse = {};
    u.path.clear();
}
// The nearest of the party on its level, a lure first; a player charged by a cinnabar ring draws everyone within reach.
inline Unit* Battle::foeTarget(Unit& u)
{
    const Unit* lure = baitFor(u, CraneRadius);
    chase(u, lure);
    if (lure)
        return find(lure->id);
    Unit* best = nullptr;
    double nearest = 1e18;
    for (auto& v : units)
    {
        if (!v.alive || v.team == u.team || v.deck != u.deck || v.climb >= 0 || v.state == UnitState::Launched)
            continue;
        double d = (v.position - u.position).lengthSquared();
        if (v.player() && v.kit.streak > 0 && d < 900. * 900.)
            d *= .2;
        if (d < nearest)
        {
            nearest = d;
            best = &v;
        }
    }
    return best;
}
inline void Battle::foeSteer(Unit& u, Vec2 goal, double speed, double dt)
{
    // A player hugging a wall stands closer to it than a bigger body can: aim for the nearest spot this body fits in
    // instead, or the straight line and the path search (both sized to the body) would give up and leave it standing.
    if (!maze.walkable(goal, u.radius + 2))
        goal = maze.unstick(goal, u.radius + 2);
    if ((goal - u.position).lengthSquared() < 16)
    {
        u.velocity = {};
        return;
    }
    if (maze.clear(u.position, goal, u.radius))
    {
        u.path.clear();
        u.velocity = (goal - u.position).normalized() * speed;
        return;
    }
    u.think -= dt;
    if (u.think <= 0 || u.path.empty())
    {
        u.path = maze.path(u.position, goal, u.radius + 2);
        u.waypoint = 0;
        u.think = .55 + (u.id % 17) * .03;
    }
    while (u.waypoint < u.path.size() && (u.path[u.waypoint] - u.position).length() < 18)
        ++u.waypoint;
    u.velocity = u.waypoint < u.path.size() ? (u.path[u.waypoint] - u.position).normalized() * speed : Vec2{};
}
// Backs away from a point; against a wall it slides off to either side instead of standing pinned.
inline void Battle::foeFlee(Unit& u, Vec2 from, double reach, double speed, double dt)
{
    const Vec2 away = (u.position - from).normalized();
    for (const double turn : {0., .7, -.7, 1.4, -1.4, 2.1, -2.1})
    {
        const Vec2 goal = u.position + detail::turned(away, turn) * reach;
        if (maze.walkable(goal, u.radius) && maze.clear(u.position, goal, u.radius))
            return foeSteer(u, goal, speed, dt);
    }
    u.velocity = {};
}
// The friend a medic (the pencil stub) keeps close to: the most hurt one near it, else the nearest of its room; never
// another medic. Null when it has nobody left.
inline const Unit* Battle::stubWard(const Unit& u) const
{
    const Unit* hurt = nullptr;
    const Unit* nearest = nullptr;
    double worst = .95, closest = 1e18;
    for (const auto& v : units)
    {
        if (!v.alive || v.id == u.id || v.kind != UnitKind::Monster || v.team != u.team || v.gadget >= 0 || v.breed == static_cast<int>(Foe::Stub) ||
            (u.room >= 0 && v.room != u.room))
            continue;
        const double d = (v.position - u.position).lengthSquared();
        if (v.maxHp > 0 && v.hp / v.maxHp < worst && d < 900. * 900. && !bossFoe(v.breed))
        {
            worst = v.hp / v.maxHp;
            hurt = &v;
        }
        if (d < closest)
        {
            closest = d;
            nearest = &v;
        }
    }
    return hurt ? hurt : nearest;
}
// Holds a band of distance from its target: backs off inside it, closes in beyond it, sidesteps while in it.
inline void Battle::foeKeep(Unit& u, const Unit& t, double nearest, double farthest, double speed, double dt)
{
    const Vec2 d = t.position - u.position;
    const double distance = d.length();
    const bool sees = maze.clear(u.position, t.position, 4);
    if (!sees || distance > farthest)
        return foeSteer(u, t.position, speed, dt);
    if (distance < nearest)
        return foeFlee(u, t.position, 200, speed, dt);
    const Vec2 side{-d.y / std::max(1., distance), d.x / std::max(1., distance)};
    const double sign = ((u.id + static_cast<int>(elapsed / 2.3)) % 2) ? 1. : -1.;
    foeSteer(u, u.position + side * (140 * sign), speed * .6, dt);
}
// Moves a dashing foe along its heading; whoever it runs into is struck once. Returns false when a wall stopped it.
inline bool Battle::foeDash(Unit& u, Vec2 heading, double speed, double damage, int effect, double dt)
{
    const Vec2 dir = heading.normalized();
    const Vec2 next = u.position + dir * (speed * dt + 4);
    if (!maze.walkable(next, u.radius))
    {
        u.velocity = {};
        return false;
    }
    u.velocity = dir * speed;
    for (auto& v : units)
    {
        if (!v.alive || v.team == u.team || !v.player() || v.seat < 0 || ((u.struck >> std::clamp(v.seat, 0, 30)) & 1))
            continue;
        if ((v.position - u.position).length() < u.radius + v.radius + 10)
        {
            u.struck |= 1 << std::clamp(v.seat, 0, 30);
            damageUnit(u.id, v.id, damage);
            v.impulse += dir * 520;
            if (effect == static_cast<int>(ShotEffect::Bleed))
            {
                v.kit.bleed = std::max(v.kit.bleed, BleedSeconds);
                v.kit.bleedDamage = std::max(v.kit.bleedDamage, 4 * expeditionPower(u));
            }
        }
    }
    return true;
}
inline bool Battle::nearFoe(const Unit& u, Foe foe, double reach) const
{
    for (const auto& v : units)
        if (v.alive && v.id != u.id && v.kind == UnitKind::Monster && v.breed == static_cast<int>(foe) && (v.position - u.position).length() < reach)
            return true;
    return false;
}

// ---- The step for one foe ----
inline void Battle::updateFoe(Unit& u, double dt)
{
    u.input.fire = false;
    const Foe foe = static_cast<Foe>(std::clamp(u.breed, 0, FoeCount - 1));
    const FoeInfo& info = foeInfo(u.breed);
    u.skillTime = std::max(0., u.skillTime - dt);
    u.hitClock = std::max(0., u.hitClock - dt);
    using A = FoeAct;
    auto is = [&](A a) { return u.act == static_cast<int>(a); };
    auto begin = [&](A a, double seconds)
    {
        u.act = static_cast<int>(a);
        u.actClock = seconds;
        u.struck = 0;
    };
    auto done = [&]()
    {
        u.actClock -= dt;
        return u.actClock <= 0;
    };
    if (!config.aiEnabled)
    {
        u.velocity = {};
        return;
    }
    // The thief runs out its clock even while stunned: it gets away with what it took.
    if (foe == Foe::Thief && done())
    {
        event(EffectStyle::FoeBurst, u.position, u.position, 2, u.radius, .6);
        return foeVanish(u);
    }
    if (u.stun > 0)
    {
        u.velocity = {};
        if (!is(A::Split) && !is(A::Broken) && !is(A::Spin))
            u.act = static_cast<int>(A::Idle);
        return;
    }
    if (bossFoe(u.breed))
    {
        Unit* t = foeTarget(u);
        if (!t)
        {
            u.velocity = {};
            return;
        }
        return updateBoss(u, *t, dt);
    }
    Unit* tp = foeTarget(u);
    if (!tp)
    {
        u.velocity = {};
        return;
    }
    Unit& t = *tp;
    const Vec2 to = t.position - u.position;
    const double distance = to.length();
    const bool sees = distance < 1600 && maze.clear(u.position, t.position, 4);
    const bool locked = is(A::Dash) || is(A::Windup) || is(A::Aim) || is(A::Leap) || is(A::Hop);
    if (sees && !locked)
        u.aim = to.normalized();
    const double power = expeditionPower(u);
    const double speed = info.speed * (u.wet > 0 ? .8 : 1);
    const DungeonRoom* room = expedition.room(u.room);
    switch (foe)
    {
    // ---- Pencil ----
    case Foe::Scribble:
        // Rolls straight at the nearest player and bursts against them in a puff of graphite.
        if (distance < u.radius + t.radius + 12)
        {
            blast(u.id, u.team, u.position, 110, 16 * power, .35, EffectStyle::KillBlast);
            addHazard(ZoneKind::Fog, u.position, u.position, 150, 2.5);
            return foeVanish(u);
        }
        return foeSteer(u, t.position, speed, dt);
    case Foe::Phantom:
        // Every three metres it fades to a dashed ghost; close in, it inks itself solid and lunges.
        if (is(A::Windup))
        {
            u.velocity = {};
            if (done())
                begin(A::Dash, .35);
            return;
        }
        if (is(A::Dash))
        {
            if (!foeDash(u, u.actAt, speed * 3.2, info.melee * 1.5 * power, 0, dt) || done())
            {
                begin(A::Idle, 0);
                u.skillTime = 2.2;
            }
            return;
        }
        if (sees && distance < 270 && u.skillTime <= 0)
        {
            begin(A::Windup, .3);
            u.actAt = to.normalized();
            foeCue(u.position, u.position + u.actAt * 360, 26, .3, detail::CueGraphite);
            return;
        }
        u.store += speed * dt;
        if (is(A::Hidden))
        {
            if (done())
                begin(A::Idle, 0);
        }
        else if (u.store >= 300)
        {
            u.store = 0;
            begin(A::Hidden, 1.5);
        }
        return foeSteer(u, t.position, speed * (is(A::Hidden) ? 1.1 : 1), dt);
    case Foe::Shaver:
        if (sees && u.skillTime <= 0 && distance < 900)
        {
            foeFan(u, t.position, 3, .34, 9 * power, LeadShot, 900, 1000);
            u.skillTime = 1.8;
        }
        return foeKeep(u, t, 420, 650, speed, dt);
    case Foe::Shield:
        // A heavily hatched front takes 40% of what hits it (foeHitTaken) until it cracks.
        return foeSteer(u, t.position, speed, dt);
    case Foe::Stub:
    {
        // The medic: it rubs out the wounds of the most hurt friend in reach, and walks with the fight to do it, tucked
        // in behind a friend (the most hurt one it can see, else the nearest) on the side away from the player. With no
        // friend left it pokes at the player itself.
        if (u.skillTime <= 0)
        {
            Unit* hurt = nullptr;
            double worst = .95;
            for (auto& v : units)
                if (v.alive && v.id != u.id && v.kind == UnitKind::Monster && v.team == u.team && !bossFoe(v.breed) && v.maxHp > 0 &&
                    (v.position - u.position).length() < 420 && v.hp / v.maxHp < worst)
                {
                    worst = v.hp / v.maxHp;
                    hurt = &v;
                }
            if (hurt)
            {
                hurt->hp = std::min(hurt->maxHp, hurt->hp + hurt->maxHp * .08);
                Effect e{u.position, hurt->position, 30, .5, EventEffect, 2};
                e.style = EffectStyle::FoeBurst;
                e.pulse = 1;
                addEffect(e);
            }
            u.skillTime = 2.2;
        }
        const Unit* ward = stubWard(u);
        if (!ward)
            return foeSteer(u, t.position, speed, dt);
        const Vec2 cover = (ward->position - t.position).normalized();
        const Vec2 behind = ward->position + (cover.lengthSquared() > .5 ? cover : Vec2{0, 1}) * (ward->radius + u.radius + 40);
        if ((behind - u.position).length() < 30)
        {
            u.velocity = {};
            return;
        }
        return foeSteer(u, behind, speed * ((behind - u.position).length() > 400 ? 1.15 : .9), dt);
    }
    // ---- Watercolor ----
    case Foe::Slime:
        // Hops toward its target; every landing leaves a slowing puddle for five seconds.
        if (is(A::Hop))
        {
            if (!foeDash(u, u.actAt, speed * 2.4, 0, 0, dt) || done())
            {
                addHazard(ZoneKind::Puddle, u.position, u.position, 90, 5);
                begin(A::Idle, .55);
            }
            return;
        }
        u.velocity = {};
        if (done())
        {
            begin(A::Hop, .4);
            u.actAt = to.normalized();
        }
        return;
    case Foe::Striker:
    {
        // Zigzags in fast, leaving a neon ribbon that slows whoever crosses it.
        if (done())
        {
            u.acts = u.acts ? 0 : 1;
            u.actClock = .45;
        }
        if ((u.position - u.actAt).length() > 70)
        {
            addHazard(ZoneKind::Ribbon, u.actAt, u.position, 24, 3, u.acts);
            u.actAt = u.position;
        }
        const Vec2 side{-to.y / std::max(1., distance), to.x / std::max(1., distance)};
        return foeSteer(u, t.position + side * (u.acts ? 220. : -220.), speed, dt);
    }
    case Foe::Sprayer:
        // Lobs paint bombs that leave pools of either colour; where colours meet, the eyes swim.
        if (sees && u.skillTime <= 0 && distance < 900)
        {
            foeLob(u, t.position + t.velocity * .5, 10 * power, .9, static_cast<int>(u.acts++ % 2 ? ShotEffect::PaintB : ShotEffect::PaintA));
            u.skillTime = 2.6;
        }
        return foeKeep(u, t, 450, 700, speed, dt);
    case Foe::Zapper:
    {
        // Swims for the nearest puddle and charges it: the puddle and every puddle touching it bite for a moment.
        int best = -1;
        double nearest = 1000;
        for (size_t i = 0; i < expedition.zones.size(); ++i)
        {
            const auto& z = expedition.zones[i];
            if (z.kind == static_cast<int>(ZoneKind::Puddle) && z.charge <= 0 && (z.from - u.position).length() < nearest)
            {
                nearest = (z.from - u.position).length();
                best = static_cast<int>(i);
            }
        }
        if (is(A::Windup))
        {
            u.velocity = {};
            if (done())
            {
                begin(A::Idle, 0);
                u.skillTime = 3.5;
                if (best >= 0)
                {
                    // The charge runs through every puddle touching the first.
                    std::vector<int> wet{best};
                    expedition.zones[static_cast<size_t>(best)].charge = 1.6;
                    for (size_t k = 0; k < wet.size(); ++k)
                    {
                        const auto a = expedition.zones[static_cast<size_t>(wet[k])];
                        for (size_t i = 0; i < expedition.zones.size(); ++i)
                        {
                            auto& z = expedition.zones[i];
                            if (z.kind == static_cast<int>(ZoneKind::Puddle) && z.charge <= 0 && (z.from - a.from).length() < z.radius + a.radius + 30)
                            {
                                z.charge = 1.6;
                                wet.push_back(static_cast<int>(i));
                            }
                        }
                    }
                    for (int i : wet)
                        event(EffectStyle::Zap, u.position, expedition.zones[static_cast<size_t>(i)].from, 2, 0, .3);
                }
            }
            return;
        }
        if (best >= 0)
        {
            const auto& z = expedition.zones[static_cast<size_t>(best)];
            if ((z.from - u.position).length() < z.radius && u.skillTime <= 0)
            {
                begin(A::Windup, .8);
                foeCue(z.from, z.from, z.radius + 20, .8, detail::CueSpark);
                return;
            }
            return foeSteer(u, z.from, speed, dt);
        }
        return foeKeep(u, t, 260, 420, speed, dt);
    }
    case Foe::Sponge:
    {
        // Soaks up the floor's puddles and pools, swelling with each; it bursts with all of it when it falls.
        int best = -1;
        double nearest = 1200;
        for (size_t i = 0; i < expedition.zones.size(); ++i)
        {
            const auto& z = expedition.zones[i];
            const bool wet = z.kind == static_cast<int>(ZoneKind::Puddle) || z.kind == static_cast<int>(ZoneKind::Paint) ||
                             z.kind == static_cast<int>(ZoneKind::Ink) || z.kind == static_cast<int>(ZoneKind::Spring) ||
                             z.kind == static_cast<int>(ZoneKind::Clean);
            if (wet && z.remaining > 0 && (z.from - u.position).length() < nearest)
            {
                nearest = (z.from - u.position).length();
                best = static_cast<int>(i);
            }
        }
        if (best >= 0)
        {
            auto& z = expedition.zones[static_cast<size_t>(best)];
            if ((z.from - u.position).length() < z.radius + u.radius * .5)
            {
                z.remaining = 0;
                u.store = std::min(8., u.store + 1);
                u.radius = info.radius + 4 * std::min(6., u.store);
                Effect e{u.position, u.position, u.radius, .4, EventEffect, 2};
                e.style = EffectStyle::FoeBurst;
                e.pulse = 2;
                addEffect(e);
            }
            return foeSteer(u, z.from, speed, dt);
        }
        return foeSteer(u, t.position, speed, dt);
    }
    // ---- Fountain pen ----
    case Foe::Lancer:
        // Coils for 0.8 s, then runs its nib straight through: a hit bleeds.
        if (is(A::Windup))
        {
            u.velocity = {};
            if (done())
                begin(A::Dash, .45);
            return;
        }
        if (is(A::Dash))
        {
            if (!foeDash(u, u.actAt, 1150, 18 * power, static_cast<int>(ShotEffect::Bleed), dt) || done())
            {
                begin(A::Idle, 0);
                u.skillTime = 2.6;
            }
            return;
        }
        if (sees && distance < 650 && u.skillTime <= 0)
        {
            begin(A::Windup, .8);
            u.actAt = to.normalized();
            foeCue(u.position, u.position + u.actAt * 560, 28, .8, detail::CueInk);
            return;
        }
        return foeKeep(u, t, 360, 560, speed, dt);
    case Foe::InkBug:
        // Waddles up and swells; it bursts into eight heavy blots (and so does a bug that is shot down).
        if (is(A::Fuse))
        {
            u.velocity = {};
            if (done())
            {
                damageUnit(-1, u.id, u.hp + 1);
                if (u.alive)
                    foeVanish(u);
            }
            return;
        }
        if (distance < 130)
        {
            begin(A::Fuse, .6);
            foeCue(u.position, u.position, 120, .6, detail::CueInk);
            return;
        }
        return foeSteer(u, t.position, speed, dt);
    case Foe::Crab:
    {
        // Side-steps at range and throws a broad wave of cross-hatching; every third hit raises its thorns (foeHitTaken).
        if (is(A::Spikes) && done())
            begin(A::Idle, 0);
        if (sees && u.skillTime <= 0 && distance < 900)
        {
            const Vec2 dir = to.normalized(), side{-dir.y, dir.x};
            for (int k = -2; k <= 2; ++k)
                foeShot(u.id, u.position + dir * (u.radius + 8) + side * (k * 24.), dir, 8 * power, NibShot, 640, 900);
            u.skillTime = 3;
        }
        if (!sees || distance > 650 || distance < 360)
            return foeKeep(u, t, 380, 620, speed, dt);
        const Vec2 side{-to.y / distance, to.x / distance};
        return foeSteer(u, u.position + side * (((u.id + static_cast<int>(elapsed / 1.7)) % 2) ? 160. : -160.), speed, dt);
    }
    case Foe::Inkwell:
    {
        // Sits far off and takes aim along a long black line, then fires a nib almost too fast to see.
        if (is(A::Aim))
        {
            u.velocity = {};
            if (u.actClock > .25 && sees)
                u.aim = to.normalized();
            if (done())
            {
                foeShot(u.id, u.position + u.aim * (u.radius + 8), u.aim, 22 * power, NibShot, 2200, 2000, static_cast<int>(ShotEffect::Bleed));
                begin(A::Idle, 0);
                u.skillTime = 3.2;
            }
            return;
        }
        if (sees && u.skillTime <= 0 && distance < 1700)
        {
            begin(A::Aim, 1);
            foeCue(u.position, u.position + to.normalized() * 1800, 14, 1, detail::CueInk);
            return;
        }
        if (room && distance < 620)
        {
            // Off to the room's far side from its target.
            const Vec2 mid = roomCenter(*room);
            const Vec2 away = mid + (mid - t.position).normalized() * (std::min(room->w, room->h) * .35 * maze.cellSize);
            return foeSteer(u, away, speed, dt);
        }
        return foeKeep(u, t, 620, 1100, speed, dt);
    }
    case Foe::Saw:
    {
        // Skims the walls at speed, bouncing off them; a touch cuts (foeHitPlayer).
        Vec2 dir = u.actAt.normalized();
        if (dir.lengthSquared() < .5)
            dir = to.normalized();
        const double step = speed * dt + 6;
        if (!maze.walkable(u.position + dir * step, u.radius))
        {
            const Vec2 fx{-dir.x, dir.y}, fy{dir.x, -dir.y};
            dir = maze.walkable(u.position + fx * step, u.radius) ? fx : maze.walkable(u.position + fy * step, u.radius) ? fy : dir * -1.;
        }
        // Now and then it turns toward its target.
        if (u.skillTime <= 0)
        {
            dir = (to.normalized() + Vec2{(random01(rng) - .5), (random01(rng) - .5)}).normalized();
            u.skillTime = 3 + random01(rng) * 2;
        }
        u.actAt = dir;
        u.velocity = dir * speed;
        return;
    }
    // ---- Ballpoint ----
    case Foe::Beader:
        if (sees && u.skillTime <= 0 && distance < 1100)
        {
            foeFan(u, t.position, 1, 0, 6 * power, BeadShot, 1300, 1100);
            u.skillTime = .45;
        }
        return foeKeep(u, t, 450, 650, speed, dt);
    case Foe::Marker:
    {
        // Picks the one reloading (or the slowest) and rings them in red: they take a fifth more for five seconds.
        Unit* mark = &t;
        for (auto& v : units)
            if (v.alive && v.player() && v.team != u.team && (v.position - u.position).length() < 900 && v.reload > 0)
                mark = &v;
        if (is(A::Windup))
        {
            u.velocity = {};
            if (done())
            {
                if (Unit* m = find(static_cast<int>(u.store)); m && m->alive && m->player() && (m->position - u.actAt).length() < 160)
                    m->kit.marked = CircleSeconds;
                begin(A::Idle, 0);
                u.skillTime = 4;
            }
            return;
        }
        if (u.skillTime <= 0 && (mark->position - u.position).length() < 900 && mark->player())
        {
            begin(A::Windup, .6);
            u.store = mark->id;
            u.actAt = mark->position;
            foeCue(mark->position, mark->position, 80, .6, detail::CueDanger);
            return;
        }
        if (sees && u.hitClock <= 0 && distance < 800)
        {
            foeFan(u, t.position, 1, 0, 7 * power, BeadShot, 900, 900);
            u.hitClock = 1.8;
        }
        return foeKeep(u, t, 450, 620, speed, dt);
    }
    case Foe::Twin:
        // Below half its health it copies itself once (the copy has 40% and never copies again).
        if (is(A::Windup))
        {
            u.velocity = {};
            if (done())
            {
                const Vec2 side{-u.aim.y, u.aim.x};
                Vec2 at = maze.move(u.position, side * 70, u.radius);
                const int copy = spawnFoe(Foe::Twin, u.room, at);
                for (auto& m : pendingUnits)
                    if (m.id == copy)
                    {
                        m.maxHp = u.maxHp;
                        m.hp = u.maxHp * .4;
                        m.store = 1;
                        m.stun = .2;
                    }
                begin(A::Idle, 0);
            }
            return;
        }
        if (u.store == 0 && u.hp < u.maxHp * .5)
        {
            u.store = 1;
            begin(A::Windup, .5);
            Effect e{u.position, u.position, u.radius, .5, EventEffect, 2};
            e.style = EffectStyle::FoeBurst;
            e.pulse = 3;
            addEffect(e);
            return;
        }
        if (sees && u.skillTime <= 0 && distance < 900)
        {
            foeFan(u, t.position, 1, 0, 9 * power, BeadShot, 1000, 900);
            u.skillTime = 1.6;
        }
        return foeKeep(u, t, 260, 420, speed, dt);
    case Foe::Roller:
        // Clicks, then rolls flat out in a straight line, laying two oily tracks that speed up whoever runs along them.
        if (is(A::Windup))
        {
            u.velocity = {};
            if (done())
            {
                begin(A::Dash, 1.2);
                u.store = 0;
            }
            return;
        }
        if (is(A::Dash))
        {
            const Vec2 dir = u.actAt.normalized(), side{-dir.y, dir.x};
            const bool moving = foeDash(u, u.actAt, 650, 20 * power, 0, dt);
            if ((u.store += dt) >= .2)
            {
                u.store = 0;
                const Vec2 back = u.position - dir * 140;
                for (double s : {-24., 24.})
                    addHazard(ZoneKind::Track, back + side * s, u.position + side * s, 12, 6);
            }
            if (!moving || done())
            {
                if (!moving)
                    u.stun = .5;
                begin(A::Idle, 0);
                u.skillTime = 4;
            }
            return;
        }
        if (sees && u.skillTime <= 0 && distance < 900)
        {
            begin(A::Windup, .7);
            u.actAt = to.normalized();
            foeCue(u.position, u.position + u.actAt * 900, 40, .7, detail::CueWater);
            return;
        }
        return foeKeep(u, t, 300, 700, speed, dt);
    case Foe::Abacus:
    {
        // Each volley flicks four beads along its rod: slow, fast, slow, very fast.
        static const double Speeds[4] = {500, 1100, 500, 1500};
        if (u.acts > 0)
        {
            if (done())
            {
                const int k = 4 - u.acts;
                foeFan(u, t.position, 1, 0, 9 * power, BeadShot, Speeds[k], 1100);
                --u.acts;
                u.actClock = .25;
                if (u.acts == 0)
                    u.skillTime = 3;
            }
        }
        else if (sees && u.skillTime <= 0 && distance < 1000)
        {
            u.acts = 4;
            u.actClock = 0;
        }
        return foeKeep(u, t, 450, 650, speed, dt);
    }
    // ---- Brush ----
    case Foe::Wisp:
    {
        // Drifts in along a cursive line; a touch smears ink across the eyes (foeHitPlayer).
        const Vec2 side{-to.y / std::max(1., distance), to.x / std::max(1., distance)};
        return foeSteer(u, t.position + side * (std::sin(elapsed * 2.6 + u.id) * std::min(260., distance * .6)), speed, dt);
    }
    case Foe::Parasol:
        // Walks in behind its fan (light shots on the fan are lost, foeDeflect), then spins and sweeps a crescent of ink.
        if (is(A::Spin))
        {
            u.velocity = {};
            if (done())
            {
                for (auto& v : units)
                    if (v.alive && v.team != u.team && (v.position - u.position).length() < 230 + v.radius &&
                        u.aim.normalized().dot((v.position - u.position).normalized()) > -.3)
                        damageUnit(u.id, v.id, 18 * power);
                Effect e{u.position, u.position + u.aim * 200, 230, .4, EventEffect, 2};
                e.style = EffectStyle::FoeBurst;
                e.pulse = 4;
                addEffect(e);
                begin(A::Idle, 0);
                u.skillTime = 3;
            }
            return;
        }
        if (distance < 230 && u.skillTime <= 0)
        {
            begin(A::Spin, .5);
            foeCue(u.position, u.position, 230, .5, detail::CueInk);
            return;
        }
        return foeSteer(u, t.position, speed, dt);
    case Foe::Seal:
        // Every five seconds it leaps and stamps down: a cinnabar shockwave throws everyone near it away.
        if (is(A::Leap))
        {
            u.velocity = {};
            if (done())
            {
                blast(u.id, u.team, u.position, 260, 16 * power, .45, EffectStyle::BossSlam);
                for (auto& v : units)
                    if (v.alive && v.team != u.team && (v.position - u.position).length() < 260 + v.radius)
                        v.impulse += (v.position - u.position).normalized() * 700;
                begin(A::Idle, 0);
                u.skillTime = 5;
            }
            return;
        }
        if (u.skillTime <= 0 && distance < 520)
        {
            begin(A::Leap, .7);
            foeCue(u.position, u.position, 260, .7, detail::CueSeal);
            return;
        }
        return foeSteer(u, t.position, speed, dt);
    case Foe::Column:
        // Bears down for a second and a half, then sweeps a wave of ink right across the room.
        if (is(A::Aim))
        {
            u.velocity = {};
            if (done())
            {
                const Vec2 a = u.position, b = u.position + u.actAt * 1700;
                for (auto& v : units)
                    if (v.alive && v.team != u.team && distanceToSegment(v.position, a, b) < 70 + v.radius)
                        damageUnit(u.id, v.id, 22 * power);
                addHazard(ZoneKind::Ink, a + u.actAt * 60, b, 46, 3, 0, 6 * power, u.id);
                begin(A::Idle, 0);
                u.skillTime = 5;
            }
            return;
        }
        if (sees && u.skillTime <= 0 && distance < 1300)
        {
            begin(A::Aim, 1.5);
            u.actAt = to.normalized();
            foeCue(u.position, u.position + u.actAt * 1700, 70, 1.5, detail::CueInk);
            return;
        }
        return foeKeep(u, t, 500, 800, speed, dt);
    case Foe::Pinwheel:
        // Turns and turns, throwing a slow spiral of blots; a burst of hits knocks it out of its spin (foeHitTaken).
        if (!is(A::Spin))
            begin(A::Spin, 0);
        if ((u.store += dt) >= .16)
        {
            u.store = 0;
            u.actClock += .65;
            const Vec2 d{std::cos(u.actClock), std::sin(u.actClock)};
            for (const Vec2 dir : {d, d * -1.})
                foeShot(u.id, u.position + dir * (u.radius + 6), dir, 7 * power, BlotShot, 380, 900);
        }
        if (distance > 600)
            return foeSteer(u, t.position, speed, dt);
        u.velocity = {};
        return;
    // ---- Elites ----
    case Foe::Leadcannon:
    {
        // Alternates an armour-piercing lead (aimed along a line) with a rain of six leads; after six volleys it jams.
        if (is(A::Jam))
        {
            u.velocity = {};
            if (done())
            {
                begin(A::Idle, 0);
                u.acts = 0;
            }
            return;
        }
        if (is(A::Aim))
        {
            u.velocity = {};
            if (u.actClock > .2 && sees)
                u.aim = to.normalized();
            if (done())
            {
                foeShot(u.id, u.position + u.aim * (u.radius + 8), u.aim, 30 * power, LeadShot, 2100, 1900, 0, 1);
                begin(A::Idle, 0);
                ++u.acts;
                u.skillTime = 1.2;
            }
            return;
        }
        if (is(A::Spin))
        {
            u.velocity = {};
            if ((u.store += dt) >= .2)
            {
                u.store = 0;
                foeFan(u, t.position, 3, .5, 8 * power, LeadShot, 950, 1000);
            }
            if (done())
            {
                begin(A::Idle, 0);
                ++u.acts;
                u.skillTime = 1.2;
            }
            return;
        }
        if (u.acts >= 6)
        {
            begin(A::Jam, 1.5);
            Effect e{u.position, u.position, u.radius, .6, EventEffect, 2};
            e.style = EffectStyle::FoeBurst;
            e.pulse = 5;
            addEffect(e);
            return;
        }
        if (sees && u.skillTime <= 0 && distance < 1300)
        {
            if (u.acts % 2 == 0)
            {
                begin(A::Aim, .8);
                foeCue(u.position, u.position + to.normalized() * 1800, 16, .8, detail::CueGraphite);
            }
            else
                begin(A::Spin, 1.2);
            return;
        }
        return foeKeep(u, t, 500, 800, speed, dt);
    }
    case Foe::Crumbler:
        // Half of every hit is lost in its bulk; the crumbs it sheds rub at players' shields (foeHitTaken).
        return foeSteer(u, t.position, speed, dt);
    case Foe::Palette:
    {
        // Turns slowly, spraying three streams at once: red burns, blue chills, yellow jolts.
        u.store += dt * .6;
        if (sees && u.skillTime <= 0 && distance < 1000)
        {
            const double base = std::atan2(to.y, to.x) + u.store;
            static const ShotEffect Effects[3] = {ShotEffect::Bleed, ShotEffect::Slow, ShotEffect::Stun};
            for (int s = 0; s < 3; ++s)
            {
                const double a = base + s * 2 * Pi / 3;
                for (int k = -1; k <= 1; ++k)
                {
                    const Vec2 d{std::cos(a + k * .09), std::sin(a + k * .09)};
                    foeShot(u.id, u.position + d * (u.radius + 8), d, 9 * power, DropShot, 700, 950, static_cast<int>(Effects[s]));
                }
            }
            u.skillTime = 2.8;
        }
        return foeKeep(u, t, 400, 600, speed, dt);
    }
    case Foe::Eel:
    {
        // Swims round its target in a long wave; each fifth of its health lost pops a body bead into water drops.
        const int segments = std::max(1, static_cast<int>(std::ceil(std::max(0., u.hp / std::max(1., u.maxHp)) * 5 - 1e-9)));
        while (u.acts > std::max(1, segments))
        {
            --u.acts;
            const Vec2 back = u.position - u.aim.normalized() * (u.radius * 1.6 * (u.acts));
            foeRing(u.id, back, 10, 4, random01(rng) * Pi, 8 * power, DropShot, 520, 700, 0);
            Effect e{back, back, 40, .4, EventEffect, 2};
            e.style = EffectStyle::FoeBurst;
            e.pulse = 2;
            addEffect(e);
        }
        const Vec2 side{-to.y / std::max(1., distance), to.x / std::max(1., distance)};
        return foeSteer(u, t.position + side * (std::sin(elapsed * 1.8 + u.id) * 340) - to.normalized() * 120, speed, dt);
    }
    case Foe::Swordsaint:
        // Two crossing lunges in quick succession; struck hard and often, it parries shots back (foeDeflect).
        if (is(A::Parry))
        {
            u.velocity = {};
            if (done())
                begin(A::Idle, 0);
            return;
        }
        if (is(A::Windup))
        {
            u.velocity = {};
            if (done())
                begin(A::Dash, .26);
            return;
        }
        if (is(A::Dash))
        {
            if (!foeDash(u, u.actAt, 950, 20 * power, static_cast<int>(ShotEffect::Bleed), dt) || done())
            {
                if (u.acts == 0)
                {
                    // The second stroke crosses the first.
                    u.acts = 1;
                    const Vec2 back = (t.position - u.position).normalized();
                    u.actAt = (detail::turned(u.actAt, Pi / 2).dot(back) >= 0) ? detail::turned(u.actAt, Pi / 2) : detail::turned(u.actAt, -Pi / 2);
                    begin(A::Windup, .15);
                    foeCue(u.position, u.position + u.actAt * 260, 30, .15, detail::CueInk);
                }
                else
                {
                    u.acts = 0;
                    begin(A::Idle, 0);
                    u.skillTime = 2.2;
                }
            }
            return;
        }
        if (sees && distance < 420 && u.skillTime <= 0)
        {
            u.acts = 0;
            begin(A::Windup, .4);
            u.actAt = to.normalized();
            foeCue(u.position, u.position + u.actAt * 260, 30, .4, detail::CueInk);
            return;
        }
        return foeSteer(u, t.position, speed, dt);
    case Foe::Inspector:
        // Hangs back behind its crew (who hit twice as hard near it); every tenth hit it calls down ink rain (foeHitTaken).
        return foeKeep(u, t, 500, 720, speed, dt);
    case Foe::Clicker:
    {
        // Every six seconds a loud click and a new ink: blue sprays, red bursts, green poisons, black pierces.
        if (u.act < static_cast<int>(A::Blue) || done())
        {
            u.act = static_cast<int>(A::Blue) + static_cast<int>(randomStep(rng) % 4);
            u.actClock = 6;
            Effect e{u.position, u.position, u.radius, .4, EventEffect, 2};
            e.style = EffectStyle::FoeBurst;
            e.pulse = 6;
            addEffect(e);
        }
        if (sees && u.skillTime <= 0 && distance < 1100)
        {
            switch (static_cast<A>(u.act))
            {
            case A::Blue:
                foeFan(u, t.position, 1, 0, 5 * power, BeadShot, 1250, 1000);
                u.skillTime = .15;
                break;
            case A::Red:
                foeLob(u, t.position, 14 * power, .8, 0);
                u.skillTime = .9;
                break;
            case A::Green:
                foeFan(u, t.position, 1, 0, 7 * power, DropShot, 650, 1000, static_cast<int>(ShotEffect::Bleed));
                u.skillTime = .6;
                break;
            default:
                foeShot(u.id, u.position + u.aim * (u.radius + 8), u.aim, 18 * power, HeavyShot, 1500, 1300, 0, 2);
                u.skillTime = 1;
                break;
            }
        }
        return foeKeep(u, t, 450, 650, speed, dt);
    }
    case Foe::Mirror:
        // Its first heavy shot taken comes straight back (foeDeflect); then it is open for four seconds.
        if (is(A::Open) && done())
            begin(A::Guard, 0);
        if (sees && u.skillTime <= 0 && distance < 1000)
        {
            foeFan(u, t.position, 2, .5, 8 * power, BeadShot, 1000, 1000);
            u.skillTime = 1.5;
        }
        return foeKeep(u, t, 400, 600, speed, dt);
    case Foe::Wildscript:
    {
        // The more it is hurt, the faster it writes: sweeping fans of blots, and every six seconds a wall of ink words.
        const double health = std::clamp(u.hp / std::max(1., u.maxHp), 0., 1.);
        if (sees && u.skillTime <= 0 && distance < 1100)
        {
            foeFan(u, t.position, 5, 1.1, 7 * power, BlotShot, 520, 950);
            u.skillTime = 1.3 * (.35 + .65 * health);
        }
        if ((u.store += dt) >= 6)
        {
            u.store = 0;
            const Vec2 dir = to.normalized(), side{-dir.y, dir.x}, mid = u.position + to * .5;
            addHazard(ZoneKind::Ink, mid - side * 260, mid + side * 260, 40, 5, 1, 8 * power, u.id);
            Effect e{mid - side * 260, mid + side * 260, 40, .5, EventEffect, 2};
            e.style = EffectStyle::FoeBurst;
            e.pulse = 7;
            addEffect(e);
        }
        return foeKeep(u, t, 300, 600, speed * (1 + (1 - health) * .7), dt);
    }
    case Foe::Judge:
        // A red seal locks a player's items for four seconds; the black seal slams the same spot a second later.
        if (is(A::Write))
        {
            u.velocity = {};
            if (done())
            {
                blast(u.id, u.team, u.actAt, 150, 30 * power, .45, EffectStyle::BossSlam);
                begin(A::Idle, 0);
                u.skillTime = 5;
            }
            return;
        }
        if (sees && u.skillTime <= 0 && distance < 1000)
        {
            begin(A::Write, 1);
            u.actAt = t.position;
            addHazard(ZoneKind::Seal, t.position, t.position, 150, 1.8, 0, 0, u.id);
            foeCue(t.position, t.position, 150, 1, detail::CueSeal);
            return;
        }
        return foeKeep(u, t, 400, 600, speed, dt);
    case Foe::Thief:
    {
        // Runs from everyone, filling its sack with any stickers it passes; caught in fifteen seconds, it pays out big.
        for (auto& l : expedition.loot)
            if (l.kind == LootKind::Stickers && l.amount > 0 && (l.position - u.position).length() < u.radius + 40)
            {
                u.store += l.amount;
                l.amount = 0;
            }
        Vec2 away{};
        for (const auto& v : units)
            if (v.alive && v.player() && v.team != u.team)
            {
                const Vec2 d = u.position - v.position;
                away += d.normalized() * (1 / std::max(150., d.length()));
            }
        Vec2 goal = u.position + away.normalized() * 400;
        // A sticker pile not too near anyone is worth a detour.
        for (const auto& l : expedition.loot)
            if (l.kind == LootKind::Stickers && l.amount > 0 && (l.position - u.position).length() < 500 && (l.position - t.position).length() > 320)
            {
                goal = l.position;
                break;
            }
        if (room && !maze.walkable(goal, u.radius))
            goal = roomCenter(*room) + (roomCenter(*room) - t.position).normalized() * 300;
        u.act = static_cast<int>(A::Spin);
        return foeSteer(u, goal, speed, dt);
    }
    case Foe::Mashup:
    {
        // Every four seconds it borrows another biome's trick: a lead fan, a puddle hop, a nib lunge, a bead stream, a seal.
        if (is(A::Dash))
        {
            if (!foeDash(u, u.actAt, u.acts == 1 ? speed * 2.4 : 1000, (u.acts == 1 ? 10 : 18) * power, u.acts == 2 ? static_cast<int>(ShotEffect::Bleed) : 0, dt) || done())
            {
                if (u.acts == 1)
                    addHazard(ZoneKind::Puddle, u.position, u.position, 100, 5);
                begin(A::Idle, 0);
            }
            return;
        }
        if (is(A::Windup) || is(A::Leap))
        {
            u.velocity = {};
            if (done())
            {
                if (u.acts == 4)
                {
                    blast(u.id, u.team, u.position, 220, 18 * power, .45, EffectStyle::BossSlam);
                    begin(A::Idle, 0);
                }
                else
                    begin(A::Dash, .4);
            }
            return;
        }
        if (is(A::Spin))
        {
            if ((u.store += dt) >= .15)
            {
                u.store = 0;
                foeFan(u, t.position, 1, 0, 6 * power, BeadShot, 1200, 1000);
            }
            if (done())
                begin(A::Idle, 0);
            return foeKeep(u, t, 300, 600, speed, dt);
        }
        if (u.skillTime <= 0 && sees)
        {
            int trick = static_cast<int>(randomStep(rng) % 5);
            if (trick == u.acts)
                trick = (trick + 1) % 5;
            u.acts = trick;
            u.skillTime = 4;
            u.actAt = to.normalized();
            switch (trick)
            {
            case 0:
                foeFan(u, t.position, 3, .34, 9 * power, LeadShot, 900, 1000);
                break;
            case 1:
                begin(A::Dash, .45);
                break;
            case 2:
                begin(A::Windup, .6);
                foeCue(u.position, u.position + u.actAt * 450, 28, .6, detail::CueInk);
                break;
            case 3:
                begin(A::Spin, .9);
                break;
            default:
                begin(A::Leap, .7);
                foeCue(u.position, u.position, 220, .7, detail::CueSeal);
                break;
            }
            return;
        }
        return foeSteer(u, t.position, speed, dt);
    }
    // ---- Summons ----
    case Foe::SpiritRed:
        if (distance < u.radius + t.radius + 14)
        {
            blast(u.id, u.team, u.position, 130, 18 * power, .35, EffectStyle::KillBlast);
            return foeVanish(u);
        }
        return foeSteer(u, t.position, speed, dt);
    case Foe::SpiritYellow:
        if (is(A::Windup))
        {
            u.velocity = {};
            if (done())
            {
                blast(u.id, u.team, u.position, 230, 14 * power, .4, EffectStyle::Voltage);
                begin(A::Idle, 0);
                u.skillTime = 3;
            }
            return;
        }
        if (u.skillTime <= 0 && distance < 420)
        {
            begin(A::Windup, .8);
            foeCue(u.position, u.position, 230, .8, detail::CueSpark);
            return;
        }
        return foeSteer(u, t.position, speed, dt);
    case Foe::SpiritBlue:
        if (is(A::Aim))
        {
            u.velocity = {};
            if (done())
            {
                foeShot(u.id, u.position + u.aim * (u.radius + 8), u.aim, 12 * power, DropShot, 1600, 1500, static_cast<int>(ShotEffect::Slow));
                begin(A::Idle, 0);
                u.skillTime = 2.5;
            }
            return;
        }
        if (sees && u.skillTime <= 0)
        {
            begin(A::Aim, .6);
            foeCue(u.position, u.position + to.normalized() * 1400, 14, .6, detail::CueWater);
            return;
        }
        return foeKeep(u, t, 450, 650, speed, dt);
    case Foe::GuardDun:
    {
        // The written shield stands between the sage and the nearest player.
        const Unit* sage = expedition.bossId >= 0 ? find(expedition.bossId) : nullptr;
        if (sage && sage->alive)
            return foeSteer(u, sage->position + (t.position - sage->position).normalized() * (sage->radius + 120), speed, dt);
        return foeSteer(u, t.position, speed, dt);
    }
    default:
        return foeSteer(u, t.position, speed, dt);
    }
}

// ---- Bosses ----
inline void Battle::updateBoss(Unit& u, Unit& t, double dt)
{
    using A = FoeAct;
    const Foe foe = static_cast<Foe>(u.breed);
    const FoeInfo& info = foeInfo(u.breed);
    auto is = [&](A a) { return u.act == static_cast<int>(a); };
    auto begin = [&](A a, double seconds)
    {
        u.act = static_cast<int>(a);
        u.actClock = seconds;
        u.struck = 0;
    };
    auto done = [&]()
    {
        u.actClock -= dt;
        return u.actClock <= 0;
    };
    const Vec2 to = t.position - u.position;
    const double distance = to.length();
    const bool sees = distance < 1800 && maze.clear(u.position, t.position, 4);
    const bool locked = is(A::Dash) || is(A::Windup) || is(A::Aim) || is(A::Leap) || is(A::Guard);
    if (sees && !locked)
        u.aim = to.normalized();
    // The editor without its weapons is a lot less dangerous (design: 火力锐减 70%).
    double power = expeditionPower(u) * (foe == Foe::Editor && u.parts == 0 ? .3 : 1);
    const double speed = info.speed;
    const DungeonRoom* room = expedition.room(u.room);
    const Vec2 mid = room ? roomCenter(*room) : u.position;
    const double span = room ? std::min(room->w, room->h) * maze.cellSize : 1600;
    auto rocks = [&](int count)
    {
        // Rubble shaken loose from the ceiling: blobs falling near the party.
        for (int k = 0; k < count; ++k)
        {
            const Unit* v = nullptr;
            int n = 0;
            for (const auto& w : units)
                if (w.alive && w.player() && w.team != u.team && randomStep(rng) % static_cast<uint64_t>(++n) == 0)
                    v = &w;
            const Vec2 at = (v ? v->position : t.position) + Vec2{(random01(rng) - .5) * 360, (random01(rng) - .5) * 360};
            foeLob(u, at, 16 * power, 1 + random01(rng) * .4, static_cast<int>(ShotEffect::Rock));
        }
    };
    switch (foe)
    {
    case Foe::Draftsman:
    {
        // The pencil gatling sweeps a storm of leads; the gold compass rings the room in a closing black disk; the table
        // itself rams and shakes rubble loose. Rubbing out a tool ends its attack for good.
        if (is(A::Spin))
        {
            u.velocity = {};
            if (sees)
                u.aim = (u.aim + (to.normalized() - u.aim) * std::min(1., dt * 1.5)).normalized();
            if ((u.store += dt) >= .07)
            {
                u.store = 0;
                const Vec2 d = detail::turned(u.aim, std::sin(u.actClock * 2.4) * .75);
                const Vec2 gun = foePartAt(u.breed, 0, u.position, u.aim, u.radius);
                foeShot(u.id, gun, d, 9 * power, LeadShot, 950, 1300);
            }
            if (done() || !(u.parts & 1))
            {
                begin(A::Idle, 0);
                u.skillTime = 1.6;
            }
            return;
        }
        if (is(A::Write))
        {
            u.velocity = {};
            if (done())
            {
                const int disk = addHazard(ZoneKind::Disk, mid, mid, span * .42, 5.5, 0, 30 * power, u.id);
                expedition.zones[static_cast<size_t>(disk)].charge = span * .42;
                begin(A::Idle, 0);
                u.skillTime = 2.5;
            }
            return;
        }
        if (is(A::Windup))
        {
            u.velocity = {};
            if (done())
                begin(A::Dash, 1);
            return;
        }
        if (is(A::Dash))
        {
            if (!foeDash(u, u.actAt, 680, 30 * power, 0, dt) || done())
            {
                rocks(u.parts ? 3 : 5);
                begin(A::Idle, 0);
                u.skillTime = 1.8;
            }
            return;
        }
        if (u.skillTime > 0)
            return foeSteer(u, t.position, speed * .6, dt);
        int options[3], count = 0;
        if (u.parts & 1)
            options[count++] = 0;
        if (u.parts & 2)
            options[count++] = 1;
        options[count++] = 2;
        int pick = options[randomStep(rng) % static_cast<uint64_t>(count)];
        if (count > 1 && pick == u.acts)
            pick = options[(std::find(options, options + count, pick) - options + 1) % count];
        u.acts = pick;
        if (pick == 0)
            begin(A::Spin, 2.6);
        else if (pick == 1)
        {
            begin(A::Write, 1.2);
            foeCue(mid, mid, span * .42, 1.2, detail::CueInk);
        }
        else
        {
            begin(A::Windup, .8);
            u.actAt = to.normalized();
            foeCue(u.position, u.position + u.actAt * 900, u.radius, .8, detail::CueDanger);
        }
        return;
    }
    case Foe::Rainbow:
    {
        // A skill boss (no tools): it splits into three spirits twice, floods the floor with paint, and pulls the party
        // into a whirlpool that bursts into bubbles.
        if (is(A::Split))
        {
            u.velocity = (mid - u.position).lengthSquared() > 400 ? (mid - u.position).normalized() * speed : Vec2{};
            int spirits = 0;
            for (const auto& v : units)
                spirits += v.alive && (v.breed == static_cast<int>(Foe::SpiritRed) || v.breed == static_cast<int>(Foe::SpiritYellow) ||
                                       v.breed == static_cast<int>(Foe::SpiritBlue));
            for (const auto& v : pendingUnits)
                spirits += v.breed >= static_cast<int>(Foe::SpiritRed) && v.breed <= static_cast<int>(Foe::SpiritBlue);
            if (spirits == 0 && (u.actClock -= dt) <= 0)
            {
                begin(A::Idle, 0);
                u.skillTime = 1.5;
                event(EffectStyle::BossPhase, u.position, u.position, 2, u.breed, 1);
            }
            return;
        }
        const double health = u.hp / std::max(1., u.maxHp);
        if ((u.acts < 1 && health < .7) || (u.acts < 2 && health < .35))
        {
            u.acts = health < .35 ? 2 : 1;
            begin(A::Split, .5);
            const Foe spirits[3] = {Foe::SpiritRed, Foe::SpiritYellow, Foe::SpiritBlue};
            for (int k = 0; k < 3; ++k)
            {
                const double a = k * 2 * Pi / 3 + random01(rng);
                spawnFoe(spirits[k], u.room, maze.move(u.position, Vec2{std::cos(a), std::sin(a)} * 220, 30));
            }
            event(EffectStyle::BossPhase, u.position, u.position, 2, u.breed, 1);
            return;
        }
        if (is(A::Windup))
        {
            u.velocity = {};
            if (done())
            {
                // The tide: paint pools of both colours across the floor, and a ring of drops.
                for (int k = 0; k < 7; ++k)
                {
                    const double a = random01(rng) * 2 * Pi, r = (.15 + random01(rng) * .3) * span;
                    const Vec2 at = mid + Vec2{std::cos(a), std::sin(a)} * r;
                    if (maze.walkable(at, 20))
                        addHazard(ZoneKind::Paint, at, at, 120, 7, k % 2, 4 * power, u.id);
                }
                foeRing(u.id, u.position, u.radius + 8, 16, random01(rng), 9 * power, DropShot, 600, 1200, 0);
                begin(A::Idle, 0);
                u.skillTime = 2.5;
            }
            return;
        }
        if (is(A::Write))
        {
            u.velocity = {};
            if (done())
            {
                foeRing(u.id, mid, 60, 24, random01(rng), 9 * power, DropShot, 520, 1300, 0);
                begin(A::Idle, 0);
                u.skillTime = 2.5;
            }
            return;
        }
        if (u.skillTime > 0)
        {
            if (sees && u.hitClock <= 0)
            {
                foeFan(u, t.position, 3, .3, 9 * power, DropShot, 800, 1100, static_cast<int>(ShotEffect::Slow));
                u.hitClock = 1.2;
            }
            return foeKeep(u, t, 350, 650, speed, dt);
        }
        if (u.store == 0)
        {
            u.store = 1;
            begin(A::Windup, 1);
            foeCue(u.position, u.position, 420, 1, detail::CueMagenta);
        }
        else
        {
            u.store = 0;
            begin(A::Write, 2.6);
            addHazard(ZoneKind::Whirl, mid, mid, span * .45, 2.6, 0, 0, u.id);
            foeCue(mid, mid, span * .45, 2.6, detail::CueWater);
        }
        return;
    }
    case Foe::Editor:
    {
        // The gilded revolver aims a red line and fires six bleeding rounds; the paper blade blinks behind a player and
        // cuts three crosses; with both rubbed out the editor can only shoulder-charge and stamp small rings of ink.
        if (is(A::Aim))
        {
            u.velocity = {};
            if (u.actClock > .2 && sees)
                u.aim = to.normalized();
            if (done())
            {
                begin(A::Spin, .84);
                u.store = 0;
            }
            return;
        }
        if (is(A::Spin))
        {
            u.velocity = {};
            if ((u.store -= dt) <= 0 && (u.parts & 1))
            {
                u.store = .14;
                const Vec2 gun = foePartAt(u.breed, 0, u.position, u.aim, u.radius);
                foeShot(u.id, gun, u.aim, 20 * power, NibShot, 1900, 1800, static_cast<int>(ShotEffect::Bleed), 1);
            }
            if (done())
            {
                begin(A::Idle, 0);
                u.skillTime = 1.6;
            }
            return;
        }
        if (is(A::Write))
        {
            // A cross: two strokes meeting just ahead.
            u.velocity = {};
            if (done())
            {
                const Vec2 c = u.position + u.actAt * 220;
                for (const double a : {Pi / 4, -Pi / 4})
                {
                    const Vec2 d = detail::turned(u.actAt, a);
                    for (auto& v : units)
                        if (v.alive && v.team != u.team && distanceToSegment(v.position, c - d * 450, c + d * 450) < 40 + v.radius)
                        {
                            damageUnit(u.id, v.id, 26 * power);
                            if (v.player())
                            {
                                v.kit.bleed = std::max(v.kit.bleed, BleedSeconds);
                                v.kit.bleedDamage = std::max(v.kit.bleedDamage, 5 * power);
                            }
                        }
                }
                Effect e{c, c, 450, .4, EventEffect, 2};
                e.style = EffectStyle::FoeBurst;
                e.pulse = 8;
                addEffect(e);
                if (++u.acts < 3 && (u.parts & 2))
                {
                    u.actAt = to.normalized();
                    begin(A::Write, .45);
                    const Vec2 c2 = u.position + u.actAt * 220;
                    for (const double a : {Pi / 4, -Pi / 4})
                        foeCue(c2 - detail::turned(u.actAt, a) * 450, c2 + detail::turned(u.actAt, a) * 450, 40, .45, detail::CueInk);
                }
                else
                {
                    begin(A::Idle, 0);
                    u.skillTime = 1.8;
                }
            }
            return;
        }
        if (is(A::Windup))
        {
            u.velocity = {};
            if (done())
                begin(A::Dash, .8);
            return;
        }
        if (is(A::Dash))
        {
            if (!foeDash(u, u.actAt, 760, 34 * power, 0, dt) || done())
            {
                blast(u.id, u.team, u.position, 160, 12 * power, .4, EffectStyle::BossSlam);
                begin(A::Idle, 0);
                u.skillTime = 1.6;
            }
            return;
        }
        if (u.skillTime > 0)
            return foeKeep(u, t, 300, 700, speed, dt);
        int options[3], count = 0;
        if (u.parts & 1)
            options[count++] = 0;
        if (u.parts & 2)
            options[count++] = 1;
        if (count < 2)
            options[count++] = 2;
        const int pick = options[randomStep(rng) % static_cast<uint64_t>(count)];
        if (pick == 0)
        {
            begin(A::Aim, .9);
            foeCue(u.position, u.position + to.normalized() * 1800, 12, .9, detail::CueDanger);
        }
        else if (pick == 1)
        {
            // Blinks behind its target.
            const Vec2 behind = t.position - t.aim.normalized() * 200;
            if (maze.walkable(behind, u.radius))
            {
                event(EffectStyle::Dash, u.position, behind, 2, u.radius, .4);
                u.position = behind;
            }
            u.acts = 0;
            u.actAt = (t.position - u.position).normalized();
            begin(A::Write, .35);
            const Vec2 c = u.position + u.actAt * 220;
            for (const double a : {Pi / 4, -Pi / 4})
                foeCue(c - detail::turned(u.actAt, a) * 450, c + detail::turned(u.actAt, a) * 450, 40, .35, detail::CueInk);
        }
        else
        {
            begin(A::Windup, .6);
            u.actAt = to.normalized();
            foeCue(u.position, u.position + u.actAt * 600, u.radius * .8, .6, detail::CueDanger);
        }
        return;
    }
    case Foe::Warlord:
    {
        // The four-colour gatling clicks through its inks; the carbon shield turns shots back two for one; hurt or
        // missing a part, the warlord bounces off the walls on its head springs.
        if (is(A::Guard))
        {
            foeSteer(u, t.position, 140, dt);
            if (done())
            {
                begin(A::Idle, 0);
                u.skillTime = 1.4;
            }
            return;
        }
        if (is(A::Dash))
        {
            Vec2 dir = u.actAt.normalized();
            const double step = 620 * dt + 6;
            if (!maze.walkable(u.position + dir * step, u.radius))
            {
                const Vec2 fx{-dir.x, dir.y}, fy{dir.x, -dir.y};
                dir = maze.walkable(u.position + fx * step, u.radius) ? fx : maze.walkable(u.position + fy * step, u.radius) ? fy : dir * -1.;
                u.actAt = dir;
                u.struck = 0;
                event(EffectStyle::Shove, u.position + dir * u.radius, u.position + dir * (u.radius + 60), 2, 200, .3);
            }
            foeDash(u, u.actAt, 620, 24 * power, 0, dt);
            if (done())
            {
                begin(A::Idle, 0);
                u.skillTime = 1.6;
            }
            return;
        }
        if (u.act >= static_cast<int>(A::Blue))
        {
            u.velocity = {};
            u.act = static_cast<int>(A::Blue) + std::clamp(static_cast<int>((3 - u.actClock) / .75), 0, 3);
            if (sees)
                u.aim = (u.aim + (to.normalized() - u.aim) * std::min(1., dt * 2)).normalized();
            if ((u.store -= dt) <= 0)
            {
                const Vec2 gun = foePartAt(u.breed, 0, u.position, u.aim, u.radius);
                switch (static_cast<A>(u.act))
                {
                case A::Blue:
                    foeShot(u.id, gun, detail::turned(u.aim, (random01(rng) - .5) * .25), 6 * power, BeadShot, 1300, 1200);
                    u.store = .08;
                    break;
                case A::Red:
                    foeLob(u, t.position, 14 * power, .8, 0);
                    u.store = .5;
                    break;
                case A::Green:
                    foeShot(u.id, gun, detail::turned(u.aim, (random01(rng) - .5) * .3), 7 * power, DropShot, 750, 1100, static_cast<int>(ShotEffect::Bleed));
                    u.store = .2;
                    break;
                default:
                    foeShot(u.id, gun, u.aim, 16 * power, HeavyShot, 1500, 1400, 0, 2);
                    u.store = .3;
                    break;
                }
            }
            if (done() || !(u.parts & 1))
            {
                begin(A::Idle, 0);
                u.skillTime = 1.6;
            }
            return;
        }
        if (u.skillTime > 0)
            return foeKeep(u, t, 300, 650, speed, dt);
        const bool hurt = u.parts != 3 || u.hp < u.maxHp * .5;
        int options[3], count = 0;
        if (u.parts & 1)
            options[count++] = 0;
        if (u.parts & 2)
            options[count++] = 1;
        if (hurt || count == 0)
            options[count++] = 2;
        const int pick = options[randomStep(rng) % static_cast<uint64_t>(count)];
        if (pick == 0)
        {
            begin(A::Blue, 3);
            u.store = 0;
        }
        else if (pick == 1)
            begin(A::Guard, 3);
        else
        {
            begin(A::Dash, 3);
            u.actAt = detail::turned(to.normalized(), (random01(rng) - .5) * .8);
        }
        return;
    }
    case Foe::Sage:
    {
        const bool dragon = (u.parts & PartsSecondForm) != 0;
        if (!dragon && u.hp < u.maxHp * .5)
        {
            // Below half: the robe tears away and the sage is a dragon of wild cursive (no tools left to rub out).
            u.parts = PartsSecondForm;
            u.radius = info.radius * 1.15;
            begin(A::Idle, 0);
            u.skillTime = 1.5;
            u.stun = 1;
            event(EffectStyle::BossPhase, u.position, u.position, 2, u.breed, 1.2);
            return;
        }
        if (!dragon)
        {
            // The brass paperweight crashes down, pinning the floor round it and throwing up eight ink spikes; the
            // brush writes soldiers (兵) and a shield (盾) to fight for it.
            if (is(A::Leap))
            {
                u.velocity = {};
                if (done())
                {
                    blast(u.id, u.team, u.actAt, 220, 28 * power, .45, EffectStyle::BossSlam);
                    addHazard(ZoneKind::Ink, u.actAt, u.actAt, 200, 3, 1, 0, u.id);
                    foeRing(u.id, u.actAt, 30, 8, random01(rng), 10 * power, BlotShot, 520, 800, 0);
                    begin(A::Idle, 0);
                    u.skillTime = 2;
                }
                return;
            }
            if ((u.store += dt) >= 14)
            {
                u.store = 0;
                int guards = 0;
                for (const auto& v : units)
                    guards += v.alive && (v.breed == static_cast<int>(Foe::GuardBing) || v.breed == static_cast<int>(Foe::GuardDun));
                if (guards < 4)
                {
                    for (const Foe g : {Foe::GuardBing, Foe::GuardBing, Foe::GuardDun})
                        spawnFoe(g, u.room, maze.move(u.position, detail::turned(u.aim, (random01(rng) - .5) * 3) * 180, 30));
                    Effect e{u.position, u.position, u.radius, .6, EventEffect, 2};
                    e.style = EffectStyle::FoeBurst;
                    e.pulse = 7;
                    addEffect(e);
                }
            }
            if (u.skillTime > 0)
            {
                if (sees && u.hitClock <= 0)
                {
                    foeFan(u, t.position, 3, .4, 10 * power, BlotShot, 600, 1000);
                    u.hitClock = 1.4;
                }
                return foeKeep(u, t, 300, 650, speed, dt);
            }
            if (u.parts & 1)
            {
                begin(A::Leap, 1);
                u.actAt = t.position;
                foeCue(t.position, t.position, 220, 1, detail::CueSeal);
                return;
            }
            u.skillTime = 1.5;
            return;
        }
        // The dragon: S-sweeps across the room that leave rivers of ink, and three bursting characters (狂, 乱, 墨);
        // at the walls it drinks the floor's spare ink for a shield.
        if (is(A::Dash))
        {
            const Vec2 heading = u.actAt - u.position;
            const bool moving = heading.length() >= 60 && foeDash(u, heading, 720, 26 * power, 0, dt);
            if ((u.actClock -= dt) <= 0 || !moving)
            {
                if (++u.acts < 4)
                {
                    u.actAt = mid + Vec2{(u.acts % 2 ? .38 : -.38) * span, (-.3 + .2 * u.acts) * span};
                    u.actClock = 1.6;
                    u.struck = 0;
                }
                else
                {
                    begin(A::Idle, 0);
                    u.skillTime = 1.8;
                }
            }
            if (heading.lengthSquared() > 1)
                u.aim = heading.normalized();
            if ((u.store += dt) >= .18)
            {
                u.store = 0;
                addHazard(ZoneKind::Ink, u.position - u.aim * 120, u.position, 55, 6, 0, 14 * power, u.id);
            }
            return;
        }
        if (u.skillTime > 0)
        {
            if (sees && u.hitClock <= 0)
            {
                foeFan(u, t.position, 5, .9, 9 * power, BlotShot, 640, 1100);
                u.hitClock = 1.1;
            }
            if (room && u.armor <= 0 && (u.position - mid).length() > span * .32 && u.hp < u.maxHp)
            {
                u.armor = u.maxHp * .08;
                Effect e{u.position, u.position, u.radius, .6, EventEffect, 2};
                e.style = EffectStyle::FoeBurst;
                e.pulse = 9;
                addEffect(e);
            }
            return foeKeep(u, t, 300, 700, speed * 1.4, dt);
        }
        if (randomStep(rng) % 2)
        {
            begin(A::Dash, 1.6);
            u.acts = 0;
            u.actAt = mid + Vec2{-span * .38, -span * .3};
        }
        else
        {
            for (int k = 0; k < 3; ++k)
            {
                const Vec2 at = t.position + Vec2{(random01(rng) - .5) * 300, (random01(rng) - .5) * 300};
                foeLob(u, at, 14 * power, 1.1 + k * .25, static_cast<int>(ShotEffect::Burst8));
            }
            u.skillTime = 2.2;
        }
        return;
    }
    default:
        return foeSteer(u, t.position, speed, dt);
    }
}

// ---- Hooks from the hit, shot and death code ----
// A hit on a foe: hatched fronts, bulk, thorns, broken spins, parries, ink rain; the rainbow is out of reach while split.
inline double Battle::foeHitTaken(Unit* attacker, Unit& v, double damage)
{
    using A = FoeAct;
    const Foe foe = static_cast<Foe>(std::clamp(v.breed, 0, FoeCount - 1));
    const Vec2 from = attacker ? attacker->position : v.position;
    const Vec2 d = from - v.position;
    const bool front = d.lengthSquared() > 1 && v.aim.normalized().dot(d.normalized()) > .5;
    auto window = [&](double seconds)
    {
        if (v.hitClock <= 0)
        {
            v.hits = 0;
            v.hitClock = seconds;
        }
        ++v.hits;
    };
    const double power = expeditionPower(v);
    switch (foe)
    {
    case Foe::Shield:
    case Foe::GuardDun:
        if (v.act != static_cast<int>(A::Broken) && front)
        {
            v.store += damage * .6;
            damage *= .4;
            if (foe == Foe::Shield && v.store >= 140)
            {
                v.act = static_cast<int>(A::Broken);
                Effect e{v.position, v.position, v.radius, .5, EventEffect, 2};
                e.style = EffectStyle::FoeBurst;
                e.pulse = 5;
                addEffect(e);
            }
        }
        break;
    case Foe::Crumbler:
        damage *= .5;
        if (random01(rng) < .25)
        {
            const double a = random01(rng) * 2 * Pi;
            const Vec2 at = maze.move(v.position, Vec2{std::cos(a), std::sin(a)} * (v.radius + 60), 20);
            addHazard(ZoneKind::Crumbs, at, at, 60, 6, 0, 0, v.id);
        }
        break;
    case Foe::Crab:
        if (++v.hits % 3 == 0)
        {
            v.act = static_cast<int>(A::Spikes);
            v.actClock = .4;
            blast(v.id, v.team, v.position, v.radius + 110, 12 * power, .35, EffectStyle::KillBlast);
        }
        break;
    case Foe::Pinwheel:
        window(1);
        if (v.hits >= 5 && v.act == static_cast<int>(A::Spin))
        {
            v.stun = 1.5;
            v.act = static_cast<int>(A::Idle);
            v.hits = 0;
        }
        break;
    case Foe::Swordsaint:
        window(1.5);
        if (v.hits >= 6 && v.act != static_cast<int>(A::Parry))
        {
            v.act = static_cast<int>(A::Parry);
            v.actClock = .8;
            v.hits = 0;
        }
        break;
    case Foe::Inspector:
        if (++v.hits % 10 == 0)
            for (const auto& p : units)
                if (p.alive && p.player() && p.team != v.team && (p.position - v.position).length() < 1600)
                    foeLob(v, p.position, 18 * power, 1.2, 0);
        break;
    case Foe::Leadcannon:
        if (v.act == static_cast<int>(A::Jam))
            damage *= 1.25;
        break;
    case Foe::Rainbow:
        if (v.act == static_cast<int>(A::Split))
            damage = 0;
        break;
    default:
        break;
    }
    return damage;
}
// A foe's hit on a player: a red circle and a glass floor make it hurt more; saws and nibs cut, wisps smear.
inline double Battle::foeHitPlayer(Unit* attacker, Unit& v, double damage)
{
    if (v.kit.marked > 0)
        damage *= 1.2;
    if (attacker && attacker->kind == UnitKind::Monster && attacker->breed >= 0)
    {
        const Foe foe = static_cast<Foe>(std::clamp(attacker->breed, 0, FoeCount - 1));
        if (foe == Foe::Saw || foe == Foe::Lancer || foe == Foe::Swordsaint)
        {
            v.kit.bleed = std::max(v.kit.bleed, 1.5);
            v.kit.bleedDamage = std::max(v.kit.bleedDamage, 4 * expeditionPower(*attacker));
        }
        if (foe == Foe::Wisp)
            v.kit.smear = std::max(v.kit.smear, SmearSeconds);
    }
    return damage;
}
// A player's shot meets a foe: 0 it lands, 1 it is lost (a fan, a split body), 2 it was turned back (the shot now flies
// back at its shooter's side).
inline int Battle::foeDeflect(Projectile& p, Unit& target, Vec2 at)
{
    using A = FoeAct;
    const Foe foe = static_cast<Foe>(std::clamp(target.breed, 0, FoeCount - 1));
    const Vec2 incoming = (p.velocity * -1.).normalized();
    const double facing = target.aim.normalized().dot(incoming);
    auto turnBack = [&](int copies)
    {
        p.velocity = p.velocity * -1.;
        p.team = target.team;
        p.owner = p.squad = target.id;
        p.hit.clear();
        p.hit.push_back(target.id);
        p.nib = -1;
        // The carbon shield copies what it turns back: a second shot a little to the side.
        for (int k = 1; k < copies; ++k)
        {
            Projectile copy = p;
            copy.id = nextProjectile++;
            copy.velocity = detail::turned(p.velocity, .14 * k);
            expedition.echoes.push_back({0, copy});
        }
        event(EffectStyle::Shield, at, at, target.team, 70, .3);
        return 2;
    };
    switch (foe)
    {
    case Foe::Parasol:
        if (facing > .64 && p.damage < 30)
        {
            event(EffectStyle::Shield, at, at, target.team, 50, .25);
            return 1;
        }
        return 0;
    case Foe::Mirror:
        if (target.act != static_cast<int>(A::Open) && p.damage >= 18)
        {
            target.act = static_cast<int>(A::Open);
            target.actClock = 4;
            return turnBack(1);
        }
        return 0;
    case Foe::Swordsaint:
        return target.act == static_cast<int>(A::Parry) ? turnBack(1) : 0;
    case Foe::Warlord:
        return target.act == static_cast<int>(A::Guard) && (target.parts & 2) && facing > .5 ? turnBack(2) : 0;
    case Foe::Rainbow:
        return target.act == static_cast<int>(A::Split) ? 1 : 0;
    default:
        return 0;
    }
}
// An enemy shot hits a player (bleeding, a chill, a smear, a jolt), or a reforged nib's shot hits a foe.
inline void Battle::expeditionShotHit(const Projectile& p, Unit& target)
{
    if (p.team == 2 && target.player())
    {
        Kit& k = target.kit;
        const Unit* shooter = find(p.owner);
        const double power = shooter ? expeditionPower(*shooter) : 1;
        switch (static_cast<ShotEffect>(p.effect))
        {
        case ShotEffect::Bleed:
            k.bleed = std::max(k.bleed, BleedSeconds);
            k.bleedDamage = std::max(k.bleedDamage, 4 * power);
            break;
        case ShotEffect::Slow:
            k.chill = std::max(k.chill, 1.2);
            break;
        case ShotEffect::Smear:
            k.smear = std::max(k.smear, SmearSeconds);
            break;
        case ShotEffect::Stun:
            k.jolt = std::max(k.jolt, .35);
            break;
        default:
            break;
        }
        if (p.weapon == BlotShot)
            k.smear = std::max(k.smear, 1.);
        return;
    }
    // A quality gun's trick (Expedition.h GunTrick; a crayon round's colour picks one of four).
    if (p.team == 1 && p.trick > 0 && target.kind == UnitKind::Monster && p.variant >= 0)
    {
        const GunVariant& gun = gunVariant(p.variant);
        GunTrick trick = static_cast<GunTrick>(p.trick);
        double power = gun.power;
        if (trick == GunTrick::Prism)
        {
            static const GunTrick Colours[4] = {GunTrick::Burn, GunTrick::Chill, GunTrick::Chain, GunTrick::Pierce};
            trick = Colours[std::clamp(p.prism, 0, 3)];
            power = trick == GunTrick::Burn ? PrismBurn : trick == GunTrick::Chill ? 1. : trick == GunTrick::Chain ? .35 : 0.;
        }
        const bool boss = bossFoe(target.breed);
        switch (trick)
        {
        case GunTrick::Pin:
            if (!boss && target.alive && random01(rng) < power)
                target.stun = std::max(target.stun, StunPin);
            break;
        case GunTrick::Chill:
            if (target.alive)
            {
                target.slowTime = std::max(target.slowTime, power * (boss ? .5 : 1));
                target.wet = std::max(target.wet, 2.5);
            }
            break;
        case GunTrick::Burn:
            if (target.alive)
            {
                target.burnTime = std::max(target.burnTime, BurnSeconds);
                target.burnDamage = std::max(target.burnDamage, power);
                target.burnOwner = p.owner;
            }
            break;
        case GunTrick::Chain:
        {
            // Arcs on to the nearest few in sight of the one hit (not back to it).
            int arcs = 0;
            for (auto& other : units)
            {
                if (!enemy(other) || other.id == target.id || (other.position - target.position).length() > ChainReach + other.radius ||
                    !maze.clear(target.position, other.position, 4))
                    continue;
                Effect arc{target.position, other.position, 0, .18, 2, p.team};
                arc.style = EffectStyle::Chain;
                addEffect(arc);
                damageUnit(p.owner, other.id, p.damage * power);
                if (++arcs >= (static_cast<GunTrick>(p.trick) == GunTrick::Prism ? 1 : ChainCount))
                    break;
            }
            break;
        }
        case GunTrick::Absorb:
            // Blotting paper (2026-11-17): a share of the damage soaks into a shield on the shooter, up to a fifth of
            // their max health.
            if (Unit* shooter = find(p.owner); shooter && shooter->alive && shooter->player())
            {
                const double cap = shooter->maxHp * AbsorbCap;
                if (shooter->kit.blot < cap)
                    shooter->kit.blot = std::min(cap, shooter->kit.blot + p.damage * power);
            }
            break;
        default:
            break;
        }
    }
    // A spiked round (a real staple, a real thumbtack; 2026-11-17) sets the enemy bleeding now and then: a point of
    // health a second for SpikeSeconds, a fresh wound restarting the clock.
    if (p.team == 1 && target.kind == UnitKind::Monster && target.alive && spikeRound(p.variant) && random01(rng) < SpikeChance)
    {
        if (target.bleedTime <= 0)
            target.bleedTick = 0;
        target.bleedTime = SpikeSeconds;
        target.bleedOwner = p.owner;
        event(EffectStyle::Bleed, target.position, target.position, p.team, target.radius, .4);
    }
    if (p.team == 1 && p.nib >= 0 && target.kind == UnitKind::Monster && target.alive)
        switch (static_cast<PenBiome>(p.nib))
        {
        case PenBiome::Pencil:
            // Graphite smudges: now and then the target loses a beat.
            if (!bossFoe(target.breed) && random01(rng) < .15)
                target.stun = std::max(target.stun, .5);
            break;
        case PenBiome::Watercolor:
            target.slowTime = std::max(target.slowTime, 1.);
            target.wet = std::max(target.wet, 2.);
            break;
        case PenBiome::Brush:
            if (!bossFoe(target.breed))
                target.impulse += p.velocity.normalized() * 260;
            break;
        default:
            break;
        }
}
// A lobbed blob comes down: a splash, and what it leaves.
inline void Battle::foeLobLanded(const Projectile& p)
{
    const ShotEffect effect = static_cast<ShotEffect>(p.effect);
    blast(p.owner, 2, p.position, FoeLobBlast, p.damage, .4, effect == ShotEffect::Rock ? EffectStyle::Fragment : EffectStyle::KillBlast);
    switch (effect)
    {
    case ShotEffect::Puddle:
        addHazard(ZoneKind::Puddle, p.position, p.position, 90, 5);
        break;
    case ShotEffect::PaintA:
    case ShotEffect::PaintB:
        addHazard(ZoneKind::Paint, p.position, p.position, 120, 4, effect == ShotEffect::PaintB ? 1 : 0);
        break;
    case ShotEffect::Burst8:
        foeRing(p.owner, p.position, 20, 8, random01(rng), p.damage * .7, BlotShot, 520, 800, 0);
        break;
    default:
        break;
    }
}
// What falling leaves behind: bursts, payouts, the thief's sack, the mash-up's prize.
inline void Battle::foeDeath(const Unit& dead, int attacker)
{
    const Foe foe = static_cast<Foe>(std::clamp(dead.breed, 0, FoeCount - 1));
    const double power = expeditionPower(dead), reward = tierInfo(config.tier).reward;
    Unit* credited = creditFor(attacker);
    switch (foe)
    {
    case Foe::Scribble:
        addHazard(ZoneKind::Fog, dead.position, dead.position, 150, 2);
        break;
    case Foe::InkBug:
    {
        foeRing(dead.id, dead.position, dead.radius, 8, random01(rng), 13 * power, NibShot, 640, 700, 0);
        blast(dead.id, dead.team, dead.position, 90, 10 * power, .35, EffectStyle::KillBlast);
        // Other bugs caught in the burst go off too.
        for (auto& v : units)
            if (v.alive && v.id != dead.id && v.breed == static_cast<int>(Foe::InkBug) && (v.position - dead.position).length() < 170 &&
                v.act != static_cast<int>(FoeAct::Fuse))
            {
                v.act = static_cast<int>(FoeAct::Fuse);
                v.actClock = .25;
            }
        break;
    }
    case Foe::Sponge:
    {
        const int soaked = static_cast<int>(dead.store);
        blast(dead.id, dead.team, dead.position, 140 + 20 * soaked, (10 + 5 * soaked) * power, .45, EffectStyle::KillBlast);
        for (int k = 0; k < std::min(5, soaked); ++k)
        {
            const double a = k * 2 * Pi / std::max(1, std::min(5, soaked));
            const Vec2 at = maze.move(dead.position, Vec2{std::cos(a), std::sin(a)} * 140, 20);
            addHazard(ZoneKind::Puddle, at, at, 90, 5);
        }
        break;
    }
    case Foe::Palette:
        for (int k = 0; k < 3; ++k)
        {
            const double a = k * 2 * Pi / 3;
            const Vec2 at = maze.move(dead.position, Vec2{std::cos(a), std::sin(a)} * 120, 20);
            addHazard(ZoneKind::Spring, at, at, 110, 6);
        }
        break;
    case Foe::Thief:
    {
        const int haul = tierStickers((40 + random01(rng) * 40) * shareScale(), reward, rng) + static_cast<int>(dead.store);
        for (int k = 0; k < 4; ++k)
            scatterLoot(LootKind::Stickers, 0, haul / 4 + (k < haul % 4 ? 1 : 0), dead.position, -1, 480);
        if (credited && credited->player())
            credited->stats.feats |= featBit(Feat::Thief);
        break;
    }
    case Foe::Mashup:
        if (random01(rng) < .3)
            scatterLoot(LootKind::Item, static_cast<int>(Gear::Eraser), 1, dead.position, -1, 300);
        else
        {
            const int w = randomWeapon();
            scatterLoot(LootKind::Weapon, w, magazine(w, false), dead.position, -1, 300);
        }
        break;
    default:
        break;
    }
    if (eliteFoe(dead.breed) && credited && credited->player())
        ++credited->stats.elites;
}

// ---- Hazards and terrain on the floor ----
inline void Battle::updateHazards(double dt)
{
    auto& x = expedition;
    std::vector<Unit*> party;
    for (auto& u : units)
        if (u.kind == UnitKind::Fighter && u.player() && u.alive && !u.kit.extracted)
        {
            Kit& k = u.kit;
            k.drag = k.boost = 1;
            k.fogged = k.dryBrush = false;
            k.marked = std::max(0., k.marked - dt);
            k.sealed = std::max(0., k.sealed - dt);
            k.smear = std::max(0., k.smear - dt);
            k.streak = std::max(0., k.streak - dt);
            k.chill = std::max(0., k.chill - dt);
            k.jolt = std::max(0., k.jolt - dt);
            if (k.chill > 0)
                k.drag = std::min(k.drag, .7);
            if (k.jolt > 0)
                k.drag = std::min(k.drag, .35);
            if (k.bleed > 0)
            {
                k.hurt += k.bleedDamage * std::min(dt, k.bleed);
                k.bleed = std::max(0., k.bleed - dt);
            }
            party.push_back(&u);
        }
    std::vector<bool> charged(party.size(), false);
    for (auto& z : x.zones)
    {
        const ZoneKind kind = static_cast<ZoneKind>(z.kind);
        if (z.kind < 10)
            continue;
        if (kind == ZoneKind::Disk && z.life > 0)
            z.radius = z.charge * std::clamp(z.remaining / z.life, 0., 1.);
        if (kind == ZoneKind::Puddle)
            z.charge = std::max(0., z.charge - dt);
        for (size_t i = 0; i < party.size(); ++i)
        {
            Unit& u = *party[i];
            Kit& k = u.kit;
            if (!detail::inZone(z, u.position, u.radius * .5))
                continue;
            switch (kind)
            {
            case ZoneKind::Fog:
            case ZoneKind::Dust:
                k.fogged = true;
                break;
            case ZoneKind::Puddle:
                k.drag = std::min(k.drag, .8);
                if (z.charge > 0)
                    k.hurt += 14 * dt;
                break;
            case ZoneKind::Paint:
                k.drag = std::min(k.drag, .85);
                k.hurt += z.dps * dt;
                // Two colours together blind.
                for (const auto& o : x.zones)
                    if (&o != &z && o.kind == z.kind && o.what != z.what && detail::inZone(o, u.position, 0))
                        k.smear = std::max(k.smear, 1.);
                break;
            case ZoneKind::Ribbon:
                k.drag = std::min(k.drag, .7);
                break;
            case ZoneKind::Ink:
                k.drag = std::min(k.drag, z.what == 1 ? .45 : .75);
                k.hurt += z.dps * dt;
                break;
            case ZoneKind::Crumbs:
            {
                const double rub = 15 * dt;
                k.graphite = std::max(0., k.graphite - rub);
                k.tape = std::max(0., k.tape - rub);
                break;
            }
            case ZoneKind::Track:
                k.boost = std::max(k.boost, 1.2);
                break;
            case ZoneKind::Disk:
                k.hurt += z.dps * dt;
                break;
            case ZoneKind::Seal:
                k.sealed = std::max(k.sealed, SealSeconds);
                break;
            case ZoneKind::Spring:
                heal(u, 12 * dt);
                break;
            case ZoneKind::Whirl:
                if ((z.from - u.position).length() > 40)
                    u.impulse += (z.from - u.position).normalized() * 900 * dt;
                break;
            case ZoneKind::Clean:
                k.bleed = 0;
                k.smear = 0;
                k.marked = 0;
                break;
            case ZoneKind::Cinnabar:
                charged[i] = true;
                break;
            case ZoneKind::DryBrush:
                k.dryBrush = true;
                break;
            case ZoneKind::Thorns:
                k.bleed = std::max(k.bleed, 1.5);
                k.bleedDamage = std::max(k.bleedDamage, 4.);
                break;
            default:
                break;
            }
        }
    }
    for (size_t i = 0; i < party.size(); ++i)
    {
        Unit& u = *party[i];
        Kit& k = u.kit;
        // A cinnabar ring charges a critical streak after a second and a half (enemies come for whoever has it).
        if (charged[i])
        {
            if ((k.cinnabar += dt) >= 1.5 && k.streak <= 0)
            {
                k.streak = 5;
                k.cinnabar = 0;
                Effect e{u.position, u.position, 130, .6, EventEffect, u.team};
                e.style = EffectStyle::FoeBurst;
                e.pulse = 10;
                addEffect(e);
            }
        }
        else
            k.cinnabar = 0;
        // The floor's bites land every half second.
        k.hurtClock -= dt;
        if (k.hurt >= 1 && k.hurtClock <= 0)
        {
            const double bite = k.hurt;
            k.hurt = 0;
            k.hurtClock = .5;
            damageUnit(-1, u.id, bite);
        }
    }
    // Enemy shots fired on carbon paper (and shots a carbon shield turned back) come again.
    for (auto& echo : x.echoes)
    {
        echo.delay -= dt;
        if (echo.delay <= 0 && projectiles.size() < 768)
        {
            Projectile p = echo.shot;
            p.id = nextProjectile++;
            projectiles.push_back(p);
        }
    }
    x.echoes.erase(std::remove_if(x.echoes.begin(), x.echoes.end(), [](const ExpeditionState::Echo& e) { return e.delay <= 0; }), x.echoes.end());
}
// A shot passing an ink sac bursts it: the enemies round it are splashed, and the party's pens fill a little.
inline void Battle::popSacs(const Projectile& p, Vec2 from, Vec2 to)
{
    for (auto& z : expedition.zones)
        if (z.kind == static_cast<int>(ZoneKind::Sac) && z.remaining > 0 && distanceToSegment(z.from, from, to) < z.radius + p.radius)
        {
            z.remaining = 0;
            blast(p.owner, 1, z.from, 220, 45, .45, EffectStyle::Combustion);
            for (auto& u : units)
                if (u.human && u.alive && !u.kit.extracted)
                    u.ink = std::min(u.maxInk, u.ink + 25);
            Effect e{z.from, z.from, 220, .6, EventEffect, 1};
            e.style = EffectStyle::FoeBurst;
            e.pulse = 11;
            addEffect(e);
        }
}
// The biome's terrain in a fight room (design 3.3): graphite dust (pencil, whose pillars are dashed pencil walls as
// well), puddles and a clean pool (watercolor), thorny pillars and ink sacs (fountain pen), rolling tracks and carbon
// paper (ballpoint), a cinnabar ring and dry-brush strokes (brush).
inline void Battle::layTerrain(int index, uint64_t& roll)
{
    auto& x = expedition;
    const DungeonRoom& r = x.rooms[static_cast<size_t>(index)];
    if (!(r.kind == RoomKind::Combat || r.kind == RoomKind::Portal))
        return;
    const double cell = maze.cellSize;
    auto spot = [&](double margin)
    {
        for (int tries = 0; tries < 24; ++tries)
        {
            const Vec2 p{(r.x + margin + random01(roll) * (r.w - 2 * margin)) * cell, (r.y + margin + random01(roll) * (r.h - 2 * margin)) * cell};
            if (maze.walkable(p, 60))
                return p;
        }
        return roomCenter(r);
    };
    auto lay = [&](ZoneKind kind, Vec2 from, Vec2 to, double radius, int what = 0)
    {
        ExpeditionState::Zone z;
        z.kind = static_cast<int>(kind);
        z.what = what;
        z.from = from;
        z.to = to;
        z.radius = radius;
        z.remaining = z.life = 1e9;
        x.zones.push_back(z);
    };
    // The portal's middle stays clear of terrain.
    switch (x.biome())
    {
    case PenBiome::Pencil:
    {
        const int strips = 1 + static_cast<int>(randomStep(roll) % 2);
        for (int k = 0; k < strips; ++k)
        {
            const Vec2 a = spot(1.2);
            const double angle = random01(roll) * Pi;
            const Vec2 d{std::cos(angle), std::sin(angle)};
            lay(ZoneKind::Dust, a - d * cell * .8, a + d * cell * .8, 55);
        }
        break;
    }
    case PenBiome::Watercolor:
        for (int k = 0; k < 2; ++k)
        {
            const Vec2 a = spot(1.3);
            lay(ZoneKind::Puddle, a, a, 100);
        }
        {
            const Vec2 a = spot(1.5);
            lay(ZoneKind::Clean, a, a, 110);
        }
        break;
    case PenBiome::Fountain:
    {
        for (int y = r.y; y < r.y + r.h; ++y)
            for (int xx = r.x; xx < r.x + r.w; ++xx)
            {
                const int id = y * maze.width + xx;
                if (maze.isFixed(id) && maze.walls[static_cast<size_t>(id)] > 0)
                {
                    const Vec2 c = maze.center(id);
                    lay(ZoneKind::Thorns, c, c, cell * .5 + 22);
                }
            }
        const int sacs = 1 + static_cast<int>(randomStep(roll) % 2);
        for (int k = 0; k < sacs; ++k)
        {
            const Vec2 a = spot(1.2);
            lay(ZoneKind::Sac, a, a, 44);
        }
        break;
    }
    case PenBiome::Ballpoint:
    {
        const Vec2 a = spot(1.5);
        const bool across = random01(roll) < .5;
        const Vec2 d = across ? Vec2{1, 0} : Vec2{0, 1}, side{-d.y, d.x};
        for (double s : {-30., 30.})
            lay(ZoneKind::Track, a - d * cell * 1.4 + side * s, a + d * cell * 1.4 + side * s, 14);
        const Vec2 c = spot(1.4);
        lay(ZoneKind::Carbon, c, c, 120);
        break;
    }
    case PenBiome::Brush:
    {
        const Vec2 c = spot(1.6);
        lay(ZoneKind::Cinnabar, c, c, 130);
        const int strokes = 1 + static_cast<int>(randomStep(roll) % 2);
        for (int k = 0; k < strokes; ++k)
        {
            const Vec2 a = spot(1.3);
            const double angle = random01(roll) * Pi;
            const Vec2 d{std::cos(angle), std::sin(angle)};
            lay(ZoneKind::DryBrush, a - d * cell * 1.1, a + d * cell * 1.1, 50);
        }
        break;
    }
    default:
        break;
    }
}
} // namespace gwgame
