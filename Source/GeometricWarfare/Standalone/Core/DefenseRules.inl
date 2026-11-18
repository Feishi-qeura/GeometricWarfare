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
    // The box holds no loose triangles (they belong to the hub and the expedition): ink buys everything here.
    flushUnits();
    return true;
}
inline bool Battle::startDefensePage(uint64_t seed, const DefenseLevel& page)
{
    if (!openPage(seed, page, 0))
        return false;
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
            case '=':
                cells[id] = 3;
                break;
            case '.':
                defense.buildable[id] = 1;
                break;
            case 'S':
                defense.entrances.push_back(id);
                break;
            case 'R':
                cells[id] = 3;
                defense.raids.push_back(id);
                break;
            case 'C':
                defense.coreCell = id;
                break;
            default:
                break;
            }
        }
    maze.fromGrid(w, h, page.cellSize, cells);
    maze.seed = rng;
    defense.inkPaid.assign(cells.size(), 0);
    defense.towerPaid.assign(cells.size(), 0);
    if (!routeOpen(maze, defense.entrances, defense.coreCell))
        return false;
    defense.level = level;
    defense.waves = page.waves;
    defense.toughness = page.toughness;
    defense.mix = page.mix;
    defense.bosses = page.bosses;
    defense.core = maze.center(defense.coreCell);
    defense.coreHealth = defense.coreMaxHealth = config.coreHealth;
    defense.phase = DefensePhase::Build;
    defense.phaseTime = config.firstBuildSeconds;
    rebuildRoutes();
    navigationVersion = maze.version;
    return true;
}
inline bool Battle::refuseCell(Unit& p, int cell, int reason)
{
    // The refusal is a cue for that player only: radius carries the reason, pulse the player's unit.
    Effect e{maze.center(cell), maze.center(cell), static_cast<double>(reason), .6, EventEffect, p.team};
    e.style = EffectStyle::Denied;
    e.pulse = p.id;
    addEffect(e);
    return false;
}
inline double Battle::paintCost(int id, int cell) const
{
    const auto* p = find(id);
    if (!defenseMode() || finished || !p || !p->human || !p->alive || cell < 0 ||
        cell >= static_cast<int>(maze.walls.size()) || cell >= static_cast<int>(defense.buildable.size()))
        return -1;
    // Building is for the build phase (design: 准备阶段); a wave is for fighting. The tutorial hands the pen over only
    // at its drawing step (walking and shooting come first, with the reticle).
    if (defense.phase != DefensePhase::Build || !defense.buildable[cell] || maze.isFixed(cell) || wireAt(cell) > 0 ||
        (defense.tutorial && tutorialStep() < TutorialStep::Draw))
        return -1;
    const double integrity = maze.integrity(cell);
    // A repair keeps the wall's own type and costs the missing share of it.
    if (maze.walls[cell] > 0)
        return integrity >= .999 ? -1 : std::max(1., std::ceil(cellInk(*p, maze.kind(cell)) * (1 - integrity)));
    // A wall is never drawn over anyone standing in the cell, over a ladder's foot, and never closes the last
    // route to the core.
    const Vec2 lo{(cell % maze.width) * maze.cellSize, (cell / maze.width) * maze.cellSize};
    const Vec2 hi = lo + Vec2{maze.cellSize, maze.cellSize};
    for (const auto& u : units)
        if (u.alive && !u.deck)
        {
            const Vec2 nearest{std::clamp(u.position.x, lo.x, hi.x), std::clamp(u.position.y, lo.y, hi.y)};
            if ((u.position - nearest).lengthSquared() < u.radius * u.radius)
                return -3;
        }
    for (const auto& l : defense.ladders)
        if (l.pit == cell)
            return -4;
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
    if (cost <= -2)
        return refuseCell(*p, cell, static_cast<int>(-cost));
    if (cost < 0)
        return false;
    if (p->ink + 1e-9 < cost)
        return refuseCell(*p, cell, 1);
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
    if (defense.phase != DefensePhase::Build || !defense.buildable[cell])
        return false;
    // A tower comes down whole and pays back its share of health of what was spent on it; wire what is left of it.
    if (const int tower = towerAt(cell); tower >= 0)
    {
        Unit* t = find(tower);
        const double spent = cell < static_cast<int>(defense.towerPaid.size()) ? defense.towerPaid[cell] : 0;
        p->ink = std::min(p->maxInk, p->ink + std::floor(spent * std::clamp(t->hp / std::max(1., t->maxHp), 0., 1.)));
        t->alive = false;
        t->turret = false;
        t->hp = 0;
        dropTowers();
        event(EffectStyle::WallErase, maze.center(cell), p->position, p->team, 0, .45);
        return true;
    }
    if (wireAt(cell) > 0)
    {
        p->ink = std::min(p->maxInk, p->ink + std::floor(config.wireInk * defense.wire[cell]));
        defense.wire[cell] = 0;
        event(EffectStyle::WallErase, maze.center(cell), p->position, p->team, 0, .45);
        return true;
    }
    if (maze.walls[cell] <= 0 || maze.isFixed(cell))
        return false;
    // The refund is the share of material left of what the cell was worth (a page's own walls: a plain cell).
    const double worth = cell < static_cast<int>(defense.inkPaid.size()) && defense.inkPaid[cell] > 0 ? defense.inkPaid[cell] : config.inkPerCell;
    p->ink = std::min(p->maxInk, p->ink + std::floor(worth * maze.integrity(cell)));
    if (cell < static_cast<int>(defense.inkPaid.size()))
        defense.inkPaid[cell] = 0;
    if (cell < static_cast<int>(defense.towerPaid.size()))
        defense.towerPaid[cell] = 0;
    maze.setWall(cell, false);
    event(EffectStyle::WallErase, maze.center(cell), p->position, p->team, 0, .45);
    return true;
}
// ---- Towers on open floor, and barbed wire ----
inline int Battle::towerAt(int cell) const
{
    if (cell < 0)
        return -1;
    for (const auto& u : units)
        if (u.alive && u.turret && u.kind == UnitKind::Companion && maze.cell(u.position) == cell)
            return u.id;
    return -1;
}
inline double Battle::towerCost(int id, int cell, int kind) const
{
    const auto* p = find(id);
    if (!defenseMode() || finished || !p || !p->human || !p->alive || cell < 0 ||
        cell >= static_cast<int>(maze.walls.size()) || cell >= static_cast<int>(defense.buildable.size()) || !defense.buildable[cell])
        return -1;
    if (defense.phase != DefensePhase::Build)
        return -1;
    kind = std::clamp(kind, 0, TowerKindCount - 1);
    if (const int tower = towerAt(cell); tower >= 0)
    {
        // F again on a tower of the chosen kind: one more side (a bolt tower) or level (mortar, tesla), twice at most.
        const Unit* t = find(tower);
        if (t->towerKind != kind || t->sides >= MaxSides)
            return -1;
        return t->sides <= 4 ? config.towerFiveInk : config.towerSixInk;
    }
    // Towers and wire stand on open floor, never on a block.
    if (maze.walls[cell] > 0)
        return maze.isFixed(cell) ? -1 : -5;
    if (maze.isFixed(cell) || wireAt(cell) > 0)
        return -1;
    for (const auto& l : defense.ladders)
        if (l.pit == cell)
            return -4;
    if (kind == static_cast<int>(TowerKind::Wire))
        return config.wireInk;
    // A tower is a post the enemies walk around: never on anyone, and never the one that seals the last road.
    const Vec2 lo{(cell % maze.width) * maze.cellSize, (cell / maze.width) * maze.cellSize};
    const Vec2 hi = lo + Vec2{maze.cellSize, maze.cellSize};
    for (const auto& u : units)
        if (u.alive && !u.deck)
        {
            const Vec2 nearest{std::clamp(u.position.x, lo.x, hi.x), std::clamp(u.position.y, lo.y, hi.y)};
            if ((u.position - nearest).lengthSquared() < u.radius * u.radius)
                return -3;
        }
    if (!routeOpen(maze, defense.entrances, defense.coreCell, cell))
        return -2;
    if (p->perks.towerKit)
        return 0;
    return kind == static_cast<int>(TowerKind::Mortar) ? config.mortarInk : kind == static_cast<int>(TowerKind::Tesla) ? config.teslaInk : config.towerInk;
}
inline bool Battle::buildTower(int id, int cell, int kind)
{
    auto* p = find(id);
    if (!p)
        return false;
    kind = std::clamp(kind, 0, TowerKindCount - 1);
    const double cost = towerCost(id, cell, kind);
    if (cost <= -2)
        return refuseCell(*p, cell, static_cast<int>(-cost));
    if (cost < 0)
        return false;
    if (p->ink + 1e-9 < cost)
        return refuseCell(*p, cell, 1);
    p->ink -= cost;
    if (defense.towerPaid.size() != maze.walls.size())
        defense.towerPaid.assign(maze.walls.size(), 0);
    const Vec2 spot = maze.center(cell);
    if (kind == static_cast<int>(TowerKind::Wire))
    {
        if (defense.wire.size() != maze.walls.size())
        {
            defense.wire.assign(maze.walls.size(), 0);
            defense.wireBuilder.assign(maze.walls.size(), -1);
        }
        defense.wire[cell] = 1;
        defense.wireBuilder[cell] = id;
        event(EffectStyle::WallDraw, spot, p->position, p->team, 2, .45);
        return true;
    }
    if (const int existing = towerAt(cell); existing >= 0)
    {
        // One more side or level: longer reach, harder hits, a faster rate, and a sturdier post.
        Unit* t = find(existing);
        t->sides = clampSides(t->sides + 1);
        if (t->towerKind == static_cast<int>(TowerKind::Bolt))
            t->shape = polygonShape(t->sides);
        t->maxHp += 150;
        t->hp += 150;
        defense.towerPaid[cell] += cost;
        event(EffectStyle::TowerBuild, spot, p->position, p->team, t->sides, .6);
        return true;
    }
    if (p->perks.towerKit)
        p->perks.towerKit = false;
    Unit t;
    t.id = nextId++;
    t.kind = UnitKind::Companion;
    t.team = p->team;
    t.state = UnitState::Idle;
    t.turret = true;
    t.towerKind = kind;
    t.sides = 4;
    // The client tells the kinds apart by outline: a bolt tower is a polygon, a mortar round, a tesla coil a triangle.
    t.shape = kind == static_cast<int>(TowerKind::Mortar) ? Shape::Circle : kind == static_cast<int>(TowerKind::Tesla) ? Shape::Triangle : polygonShape(4);
    t.radius = 40;
    t.maxHp = t.hp = towerHealth(kind);
    t.position = spot;
    maze.setTowerPost(cell, TowerPostRadius);
    defense.towerCells.push_back(cell);
    t.builder = id;
    t.cooldown = .5;
    t.aim = (spot - p->position).normalized();
    if (t.aim.lengthSquared() < .5)
        t.aim = {1, 0};
    defense.towerPaid[cell] = cost;
    ++defense.towersBuilt;
    event(EffectStyle::TowerBuild, spot, p->position, p->team, t.sides, .6);
    queueUnit(t);
    flushUnits();
    return true;
}
// A tower's post is solid while the tower stands; when it falls (or is rubbed out) the floor opens again.
inline void Battle::dropTowers()
{
    auto& cells = defense.towerCells;
    for (size_t i = 0; i < cells.size();)
    {
        const int cell = cells[i];
        if (towerAt(cell) >= 0)
        {
            ++i;
            continue;
        }
        maze.setPost(cell, false);
        if (cell < static_cast<int>(defense.towerPaid.size()))
            defense.towerPaid[cell] = 0;
        event(EffectStyle::Shatter, maze.center(cell), maze.center(cell) + Vec2{0, -80}, 1, 4, .5);
        cells.erase(cells.begin() + static_cast<std::ptrdiff_t>(i));
    }
}
// The nearest standing tower within range of an enemy down in the box (with a clear shot at its post, for shooters).
inline int Battle::towerTarget(const Unit& u, double range, bool needSight) const
{
    int best = -1;
    double nearest = range * range;
    for (const auto& t : units)
        if (t.alive && t.turret && t.kind == UnitKind::Companion && t.builder >= 0 && t.team != u.team && !u.deck)
        {
            const double d2 = (t.position - u.position).lengthSquared();
            if (d2 >= nearest)
                continue;
            if (needSight)
            {
                double fraction = 1;
                Vec2 normal;
                int hit = -1;
                if (maze.raycast(u.position, t.position, 6, fraction, normal, &hit, false) && hit != maze.cell(t.position))
                    continue;
            }
            nearest = d2;
            best = t.id;
        }
    return best;
}
// Barbed wire slows and cuts every enemy walking through it, a quarter second at a time, and wears out as it does.
inline void Battle::updateWire(double dt)
{
    auto& d = defense;
    if (d.wire.size() != maze.walls.size() || d.phase != DefensePhase::Wave)
        return;
    d.wireClock += dt;
    if (d.wireClock < .25)
        return;
    d.wireClock -= .25;
    for (auto& u : units)
    {
        if (!u.alive || u.team != 2 || u.kind != UnitKind::Monster || u.deck || u.climb >= 0)
            continue;
        const int cell = maze.cell(u.position);
        if (cell < 0 || d.wire[cell] <= 0)
            continue;
        u.slowTime = std::max(u.slowTime, .4);
        d.wire[cell] = std::max(0., d.wire[cell] - (u.shape == Shape::Trapezoid ? .2 : u.elite ? .04 : .012));
        const int builder = d.wireBuilder[cell];
        damageUnit(builder, u.id, WireDamage * .25);
        if (d.wire[cell] <= 0)
            event(EffectStyle::Shatter, maze.center(cell), maze.center(cell) + Vec2{0, -60}, 1, 3, .4);
    }
}
// ---- Ladders down the box wall ----
inline int Battle::ladderAt(int deckCell) const
{
    for (size_t i = 0; i < defense.ladders.size(); ++i)
        if (defense.ladders[i].deck == deckCell)
            return static_cast<int>(i);
    return -1;
}
inline double Battle::ladderCost(int id, int deckCell) const
{
    const auto* p = find(id);
    if (!defenseMode() || finished || !p || !p->human || !p->alive ||
        defense.phase != DefensePhase::Build || !maze.isDeck(deckCell) || ladderAt(deckCell) >= 0)
        return -1;
    const int pit = ladderPit(maze, deckCell);
    if (pit < 0)
        return -1;
    // Its foot needs open floor: not a block, the core or a tunnel mouth.
    if (maze.walls[pit] > 0 || pit == defense.coreCell ||
        std::find(defense.entrances.begin(), defense.entrances.end(), pit) != defense.entrances.end())
        return -6;
    return config.ladderInk;
}
inline bool Battle::buildLadder(int id, int deckCell)
{
    auto* p = find(id);
    if (!p)
        return false;
    const double cost = ladderCost(id, deckCell);
    if (cost <= -2)
        return refuseCell(*p, deckCell, static_cast<int>(-cost));
    if (cost < 0)
        return false;
    if (p->ink + 1e-9 < cost)
        return refuseCell(*p, deckCell, 1);
    p->ink -= cost;
    Ladder l;
    l.deck = deckCell;
    l.pit = ladderPit(maze, deckCell);
    l.ink = cost;
    defense.ladders.push_back(l);
    event(EffectStyle::LadderDraw, ladderHead(maze, l), ladderFoot(maze, l), p->team, 0, .5);
    return true;
}
inline bool Battle::eraseLadder(int id, int deckCell)
{
    auto* p = find(id);
    if (!defenseMode() || finished || !p || !p->human || !p->alive ||
        defense.phase != DefensePhase::Build)
        return false;
    const int index = ladderAt(deckCell);
    if (index < 0)
        return false;
    const Ladder l = defense.ladders[index];
    // Whoever is on it lets go at the nearer end; runners heading for it go back to their road.
    for (auto& u : units)
    {
        if (u.climb == index)
        {
            const bool top = (u.climbTime >= .5) == u.climbUp;
            u.climb = -1;
            u.deck = top;
            u.position = top ? ladderHead(maze, l) : ladderFoot(maze, l);
        }
        else if (u.climb > index)
            --u.climb;
        if (u.ladderGoal == index)
            u.ladderGoal = -1;
        else if (u.ladderGoal > index)
            --u.ladderGoal;
    }
    p->ink = std::min(p->maxInk, p->ink + std::floor(l.ink));
    defense.ladders.erase(defense.ladders.begin() + index);
    event(EffectStyle::LadderErase, ladderHead(maze, l), ladderFoot(maze, l), p->team, 0, .45);
    return true;
}
// ---- Bringing fallen teammates back ----
inline Vec2 Battle::reviveSpot(Vec2 near) const
{
    const int cell = maze.nearestDeck(near);
    return cell >= 0 ? maze.center(cell) : near;
}
inline int Battle::fallenNear(int id) const
{
    const auto* p = find(id);
    if (!defenseMode() || finished || !p || !p->alive || !p->human || !p->deck || p->climb >= 0)
        return -1;
    int best = -1;
    double nearest = config.reviveReach * config.reviveReach;
    for (const auto& u : units)
        if (!u.alive && u.id != id && u.kind == UnitKind::Fighter && u.player() && u.team == p->team && u.respawn > 0)
        {
            const double d2 = (u.position - p->position).lengthSquared();
            if (d2 < nearest)
            {
                nearest = d2;
                best = u.id;
            }
        }
    return best;
}
inline bool Battle::reviveMate(int id)
{
    const int target = fallenNear(id);
    Unit* p = find(id);
    Unit* t = find(target);
    if (!p || !t)
        return false;
    if (p->ink + 1e-9 < config.reviveInk)
        return refuseCell(*p, std::max(0, maze.cell(t->position)), 1);
    p->ink -= config.reviveInk;
    ++p->stats.revives;
    // Back on their feet at their outline, whole: no plaster half measures for a friend's ink.
    t->perks.standUp = false;
    revive(*t);
    event(EffectStyle::Revive, p->position, t->position, p->team, t->seat, .8);
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
inline int Battle::spawnWaveEnemy(Shape kind, bool elite, int entrance, bool raid)
{
    if (!defenseMode() || defense.entrances.empty() || finished || (raid && defense.raids.empty()))
        return -1;
    SpawnOrder order;
    order.kind = kind;
    order.elite = elite && kind != Shape::Trapezoid;
    order.entrance = entrance;
    order.raid = raid;
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
        for (const auto& order :
             wavePlan(d.wave, d.waves, crowd, static_cast<int>(d.entrances.size()), rng, d.mix, d.bosses, static_cast<int>(d.raids.size())))
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
    dropTowers();
    updateWire(dt);
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
        // A drop lies on the level it fell on. Pickup range reaches further with research, and three times as far
        // with a magnet.
        const bool high = maze.isDeck(maze.cell(drop.position));
        for (auto& u : units)
            if (drop.remaining > 0 && u.alive && u.human && u.deck == high && u.climb < 0 && u.ink < u.maxInk &&
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
            if (!u.deck && u.climb < 0 && (u.position - d.core).length() < DefenseState::CoreRadius + u.radius)
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
    if (order.raid && !d.raids.empty())
    {
        // A raider climbs in over the rim: it starts up on the platform at its gate and hunts players there
        // before it drops into the box (updateDeckMonster).
        const int gate = d.raids[static_cast<size_t>(order.entrance) % d.raids.size()];
        const Vec2 spot = maze.center(gate) + Vec2{(random01(rng) - .5) * 120, (random01(rng) - .5) * 120};
        m.position = maze.deckWalkable(spot, m.radius) ? spot : maze.center(gate);
        m.deck = true;
    }
    else
    {
        const int gate = d.entrances[static_cast<size_t>(order.entrance) % d.entrances.size()];
        const Vec2 spot = maze.center(gate) + Vec2{(random01(rng) - .5) * 160, (random01(rng) - .5) * 160};
        m.position = maze.walkable(spot, m.radius) ? spot : maze.center(gate);
    }
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
// Whether a runner takes a given ladder when it passes one (two runners in three do; the same runner always
// decides the same way about the same ladder).
inline bool Battle::wantsLadder(const Unit& u, int ladder) const
{
    const uint32_t h = (static_cast<uint32_t>(u.id) * 2654435761u) ^ (static_cast<uint32_t>(ladder + 1) * 40503u);
    return (h >> 7) % 3 != 0;
}
// Defense enemies keep to the route: circles run it, rectangles ram walls in their way, triangles stop to
// shoot whatever is in range (players up on the platform too, allies, turrets or the core), the boss walks and
// casts. Runners passing near a ladder climb it and go after the players instead.
inline void Battle::updateSiege(Unit& u, double dt)
{
    u.input.fire = false;
    if (!config.aiEnabled || u.castTime > 0 || u.laserTime > 0)
    {
        u.velocity = {};
        return;
    }
    if (u.deck)
    {
        updateDeckMonster(u, dt);
        return;
    }
    const EnemyStats stats = enemyStats(u.shape);
    const double pace = stats.speed * (u.elite ? .85 : 1) * (1 + .02 * std::max(0, defense.wave - 1));
    u.think -= dt;
    // A teammate lying as bait pulls every enemy that can see it off the road: shooters take aim at it, the
    // rest walk up to it and swing (rams forget the wall they were chewing).
    const Unit* lure = baitFor(u, u.shape == Shape::Triangle ? std::max(650., config.baitRadius) : config.baitRadius);
    chase(u, lure);
    if (lure)
    {
        u.path.clear();
        u.ladderGoal = -1;
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
        u.velocity = gap > 1 ? delta.normalized() * std::min(pace, gap * 8) : Vec2{};
        if (delta.lengthSquared() > 1)
            u.aim = delta.normalized();
        if (u.shape == Shape::Rectangle)
            u.target = -1;
        return;
    }
    // Elites go for the towers first (design: 精英优先攻击防御塔): up to the post, and batter it. Elite shooters take aim
    // at it below with the other shooters.
    if (u.elite && u.shape != Shape::Trapezoid && u.shape != Shape::Triangle)
    {
        u.batter = std::max(0., u.batter - dt);
        if (u.think <= 0 || (u.target >= 0 && !find(u.target)))
            u.target = towerTarget(u, 900, false);
        const Unit* tower = u.target >= 0 ? find(u.target) : nullptr;
        if (tower && tower->alive && tower->turret)
        {
            // Up to the post itself (a disc in the middle of its cell since 2026-11-17).
            const Vec2 out = (u.position - tower->position).lengthSquared() > 1 ? (u.position - tower->position).normalized() : Vec2{1, 0};
            const Vec2 face = tower->position + out * TowerPostRadius;
            const Vec2 delta = face - u.position;
            const double gap = delta.length() - u.radius;
            if (gap < 26)
            {
                u.velocity = {};
                u.aim = (tower->position - u.position).normalized();
                if (u.batter <= 0)
                {
                    damageUnit(u.id, tower->id, stats.melee * 3 * enemyPower());
                    u.batter = 1;
                }
                return;
            }
            if (maze.clear(u.position, face - delta.normalized() * (u.radius + 4), u.radius))
            {
                u.velocity = delta.normalized() * pace;
                u.aim = delta.normalized();
                return;
            }
        }
        else
            u.target = -1;
    }
    // Runners near a ladder take it when there is a player up on the platform to go after (design: 梯子让敌人也能
    // 上平台).
    if (u.shape == Shape::Circle && !defense.ladders.empty())
    {
        if (u.ladderGoal >= static_cast<int>(defense.ladders.size()))
            u.ladderGoal = -1;
        if (u.ladderGoal < 0 && u.think <= 0)
            for (size_t i = 0; i < defense.ladders.size(); ++i)
            {
                const Vec2 foot = ladderFoot(maze, defense.ladders[i]), head = ladderHead(maze, defense.ladders[i]);
                if ((foot - u.position).length() >= LadderPull || !wantsLadder(u, static_cast<int>(i)) || !maze.clear(u.position, foot, u.radius))
                    continue;
                bool prey = false;
                for (const auto& v : units)
                    prey |= v.alive && v.team != u.team && v.deck && v.climb < 0 && (v.position - head).lengthSquared() < 2000. * 2000.;
                if (prey)
                {
                    u.ladderGoal = static_cast<int>(i);
                    break;
                }
            }
        if (u.ladderGoal >= 0)
        {
            const Vec2 delta = ladderFoot(maze, defense.ladders[u.ladderGoal]) - u.position;
            const double length = delta.length();
            if (length < LadderUse * .5)
            {
                startClimb(u, u.ladderGoal, true);
                return;
            }
            u.velocity = delta * (std::min(pace, length * 8) / std::max(length, 1e-6));
            u.aim = delta.normalized();
            return;
        }
    }
    if (u.shape == Shape::Triangle && u.think <= 0)
    {
        // Towers first (design: 远程敌人优先攻击防御塔), then whoever is in reach down here (the platform is out of a
        // box shooter's line), then the core from close by.
        u.target = towerTarget(u, 650, true);
        if (u.target >= 0)
            u.castTarget = find(u.target)->position;
        double best = u.target >= 0 ? 0 : 650 * 650;
        for (const auto& v : units)
            if (v.alive && v.team != u.team && v.state != UnitState::Launched && v.climb < 0 && v.deck == u.deck && !v.turret)
            {
                const double d2 = (v.position - u.position).lengthSquared();
                if (d2 < best && maze.sight(u.position, v.position, 6))
                {
                    best = d2;
                    u.target = v.id;
                    u.castTarget = v.position;
                }
            }
        // The core itself only from close by, within reach of the platform beside it: a shooter never sieges the
        // core from where nobody up top can answer.
        const double reach = 420 + DefenseState::CoreRadius;
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
        if (u.target >= 0 && (!v || !v->alive || v->climb >= 0))
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
    u.velocity = length > 1 ? delta * (std::min(pace, length * 8) / length) : Vec2{};
    if (length > 1)
        u.aim = delta * (1 / length);
    if (length < 30)
        u.think = 0;
}
inline void Battle::dropLoot(const Unit& dead, int attacker)
{
    auto& d = defense;
    double ink = dead.shape == Shape::Trapezoid   ? 60
                 : dead.shape == Shape::Rectangle ? 8
                 : dead.shape == Shape::Triangle  ? 5
                                                  : 3;
    ink *= dead.elite ? 3 : 1;
    // The ink goes straight to whoever made the kill (a tower's builder, a thrower, an AI teammate's nearest
    // human); a magnet adds a quarter. Whatever they cannot hold lies where the enemy fell.
    Unit* credited = creditFor(attacker);
    if (credited && credited->bot)
    {
        Unit* nearest = nullptr;
        double best = std::numeric_limits<double>::infinity();
        for (auto& u : units)
            if (u.alive && u.human && u.team == credited->team && (u.position - credited->position).lengthSquared() < best)
            {
                best = (u.position - credited->position).lengthSquared();
                nearest = &u;
            }
        credited = nearest;
    }
    if (credited && credited->alive && credited->human && credited->team != dead.team)
    {
        ink *= credited->perks.magnet ? MagnetInk : 1;
        const double taken = std::min(ink, std::max(0., credited->maxInk - credited->ink));
        if (taken > 1e-9)
        {
            credited->ink += taken;
            event(EffectStyle::InkPickup, dead.position, credited->position, credited->team, taken, .45);
            ink -= taken;
        }
    }
    if (ink <= 1e-9)
        return;
    InkDrop drop;
    drop.id = d.nextDrop++;
    drop.position = dead.position;
    drop.amount = ink;
    d.drops.push_back(drop);
    if (d.drops.size() > 160)
        d.drops.erase(d.drops.begin());
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
// Players are born up on the platform, nearest the core first, one platform cell apart (design: 玩家出生在平台上).
inline Vec2 Battle::deckSpawn(int index) const
{
    std::vector<int> cells;
    for (int id = 0; id < static_cast<int>(maze.decks.size()); ++id)
        if (maze.decks[id] && ladderPit(maze, id) >= 0)
            cells.push_back(id);
    if (cells.empty())
        return coreSpawn(index);
    std::stable_sort(cells.begin(), cells.end(), [&](int a, int b)
                     { return (maze.center(a) - defense.core).lengthSquared() < (maze.center(b) - defense.core).lengthSquared(); });
    const int slot = std::max(0, index) % static_cast<int>(cells.size());
    return maze.center(cells[static_cast<size_t>(slot)]);
}
inline void Battle::revive(Unit& u)
{
    int index = 0;
    for (const auto& other : units)
        index += other.kind == UnitKind::Fighter && other.id < u.id;
    u.alive = true;
    u.hp = u.maxHp;
    u.armor = u.respawn = 0;
    u.climb = u.ladderGoal = -1;
    if (hubMode())
    {
        u.position = hubSpawn(index);
        u.deck = false;
    }
    else if (maze.hasDeck())
    {
        // Back up on the platform where their outline waited (a teammate may have paid ink to hurry it).
        if (!maze.deckWalkable(u.position, u.radius))
            u.position = deckSpawn(index);
        u.deck = true;
    }
    else
        u.position = coreSpawn(index);
    // A plaster stands the player up where they fell, at half health.
    if (u.perks.standUp)
    {
        u.perks.standUp = false;
        u.hp = u.maxHp * .5;
        if (maze.standable(u.standDeck, u.standAt, u.radius))
        {
            u.position = u.standAt;
            u.deck = u.standDeck;
        }
    }
    u.velocity = u.impulse = {};
    u.state = UnitState::Free;
    u.followerOf = -1;
    u.tailOrder = 0;
    u.carried = false;
    u.bait = u.guard = 0;
    u.rocketHeld = false;
    u.rocketCharge = 0;
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
inline void Battle::fireBolt(Unit& u, Vec2 target, double damage, int weapon, double speed, double range, bool high)
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
    p.high = high;
    p.position = u.position + dir * (u.radius + 8);
    p.velocity = dir * speed;
    p.damage = damage;
    p.remaining = range / speed;
    p.radius = weapon == 9 ? 7 : 5;
    projectiles.push_back(p);
}
// Turret fire grows with its sides: range, damage and rate. A tower stands on its block, above every other wall,
// so it sees and shoots over them; it guards the box, not the platform.
inline void Battle::fireTurret(Unit& u)
{
    const bool tower = u.builder >= 0;
    if (tower && u.towerKind == static_cast<int>(TowerKind::Mortar))
        return fireMortar(u);
    if (tower && u.towerKind == static_cast<int>(TowerKind::Tesla))
        return fireTesla(u);
    const double range = tower ? 720 + 80 * (u.sides - 4) : 650 + 60 * (u.sides - 4);
    const Unit* target = nullptr;
    double best = range * range;
    for (const auto& other : units)
        if (enemy(other) && other.team != u.team && !other.deck && other.climb < 0)
        {
            const double d2 = (other.position - u.position).lengthSquared();
            if (d2 < best && (tower || maze.clear(u.position, other.position, 4)))
            {
                best = d2;
                target = &other;
            }
        }
    if (!target)
        return;
    u.aim = (target->position - u.position).normalized();
    fireBolt(u, target->position, (tower ? 22 : 16) * sideDamage(u.sides), 6, 1500, range + 80, tower);
    u.cooldown = std::max(.3, .62 - .04 * (u.sides - 4));
}
// A mortar lobs a shell at the thickest crowd it can reach, never close to itself; the shell bursts on landing.
inline void Battle::fireMortar(Unit& u)
{
    const int level = std::max(0, u.sides - 4);
    const double reach = 1500 + 150 * level;
    const Unit* target = nullptr;
    int crowd = 0;
    for (const auto& a : units)
    {
        if (!enemy(a) || a.team == u.team || a.deck || a.climb >= 0)
            continue;
        const double d = (a.position - u.position).length();
        if (d < MortarMinRange || d > reach)
            continue;
        int n = 0;
        for (const auto& b : units)
            n += enemy(b) && b.team != u.team && !b.deck && (b.position - a.position).lengthSquared() < MortarBlast * MortarBlast;
        if (n > crowd)
        {
            crowd = n;
            target = &a;
        }
    }
    if (!target)
    {
        u.cooldown = .4;
        return;
    }
    const double flight = .8 + (target->position - u.position).length() / 2400;
    Vec2 land = target->position + target->velocity * (flight * .6);
    land = {std::clamp(land.x, 1., maze.sizeX() - 1), std::clamp(land.y, 1., maze.sizeY() - 1)};
    Projectile p;
    p.id = nextProjectile++;
    p.owner = u.id;
    p.squad = u.id;
    p.team = u.team;
    p.weapon = MortarWeapon;
    p.high = true;
    p.position = u.position;
    p.velocity = (land - u.position) * (1 / flight);
    p.remaining = flight;
    p.radius = 12;
    p.damage = 70 * (1 + .35 * level);
    if (projectiles.size() < 768)
        projectiles.push_back(p);
    u.aim = (land - u.position).normalized();
    u.cooldown = 2.8 - .3 * level;
    event(EffectStyle::RocketLob, u.position, land, u.team, .5, .3);
}
// A tesla coil sparks at the nearest enemy and the spark jumps on to the next ones close by; every one struck is
// slowed for a moment.
inline void Battle::fireTesla(Unit& u)
{
    const int level = std::max(0, u.sides - 4);
    std::vector<int> struck;
    Vec2 from = u.position;
    double radius = 430 + 40 * level;
    for (int i = 0; i < 4 + level; ++i)
    {
        const Unit* next = nullptr;
        double best = radius * radius;
        for (const auto& v : units)
            if (enemy(v) && v.team != u.team && !v.deck && v.climb < 0 && std::find(struck.begin(), struck.end(), v.id) == struck.end())
            {
                const double d2 = (v.position - from).lengthSquared();
                if (d2 < best)
                {
                    best = d2;
                    next = &v;
                }
            }
        if (!next)
            break;
        struck.push_back(next->id);
        event(EffectStyle::Zap, from, next->position, u.team, i, .3);
        from = next->position;
        radius = 260;
    }
    if (struck.empty())
    {
        u.cooldown = .25;
        return;
    }
    u.aim = (find(struck[0])->position - u.position).normalized();
    for (int id : struck)
        if (Unit* v = find(id))
        {
            v->slowTime = std::max(v->slowTime, 1.);
            damageUnit(u.id, id, 30 * (1 + .3 * level));
        }
    u.cooldown = 1.5;
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
        {
            // A tower's post stops the beam, and the tower takes it.
            if (const int tower = towerAt(cell); tower >= 0)
                damageUnit(boss.id, tower, 300 * enemyPower());
            break;
        }
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
    // The beam sweeps the floor of the box; the platform stands above it.
    for (auto& v : units)
        if (v.alive && v.team != boss.team && !v.deck && distanceToSegment(v.position, from, to) < 80 + v.radius)
            damageUnit(boss.id, v.id, 120 * (1 + .06 * std::max(0, defense.wave - 1)) * enemyPower());
    boss.laserCooldown = 10;
}
inline double Battle::meleeDamage(const Unit& u) const
{
    if (expeditionMode())
        return foeInfo(u.breed).melee * expeditionPower(u);
    if (!defenseMode())
        return 13 + elapsed * .02;
    return enemyStats(u.shape).melee * (u.elite ? 2 : 1) * (1 + .06 * std::max(0, defense.wave - 1)) * enemyPower();
}
} // namespace gwgame
