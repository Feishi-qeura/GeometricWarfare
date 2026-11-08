#pragma once
#include <cstring>
namespace gwgame
{
// The hub: a sheet with its stations built into the wall ring, a training corner with two targets that stand
// up again, a bullseye for throws, and a few triangles to recruit, merge and throw. No enemies, no end.
inline bool Battle::startHub(uint64_t seed)
{
    const auto& rows = hubRows();
    const int w = static_cast<int>(std::strlen(rows[0])), h = static_cast<int>(rows.size());
    for (const char* row : rows)
        if (static_cast<int>(std::strlen(row)) != w)
            return false;
    config.rules = RuleSet::Hub;
    // A friend thrown across the hub lies pinned only briefly: there is nothing to hold off here.
    config.baitSeconds = 3;
    rng = seed ? seed : 1;
    resetRun();
    defense = DefenseState{};
    hub = HubState{};
    std::vector<int> cells(static_cast<size_t>(w * h), 0);
    // The training mat is drawn as paper; nothing can be drawn on it (painting is a defense rule).
    defense.buildable.assign(cells.size(), 0);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            const int id = y * w + x;
            const char c = rows[y][x];
            if (c == '#' || hubStationOf(c) >= 0)
                cells[id] = 2;
            else if (c == '.' || c == 't' || c == 'o')
                defense.buildable[id] = 1;
        }
    maze.fromGrid(w, h, HubCell, cells);
    maze.seed = rng;
    const std::vector<int> spawn = hubCells('P'), eye = hubCells('o');
    if (spawn.empty() || eye.empty())
        return false;
    hub.spawn = maze.center(spawn[0]);
    hub.bullseye = maze.center(eye[0]);
    for (int cell : hubCells('t'))
        spawnDummy(maze.center(cell));
    // Weapon crates belong to missions.
    nextCrate = std::numeric_limits<double>::infinity();
    supplyTriangles(hub.bullseye, 700, 5, 400);
    flushUnits();
    navigationVersion = maze.version;
    return true;
}
inline Vec2 Battle::hubSpawn(int index) const
{
    // Arrivals stand in a ring around the spawn mark, eight to a ring.
    for (int tries = 0; tries < 16; ++tries)
    {
        const int slot = index + tries;
        const double angle = (slot % 8) * Pi / 4 + (slot / 8) * .39;
        const Vec2 p = hub.spawn + Vec2{std::cos(angle), std::sin(angle)} * (110. + 100 * ((slot / 8) % 3));
        if (maze.walkable(p, 34))
            return p;
    }
    return hub.spawn;
}
inline int Battle::spawnDummy(Vec2 at)
{
    Unit m;
    m.id = nextId++;
    m.team = 2;
    m.kind = UnitKind::Monster;
    m.shape = Shape::Circle;
    m.dummy = true;
    m.radius = 34;
    m.hp = m.maxHp = 150;
    m.position = m.home = at;
    m.aim = {0, 1};
    queueUnit(m);
    return m.id;
}
inline void Battle::reviveDummies(double dt)
{
    for (auto& u : units)
        if (u.dummy && !u.alive && u.respawn > 0 && (u.respawn -= dt) <= 0)
        {
            u.alive = true;
            u.hp = u.maxHp;
            u.respawn = 0;
            u.position = u.home;
            u.velocity = u.impulse = {};
            u.state = UnitState::Free;
            u.burnTime = u.slowTime = u.hitFlash = 0;
            event(EffectStyle::Respawn, u.position, u.position, u.team, u.radius, .5);
        }
}
inline void Battle::supplyTriangles(Vec2 near, double reach, int count, double spread)
{
    int idle = 0;
    for (const auto& u : units)
        idle += u.alive && u.kind == UnitKind::Companion && u.followerOf < 0 && !u.turret && u.state != UnitState::Launched &&
                (u.position - near).length() < reach;
    for (const auto& u : pendingUnits)
        idle += u.kind == UnitKind::Companion;
    for (int i = idle; i < count; ++i)
        spawnTriangle(near, spread);
}
inline void Battle::updateHub(double dt)
{
    for (auto& u : units)
        if (!u.alive && u.kind == UnitKind::Fighter && (u.human || u.bot) && u.respawn > 0 && (u.respawn -= dt) <= 0)
            revive(u);
    reviveDummies(dt);
    if ((hub.supplyClock -= dt) <= 0)
    {
        hub.supplyClock = 3;
        supplyTriangles(hub.bullseye, 700, 5, 400);
    }
}
inline void Battle::landed(Unit& u)
{
    if (!hubMode() || u.launchOwner < 0 || (u.position - hub.bullseye).length() > BullseyeRadius)
        return;
    if (Unit* thrower = find(u.launchOwner))
        ++thrower->stats.bullseyes;
    ++hub.bullseyes;
    event(EffectStyle::Bullseye, hub.bullseye, u.position, u.team, BullseyeRadius, .9);
}

// The tutorial: a defense page whose build phase waits while the player does each step once.
inline bool Battle::startTutorial(uint64_t seed)
{
    const TutorialPage& t = tutorialPage();
    if (!openPage(seed, t.page, -1))
        return false;
    defense.tutorial = true;
    defense.step = static_cast<int>(TutorialStep::Move);
    defense.waves = 1;
    for (const auto& target : t.targets)
        spawnDummy(maze.center(target.second * maze.width + target.first));
    flushUnits();
    return true;
}
inline TutorialProgress Battle::tutorialProgress() const
{
    TutorialProgress out;
    if (!tutorialMode())
        return out;
    const Unit* p = nullptr;
    for (const auto& u : units)
        if (u.human)
        {
            p = &u;
            break;
        }
    auto allies = [&]
    {
        int n = 0;
        if (p)
            for (int id : tailIds(p->id))
                n += !find(id)->human;
        return n;
    };
    const auto& d = defense;
    switch (tutorialStep())
    {
    case TutorialStep::Shoot:
        return {std::min(2, d.dummiesBroken), 2};
    case TutorialStep::Recruit:
        return {std::min(2, allies()), 2};
    case TutorialStep::Draw:
        return {std::min(3, d.wallsDrawn), 3};
    case TutorialStep::Defend:
    {
        int alive = 0;
        for (const auto& u : units)
            alive += enemy(u);
        const int needed = tutorialPage().waveSize;
        return {d.phase == DefensePhase::Build ? 0 : std::clamp(static_cast<int>(d.queued) - alive, 0, needed), needed};
    }
    case TutorialStep::Done:
        return {1, 1};
    default:
        return out;
    }
}
inline std::array<int, 3> Battle::tutorialWall() const
{
    const TutorialPage& t = tutorialPage();
    const int column = defense.wallColumn >= 0 ? defense.wallColumn : t.wall[0].first;
    std::array<int, 3> cells{};
    for (size_t i = 0; i < cells.size(); ++i)
        cells[i] = t.wall[i].second * maze.width + column;
    return cells;
}
namespace detail
{
// Whether a unit's body reaches into a cell (the test a wall uses before it is drawn).
inline bool inCell(const Maze& maze, const Unit& u, int cell)
{
    const Vec2 lo{(cell % maze.width) * maze.cellSize, (cell / maze.width) * maze.cellSize};
    const Vec2 hi = lo + Vec2{maze.cellSize, maze.cellSize};
    const Vec2 nearest{std::clamp(u.position.x, lo.x, hi.x), std::clamp(u.position.y, lo.y, hi.y)};
    return (u.position - nearest).lengthSquared() < u.radius * u.radius;
}
} // namespace detail
inline void Battle::placeTutorialWall()
{
    // The page's short wall across the road, moved a column or two aside when the player's turret or a
    // target stands in it: a wall is never drawn over anyone, and those two never move.
    const TutorialPage& t = tutorialPage();
    auto open = [&](int column)
    {
        for (const auto& w : t.wall)
        {
            const int cell = w.second * maze.width + column;
            if (column <= 0 || column >= maze.width - 1 || cell >= static_cast<int>(defense.buildable.size()) || !defense.buildable[cell] ||
                maze.walls[cell] > 0 || maze.isFixed(cell))
                return false;
            for (const auto& u : units)
                if (u.alive && (u.turret || u.dummy) && detail::inCell(maze, u, cell))
                    return false;
        }
        return true;
    };
    defense.wallColumn = t.wall[0].first;
    for (int shift : {0, -1, 1, -2, 2})
        if (open(t.wall[0].first + shift))
        {
            defense.wallColumn = t.wall[0].first + shift;
            break;
        }
    clearTutorialWall();
}
inline void Battle::clearTutorialWall()
{
    // Loose triangles idling in a suggested cell step out of it, to whichever side is nearer.
    for (int cell : tutorialWall())
    {
        if (maze.walls[cell] > 0)
            continue;
        const double left = (cell % maze.width) * maze.cellSize, right = left + maze.cellSize;
        for (auto& u : units)
            if (u.alive && u.kind == UnitKind::Companion && u.followerOf < 0 && !u.turret && u.state != UnitState::Launched &&
                detail::inCell(maze, u, cell))
                u.position.x = u.position.x < (left + right) * .5 ? left - u.radius - 6 : right + u.radius + 6;
    }
}
inline void Battle::updateTutorial(double dt)
{
    auto& d = defense;
    Unit* p = nullptr;
    for (auto& u : units)
        if (u.human)
        {
            p = &u;
            break;
        }
    if (!p || !p->alive || finished)
        return;
    const TutorialPage& t = tutorialPage();
    const TutorialStep step = tutorialStep();
    std::vector<int> tail;
    for (int id : tailIds(p->id))
        if (!find(id)->human)
            tail.push_back(id);
    // Triangles stay within reach while the polygon steps need them (a square thrown into a target bursts).
    if (step >= TutorialStep::Recruit && step <= TutorialStep::Turret && (d.supplyClock -= dt) <= 0)
    {
        d.supplyClock = 2;
        supplyTriangles(p->position, 1200, std::max(0, 3 - static_cast<int>(tail.size())), 260);
    }
    if (step == TutorialStep::Draw)
        clearTutorialWall();
    bool done = false;
    switch (step)
    {
    case TutorialStep::Move:
        done = (p->position - maze.center(t.moveY * maze.width + t.moveX)).length() < 170;
        break;
    case TutorialStep::Shoot:
        done = d.dummiesBroken >= 2;
        break;
    case TutorialStep::Recruit:
        done = tail.size() >= 2;
        break;
    case TutorialStep::Merge:
        for (int id : tail)
            done |= find(id)->sides > MinSides;
        break;
    case TutorialStep::Turret:
        for (const auto& u : units)
            done |= u.alive && u.turret;
        break;
    case TutorialStep::Draw:
        done = d.wallsDrawn >= 3;
        break;
    default:
        // Defend ends when its wave is cleared (clearWave).
        break;
    }
    if (!done)
        return;
    ++d.step;
    event(EffectStyle::StepDone, p->position, p->position, p->team, d.step, 1.2);
    if (tutorialStep() == TutorialStep::Draw)
        placeTutorialWall();
    // The recruiting step begins with three triangles right beside the player.
    if (tutorialStep() == TutorialStep::Recruit)
    {
        for (int i = 0; i < 3; ++i)
            spawnTriangle(p->position, 260);
        d.supplyClock = 2;
    }
}
} // namespace gwgame
