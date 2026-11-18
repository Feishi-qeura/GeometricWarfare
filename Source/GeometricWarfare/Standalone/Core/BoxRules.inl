#pragma once
namespace gwgame
{
// The box's two levels (defense pages): the floor of the box and the platform around its rim. Units on the
// platform walk only on it; a thrown unit lands on whichever level is under it; ladders carry anyone (runners
// too) between the two; monsters that reach the platform go after the players up there and hop back down when
// nobody is left to chase. AI teammates keep to the platform and cover the part of the box with enemies in it.
inline void Battle::startClimb(Unit& u, int ladder, bool up)
{
    if (ladder < 0 || ladder >= static_cast<int>(defense.ladders.size()))
        return;
    const Ladder& l = defense.ladders[static_cast<size_t>(ladder)];
    u.climb = ladder;
    u.climbUp = up;
    u.climbTime = 0;
    u.ladderGoal = -1;
    u.velocity = u.impulse = {};
    u.path.clear();
    u.trail.clear();
    u.rocketHeld = false;
    u.rocketCharge = 0;
    cancelCharge(u.id);
    event(EffectStyle::Climb, up ? ladderFoot(maze, l) : ladderHead(maze, l), up ? ladderHead(maze, l) : ladderFoot(maze, l), u.team,
          up ? 1 : 0, config.climbSeconds);
}
inline void Battle::updateClimb(Unit& u, double dt)
{
    if (u.climb < 0 || u.climb >= static_cast<int>(defense.ladders.size()))
    {
        u.climb = -1;
        return;
    }
    const Ladder& l = defense.ladders[static_cast<size_t>(u.climb)];
    const Vec2 from = u.climbUp ? ladderFoot(maze, l) : ladderHead(maze, l), to = u.climbUp ? ladderHead(maze, l) : ladderFoot(maze, l);
    u.climbTime = std::min(1., u.climbTime + dt / std::max(.05, config.climbSeconds));
    u.position = from + (to - from) * u.climbTime;
    u.velocity = u.impulse = {};
    if ((to - from).lengthSquared() > 1)
        u.aim = (to - from).normalized();
    if (u.climbTime < 1)
        return;
    u.climb = -1;
    u.deck = u.climbUp;
    u.position = to;
    u.deckIdle = 0;
    u.think = 0;
}
// A player (or an AI teammate) steering into a ladder from either end starts climbing it.
inline bool Battle::tryLadder(Unit& u, Vec2 steer)
{
    if (!defenseMode() || defense.ladders.empty() || u.climb >= 0 || u.bait > 0 || u.carried || u.state != UnitState::Free ||
        steer.lengthSquared() < .04 || carrying(u.id) >= 0)
        return false;
    const Vec2 dir = steer.normalized();
    for (size_t i = 0; i < defense.ladders.size(); ++i)
    {
        const Ladder& l = defense.ladders[i];
        const Vec2 foot = ladderFoot(maze, l), head = ladderHead(maze, l), up = (head - foot).normalized();
        if (!u.deck && (u.position - foot).length() < LadderUse && dir.dot(up) > .55)
        {
            startClimb(u, static_cast<int>(i), true);
            return true;
        }
        if (u.deck && (u.position - head).length() < LadderUse && dir.dot(up) < -.55)
        {
            startClimb(u, static_cast<int>(i), false);
            return true;
        }
    }
    return false;
}
// A unit that has just come to rest after a throw stands on whichever level is under it.
inline void Battle::settle(Unit& u)
{
    if (!maze.hasDeck())
        return;
    const int cell = maze.cell(u.position);
    if (maze.isDeck(cell))
    {
        u.deck = true;
        if (!maze.deckWalkable(u.position, u.radius))
        {
            const Vec2 lo{(cell % maze.width) * maze.cellSize, (cell / maze.width) * maze.cellSize};
            const double inset = u.radius + 1;
            u.position = {std::clamp(u.position.x, lo.x + inset, lo.x + maze.cellSize - inset),
                          std::clamp(u.position.y, lo.y + inset, lo.y + maze.cellSize - inset)};
        }
        return;
    }
    u.deck = false;
    if (maze.walkable(u.position, u.radius))
        return;
    // Half over the box wall: slide down off it to the nearest open floor.
    for (double reach = 8; reach <= 400; reach += 8)
        for (int k = 0; k < 16; ++k)
        {
            const double a = k * Pi / 8;
            const Vec2 p = u.position + Vec2{std::cos(a), std::sin(a)} * reach;
            if (!maze.isDeck(maze.cell(p)) && maze.walkable(p, u.radius))
            {
                u.position = p;
                return;
            }
        }
}
// Toward a point along the platform: straight on within one cell, otherwise via the next cell of the walk.
inline Vec2 Battle::deckToward(const Unit& u, Vec2 goal) const
{
    const int from = maze.cell(u.position), to = maze.nearestDeck(goal);
    if (from < 0 || to < 0 || from == to)
        return maze.isDeck(maze.cell(goal)) ? goal : (to >= 0 ? maze.center(to) : u.position);
    const int next = maze.deckStep(from, to);
    if (next == to && maze.isDeck(maze.cell(goal)))
        return goal;
    return maze.center(next);
}
// Off the platform at the nearest place with open floor below it.
inline void Battle::hopDown(Unit& u)
{
    int bestPit = -1;
    double nearest = std::numeric_limits<double>::infinity();
    for (int id = 0; id < static_cast<int>(maze.decks.size()); ++id)
    {
        const int pit = maze.decks[id] ? ladderPit(maze, id) : -1;
        if (pit < 0 || maze.walls[pit] > 0)
            continue;
        const double d2 = (maze.center(id) - u.position).lengthSquared();
        if (d2 < nearest)
        {
            nearest = d2;
            bestPit = pit;
        }
    }
    u.climb = -1;
    u.ladderGoal = -1;
    u.deckIdle = 0;
    if (bestPit < 0)
        return;
    const Vec2 from = u.position;
    u.position = maze.center(bestPit);
    u.deck = false;
    u.path.clear();
    u.think = 0;
    event(EffectStyle::HopDown, from, u.position, u.team, u.radius, .5);
}
// A monster up on the platform: it chases the nearest player up here (shooters stop to shoot), and with nobody
// left it heads back down a nearby ladder or hops off the edge.
inline void Battle::updateDeckMonster(Unit& u, double dt)
{
    const EnemyStats stats = enemyStats(u.shape);
    const double pace = stats.speed * (u.elite ? .85 : 1) * (1 + .02 * std::max(0, defense.wave - 1));
    const Unit* prey = nullptr;
    double best = std::numeric_limits<double>::infinity();
    for (const auto& v : units)
        if (v.alive && v.team != u.team && v.deck && v.climb < 0 && v.state != UnitState::Launched && !v.carried &&
            (v.position - u.position).lengthSquared() < best)
        {
            best = (v.position - u.position).lengthSquared();
            prey = &v;
        }
    chase(u, prey && prey->bait > 0 ? prey : nullptr);
    if (prey)
    {
        u.deckIdle = 0;
        if (u.shape == Shape::Triangle && best < 650. * 650.)
        {
            u.target = prey->id;
            u.castTarget = prey->position;
            u.aim = (prey->position - u.position).normalized();
            u.velocity = {};
            return;
        }
        u.target = -1;
        const Vec2 step = deckToward(u, prey->position), delta = step - u.position;
        const double gap = (prey->position - u.position).length() - (u.radius + prey->radius + 4);
        u.velocity = gap > 1 && delta.lengthSquared() > 1 ? delta.normalized() * std::min(pace, gap * 8) : Vec2{};
        if (delta.lengthSquared() > 1)
            u.aim = delta.normalized();
        return;
    }
    u.target = -1;
    u.deckIdle += dt;
    if (u.deckIdle < 2.5)
    {
        u.velocity = {};
        return;
    }
    // Back down: a ladder within a few steps, otherwise straight off the edge.
    int ladder = -1;
    double nearest = 900 * 900;
    for (size_t i = 0; i < defense.ladders.size(); ++i)
    {
        const double d2 = (ladderHead(maze, defense.ladders[i]) - u.position).lengthSquared();
        if (d2 < nearest)
        {
            nearest = d2;
            ladder = static_cast<int>(i);
        }
    }
    if (ladder < 0)
    {
        hopDown(u);
        return;
    }
    const Vec2 head = ladderHead(maze, defense.ladders[static_cast<size_t>(ladder)]);
    if ((head - u.position).length() < LadderUse * .6)
    {
        startClimb(u, ladder, false);
        return;
    }
    const Vec2 delta = deckToward(u, head) - u.position;
    u.velocity = delta.lengthSquared() > 1 ? delta.normalized() * pace : Vec2{};
    if (delta.lengthSquared() > 1)
        u.aim = delta.normalized();
    if (u.deckIdle > 8)
        hopDown(u);
}
// An AI teammate in the box keeps to the platform: it walks to the platform cell nearest the enemy it covers
// (one cell aside when a friend already stands there) and shoots whatever it can see within reach.
inline void Battle::updateDeckBot(Unit& u, double dt)
{
    (void)dt;
    const Unit* nearest = nullptr;
    double nearestDistance = std::numeric_limits<double>::infinity();
    for (const auto& v : units)
        if (enemy(v) && v.climb < 0 && (v.position - u.position).lengthSquared() < nearestDistance)
        {
            nearestDistance = (v.position - u.position).lengthSquared();
            nearest = &v;
        }
    // Cover: the enemy nearest the core (the one that matters most).
    const Unit* cover = nullptr;
    double danger = std::numeric_limits<double>::infinity();
    for (const auto& v : units)
        if (enemy(v) && !v.deck && v.climb < 0 && (v.position - defense.core).lengthSquared() < danger)
        {
            danger = (v.position - defense.core).lengthSquared();
            cover = &v;
        }
    if (!cover)
        cover = nearest;
    const double reach = weaponStats(u.weapon).range * .92;
    auto canHit = [&](Vec2 from, const Unit* v) {
        return v && (v->position - from).lengthSquared() < reach * reach && maze.sight(from, v->position, 4);
    };
    // Shoot the threat to the core when it is in reach and in sight from here, else whatever is nearest.
    const Unit* shot = canHit(u.position, cover) ? cover : canHit(u.position, nearest) ? nearest : nullptr;
    if (shot)
    {
        u.aim = (shot->position - u.position).normalized();
        u.input.fire = !lineOfFireBlocked(u, shot->position);
        aimRocket(u, shot->position);
    }
    // Stand where that threat can be hit: the nearest free spot on this bot's own piece of platform (chapter 2's rim
    // comes in pieces) with reach and a clear line to it, on a cell's inner edge; failing that, the nearest free spot.
    Vec2 goal = u.position;
    if (cover)
    {
        std::vector<int> piece = maze.deckPiece(maze.cell(u.position));
        std::sort(piece.begin(), piece.end(), [&](int a, int b) {
            return (maze.center(a) - cover->position).lengthSquared() < (maze.center(b) - cover->position).lengthSquared();
        });
        const double inset = u.radius + 8, size = maze.cellSize;
        bool fallback = false, found = false;
        for (size_t i = 0; i < piece.size() && i < 12 && !found; ++i)
        {
            const Vec2 lo{(piece[i] % maze.width) * size, (piece[i] / maze.width) * size};
            const Vec2 spot{std::clamp(cover->position.x, lo.x + inset, lo.x + size - inset),
                            std::clamp(cover->position.y, lo.y + inset, lo.y + size - inset)};
            bool taken = false;
            for (const auto& v : units)
                taken |= v.alive && v.id != u.id && v.player() && v.deck && (v.position - spot).lengthSquared() < 160. * 160.;
            if (taken)
                continue;
            if (!fallback)
            {
                goal = spot;
                fallback = true;
            }
            if ((spot - cover->position).lengthSquared() < reach * reach && maze.sight(spot, cover->position, 4))
            {
                goal = spot;
                found = true;
            }
        }
    }
    const double distance = (goal - u.position).length();
    if (distance > 40)
    {
        const Vec2 delta = deckToward(u, goal) - u.position;
        u.velocity = delta.lengthSquared() > 1 ? delta.normalized() * std::min(config.playerSpeed, distance * 4) : Vec2{};
        if (!u.input.fire && delta.lengthSquared() > 1)
            u.aim = delta.normalized();
    }
    else
        u.velocity = {};
}
// AI with a rocket launcher holds the trigger just long enough for the shell to land on the target.
inline void Battle::aimRocket(Unit& u, Vec2 target)
{
    if (u.weapon != RocketWeapon || !u.input.fire)
        return;
    const double distance = (target - u.position).length();
    const double needed = std::clamp((distance - RocketMinRange) / (RocketMaxRange - RocketMinRange), 0., 1.) * RocketChargeSeconds;
    u.input.fire = !u.rocketHeld || u.rocketCharge + 1e-9 < needed;
}
} // namespace gwgame
