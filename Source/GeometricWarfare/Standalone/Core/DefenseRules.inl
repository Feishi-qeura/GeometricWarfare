#pragma once
#include <cstring>
namespace gwgame
{
inline bool Battle::startDefense(uint64_t seed, int level)
{
    const auto& levels = defenseLevels();
    if (levels.empty())
        return false;
    const int index = std::clamp(level, 0, static_cast<int>(levels.size()) - 1);
    if (!openPage(seed, levels[index], index))
        return false;
    for (int i = 0; i < 4; ++i)
        spawnTriangle(defense.core, 460);
    flushUnits();
    return true;
}
inline bool Battle::openPage(uint64_t seed, const DefenseLevel& page, int level)
{
    const int w = static_cast<int>(std::strlen(page.rows[0])), h = static_cast<int>(page.rows.size());
    for (const char* row : page.rows)
        if (static_cast<int>(std::strlen(row)) != w)
            return false;
    config.rules = RuleSet::Defense;
    rng = seed ? seed : 1;
    resetRun();
    defense = DefenseState{};
    std::vector<int> cells(static_cast<size_t>(w * h), 0);
    defense.buildable.assign(cells.size(), 0);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            const int id = y * w + x;
            switch (page.rows[y][x])
            {
            case '#':
                cells[id] = 2;
                break;
            case '.':
                defense.buildable[id] = 1;
                break;
            case 'S':
                defense.entrances.push_back(id);
                break;
            case 'C':
                defense.coreCell = id;
                break;
            default:
                break;
            }
        }
    maze.fromGrid(w, h, 400, cells);
    maze.seed = rng;
    defense.inkPaid.assign(cells.size(), 0);
    if (!routeOpen(maze, defense.entrances, defense.coreCell))
        return false;
    defense.level = level;
    defense.waves = page.waves;
    defense.toughness = page.toughness;
    defense.core = maze.center(defense.coreCell);
    defense.coreHealth = defense.coreMaxHealth = config.coreHealth;
    defense.phase = DefensePhase::Build;
    defense.phaseTime = config.firstBuildSeconds;
    rebuildRoutes();
    navigationVersion = maze.version;
    return true;
}
inline double Battle::paintCost(int id, int cell) const
{
    const auto* p = find(id);
    if (!defenseMode() || finished || !p || !p->human || !p->alive || cell < 0 ||
        cell >= static_cast<int>(maze.walls.size()) || cell >= static_cast<int>(defense.buildable.size()))
        return -1;
    if ((defense.phase != DefensePhase::Build && defense.phase != DefensePhase::Wave) || !defense.buildable[cell] ||
        maze.isFixed(cell))
        return -1;
    const double integrity = maze.integrity(cell);
    // A repair keeps the wall's own type and costs the missing share of it.
    if (maze.walls[cell] > 0)
        return integrity >= .999 ? -1 : std::max(1., std::ceil(cellInk(*p, maze.kind(cell)) * (1 - integrity)));
    // A wall is never drawn over anyone standing in the cell, and never closes the last route to the core.
    const Vec2 lo{(cell % maze.width) * maze.cellSize, (cell / maze.width) * maze.cellSize};
    const Vec2 hi = lo + Vec2{maze.cellSize, maze.cellSize};
    for (const auto& u : units)
        if (u.alive)
        {
            const Vec2 nearest{std::clamp(u.position.x, lo.x, hi.x), std::clamp(u.position.y, lo.y, hi.y)};
            if ((u.position - nearest).lengthSquared() < u.radius * u.radius)
                return -3;
        }
    if (!routeOpen(maze, defense.entrances, defense.coreCell, cell))
        return -2;
    return cellInk(*p, static_cast<WallKind>(std::clamp(p->perks.pen, 0, PenCount - 1)));
}
inline bool Battle::paintWall(int id, int cell)
{
    auto* p = find(id);
    if (!p)
        return false;
    const double cost = paintCost(id, cell);
    // The refusal is a cue for that player only: radius carries the reason, pulse the player's unit.
    auto refuse = [&](int reason)
    {
        Effect e{maze.center(cell), maze.center(cell), static_cast<double>(reason), .6, EventEffect, p->team};
        e.style = EffectStyle::Denied;
        e.pulse = id;
        addEffect(e);
        return false;
    };
    if (cost <= -2)
        return refuse(cost <= -3 ? 3 : 2);
    if (cost < 0)
        return false;
    if (p->ink + 1e-9 < cost)
        return refuse(1);
    p->ink -= cost;
    const bool repair = maze.walls[cell] > 0;
    const WallKind kind = repair ? maze.kind(cell) : static_cast<WallKind>(std::clamp(p->perks.pen, 0, PenCount - 1));
    maze.setWall(cell, true, kind);
    if (defense.inkPaid.size() != maze.walls.size())
        defense.inkPaid.assign(maze.walls.size(), 0);
    // A cell is worth the cheapest full price anyone paid for it (what an eraser gets back a share of).
    const double price = cellInk(*p, kind);
    defense.inkPaid[cell] = repair && defense.inkPaid[cell] > 0 ? std::min(defense.inkPaid[cell], price) : price;
    if (!repair)
    {
        ++defense.wallsDrawn;
        ++p->stats.walls;
    }
    event(EffectStyle::WallDraw, maze.center(cell), p->position, p->team, repair ? 1 : 0, .45);
    return true;
}
inline bool Battle::eraseWall(int id, int cell)
{
    auto* p = find(id);
    if (!defenseMode() || finished || !p || !p->human || !p->alive || cell < 0 ||
        cell >= static_cast<int>(defense.buildable.size()))
        return false;
    if ((defense.phase != DefensePhase::Build && defense.phase != DefensePhase::Wave) || !defense.buildable[cell] ||
        maze.walls[cell] <= 0 || maze.isFixed(cell))
        return false;
    // The refund is the share of material left, of what the cell was worth (a page's own walls: a plain cell).
    const double worth = cell < static_cast<int>(defense.inkPaid.size()) && defense.inkPaid[cell] > 0 ? defense.inkPaid[cell] : config.inkPerCell;
    p->ink = std::min(p->maxInk, p->ink + std::floor(worth * maze.integrity(cell)));
    if (cell < static_cast<int>(defense.inkPaid.size()))
        defense.inkPaid[cell] = 0;
    maze.setWall(cell, false);
    event(EffectStyle::WallErase, maze.center(cell), p->position, p->team, 0, .45);
    return true;
}
inline bool Battle::callWave(int id)
{
    const auto* p = find(id);
    if (!defenseMode() || finished || !p || !p->human || defense.phase != DefensePhase::Build)
        return false;
    // The tutorial's one wave waits for its last step.
    if (defense.tutorial && tutorialStep() != TutorialStep::Defend)
        return false;
    beginWave();
    return true;
}
inline int Battle::spawnWaveEnemy(Shape kind, bool elite, int entrance)
{
    if (!defenseMode() || defense.entrances.empty() || finished)
        return -1;
    SpawnOrder order;
    order.kind = kind;
    order.elite = elite && kind != Shape::Trapezoid;
    order.entrance = entrance;
    const int id = nextId;
    spawnEnemy(order);
    flushUnits();
    return find(id) ? id : -1;
}
inline void Battle::beginWave()
{
    auto& d = defense;
    d.phase = DefensePhase::Wave;
    d.wave = d.wavesCleared + 1;
    d.waveClock = d.phaseTime = 0;
    double players = 0;
    for (const auto& u : units)
        if (u.kind == UnitKind::Fighter && (u.human || u.bot))
        {
            players += u.human ? 1 : .5;
            d.botsUsed |= u.bot;
        }
    // The room's difficulty tier makes every enemy tougher and adds elites (the tutorial is always tier 1).
    const TierInfo& tier = tierInfo(d.tutorial ? 1 : config.tier);
    CrowdScale crowd = crowdScale(players);
    crowd.elite += tier.elite;
    d.waveHealth = (1 + .1 * (d.wave - 1)) * d.toughness * crowd.health * tier.enemyHealth;
    d.waveBoss = d.toughness * crowd.boss * tier.enemyHealth;
    d.queue.clear();
    if (config.enemiesEnabled && d.tutorial)
    {
        // The tutorial's wave: a short line of runners from the first entrance, nothing else.
        for (int i = 0; i < tutorialPage().waveSize; ++i)
        {
            SpawnOrder o;
            o.at = 1 + i * 1.2;
            d.queue.push_back(o);
        }
    }
    else if (config.enemiesEnabled)
        for (const auto& order : wavePlan(d.wave, d.waves, crowd, static_cast<int>(d.entrances.size()), rng))
            if (order.kind != Shape::Trapezoid || config.bossEnabled)
                d.queue.push_back(order);
    d.queued = 0;
    // Every wave tops each player's ink up to half of their capacity.
    for (auto& u : units)
        if (u.human)
            u.ink = std::max(u.ink, u.maxInk * .5);
    event(EffectStyle::WaveStart, d.core, d.core, 1, d.wave, 1.2);
}
inline void Battle::clearWave()
{
    auto& d = defense;
    // Lying as bait pays: one sticker per enemy held this wave, up to the cap (sent with the wave's reward).
    for (auto& u : units)
        if (u.player())
        {
            u.stats.baitBonus += std::min(u.stats.waveHeld, config.baitStickerCap);
            u.stats.waveHeld = 0;
        }
    ++d.wavesCleared;
    event(EffectStyle::WaveClear, d.core, d.core, 1, d.wave, 1.2);
    if (d.wavesCleared >= d.waves)
    {
        d.phase = DefensePhase::Won;
        if (d.tutorial)
        {
            d.step = static_cast<int>(TutorialStep::Done);
            event(EffectStyle::StepDone, d.core, d.core, 1, d.step, 1.2);
        }
        finished = true;
        winner = 1;
        return;
    }
    d.phase = DefensePhase::Build;
    d.phaseTime = config.buildSeconds;
    for (int i = 0; i < 2; ++i)
        spawnTriangle(d.core, 460);
}
inline void Battle::rebuildRoutes()
{
    defense.route = routeCosts(maze, defense.coreCell, false);
    defense.ramRoute = routeCosts(maze, defense.coreCell, true);
    defense.routeVersion = maze.version;
}
// A drawn wall that has lost two thirds of its material gives way all at once and the route reopens.
inline void Battle::collapseWalls()
{
    for (int id = 0; id < static_cast<int>(maze.walls.size()); ++id)
        if (maze.walls[id] > 0 && !maze.isFixed(id) && maze.integrity(id) < .35)
        {
            maze.setWall(id, false);
            ++defense.wallsLost;
            event(EffectStyle::WallBreak, maze.center(id), maze.center(id), 2, maze.cellSize * .5, .7);
        }
}
inline void Battle::updateDefense(double dt)
{
    auto& d = defense;
    if (d.phase == DefensePhase::Won || d.phase == DefensePhase::Lost)
        return;
    if (d.routeVersion != maze.version)
    {
        collapseWalls();
        rebuildRoutes();
    }
    for (auto& u : units)
        if (!u.alive && u.kind == UnitKind::Fighter && (u.human || u.bot) && u.respawn > 0 && (u.respawn -= dt) <= 0)
            revive(u);
    if (d.tutorial)
    {
        reviveDummies(dt);
        updateTutorial(dt);
    }
    for (auto& drop : d.drops)
    {
        drop.remaining -= dt;
        // Pickup range reaches further with research, and three times as far with a magnet.
        for (auto& u : units)
            if (drop.remaining > 0 && u.alive && u.human && u.ink < u.maxInk &&
                (u.position - drop.position).length() < u.radius + 60 * perk(u, Attribute::Reach) * (u.perks.magnet ? MagnetReach : 1))
            {
                u.ink = std::min(u.maxInk, u.ink + drop.amount);
                event(EffectStyle::InkPickup, drop.position, u.position, u.team, drop.amount, .35);
                drop.remaining = 0;
            }
    }
    d.drops.erase(std::remove_if(d.drops.begin(), d.drops.end(), [](const InkDrop& drop) { return drop.remaining <= 0; }),
                  d.drops.end());
    if (d.phase == DefensePhase::Build)
    {
        // The tutorial gives every step all the time it needs.
        if (!d.tutorial)
        {
            d.phaseTime -= dt;
            if (d.phaseTime <= 0)
                beginWave();
        }
        return;
    }
    d.waveClock += dt;
    // The on-screen cap holds the rest of the wave back until enemies fall or leak.
    int onField = 0;
    for (const auto& u : units)
        onField += enemy(u);
    while (d.queued < d.queue.size() && d.queue[d.queued].at <= d.waveClock && onField < config.maxMonsters)
    {
        spawnEnemy(d.queue[d.queued++]);
        ++onField;
    }
    flushUnits();
    int marching = 0;
    for (auto& u : units)
        if (enemy(u))
        {
            // Reaching the core costs it the enemy's leak damage; the enemy is spent.
            if ((u.position - d.core).length() < DefenseState::CoreRadius + u.radius)
            {
                u.alive = false;
                u.hp = 0;
                u.path.clear();
                hurtCore(enemyStats(u.shape).leak * (u.elite ? 4 : 1) * enemyPower(), u.position);
                if (finished)
                    return;
                continue;
            }
            ++marching;
        }
    if (d.queued >= d.queue.size() && marching == 0)
        clearWave();
}
inline void Battle::hurtCore(double amount, Vec2 at)
{
    auto& d = defense;
    if (!std::isfinite(amount) || amount <= 0 || d.phase != DefensePhase::Wave)
        return;
    d.coreHealth = std::max(0., d.coreHealth - amount);
    d.coreDamaged = true;
    emitDamage(-1, -2, at, amount, 1);
    event(EffectStyle::CoreHit, at, d.core, 2, amount, .5);
    if (d.coreHealth <= 0)
    {
        d.phase = DefensePhase::Lost;
        finished = true;
        winner = 2;
    }
}
inline void Battle::spawnEnemy(const SpawnOrder& order)
{
    auto& d = defense;
    if (d.entrances.empty())
        return;
    const EnemyStats stats = enemyStats(order.kind);
    const bool boss = order.kind == Shape::Trapezoid;
    Unit m;
    m.id = nextId++;
    m.team = 2;
    m.kind = UnitKind::Monster;
    m.shape = order.kind;
    m.elite = order.elite;
    m.radius = stats.radius * (order.elite ? 1.8 : 1);
    m.hp = m.maxHp = stats.health * (boss ? d.waveBoss : d.waveHealth) * (order.elite ? 8 : 1);
    const int gate = d.entrances[static_cast<size_t>(order.entrance) % d.entrances.size()];
    const Vec2 spot = maze.center(gate) + Vec2{(random01(rng) - .5) * 160, (random01(rng) - .5) * 160};
    m.position = maze.walkable(spot, m.radius) ? spot : maze.center(gate);
    m.think = random01(rng) * .2;
    if (boss)
    {
        m.cooldown = 4;
        m.laserCooldown = 7;
    }
    event(EffectStyle::SpawnEnemy, m.position, m.position, 2, m.radius, .5);
    queueUnit(m);
}
// The next point to walk to: down the cost field, cutting corners wherever the straight line is clear.
// A ram stops at the first wall on its road and reports it so it can chew through.
inline Vec2 Battle::siegeTarget(const Unit& u, bool ram, int& wallAhead) const
{
    wallAhead = -1;
    const auto& d = defense;
    const auto& field = ram ? d.ramRoute : d.route;
    const int w = maze.width, h = maze.height;
    const int cell = maze.cell(u.position);
    if (cell < 0 || field.size() != maze.walls.size() || cell == d.coreCell)
        return d.core;
    auto clearCell = [&](int id) { return !maze.isFixed(id) && maze.walls[id] <= 0; };
    // A ram that has bitten its way into a wall keeps chewing the cell it stands in until it gives way.
    const bool inside = ram && maze.walls[cell] > 0;
    if (inside)
        wallAhead = cell;
    std::array<int, 5> chain{};
    int count = 0, at = cell;
    double here = field[cell];
    while (count < 5)
    {
        int best = -1;
        double bestCost = here;
        const int x = at % w, y = at / w;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
            {
                const int nx = x + dx, ny = y + dy;
                if ((!dx && !dy) || nx < 0 || ny < 0 || nx >= w || ny >= h)
                    continue;
                const int next = ny * w + nx;
                if (!std::isfinite(field[next]) ||
                    (dx && dy && (!clearCell(y * w + nx) || !clearCell(ny * w + x) || !clearCell(next))))
                    continue;
                if (field[next] < bestCost - 1e-9)
                {
                    bestCost = field[next];
                    best = next;
                }
            }
        if (best < 0)
            break;
        chain[count++] = best;
        at = best;
        here = bestCost;
        if (ram && maze.walls[best] > 0)
        {
            if (count == 1 && !inside)
                wallAhead = best;
            break;
        }
        if (best == d.coreCell)
            break;
    }
    if (count == 0)
        return d.core;
    if (inside)
        return chain[0] == d.coreCell ? d.core : maze.center(chain[0]);
    if (wallAhead >= 0)
        return maze.center(wallAhead);
    for (int i = count - 1; i >= 1; --i)
    {
        if (maze.walls[chain[i]] > 0)
            continue;
        const Vec2 p = chain[i] == d.coreCell ? d.core : maze.center(chain[i]);
        if (maze.clear(u.position, p, u.radius + 6))
            return p;
    }
    return chain[0] == d.coreCell ? d.core : maze.center(chain[0]);
}
// Defense enemies keep to the route: circles run it, rectangles ram walls in their way, triangles stop to
// shoot whatever is in range (players, allies, turrets or the core), the boss walks and casts.
inline void Battle::updateSiege(Unit& u, double dt)
{
    u.input.fire = false;
    if (!config.aiEnabled || u.castTime > 0 || u.laserTime > 0)
    {
        u.velocity = {};
        return;
    }
    const EnemyStats stats = enemyStats(u.shape);
    u.think -= dt;
    // A teammate lying as bait pulls every enemy that can see it off the road: shooters take aim at it, the
    // rest walk up to it and swing (rams forget the wall they were chewing).
    const Unit* lure = baitFor(u, u.shape == Shape::Triangle ? std::max(650., config.baitRadius) : config.baitRadius);
    chase(u, lure);
    if (lure)
    {
        u.path.clear();
        if (u.shape == Shape::Triangle)
        {
            u.target = lure->id;
            u.castTarget = lure->position;
            u.aim = (lure->position - u.position).normalized();
            u.velocity = {};
            return;
        }
        const Vec2 delta = lure->position - u.position;
        const double gap = delta.length() - (u.radius + lure->radius + 4);
        const double pace = stats.speed * (u.elite ? .85 : 1) * (1 + .02 * std::max(0, defense.wave - 1));
        u.velocity = gap > 1 ? delta.normalized() * std::min(pace, gap * 8) : Vec2{};
        if (delta.lengthSquared() > 1)
            u.aim = delta.normalized();
        if (u.shape == Shape::Rectangle)
            u.target = -1;
        return;
    }
    if (u.shape == Shape::Triangle && u.think <= 0)
    {
        u.target = -1;
        double best = 650 * 650;
        for (const auto& v : units)
            if (v.alive && v.team != u.team && v.state != UnitState::Launched)
            {
                const double d2 = (v.position - u.position).lengthSquared();
                if (d2 < best && maze.clear(u.position, v.position, 6))
                {
                    best = d2;
                    u.target = v.id;
                    u.castTarget = v.position;
                }
            }
        const double reach = 650 + DefenseState::CoreRadius;
        if (u.target < 0 && (defense.core - u.position).lengthSquared() < reach * reach &&
            maze.clear(u.position, defense.core, 6))
        {
            u.target = -2;
            u.castTarget = defense.core;
        }
    }
    if (u.shape == Shape::Triangle && u.target != -1)
    {
        const Unit* v = u.target >= 0 ? find(u.target) : nullptr;
        if (u.target >= 0 && (!v || !v->alive))
            u.target = -1;
        else
        {
            const Vec2 aimAt = v ? v->position : defense.core;
            u.castTarget = aimAt;
            u.aim = (aimAt - u.position).normalized();
            u.velocity = {};
            return;
        }
    }
    const bool ram = u.shape == Shape::Rectangle;
    if (u.think <= 0 || u.path.empty())
    {
        int wallAhead = -1;
        u.path.assign(1, siegeTarget(u, ram, wallAhead));
        u.waypoint = 0;
        if (ram)
            u.target = wallAhead;
        u.think = .2 + (u.id % 7) * .015;
    }
    const Vec2 delta = u.path[0] - u.position;
    const double length = delta.length();
    const double speed = stats.speed * (u.elite ? .85 : 1) * (1 + .02 * std::max(0, defense.wave - 1));
    u.velocity = length > 1 ? delta * (std::min(speed, length * 8) / length) : Vec2{};
    if (length > 1)
        u.aim = delta * (1 / length);
    if (length < 30)
        u.think = 0;
}
inline void Battle::dropLoot(const Unit& dead)
{
    auto& d = defense;
    const double ink = dead.shape == Shape::Trapezoid   ? 60
                       : dead.shape == Shape::Rectangle ? 8
                       : dead.shape == Shape::Triangle  ? 5
                                                        : 3;
    InkDrop drop;
    drop.id = d.nextDrop++;
    drop.position = dead.position;
    drop.amount = ink * (dead.elite ? 3 : 1);
    d.drops.push_back(drop);
    if (d.drops.size() > 160)
        d.drops.erase(d.drops.begin());
    const double chance = dead.shape == Shape::Rectangle ? .35 : dead.shape == Shape::Triangle ? .25 : .12;
    const int triangles = dead.shape == Shape::Trapezoid ? 6 : dead.elite ? 2 : random01(rng) < chance ? 1 : 0;
    for (int i = 0; i < triangles; ++i)
        spawnTriangle(dead.position, 110);
}
inline void Battle::spawnTriangle(Vec2 near, double spread)
{
    int idle = 0;
    for (const auto& u : units)
        idle += u.alive && u.kind == UnitKind::Companion && u.followerOf < 0 && !u.turret;
    for (const auto& u : pendingUnits)
        idle += u.kind == UnitKind::Companion;
    if (idle >= config.maxIdleCompanions)
        return;
    Unit a;
    a.id = nextId++;
    a.kind = UnitKind::Companion;
    a.state = UnitState::Idle;
    a.sides = MinSides;
    a.shape = Shape::Triangle;
    a.maxHp = a.hp = CompanionHealth;
    a.position = near;
    for (int tries = 0; tries < 16; ++tries)
    {
        const double angle = random01(rng) * 2 * Pi, reach = spread * (.45 + .55 * random01(rng));
        const Vec2 p = near + Vec2{std::cos(angle), std::sin(angle)} * reach;
        if (maze.walkable(p, a.radius + 4) &&
            (!defenseMode() || (p - defense.core).length() > DefenseState::CoreRadius + a.radius + 10))
        {
            a.position = p;
            break;
        }
    }
    event(EffectStyle::SpawnAlly, a.position, a.position, 1, a.radius, .5);
    queueUnit(a);
}
inline Vec2 Battle::coreSpawn(int index) const
{
    // The tutorial's core stands by the page's right edge: its player starts on the open side, facing the page.
    const double first = tutorialMode() ? Pi : 0;
    for (int ring = 0; ring < 4; ++ring)
        for (int k = 0; k < 8; ++k)
        {
            const double angle = first + (index * 3 + k) * Pi / 4 + ring * .4;
            const Vec2 p = defense.core + Vec2{std::cos(angle), std::sin(angle)} * (DefenseState::CoreRadius + 80 + ring * 90);
            if (maze.walkable(p, 34))
                return p;
        }
    return defense.core + Vec2{0, DefenseState::CoreRadius + 80};
}
inline void Battle::revive(Unit& u)
{
    int index = 0;
    for (const auto& other : units)
        index += other.kind == UnitKind::Fighter && other.id < u.id;
    u.alive = true;
    u.hp = u.maxHp;
    u.armor = u.respawn = 0;
    u.position = hubMode() ? hubSpawn(index) : coreSpawn(index);
    // A plaster stands the player up where they fell, at half health.
    if (u.perks.standUp)
    {
        u.perks.standUp = false;
        u.hp = u.maxHp * .5;
        if (maze.walkable(u.standAt, u.radius))
            u.position = u.standAt;
    }
    u.velocity = u.impulse = {};
    u.state = UnitState::Free;
    u.followerOf = -1;
    u.tailOrder = 0;
    u.carried = false;
    u.bait = u.guard = 0;
    u.input = {};
    u.moveQueue.clear();
    u.trail.clear();
    u.path.clear();
    u.burnTime = u.slowTime = u.hitFlash = 0;
    event(EffectStyle::Respawn, u.position, u.position, u.team, u.radius, .6);
}
// A polygon of four or more sides that comes to rest on open paper pins itself to that cell's centre.
inline void Battle::plantTurret(Unit& u)
{
    const int cell = maze.cell(u.position);
    if (cell < 0 || maze.walls[cell] > 0 || maze.isFixed(cell) || cell == defense.coreCell)
        return;
    for (int gate : defense.entrances)
        if (cell == gate)
            return;
    for (const auto& other : units)
        if (other.alive && other.turret && other.id != u.id && maze.cell(other.position) == cell)
            return;
    const Vec2 spot = maze.center(cell);
    if (!maze.walkable(spot, u.radius))
        return;
    u.position = spot;
    u.turret = true;
    u.velocity = u.impulse = {};
    u.cooldown = .5;
    event(EffectStyle::Turret, spot, spot, u.team, u.sides, .6);
}
inline void Battle::fireBolt(Unit& u, Vec2 target, double damage, int weapon, double speed, double range)
{
    if (projectiles.size() >= 768)
        return;
    Vec2 dir = (target - u.position).normalized();
    if (dir.lengthSquared() < .5)
        dir = u.aim.normalized();
    Projectile p;
    p.id = nextProjectile++;
    p.owner = p.squad = u.id;
    p.team = u.team;
    p.weapon = weapon;
    p.position = u.position + dir * (u.radius + 8);
    p.velocity = dir * speed;
    p.damage = damage;
    p.remaining = range / speed;
    p.radius = weapon == 9 ? 7 : 5;
    projectiles.push_back(p);
}
// Turret fire grows with its sides: range, damage and rate.
inline void Battle::fireTurret(Unit& u)
{
    const double range = 650 + 60 * (u.sides - 4);
    const Unit* target = nullptr;
    double best = range * range;
    for (const auto& other : units)
        if (enemy(other) && other.team != u.team)
        {
            const double d2 = (other.position - u.position).lengthSquared();
            if (d2 < best && maze.clear(u.position, other.position, 4))
            {
                best = d2;
                target = &other;
            }
        }
    if (!target)
        return;
    u.aim = (target->position - u.position).normalized();
    fireBolt(u, target->position, 16 * sideDamage(u.sides), 6, 1500, range + 80);
    u.cooldown = std::max(.3, .62 - .04 * (u.sides - 4));
}
// The boss beam runs toward the core until it meets rock, cutting every drawn wall along its width.
inline void Battle::fireLaser(Unit& boss)
{
    const Vec2 dir = boss.laserDir.normalized();
    const Vec2 from = boss.position + dir * boss.radius;
    Vec2 to = from;
    for (double reach = 0; reach < 2600; reach += maze.cellSize * .25)
    {
        const Vec2 p = from + dir * reach;
        const int cell = maze.cell(p);
        if (cell < 0 || maze.isFixed(cell))
            break;
        to = p;
    }
    maze.carveWalls(from, to, 80);
    // Routes are cell-wide, so the channel is too: every drawn cell the beam crosses burns away.
    for (int id = 0; id < static_cast<int>(maze.walls.size()); ++id)
        if (maze.walls[id] > 0 && !maze.isFixed(id) && distanceToSegment(maze.center(id), from, to) < maze.cellSize * .5)
        {
            maze.setWall(id, false);
            ++defense.wallsLost;
            event(EffectStyle::WallBreak, maze.center(id), maze.center(id), 2, maze.cellSize * .5, .7);
        }
    Effect beam{from, to, 80, .55, 1, boss.team};
    beam.style = EffectStyle::Laser;
    addEffect(beam);
    for (auto& v : units)
        if (v.alive && v.team != boss.team && distanceToSegment(v.position, from, to) < 80 + v.radius)
            damageUnit(boss.id, v.id, 120 * (1 + .06 * std::max(0, defense.wave - 1)) * enemyPower());
    boss.laserCooldown = 10;
}
inline double Battle::meleeDamage(const Unit& u) const
{
    if (!defenseMode())
        return 13 + elapsed * .02;
    return enemyStats(u.shape).melee * (u.elite ? 2 : 1) * (1 + .06 * std::max(0, defense.wave - 1)) * enemyPower();
}
} // namespace gwgame
