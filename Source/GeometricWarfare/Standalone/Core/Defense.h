#pragma once
#include "Maze.h"
namespace gwgame
{
// Line defense: players draw walls on graph paper during the build phase, enemies march from the
// entrances to the core along the shortest open route, and the players fight on the field.
enum class DefensePhase : uint8_t
{
    Build,
    Wave,
    Won,
    Lost
};
// One hand-authored page of graph paper. '#' fixed rock, '.' paper the players may draw on,
// ',' open floor that stays clear, 'S' an entrance, 'C' the core.
struct DefenseLevel
{
    const char* id;
    int waves;
    double toughness;
    std::vector<const char*> rows;
};
inline const std::vector<DefenseLevel>& defenseLevels()
{
    static const std::vector<DefenseLevel> Levels = {
        {"1-1",
         10,
         1.0,
         {"#################", "#...............#", "#...............#", "#.......#.......#", "#,,.....#.....,,#",
          "S,,...........,C#", "#,,.....#.....,,#", "#.......#.......#", "#...............#", "#...............#",
          "#################"}},
        {"1-2",
         10,
         1.15,
         {"#################", "S,,.....#.......#", "#,......#.......#", "#.......#....#..#", "#............#,,#",
          "#..#..........,C#", "#..#.........#,,#", "#............#..#", "#,......#.......#", "S,,.....#.......#",
          "#################"}},
        {"1-3",
         12,
         1.3,
         {"#################", "S,,.............#", "#,..###.....###.#", "#...............#", "#..#....###....##",
          "#..#...........,#", "#..#....###....,#", "#...............#", "#...###.....###,#", "#.............,C#",
          "#################"}}};
    return Levels;
}
// Enemy roles follow their outline: circles run the route, rectangles ram walls, triangles shoot,
// the trapezoid is the boss.
struct EnemyStats
{
    double radius, health, speed, melee, leak;
};
inline EnemyStats enemyStats(Shape shape)
{
    switch (shape)
    {
    case Shape::Rectangle:
        return {34, 220, 120, 18, 60};
    case Shape::Triangle:
        return {26, 90, 150, 8, 30};
    case Shape::Trapezoid:
        return {110, 4500, 95, 40, 400};
    default:
        return {24, 60, 230, 10, 25};
    }
}
// Party size scaling from the design doc (1, 2, 4 players are the balance anchors; 8 is the party
// tier). Other sizes interpolate; an AI teammate counts as half a player.
struct CrowdScale
{
    double count, health, elite, boss;
};
inline CrowdScale crowdScale(double players)
{
    static const double Anchor[4] = {1, 2, 4, 8};
    static const CrowdScale Table[4] = {{1, 1, .04, 1}, {1.4, 1.2, .06, 1.8}, {1.8, 1.6, .08, 3.2}, {2.2, 2.4, .12, 5.6}};
    players = std::clamp(players, 1., 8.);
    for (int i = 0; i < 3; ++i)
        if (players <= Anchor[i + 1])
        {
            const double t = (players - Anchor[i]) / (Anchor[i + 1] - Anchor[i]);
            const CrowdScale &a = Table[i], &b = Table[i + 1];
            return {a.count + (b.count - a.count) * t, a.health + (b.health - a.health) * t,
                    a.elite + (b.elite - a.elite) * t, a.boss + (b.boss - a.boss) * t};
        }
    return Table[3];
}
struct SpawnOrder
{
    double at = 0;
    Shape kind = Shape::Circle;
    bool elite = false;
    int entrance = 0;
};
struct InkDrop
{
    int id = 0;
    Vec2 position{};
    double amount = 0, remaining = 20;
};
struct DefenseState
{
    int level = 0, waves = 10, wave = 0, wavesCleared = 0, coreCell = -1;
    DefensePhase phase = DefensePhase::Build;
    double phaseTime = 0, coreHealth = 1000, coreMaxHealth = 1000, toughness = 1, waveClock = 0;
    double waveHealth = 1, waveBoss = 1;
    static constexpr double CoreRadius = 140;
    Vec2 core{};
    std::vector<int> entrances;
    std::vector<uint8_t> buildable;
    // Route costs to the core: one for everyone, one where walls count as slow but passable (rams).
    std::vector<double> route, ramRoute;
    uint32_t routeVersion = std::numeric_limits<uint32_t>::max();
    std::vector<SpawnOrder> queue;
    size_t queued = 0;
    // Star objectives: core never touched, fewer than 20 wall cells lost, no AI teammates.
    bool coreDamaged = false, botsUsed = false;
    int wallsLost = 0, wallsDrawn = 0;
    std::vector<InkDrop> drops;
    int nextDrop = 1;
    // The ink a drawn cell is worth (the cheapest full price anyone paid for it): erasing refunds a share of this,
    // so pens and carbon paper can never turn drawing and erasing into an ink mill.
    std::vector<double> inkPaid;
    // The tutorial page runs a script on top of the defense rules: the build phase never times out, the one
    // wave starts only at the last step, and step counts what has been done (TutorialStep in Hub.h).
    bool tutorial = false;
    int step = 0, dummiesBroken = 0;
    double supplyClock = 0;
    // The column of the drawing step's suggested wall, picked when that step starts (-1 before).
    int wallColumn = -1;
    uint8_t stars() const
    {
        return (coreDamaged ? 0 : 1) | (wallsLost < 20 ? 2 : 0) | (botsUsed ? 0 : 4);
    }
};
// True while every entrance still reaches the core through open cells. Four-neighbour, so a route never
// squeezes diagonally between two walls that touch at a corner. extraBlocked tests a wall before it is drawn.
// Shared by the simulation (authority) and the client's drawing preview.
inline bool routeOpen(const Maze& maze, const std::vector<int>& entrances, int core, int extraBlocked = -1)
{
    const int count = static_cast<int>(maze.walls.size());
    if (core < 0 || core >= count || core == extraBlocked)
        return false;
    std::vector<uint8_t> seen(maze.walls.size(), 0);
    std::vector<int> open{core};
    seen[core] = 1;
    while (!open.empty())
    {
        const int id = open.back(), x = id % maze.width, y = id / maze.width;
        open.pop_back();
        for (auto d : std::array<std::pair<int, int>, 4>{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}})
        {
            const int nx = x + d.first, ny = y + d.second;
            if (nx < 0 || ny < 0 || nx >= maze.width || ny >= maze.height)
                continue;
            const int next = ny * maze.width + nx;
            if (seen[next] || next == extraBlocked || maze.walls[next] > 0)
                continue;
            seen[next] = 1;
            open.push_back(next);
        }
    }
    for (int entrance : entrances)
        if (entrance < 0 || entrance >= count || !seen[entrance])
            return false;
    return !entrances.empty();
}
// Cost of the road from every cell to the core (8-neighbour, never cutting a wall corner). The march treats
// walls as closed; a ram treats them as passable at a price that grows with their strength, so it chews
// through whichever wall saves it the most road. Shared by the simulation and the client's route preview.
inline std::vector<double> routeCosts(const Maze& maze, int core, bool ram)
{
    const int w = maze.width, h = maze.height, n = w * h;
    std::vector<double> out(static_cast<size_t>(std::max(0, n)), std::numeric_limits<double>::infinity());
    if (core < 0 || core >= n || static_cast<int>(maze.walls.size()) != n)
        return out;
    auto clearCell = [&](int id) { return !maze.isFixed(id) && maze.walls[id] <= 0; };
    using Item = std::pair<double, int>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
    out[core] = 0;
    open.push({0., core});
    while (!open.empty())
    {
        const Item top = open.top();
        open.pop();
        const int id = top.second;
        if (top.first > out[id] + 1e-9)
            continue;
        const int x = id % w, y = id / w;
        // A thick wall is twice the chew, so rams look for a thinner way first.
        const double enter = ram && maze.walls[id] > 0 ? 6 * maze.integrity(id) * (maze.kind(id) == WallKind::Thick ? 2 : 1) : 0;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
            {
                const int nx = x + dx, ny = y + dy;
                if ((!dx && !dy) || nx < 0 || ny < 0 || nx >= w || ny >= h)
                    continue;
                const int next = ny * w + nx;
                if (maze.isFixed(next) || (!ram && maze.walls[next] > 0))
                    continue;
                if (dx && dy && (!clearCell(y * w + nx) || !clearCell(ny * w + x) || !clearCell(next) || !clearCell(id)))
                    continue;
                const double total = top.first + (dx && dy ? 1.41421356 : 1.) + enter;
                if (total + 1e-9 < out[next])
                {
                    out[next] = total;
                    open.push({total, next});
                }
            }
    }
    return out;
}
// The march from one cell down the cost field to the core, as cell ids (empty when cut off).
inline std::vector<int> routeCells(const Maze& maze, const std::vector<double>& costs, int from, int core)
{
    std::vector<int> cells;
    const int w = maze.width, h = maze.height;
    if (from < 0 || from >= static_cast<int>(costs.size()) || !std::isfinite(costs[from]))
        return cells;
    auto clearCell = [&](int id) { return !maze.isFixed(id) && maze.walls[id] <= 0; };
    cells.push_back(from);
    for (int at = from, guard = 0; at != core && guard < w * h; ++guard)
    {
        int best = -1;
        double bestCost = costs[at];
        const int x = at % w, y = at / w;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
            {
                const int nx = x + dx, ny = y + dy;
                if ((!dx && !dy) || nx < 0 || ny < 0 || nx >= w || ny >= h)
                    continue;
                const int next = ny * w + nx;
                if (dx && dy && (!clearCell(y * w + nx) || !clearCell(ny * w + x)))
                    continue;
                if (costs[next] < bestCost - 1e-9)
                {
                    bestCost = costs[next];
                    best = next;
                }
            }
        if (best < 0)
            break;
        cells.push_back(best);
        at = best;
    }
    return cells;
}
// Wave w of a level: runners from the start, rams from wave 3, shooters from wave 4, the boss closes
// the level. Counts grow with the party; elites replace a share of the spawns.
inline std::vector<SpawnOrder> wavePlan(int wave, int waves, const CrowdScale& crowd, int entrances, uint64_t& rng)
{
    std::vector<SpawnOrder> plan;
    auto add = [&](Shape kind, int count, double start, double gap)
    {
        count = std::max(0, static_cast<int>(std::round(count * crowd.count)));
        for (int i = 0; i < count; ++i)
        {
            SpawnOrder o;
            o.kind = kind;
            o.at = start + i * gap / std::max(1., crowd.count);
            o.elite = kind != Shape::Trapezoid && random01(rng) < crowd.elite;
            plan.push_back(o);
        }
    };
    add(Shape::Circle, 5 + 2 * wave, 0, .9);
    if (wave >= 3)
        add(Shape::Rectangle, 1 + (wave - 3) / 2, 4, 3.2);
    if (wave >= 4)
        add(Shape::Triangle, 1 + (wave - 4) / 2, 6, 2.6);
    if (wave == waves)
    {
        SpawnOrder boss;
        boss.kind = Shape::Trapezoid;
        boss.at = 8;
        plan.push_back(boss);
    }
    std::stable_sort(plan.begin(), plan.end(), [](const SpawnOrder& a, const SpawnOrder& b) { return a.at < b.at; });
    for (size_t i = 0; i < plan.size(); ++i)
        plan[i].entrance = static_cast<int>(i % std::max(1, entrances));
    return plan;
}
} // namespace gwgame
