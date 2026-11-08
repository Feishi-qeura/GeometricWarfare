#pragma once
namespace gwgame
{
// Throwing teammates (design doc: 投掷真人, 整活强度, 防止变成恶意). E grabs a willing teammate into your tail,
// Q throws them; they land as bait that pulls nearby enemies for a few seconds and may roll out of it.
// Ink handover lets a player pour ink to a teammate. Shared by every rule set.
inline bool Battle::canGrab(const Unit& holder, const Unit& target) const
{
    if (config.prank == PrankLevel::Off || !target.human || !target.alive || !holder.alive || target.id == holder.id ||
        target.team != holder.team || target.followerOf >= 0 || holder.followerOf >= 0 || target.state == UnitState::Launched ||
        target.bait > 0 || target.guard > 0 || holder.bait > 0)
        return false;
    if (config.prank != PrankLevel::Chaos && !target.throwable)
        return false;
    // One teammate in hand at a time, and nobody is grabbed while carrying someone themselves.
    for (const auto& u : units)
        if (u.alive && u.carried && (u.followerOf == holder.id || u.followerOf == target.id))
            return false;
    return true;
}
inline int Battle::carrying(int id) const
{
    for (const auto& u : units)
        if (u.alive && u.carried && u.followerOf == id)
            return u.id;
    return -1;
}
inline bool Battle::setThrowable(int id, bool allowed)
{
    auto* p = find(id);
    if (!p || !p->human)
        return false;
    p->throwable = allowed;
    // Taking the permission back in a Friendly room also lets go of a grip.
    if (!allowed && p->carried && config.prank != PrankLevel::Chaos)
        leaveTail(*p);
    return true;
}
inline void Battle::grab(Unit& holder, Unit& target)
{
    cancelCharge(target.id);
    target.followerOf = holder.id;
    target.carried = true;
    target.state = UnitState::Following;
    target.tailOrder = nextTailOrder++;
    target.velocity = target.impulse = {};
    target.path.clear();
    event(EffectStyle::Grab, holder.position, target.position, holder.team, holder.seat, .4);
}
inline void Battle::leaveTail(Unit& member)
{
    member.followerOf = -1;
    member.tailOrder = 0;
    member.carried = false;
    if (member.state == UnitState::Following)
        member.state = member.human ? UnitState::Free : UnitState::Idle;
}
inline bool Battle::throwableTail(int id) const
{
    for (const auto& u : units)
        if (u.alive && u.followerOf == id && u.state == UnitState::Following && (!u.human || u.carried))
            return true;
    return false;
}
inline bool Battle::roll(int id)
{
    auto* p = find(id);
    if (!p || !p->alive || !p->human || p->bait <= 0 || config.baitSeconds - p->bait + 1e-9 < config.rollDelay)
        return false;
    p->bait = 0;
    // Roll the way the player is steering; with no steer, away from the closest enemy.
    Vec2 dir = p->input.move.lengthSquared() > .01 ? p->input.move.normalized() : Vec2{};
    if (dir.lengthSquared() < .5)
    {
        double nearest = 900 * 900;
        for (const auto& m : units)
            if (m.alive && m.team != p->team && (m.position - p->position).lengthSquared() < nearest)
            {
                nearest = (m.position - p->position).lengthSquared();
                dir = (p->position - m.position).normalized();
            }
    }
    if (dir.lengthSquared() < .5)
        dir = p->aim.normalized() * -1.;
    p->impulse += dir * 900;
    for (auto& m : units)
        if (m.lure == id)
            m.lure = -1;
    baits.erase(std::remove(baits.begin(), baits.end(), id), baits.end());
    event(EffectStyle::Roll, p->position, p->position + dir * 180, p->team, p->seat, .4);
    return true;
}
inline double Battle::pourInk(int from, int to, double amount)
{
    auto* a = find(from);
    auto* b = find(to);
    if (!defenseMode() || finished || !a || !b || a == b || !a->human || !b->human || !a->alive || !b->alive || a->team != b->team ||
        !std::isfinite(amount) || amount <= 0 || (a->position - b->position).length() > config.pourRange)
        return 0;
    const double moved = std::min({amount, a->ink, b->maxInk - b->ink});
    if (moved <= 1e-9)
        return 0;
    a->ink -= moved;
    b->ink += moved;
    a->stats.inkGiven += moved;
    event(EffectStyle::InkPour, a->position, b->position, a->team, moved, .5);
    return moved;
}
// The closest teammate lying as bait that a monster can see within reach (nullptr when none).
inline const Unit* Battle::baitFor(const Unit& monster, double reach) const
{
    const Unit* best = nullptr;
    double nearest = reach * reach;
    for (int id : baits)
        if (const Unit* b = find(id); b && b->alive && b->bait > 0 && b->team != monster.team)
        {
            const double d2 = (b->position - monster.position).lengthSquared();
            if (d2 < nearest && maze.clear(monster.position, b->position, 6))
            {
                nearest = d2;
                best = b;
            }
        }
    return best;
}
// Each enemy pulled onto a bait counts once for that bait (the 最佳诱饵 award and the bait stickers).
inline void Battle::chase(Unit& monster, const Unit* bait)
{
    const int target = bait ? bait->id : -1;
    if (target >= 0 && monster.lure != target)
        if (Unit* b = find(target))
        {
            ++b->stats.held;
            ++b->stats.waveHeld;
        }
    monster.lure = target;
}
inline Unit* Battle::creditFor(int attacker)
{
    Unit* a = find(attacker);
    if (!a)
        return nullptr;
    if (a->player())
        return a;
    if (a->state == UnitState::Launched && a->launchOwner >= 0)
        return find(a->launchOwner);
    if (a->followerOf >= 0)
        if (Unit* head = find(a->followerOf); head && head->player())
            return head;
    return nullptr;
}
inline int Battle::freeSeat() const
{
    for (int seat = 0; seat < 16; ++seat)
    {
        bool used = false;
        for (const auto& u : units)
            used |= u.kind == UnitKind::Fighter && u.seat == seat;
        for (const auto& [owner, parked] : dormant)
            used |= parked.player.seat == seat;
        if (!used)
            return seat;
    }
    return -1;
}
inline void Battle::consumeInput(Unit& u)
{
    if (!u.moveQueue.empty())
    {
        u.input = u.moveQueue.front();
        u.moveQueue.pop_front();
        u.ackSequence = u.input.sequence;
        u.aim = u.input.aim;
    }
    else if (u.input.sequence)
    {
        u.input.move = {};
        u.input.fire = false;
    }
}
} // namespace gwgame
