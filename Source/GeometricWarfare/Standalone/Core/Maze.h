#pragma once
#include "Types.h"
#include <functional>
#include <queue>
namespace gwgame
{
class Maze
{
public:
    int width = 31, height = 31;
    double cellSize = 400;
    uint32_t version = 0;
    uint64_t seed = 1;
    std::vector<double> walls;
    static constexpr int FragmentsPerSide = 16;
    static constexpr int FragmentCount = FragmentsPerSide * FragmentsPerSide;
    using WallPatch = std::array<uint8_t, FragmentCount>;
    std::unordered_map<int, WallPatch> fragments;
    // Fixed cells are the level's rock: never damaged, carved, drawn over or erased.
    std::vector<uint8_t> fixed;
    // The WallKind of each drawn cell (plain for rock, empty cells and generated arenas).
    std::vector<uint8_t> kinds;
    // The platform around a box (defense pages): deck cells are rock to everything down in the box, a floor to
    // whoever stands up on them, and never stop a shot (the box wall is below the line of fire).
    std::vector<uint8_t> decks;
    bool generated = false;
    WallKind kind(int id) const
    {
        return id >= 0 && id < static_cast<int>(kinds.size()) ? static_cast<WallKind>(kinds[id]) : WallKind::Plain;
    }
    bool isDeck(int id) const
    {
        return id >= 0 && id < static_cast<int>(decks.size()) && decks[id];
    }
    bool hasDeck() const
    {
        return std::find(decks.begin(), decks.end(), uint8_t(1)) != decks.end();
    }
    double fragmentSize() const
    {
        return cellSize / FragmentsPerSide;
    }
    bool isFixed(int id) const
    {
        return id >= 0 && id < static_cast<int>(fixed.size()) && fixed[id];
    }
    // A freshly drawn wall is one solid cell at full strength; erasing clears its fragments too.
    void setWall(int id, bool solid, WallKind type = WallKind::Plain)
    {
        if (id < 0 || id >= static_cast<int>(walls.size()) || isFixed(id))
            return;
        walls[id] = solid ? 200 : 0;
        fragments.erase(id);
        if (kinds.size() != walls.size())
            kinds.assign(walls.size(), 0);
        kinds[id] = static_cast<uint8_t>(solid ? type : WallKind::Plain);
        ++version;
    }
    // A tower's post (2026-11-17): a round fixture of this radius in the middle of an open cell, so bodies walk round the
    // post itself; routes (walls > 0) still treat the cell as closed. setPost(id, false) takes it away again.
    void setTowerPost(int id, double radius)
    {
        if (id < 0 || id >= static_cast<int>(walls.size()) || walls[id] > 0 || isFixed(id))
            return;
        placeFixture(center(id), {radius, radius}, true);
    }
    // A shut door, or any cell made as solid as rock while something stands on it.
    void setPost(int id, bool solid)
    {
        if (id < 0 || id >= static_cast<int>(walls.size()) || (solid && (walls[id] > 0 || isFixed(id))))
            return;
        walls[id] = solid ? 200 : 0;
        fixed[id] = solid ? 1 : 0;
        fragments.erase(id);
        ++version;
    }
    // A fixture on the open floor (an expedition stand or machine): only the fragments under its footprint are solid,
    // and the cell is fixed, so shots, rams and erasers never chip it.
    bool isFixture(int id) const
    {
        return isFixed(id) && fragments.count(id) > 0;
    }
    // Stands a fixture at a point: a disc of radius half.x (round), or a box of half extents half. Its footprint turns
    // solid fragment by fragment, so bodies walk round the stand itself rather than an invisible cell; cells that are
    // already solid (rock, a wall, a shut door) are left alone.
    void placeFixture(Vec2 at, Vec2 half, bool round)
    {
        if (!at.finite() || walls.empty())
            return;
        const double reach = std::max(half.x, half.y), s = fragmentSize();
        const int x0 = std::max(0, static_cast<int>(std::floor((at.x - reach) / cellSize))),
                  x1 = std::min(width - 1, static_cast<int>(std::floor((at.x + reach) / cellSize)));
        const int y0 = std::max(0, static_cast<int>(std::floor((at.y - reach) / cellSize))),
                  y1 = std::min(height - 1, static_cast<int>(std::floor((at.y + reach) / cellSize)));
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
            {
                const int id = y * width + x;
                const bool fixture = isFixture(id);
                if (walls[id] > 0 && !fixture)
                    continue;
                WallPatch patch{};
                if (fixture)
                    patch = fragments[id];
                bool any = false;
                for (int k = 0; k < FragmentCount; ++k)
                {
                    const Vec2 d = Vec2{x * cellSize + (k % FragmentsPerSide + .5) * s, y * cellSize + (k / FragmentsPerSide + .5) * s} - at;
                    if (round ? d.lengthSquared() <= half.x * half.x : std::abs(d.x) <= half.x && std::abs(d.y) <= half.y)
                    {
                        patch[k] = 200;
                        any = true;
                    }
                }
                if (!any)
                    continue;
                fixed[id] = 1;
                applyPatch(id, patch);
                ++version;
            }
    }
    // Whether a circle overlaps any platform cell.
    bool touchesDeck(Vec2 p, double radius) const
    {
        const int x0 = static_cast<int>(std::floor((p.x - radius) / cellSize)), x1 = static_cast<int>(std::floor((p.x + radius) / cellSize));
        const int y0 = static_cast<int>(std::floor((p.y - radius) / cellSize)), y1 = static_cast<int>(std::floor((p.y + radius) / cellSize));
        for (int y = std::max(0, y0); y <= std::min(height - 1, y1); ++y)
            for (int x = std::max(0, x0); x <= std::min(width - 1, x1); ++x)
                if (isDeck(y * width + x))
                    return true;
        return false;
    }
    // How far a shot from the platform flies before it is clear of it (0 for a shot fired down in the box).
    double deckRun(Vec2 from, Vec2 to, double radius) const
    {
        const Vec2 d = to - from;
        const double length = d.length();
        if (length < 1e-6 || !touchesDeck(from, radius))
            return 0;
        const double step = cellSize * .05;
        for (double t = step; t < length; t += step)
            if (!touchesDeck(from + d * (t / length), radius))
                return t;
        return length;
    }
    // A bullet's flight: over the platform first when fired from it (the platform never stops it there), then down in
    // the box, where the platform's wall is as solid as rock.
    bool shotRay(Vec2 from, Vec2 to, double radius, bool deckStart, double& fraction, Vec2& normal, int* hitCell = nullptr) const
    {
        const double length = (to - from).length();
        fraction = 1;
        if (length < 1e-9)
            return false;
        const double over = deckStart ? deckRun(from, to, radius) : 0;
        if (over <= 0)
            return raycast(from, to, radius, fraction, normal, hitCell, false);
        const Vec2 exit = from + (to - from) * (over / length);
        double part = 1;
        if (raycast(from, exit, radius, part, normal, hitCell, true))
        {
            fraction = part * over / length;
            return true;
        }
        if (over >= length || !raycast(exit, to, radius, part, normal, hitCell, false))
            return false;
        fraction = (over + part * (length - over)) / length;
        return true;
    }
    // A bite out of a thick wall takes only part of its strength (lasers burn through regardless).
    double bite(int id, double damage) const
    {
        return kind(id) == WallKind::Thick ? damage * ThickBite : damage;
    }
    // Health of a cell's surviving material, 0..1.
    double integrity(int id) const
    {
        return id >= 0 && id < static_cast<int>(walls.size()) ? std::clamp(walls[id] / 200., 0., 1.) : 0;
    }
    void applyPatch(int id, const WallPatch& patch)
    {
        if (id < 0 || id >= static_cast<int>(walls.size()))
            return;
        fragments[id] = patch;
        double total = 0;
        for (auto hp : patch)
            total += hp;
        walls[id] = total / FragmentCount;
    }
    // Visit only surviving geometry intersecting this query, never invisible whole-cell proxies. overDeck leaves
    // the platform's cells out (shots and throws pass over the box wall).
    template <class Visitor> void solids(Vec2 lo, Vec2 hi, Visitor visit, bool overDeck = false) const
    {
        int x0 = std::max(0, static_cast<int>(std::floor(lo.x / cellSize))),
            x1 = std::min(width - 1, static_cast<int>(std::floor(hi.x / cellSize)));
        int y0 = std::max(0, static_cast<int>(std::floor(lo.y / cellSize))),
            y1 = std::min(height - 1, static_cast<int>(std::floor(hi.y / cellSize)));
        double s = fragmentSize();
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
            {
                int id = y * width + x;
                if (walls[id] <= 0 || (overDeck && isDeck(id)))
                    continue;
                Vec2 base{x * cellSize, y * cellSize};
                auto it = fragments.find(id);
                if (it == fragments.end())
                {
                    visit(id, base, base + Vec2{cellSize, cellSize}, 200);
                    continue;
                }
                int a = std::clamp(static_cast<int>(std::floor((lo.x - base.x) / s)), 0, 15),
                    b = std::clamp(static_cast<int>(std::floor((hi.x - base.x) / s)), 0, 15);
                int c = std::clamp(static_cast<int>(std::floor((lo.y - base.y) / s)), 0, 15),
                    d = std::clamp(static_cast<int>(std::floor((hi.y - base.y) / s)), 0, 15);
                for (int yy = c; yy <= d; ++yy)
                    for (int xx = a; xx <= b; ++xx)
                        if (auto hp = it->second[yy * 16 + xx])
                        {
                            Vec2 p = base + Vec2{xx * s, yy * s};
                            visit(id, p, p + Vec2{s, s}, hp);
                        }
            }
    }
    // hitCell (optional) names the wall cell the ray stopped at. overDeck: the platform never stops the ray.
    bool raycast(Vec2 from, Vec2 to, double radius, double& fraction, Vec2& normal, int* hitCell = nullptr,
                 bool overDeck = false) const
    {
        fraction = 1;
        bool hit = false;
        solids({std::min(from.x, to.x) - radius, std::min(from.y, to.y) - radius},
               {std::max(from.x, to.x) + radius, std::max(from.y, to.y) + radius},
               [&](int id, Vec2 lo, Vec2 hi, int)
               {
                   lo = lo - Vec2{radius, radius};
                   hi = hi + Vec2{radius, radius};
                   double f = 1;
                   if (segmentBox(from, to, lo, hi, &f) && f <= fraction)
                   {
                       fraction = f;
                       hit = true;
                       if (hitCell)
                           *hitCell = id;
                       Vec2 p = from + (to - from) * f;
                       double dx = std::min(std::abs(p.x - lo.x), std::abs(p.x - hi.x));
                       double dy = std::min(std::abs(p.y - lo.y), std::abs(p.y - hi.y));
                       normal = dx < dy ? Vec2{to.x > from.x ? -1. : 1., 0} : Vec2{0, to.y > from.y ? -1. : 1.};
                   }
               },
               overDeck);
        return hit;
    }
    double sizeX() const
    {
        return width * cellSize;
    }
    double sizeY() const
    {
        return height * cellSize;
    }
    int cell(Vec2 p) const
    {
        int x = static_cast<int>(std::floor(p.x / cellSize)), y = static_cast<int>(std::floor(p.y / cellSize));
        return x >= 0 && y >= 0 && x < width && y < height ? y * width + x : -1;
    }
    Vec2 center(int id) const
    {
        return {(id % width + .5) * cellSize, (id / width + .5) * cellSize};
    }
    bool blocked(int x, int y) const
    {
        return x < 0 || y < 0 || x >= width || y >= height || walls[y * width + x] > 0;
    }
    // Cells: 0 open, 1 a wall, 2 rock, 3 platform (rock to the box, a floor to whoever stands on it).
    void fromGrid(int w, int h, double size, const std::vector<int>& cells)
    {
        width = w;
        height = h;
        cellSize = size;
        walls.assign(cells.size(), 0);
        fragments.clear();
        fixed.assign(cells.size(), 0);
        kinds.assign(cells.size(), 0);
        decks.assign(cells.size(), 0);
        generated = false;
        for (size_t i = 0; i < cells.size(); ++i)
        {
            walls[i] = cells[i] ? 200 : 0;
            fixed[i] = cells[i] >= 2 ? 1 : 0;
            decks[i] = cells[i] == 3 ? 1 : 0;
        }
        version = 0;
    }
    void generate(uint64_t initialSeed, bool open = false)
    {
        width = height = 31;
        cellSize = 400;
        seed = initialSeed ? initialSeed : 1;
        uint64_t rng = seed;
        version = 0;
        walls.assign(width * height, 200);
        fragments.clear();
        fixed.clear();
        decks.clear();
        kinds.assign(walls.size(), 0);
        generated = true;
        if (open)
        {
            for (int y = 1; y < height - 1; ++y)
                for (int x = 1; x < width - 1; ++x)
                    walls[y * width + x] = 0;
            for (int i = 0; i < 48; ++i)
            {
                int x = 3 + static_cast<int>(random01(rng) * 25), y = 3 + static_cast<int>(random01(rng) * 25);
                if (std::abs(x - 15) > 3 && std::abs(y - 15) > 3)
                    walls[y * width + x] = 200;
            }
        }
        else
        {
            std::vector<int> stack{width + 1};
            walls[width + 1] = 0;
            while (!stack.empty())
            {
                int id = stack.back(), x = id % width, y = id / width;
                std::vector<int> choices;
                for (auto d : std::array<std::pair<int, int>, 4>{{{2, 0}, {-2, 0}, {0, 2}, {0, -2}}})
                {
                    int nx = x + d.first, ny = y + d.second;
                    if (nx > 0 && ny > 0 && nx < width - 1 && ny < height - 1 && walls[ny * width + nx] > 0)
                        choices.push_back(ny * width + nx);
                }
                if (choices.empty())
                {
                    stack.pop_back();
                    continue;
                }
                int next = choices[static_cast<size_t>(randomStep(rng) % choices.size())];
                walls[(id + next) / 2] = walls[next] = 0;
                stack.push_back(next);
            }
            for (int y = 1; y < height - 1; ++y)
                for (int x = 1; x < width - 1; ++x)
                    if ((x + y) % 2 == 1 && random01(rng) < .13)
                        walls[y * width + x] = 0;
        }
        for (auto p : anchors())
        {
            int id = cell(p), cx = id % width, cy = id / width;
            for (int y = cy - 1; y <= cy + 1; ++y)
                for (int x = cx - 1; x <= cx + 1; ++x)
                    if (x > 0 && y > 0 && x < width - 1 && y < height - 1)
                        walls[y * width + x] = 0;
        }
        for (int y = 14; y <= 16; ++y)
            for (int x = 14; x <= 16; ++x)
                walls[y * width + x] = 0;
    }
    std::array<Vec2, 6> anchors() const
    {
        return {{{3.5 * cellSize, 15.5 * cellSize},
                 {27.5 * cellSize, 15.5 * cellSize},
                 {15.5 * cellSize, 1.5 * cellSize},
                 {29.5 * cellSize, 15.5 * cellSize},
                 {15.5 * cellSize, 29.5 * cellSize},
                 {1.5 * cellSize, 15.5 * cellSize}}};
    }
    bool walkable(Vec2 p, double radius, bool overDeck = false) const
    {
        if (!p.finite() || p.x < radius || p.y < radius || p.x > sizeX() - radius || p.y > sizeY() - radius)
            return false;
        bool free = true;
        solids(p - Vec2{radius, radius}, p + Vec2{radius, radius},
               [&](int, Vec2 lo, Vec2 hi, int)
               {
                   Vec2 nearest{std::clamp(p.x, lo.x, hi.x), std::clamp(p.y, lo.y, hi.y)};
                   if ((p - nearest).lengthSquared() < radius * radius + 1e-6)
                       free = false;
               },
               overDeck);
        return free;
    }
    // A body overlapping solid geometry (a door shut on it, a fixture or a wall put down under it) is pushed out the
    // shortest way, deepest overlap first. move() only takes steps that end clear, so without this such a body could
    // never step out again. Returns p itself when it is clear already, or when no short push frees it.
    Vec2 unstick(Vec2 p, double radius, bool overDeck = false) const
    {
        if (!p.finite() || walkable(p, radius, overDeck))
            return p;
        Vec2 q = p;
        for (int pass = 0; pass < 8; ++pass)
        {
            double deepest = 0;
            Vec2 push{};
            solids(q - Vec2{radius, radius}, q + Vec2{radius, radius},
                   [&](int, Vec2 lo, Vec2 hi, int)
                   {
                       const Vec2 nearest{std::clamp(q.x, lo.x, hi.x), std::clamp(q.y, lo.y, hi.y)};
                       const Vec2 away = q - nearest;
                       const double d = away.length();
                       double depth = 0;
                       Vec2 out{};
                       if (d > 1e-6)
                       {
                           if (d >= radius)
                               return;
                           depth = radius - d;
                           out = away * (1 / d);
                       }
                       else
                       {
                           // The centre is inside the box: out through its nearest face.
                           const double faces[4] = {q.x - lo.x, hi.x - q.x, q.y - lo.y, hi.y - q.y};
                           const int k = static_cast<int>(std::min_element(faces, faces + 4) - faces);
                           depth = faces[k] + radius;
                           out = k == 0 ? Vec2{-1, 0} : k == 1 ? Vec2{1, 0} : k == 2 ? Vec2{0, -1} : Vec2{0, 1};
                       }
                       if (depth > deepest)
                       {
                           deepest = depth;
                           push = out * (depth + .5);
                       }
                   },
                   overDeck);
            if (deepest <= 0)
                break;
            q = q + push;
            if (walkable(q, radius, overDeck))
                return q;
        }
        return p;
    }
    bool clear(Vec2 a, Vec2 b, double radius = 0, bool overDeck = false) const
    {
        if (!a.finite() || !b.finite() || b.x < radius || b.y < radius || b.x > sizeX() - radius ||
            b.y > sizeY() - radius)
            return false;
        double fraction = 1;
        Vec2 normal;
        return !raycast(a, b, radius, fraction, normal, nullptr, overDeck);
    }
    // A sight line between two points on any level: the platform is below every line of fire.
    bool sight(Vec2 a, Vec2 b, double radius = 0) const
    {
        return clear(a, b, radius, true);
    }
    Vec2 move(Vec2 p, Vec2 delta, double radius, bool overDeck = false) const
    {
        if (!p.finite() || !delta.finite())
            return p;
        p = unstick(p, radius, overDeck);
        int steps = std::min(256, std::max(1, static_cast<int>(std::ceil(delta.length() / std::max(4., radius * .5)))));
        Vec2 d = delta * (1. / steps);
        for (int i = 0; i < steps; ++i)
        {
            Vec2 nx = p + Vec2{d.x, 0};
            if (walkable(nx, radius, overDeck))
                p = nx;
            Vec2 ny = p + Vec2{0, d.y};
            if (walkable(ny, radius, overDeck))
                p = ny;
        }
        return p;
    }
    // Up on the platform: a body stands only where every cell it touches is platform.
    bool deckWalkable(Vec2 p, double radius) const
    {
        if (!p.finite() || p.x < radius || p.y < radius || p.x > sizeX() - radius || p.y > sizeY() - radius ||
            !isDeck(cell(p)))
            return false;
        const int x0 = std::max(0, static_cast<int>(std::floor((p.x - radius) / cellSize))),
                  x1 = std::min(width - 1, static_cast<int>(std::floor((p.x + radius) / cellSize)));
        const int y0 = std::max(0, static_cast<int>(std::floor((p.y - radius) / cellSize))),
                  y1 = std::min(height - 1, static_cast<int>(std::floor((p.y + radius) / cellSize)));
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
            {
                const int id = y * width + x;
                if (isDeck(id))
                    continue;
                const Vec2 lo{x * cellSize, y * cellSize}, hi = lo + Vec2{cellSize, cellSize};
                const Vec2 nearest{std::clamp(p.x, lo.x, hi.x), std::clamp(p.y, lo.y, hi.y)};
                if ((p - nearest).lengthSquared() < radius * radius - 1e-6)
                    return false;
            }
        return true;
    }
    Vec2 deckMove(Vec2 p, Vec2 delta, double radius) const
    {
        if (!p.finite() || !delta.finite())
            return p;
        int steps = std::min(256, std::max(1, static_cast<int>(std::ceil(delta.length() / std::max(4., radius * .5)))));
        Vec2 d = delta * (1. / steps);
        for (int i = 0; i < steps; ++i)
        {
            Vec2 nx = p + Vec2{d.x, 0};
            if (deckWalkable(nx, radius))
                p = nx;
            Vec2 ny = p + Vec2{0, d.y};
            if (deckWalkable(ny, radius))
                p = ny;
        }
        return p;
    }
    // Moves on whichever level the body is on.
    Vec2 moveOn(bool onDeck, Vec2 p, Vec2 delta, double radius) const
    {
        return onDeck ? deckMove(p, delta, radius) : move(p, delta, radius);
    }
    bool standable(bool onDeck, Vec2 p, double radius) const
    {
        return onDeck ? deckWalkable(p, radius) : walkable(p, radius);
    }
    // The next platform cell on the shortest walk along the platform from one cell toward another (the platform is
    // a ring, so a straight line across the box is no road). Returns to itself when already there or cut off.
    int deckStep(int from, int to) const
    {
        if (!isDeck(from) || !isDeck(to) || from == to)
            return from;
        std::vector<int> parent(walls.size(), -1);
        std::vector<int> open{to};
        parent[to] = to;
        for (size_t head = 0; head < open.size(); ++head)
        {
            const int id = open[head], x = id % width, y = id / width;
            for (auto d : std::array<std::pair<int, int>, 4>{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}})
            {
                const int nx = x + d.first, ny = y + d.second;
                if (nx < 0 || ny < 0 || nx >= width || ny >= height)
                    continue;
                const int next = ny * width + nx;
                if (!isDeck(next) || parent[next] >= 0)
                    continue;
                parent[next] = id;
                if (next == from)
                    return id;
                open.push_back(next);
            }
        }
        return from;
    }
    // Every platform cell joined to this one along the platform (empty off the platform).
    std::vector<int> deckPiece(int from) const
    {
        std::vector<int> piece;
        if (!isDeck(from))
            return piece;
        std::vector<uint8_t> seen(walls.size(), 0);
        piece.push_back(from);
        seen[from] = 1;
        for (size_t head = 0; head < piece.size(); ++head)
        {
            const int x = piece[head] % width, y = piece[head] / width;
            for (auto d : std::array<std::pair<int, int>, 4>{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}})
            {
                const int nx = x + d.first, ny = y + d.second;
                if (nx < 0 || ny < 0 || nx >= width || ny >= height)
                    continue;
                const int next = ny * width + nx;
                if (isDeck(next) && !seen[next])
                {
                    seen[next] = 1;
                    piece.push_back(next);
                }
            }
        }
        return piece;
    }
    // The platform cell whose centre is nearest a point (-1 when the page has no platform).
    int nearestDeck(Vec2 p) const
    {
        int best = -1;
        double nearest = std::numeric_limits<double>::infinity();
        for (int id = 0; id < static_cast<int>(decks.size()); ++id)
            if (decks[id])
            {
                const double d2 = (center(id) - p).lengthSquared();
                if (d2 < nearest)
                {
                    nearest = d2;
                    best = id;
                }
            }
        return best;
    }
    std::vector<Vec2> path(Vec2 from, Vec2 to, double radius) const
    {
        if (!fragments.empty())
            return fracturedPath(from, to, radius);
        int start = cell(from), goal = cell(to);
        if (start < 0 || goal < 0 || walls[start] > 0 || !walkable(to, radius))
            return {};
        if (clear(from, to, radius))
            return {to};
        std::vector<double> cost(walls.size(), std::numeric_limits<double>::infinity());
        std::vector<int> parent(walls.size(), -1);
        using Item = std::pair<double, int>;
        std::priority_queue<Item, std::vector<Item>, std::greater<Item>> queue;
        auto heuristic = [&](int id)
        { return std::abs(id % width - goal % width) + std::abs(id / width - goal / width); };
        cost[start] = 0;
        queue.push({static_cast<double>(heuristic(start)), start});
        while (!queue.empty())
        {
            int id = queue.top().second;
            queue.pop();
            if (id == goal)
                break;
            for (auto d : std::array<std::pair<int, int>, 4>{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}})
            {
                int x = id % width + d.first, y = id / width + d.second;
                if (blocked(x, y))
                    continue;
                int next = y * width + x;
                double n = cost[id] + 1;
                if (n >= cost[next])
                    continue;
                cost[next] = n;
                parent[next] = id;
                queue.push({n + heuristic(next), next});
            }
        }
        if (start != goal && parent[goal] < 0)
            return {};
        std::vector<Vec2> reverse;
        for (int id = goal; id != start && id >= 0; id = parent[id])
            reverse.push_back(center(id));
        std::reverse(reverse.begin(), reverse.end());
        reverse.push_back(to);
        std::vector<Vec2> result;
        Vec2 previous = from;
        for (size_t i = 0; i < reverse.size();)
        {
            size_t best = i;
            for (size_t j = i + 1; j < reverse.size(); ++j)
            {
                if (!clear(previous, reverse[j], radius))
                    break;
                best = j;
            }
            result.push_back(reverse[best]);
            previous = reverse[best];
            i = best + 1;
        }
        return result;
    }
    std::vector<int> editWall(Vec2 from, Vec2 to, double radius, double damage, bool laser)
    {
        std::vector<int> changed;
        if (!from.finite() || !to.finite() || !std::isfinite(radius) || !std::isfinite(damage) || damage <= 0)
            return changed;
        int border = generated ? 1 : 0;
        int x0 = std::max(border, static_cast<int>((std::min(from.x, to.x) - radius) / cellSize));
        int x1 = std::min(width - 1 - border, static_cast<int>((std::max(from.x, to.x) + radius) / cellSize));
        int y0 = std::max(border, static_cast<int>((std::min(from.y, to.y) - radius) / cellSize));
        int y1 = std::min(height - 1 - border, static_cast<int>((std::max(from.y, to.y) + radius) / cellSize));
        double step = fragmentSize();
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
            {
                int id = y * width + x;
                if (walls[id] <= 0 || isFixed(id))
                    continue;
                WallPatch patch;
                auto found = fragments.find(id);
                if (found == fragments.end())
                    patch.fill(200);
                else
                    patch = found->second;
                bool edited = false;
                const double chunk = bite(id, damage);
                for (int yy = 0; yy < 16; ++yy)
                    for (int xx = 0; xx < 16; ++xx)
                    {
                        int i = yy * 16 + xx;
                        if (!patch[i])
                            continue;
                        Vec2 lo{x * cellSize + xx * step - radius, y * cellSize + yy * step - radius};
                        if (!segmentBox(from, to, lo, lo + Vec2{step + radius * 2, step + radius * 2}))
                            continue;
                        uint8_t next = laser ? 0 : static_cast<uint8_t>(std::max(0., patch[i] - chunk));
                        if (next != patch[i])
                        {
                            patch[i] = next;
                            edited = true;
                        }
                    }
                if (edited)
                {
                    applyPatch(id, patch);
                    changed.push_back(id);
                    ++version;
                }
            }
        return changed;
    }
    std::vector<int> damageWalls(Vec2 from, Vec2 to, double radius, double damage)
    {
        return editWall(from, to, radius, damage, false);
    }
    std::vector<int> damageRing(Vec2 origin, double previous, double current, double damage)
    {
        std::vector<int> changed;
        if (!origin.finite() || !std::isfinite(previous) || !std::isfinite(current) || !std::isfinite(damage) ||
            damage <= 0 || current < previous)
            return changed;
        int border = generated ? 1 : 0;
        int x0 = std::max(border, static_cast<int>((origin.x - current) / cellSize)),
            x1 = std::min(width - 1 - border, static_cast<int>((origin.x + current) / cellSize));
        int y0 = std::max(border, static_cast<int>((origin.y - current) / cellSize)),
            y1 = std::min(height - 1 - border, static_cast<int>((origin.y + current) / cellSize));
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
            {
                int id = y * width + x;
                if (walls[id] <= 0 || isFixed(id))
                    continue;
                WallPatch patch;
                auto found = fragments.find(id);
                if (found == fragments.end())
                    patch.fill(200);
                else
                    patch = found->second;
                bool edited = false;
                const double chunk = bite(id, damage);
                for (int i = 0; i < FragmentCount; ++i)
                    if (patch[i])
                    {
                        Vec2 p{x * cellSize + (i % 16 + .5) * fragmentSize(),
                               y * cellSize + (i / 16 + .5) * fragmentSize()};
                        double d = (p - origin).length();
                        if (d > previous && d <= current)
                        {
                            patch[i] = static_cast<uint8_t>(std::max(0., patch[i] - chunk));
                            edited = true;
                        }
                    }
                if (edited)
                {
                    applyPatch(id, patch);
                    changed.push_back(id);
                    ++version;
                }
            }
        return changed;
    }
    std::vector<int> carveWalls(Vec2 from, Vec2 to, double radius)
    {
        return editWall(from, to, radius, 200, true);
    }
    // A ram or a slam bites a round chunk out of whatever wall it touches.
    std::vector<int> damageDisc(Vec2 origin, double radius, double damage)
    {
        return damageRing(origin, -1, radius, damage);
    }
    std::vector<Vec2> fracturedPath(Vec2 from, Vec2 to, double radius) const
    {
        if (!walkable(from, radius) || !walkable(to, radius))
            return {};
        if (clear(from, to, radius))
            return {to};
        constexpr int stride = FragmentCount + 1;
        auto nodeAt = [&](Vec2 p)
        {
            int id = cell(p);
            if (id < 0)
                return -1;
            if (walls[id] <= 0)
                return id * stride;
            auto patch = fragments.find(id);
            if (patch == fragments.end())
                return -1;
            int x = std::clamp(static_cast<int>((p.x - id % width * cellSize) / fragmentSize()), 0, 15);
            int y = std::clamp(static_cast<int>((p.y - id / width * cellSize) / fragmentSize()), 0, 15);
            return patch->second[y * 16 + x] == 0 ? id * stride + 1 + y * 16 + x : -1;
        };
        auto point = [&](int node)
        {
            int id = node / stride, part = node % stride - 1;
            return part < 0 ? center(id)
                            : Vec2{(id % width) * cellSize + (part % 16 + .5) * fragmentSize(),
                                   (id / width) * cellSize + (part / 16 + .5) * fragmentSize()};
        };
        int start = nodeAt(from), goal = nodeAt(to);
        if (start < 0 || goal < 0)
            return {};
        std::unordered_map<int, double> cost;
        std::unordered_map<int, int> parent;
        using Item = std::pair<double, int>;
        std::priority_queue<Item, std::vector<Item>, std::greater<Item>> queue;
        cost[start] = 0;
        queue.push({0, start});
        while (!queue.empty())
        {
            int node = queue.top().second;
            queue.pop();
            if (node == goal)
                break;
            Vec2 here = node == start ? from : point(node);
            int id = node / stride, part = node % stride - 1, x = id % width, y = id / width;
            auto connect = [&](int next)
            {
                Vec2 target = next == goal ? to : point(next);
                if (!walkable(target, radius) || !clear(here, target, radius))
                    return;
                double n = cost[node] + (target - here).length();
                auto old = cost.find(next);
                if (old != cost.end() && old->second <= n)
                    return;
                cost[next] = n;
                parent[next] = node;
                queue.push({n + (target - to).length(), next});
            };
            for (auto d : std::array<std::pair<int, int>, 4>{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}})
            {
                if (part < 0)
                {
                    int nx = x + d.first, ny = y + d.second;
                    if (nx < 0 || ny < 0 || nx >= width || ny >= height)
                        continue;
                    int next = ny * width + nx;
                    if (walls[next] <= 0)
                        connect(next * stride);
                    else if (auto patch = fragments.find(next); patch != fragments.end())
                        for (int k = 0; k < 16; ++k)
                        {
                            int fx = d.first == 1 ? 0 : d.first == -1 ? 15 : k;
                            int fy = d.second == 1 ? 0 : d.second == -1 ? 15 : k;
                            int sub = fy * 16 + fx;
                            if (!patch->second[sub])
                                connect(next * stride + 1 + sub);
                        }
                }
                else
                {
                    int fx = part % 16 + d.first, fy = part / 16 + d.second, nx = x, ny = y;
                    if (fx < 0)
                    {
                        --nx;
                        fx = 15;
                    }
                    if (fx > 15)
                    {
                        ++nx;
                        fx = 0;
                    }
                    if (fy < 0)
                    {
                        --ny;
                        fy = 15;
                    }
                    if (fy > 15)
                    {
                        ++ny;
                        fy = 0;
                    }
                    if (nx < 0 || ny < 0 || nx >= width || ny >= height)
                        continue;
                    int next = ny * width + nx;
                    if (walls[next] <= 0)
                        connect(next * stride);
                    else if (auto patch = fragments.find(next);
                             patch != fragments.end() && !patch->second[fy * 16 + fx])
                        connect(next * stride + 1 + fy * 16 + fx);
                }
            }
        }
        if (start != goal && parent.find(goal) == parent.end())
            return {};
        std::vector<Vec2> reverse;
        for (int n = goal; n != start; n = parent.at(n))
            reverse.push_back(n == goal ? to : point(n));
        std::reverse(reverse.begin(), reverse.end());
        if (reverse.empty())
            reverse.push_back(to);
        std::vector<Vec2> result;
        Vec2 previous = from;
        for (size_t i = 0; i < reverse.size();)
        {
            size_t best = i;
            for (size_t j = i + 1; j < reverse.size(); ++j)
            {
                if (!clear(previous, reverse[j], radius))
                    break;
                best = j;
            }
            result.push_back(reverse[best]);
            previous = reverse[best];
            i = best + 1;
        }
        return result;
    }
    Vec2 randomOpen(uint64_t& rng, Vec2 near = {}, double maxDistance = 1e9) const
    {
        for (int n = 0; n < 1000; ++n)
        {
            int id = static_cast<int>(randomStep(rng) % walls.size());
            auto p = center(id);
            if (walls[id] == 0 && (p - near).length() <= maxDistance)
                return p;
        }
        return center(width * 15 + 15);
    }
};
} // namespace gwgame
