#pragma once
namespace gwgame
{
inline bool Battle::start(uint64_t seed)
{
    config.rules = RuleSet::Survival;
    rng = seed ? seed : 1;
    maze.generate(rng, true);
    resetRun();
    defense = DefenseState{};
    expedition = ExpeditionState{};
    navigationVersion = maze.version;
    return true;
}
inline void Battle::resetRun()
{
    units.clear();
    pendingUnits.clear();
    dormant.clear();
    nextTailOrder = 1;
    projectiles.clear();
    crates.clear();
    effects.clear();
    damageNumbers.clear();
    nextDamageNumber = 1;
    nextEffect = 1;
    elapsed = accumulator = 0;
    nextCrate = 20;
    nextRecruit = 3;
    nextWave = 1;
    bossWave = 0;
    nextBoss = BossSchedule[0];
    finished = false;
    winner = 0;
    nextId = nextProjectile = 1;
    sharedLevel = 1;
    sharedXP = 0;
}
inline int Battle::joinHuman(int owner)
{
    if (owner < 0 || finished)
        return -1;
    for (auto& u : units)
        if (u.owner == owner && u.human)
            return u.id;
    auto claim = [&](Unit& u)
    {
        u.human = true;
        u.bot = false;
        u.owner = u.previousOwner = owner;
        u.followerOf = -1;
        u.tailOrder = 0;
        u.carried = false;
        u.bait = 0;
        if (u.seat < 0)
            u.seat = freeSeat();
        u.state = UnitState::Free;
        u.velocity = u.impulse = {};
        u.input = {};
        u.moveQueue.clear();
        u.lastQueuedSequence = u.ackSequence = 0;
        u.path.clear();
        u.trail.clear();
        // A player who left while down comes back through the normal respawn.
        if ((defenseMode() || hubMode()) && !u.alive)
            u.respawn = std::max(u.respawn, 1.);
        return u.id;
    };
    // A returning player reclaims their own character, alive or not: leaving never resets a run.
    for (auto& u : units)
        if (u.previousOwner == owner && !u.human && u.kind == UnitKind::Fighter && !u.bot)
            return claim(u);
    // A player restored from a save comes back with their squad exactly as it was saved.
    if (auto parked = dormant.find(owner); parked != dormant.end())
    {
        if (units.size() + 1 + parked->second.squad.size() > 500)
            return -1;
        units.push_back(parked->second.player);
        int id = claim(units.back());
        for (auto& member : parked->second.squad)
            units.push_back(member);
        dormant.erase(parked);
        return id;
    }
    if (humanCount() >= 10 || units.size() >= 500)
        return -1;
    Unit u;
    u.id = nextId++;
    u.team = 1;
    u.human = true;
    u.owner = u.previousOwner = owner;
    u.position = defenseMode()      ? deckSpawn(humanCount())
                 : hubMode()        ? hubSpawn(humanCount())
                 : expeditionMode() ? floorSpawn(humanCount())
                                    : spawnPosition(humanCount() + 12);
    u.deck = defenseMode() && maze.hasDeck();
    u.seat = freeSeat();
    u.maxInk = u.ink = defenseMode() ? config.inkMax : 0;
    u.pendingUpgrades = sharedLevel - 1;
    u.level = sharedLevel;
    makeChoices(u);
    if (expeditionMode())
    {
        // Into the dungeon with the starting gun in slot 1 and nothing else (the loadout swaps in the player's own gun),
        // an empty pouch and a little ink (design 2.2: 白手起家).
        u.kit.slots[0] = {SlotKind::Weapon, 0, 1, magazine(0, false), 0};
        syncHand(u);
        refreshCapacities(u);
        u.ink = std::min(u.maxInk, 40.);
    }
    units.push_back(u);
    return u.id;
}
inline void Battle::leaveHuman(int owner)
{
    for (auto& u : units)
        if (u.human && u.owner == owner)
        {
            releaseFollowers(u.id);
            cancelCharge(u.id);
            leaveTail(u);
            u.human = false;
            u.owner = -1;
            u.input = {};
            u.alive = false;
            u.hp = 0;
            u.bait = 0;
            return;
        }
}
inline bool Battle::setInput(int id, Input input)
{
    auto* u = find(id);
    if (upgradePaused() || !u || !u->human || !u->alive || !input.move.finite() || !input.aim.finite())
        return false;
    if (input.move.lengthSquared() > 1)
        input.move = input.move.normalized();
    input.aim = input.aim.normalized();
    if (input.aim.lengthSquared() < .5)
        input.aim = u->aim;
    if (input.sequence)
    {
        if (u->lastQueuedSequence && !newerSequence(input.sequence, u->lastQueuedSequence))
            return false;
        if (u->moveQueue.size() >= 8)
            return false;
        u->lastQueuedSequence = input.sequence;
        u->moveQueue.push_back(input);
    }
    else
    {
        u->input = input;
        u->moveQueue.clear();
        u->aim = input.aim;
    }
    return true;
}
inline int Battle::candidate(int id) const
{
    auto* p = find(id);
    if (upgradePaused() || !p || !p->alive || !p->human || p->followerOf >= 0 || p->bait > 0 ||
        p->state == UnitState::Launched || finished)
        return -1;
    double best = config.recruitRange * perk(*p, Attribute::Reach);
    int selected = -1;
    // Allies and grabbable teammates compete on distance, so E always takes what is closest (on the same level:
    // nobody reaches up the box wall or down off the platform).
    for (auto& u : units)
    {
        const bool ally = u.kind == UnitKind::Companion && u.followerOf < 0 && u.state != UnitState::Launched && u.shardDelay <= 0 && !u.turret &&
                          u.gadget < 0;
        if (!u.alive || u.team != p->team || u.deck != p->deck || u.climb >= 0 || !(ally || (u.human && canGrab(*p, u))))
            continue;
        double d = (u.position - p->position).length();
        if (d <= best && maze.clear(p->position, u.position, std::min(p->radius, u.radius), p->deck) &&
            (d < best || selected < 0 || u.id < selected))
        {
            best = d;
            selected = u.id;
        }
    }
    return selected;
}
inline int Battle::recruit(int id)
{
    if (upgradePaused())
        return -1;
    auto* p = find(id);
    if (!p || !p->alive || !p->human || p->state == UnitState::Launched || p->climb >= 0)
        return -1;
    // Lying as bait, E rolls out once the landing has settled.
    if (p->bait > 0)
        return roll(id) ? id : -1;
    // On the expedition E takes, buys, opens or works whatever is in reach first; with nothing there it grabs a willing
    // teammate (2026-11-17: the grab and the throw are back in the dungeon at the user's call; no AI recruits down there).
    if (expeditionMode() && p->followerOf < 0)
    {
        const int act = interact(id);
        if (act != 0)
            return act > 0 ? id : -1;
        Unit* mate = find(candidate(id));
        if (!mate || !mate->human)
            return -1;
        grab(*p, *mate);
        return mate->id;
    }
    // Beside a fallen teammate's outline up on the platform, E brings them back for ink.
    if (p->followerOf < 0 && fallenNear(id) >= 0)
    {
        const int fallen = fallenNear(id);
        return reviveMate(id) ? fallen : -1;
    }
    if (p->followerOf >= 0)
    {
        // Leaving a squad, or wriggling out of a teammate's grip (which buys a moment before the next grab).
        const bool escaped = p->carried;
        const Unit* holder = find(p->followerOf);
        const Vec2 away = holder ? (p->position - holder->position).normalized() : p->aim.normalized() * -1.;
        leaveTail(*p);
        if (escaped)
        {
            p->guard = std::max(p->guard, config.escapeGuard);
            p->impulse += away * 520;
            event(EffectStyle::Escape, p->position, p->position + away * 120, p->team, p->seat, .4);
        }
        return id;
    }
    int selected = candidate(id);
    auto* u = find(selected);
    if (u && u->human)
    {
        grab(*p, *u);
        return selected;
    }
    if (!u)
    {
        double best = config.recruitRange * perk(*p, Attribute::Reach);
        int leader = -1;
        for (const auto& other : units)
            if (other.id != id && other.human && other.alive && other.team == p->team && other.followerOf < 0 && other.deck == p->deck &&
                other.climb < 0 && (other.position - p->position).length() < best &&
                maze.clear(p->position, other.position, std::min(p->radius, other.radius), p->deck))
            {
                best = (other.position - p->position).length();
                leader = other.id;
            }
        return leader >= 0 && joinSquad(id, leader) ? leader : -1;
    }
    int tail = 0;
    for (int member : tailIds(id))
        tail += !find(member)->human;
    if (tail >= config.tailLimit)
        return -1;
    u->turret = false;
    u->followerOf = id;
    u->state = UnitState::Following;
    u->tailOrder = nextTailOrder++;
    u->path.clear();
    u->velocity = {};
    syncSquadWeapon(*u, *p);
    event(EffectStyle::Recruit, p->position, u->position, p->team);
    return selected;
}
inline int Battle::launch(int id)
{
    auto* p = find(id);
    if (upgradePaused() || !p || !p->alive || !p->human || finished || p->state == UnitState::Launched)
        return -1;
    auto tail = tailIds(id);
    const int held = carrying(id);
    Unit* last = held >= 0 ? find(held) : nullptr;
    for (int follower : tail)
        if (auto* a = find(follower); !last && a && !a->human)
            last = a;
    if (!last)
        return -1;
    const bool person = last->human;
    leaveTail(*last);
    last->launchOwner = id;
    last->state = UnitState::Launched;
    last->launchPower = p->charging ? std::clamp(p->chargeTime / 1.5, 0., 1.) : 0;
    last->launchTime = 1.1 + last->launchPower * .6;
    last->launchInitialSpeed = config.launchSpeed * (.55 + last->launchPower * 1.45);
    last->velocity = p->aim.normalized() * last->launchInitialSpeed;
    last->impulse = {};
    // From the platform the throw goes out over the box wall (design: 把队友抬起来扔进盒子).
    last->position = maze.move(p->position, p->aim.normalized() * (p->radius + last->radius + 8), last->radius, true);
    if (person)
    {
        // The same player cannot be thrown again for a while (design doc: 10 s).
        last->guard = config.throwGuard;
        cancelCharge(last->id);
        ++last->stats.thrown;
        ++p->stats.throws;
    }
    ++p->stats.launches;
    event(EffectStyle::Launch, p->position, p->position + p->aim.normalized() * 100, p->team, last->launchPower);
    p->charging = false;
    p->chargeTime = 0;
    return last->id;
}
inline bool Battle::beginCharge(int id)
{
    auto* p = find(id);
    if (upgradePaused() || !p || !p->human || !p->alive || p->charging || finished || !throwableTail(id))
        return false;
    p->charging = true;
    p->chargeTime = 0;
    return true;
}
inline void Battle::cancelCharge(int id)
{
    if (auto* p = find(id))
    {
        p->charging = false;
        p->chargeTime = 0;
    }
}
inline int Battle::releaseCharge(int id, Vec2 aim)
{
    auto* p = find(id);
    if (!p || !p->charging || !aim.finite() || aim.lengthSquared() < 1e-8)
    {
        cancelCharge(id);
        return -1;
    }
    p->aim = aim.normalized();
    int result = launch(id);
    cancelCharge(id);
    return result;
}
inline void Battle::releaseFollowers(int id)
{
    for (auto& u : units)
        if (u.followerOf == id)
        {
            u.followerOf = -1;
            u.tailOrder = 0;
            u.carried = false;
            u.state = u.human ? UnitState::Free : UnitState::Idle;
            u.velocity = {};
        }
}
inline void Battle::emitDamage(int source, int victim, Vec2 position, double amount, int team)
{
    if (!std::isfinite(amount) || amount <= 0)
        return;
    // Shotgun pellets and synchronized followers form one readable short burst.
    for (auto it = damageNumbers.rbegin(); it != damageNumbers.rend(); ++it)
        if (it->victim == victim && it->remaining > .82 && (it->position - position).length() < 30)
        {
            it->amount += amount;
            return;
        }
    if (damageNumbers.size() >= 128)
        damageNumbers.erase(damageNumbers.begin());
    damageNumbers.push_back({nextDamageNumber++, victim, source, team, position, amount, .9});
}
inline void Battle::damageUnit(int attacker, int victim, double damage, bool friendly)
{
    auto* v = find(victim);
    auto* a = find(attacker);
    if (!v || !v->alive || !std::isfinite(damage) || damage <= 0 || (a && a->team == v->team && !friendly))
        return;
    // Mid-dodge, nothing lands.
    if (v->dashTime > 0 && v->player())
        return;
    // The expedition's items and pen traits change the hit (critical hits, stamps, shields, notes).
    if (expeditionMode())
    {
        damage = expeditionDamage(a, *v, damage);
        if (!(damage > 0))
            return;
    }
    // A teammate lying as bait shrugs off half of every hit.
    if (v->bait > 0)
        damage *= config.baitDamage;
    // Armour soaks up to 70% of a hit (the eraser's cut goes straight through it).
    double absorbed = rawHit ? 0 : std::min(v->armor, damage * .7);
    double effective = absorbed + std::min(v->hp, damage - absorbed);
    emitDamage(attacker, victim, v->position, effective, v->team);
    if (Unit* credited = creditFor(attacker); credited && credited->team != v->team)
        credited->stats.damage += effective;
    v->armor -= absorbed;
    v->hp = std::max(0., v->hp - (damage - absorbed));
    v->hitFlash = .15;
    if (v->hp > 0)
        return;
    // A pencil body's dodge, a phoenix charm or a stand-in may keep an expedition player on their feet.
    if (expeditionMode() && v->kind == UnitKind::Fighter && v->player() && cheatDeath(*v))
        return;
    v->alive = false;
    v->turret = false;
    v->hasPickupWeapon = false;
    // An expedition player's whole kit falls to the floor and they become a ghost (design 4.4); anywhere else a player
    // drops any crate weapon and gets up holding their own starting weapon.
    if (expeditionMode() && v->kind == UnitKind::Fighter && v->player())
        expeditionFall(*v);
    else
    {
        v->weapon = v->player() && !survivalMode() ? v->perks.weapon : 0;
        if (v->player() && !survivalMode())
            v->ammo = weaponStats(v->weapon).ammo;
    }
    v->maxHp = (v->kind == UnitKind::Fighter ? healthCapacity(*v) : v->maxHp) + v->skills[17] * 35;
    cancelCharge(victim);
    v->hp = 0;
    v->moveQueue.clear();
    v->input = {};
    v->impulse = {};
    v->followerOf = -1;
    v->carried = false;
    v->bait = 0;
    v->state = UnitState::Idle;
    v->velocity = {};
    v->path.clear();
    v->trail.clear();
    v->climb = v->ladderGoal = -1;
    v->rocketHeld = false;
    v->rocketCharge = 0;
    releaseFollowers(victim);
    if (v->kind == UnitKind::Monster)
        expeditionKill(*v, attacker);
    if (v->dummy)
    {
        // A training target drops nothing and stands up again at home (reviveDummies).
        v->respawn = 3;
        if (defenseMode())
            ++defense.dummiesBroken;
    }
    else if (defenseMode())
    {
        // Defense has no team level: enemies pay ink to whoever killed them, fallen players get back up on the
        // platform (at once if a teammate pays ink at their outline; once, where they fell, with a plaster).
        if (v->kind == UnitKind::Monster)
            dropLoot(*v, attacker);
        else if (v->kind == UnitKind::Fighter && (v->human || v->bot))
        {
            if (v->perks.plaster)
            {
                v->perks.plaster = false;
                v->perks.standUp = true;
                v->standAt = v->position;
                v->standDeck = v->deck;
                v->respawn = PlasterDelay;
            }
            else
                v->respawn = config.respawnSeconds;
            // The outline waits up on the platform, nearest where they fell.
            if (maze.hasDeck())
            {
                v->position = reviveSpot(v->position);
                v->deck = true;
            }
        }
    }
    else if (hubMode() && v->kind == UnitKind::Fighter && (v->human || v->bot))
        v->respawn = 2;
    if (a && a->team != v->team)
    {
        ++a->kills;
        if (v->kind == UnitKind::Monster && survivalMode())
            addExperience(18 + sharedLevel * 2);
    }
}
inline void Battle::tick(double dt)
{
    if (finished || upgradePaused())
        return;
    elapsed += dt;
    baits.clear();
    for (auto& u : units)
    {
        if (u.charging)
        {
            if (!throwableTail(u.id))
                cancelCharge(u.id);
            else
                u.chargeTime = std::min(1.5, u.chargeTime + dt);
        }
        u.hitFlash = std::max(0., u.hitFlash - dt);
        u.slowTime = std::max(0., u.slowTime - dt);
        u.dashTime = std::max(0., u.dashTime - dt);
        u.dashCooldown = std::max(0., u.dashCooldown - dt);
        u.shardDelay = std::max(0., u.shardDelay - dt);
        u.guard = std::max(0., u.guard - dt);
        if (u.bait > 0)
            u.bait = u.alive ? std::max(0., u.bait - dt) : 0;
        if (u.alive && u.bait > 0)
            baits.push_back(u.id);
    }
    if (navigationVersion != maze.version)
    {
        for (auto& u : units)
        {
            u.path.clear();
            u.think = std::min(u.think, (u.id % 20) * .015);
        }
        navigationVersion = maze.version;
    }
    if (defenseMode())
        updateDefense(dt);
    else if (hubMode())
        updateHub(dt);
    if (finished)
        return;
    updateEvolution(dt);
    updateExpedition(dt);
    // Ink into wounds (R) before anyone walks, so the mend's slow holds this very step.
    updateMend(dt);
    flushUnits();
    moveUnits(dt);
    flushUnits();
    updateWeapons(dt);
    flushUnits();
    updateProjectiles(dt);
    flushUnits();
    // Weapon crates fall anywhere in Survival; in Defense only while a wave runs, onto the platform where the
    // players walk (or near the core on a page without one).
    if (elapsed >= nextCrate && defenseMode() && defense.phase != DefensePhase::Wave)
        nextCrate = elapsed + 15;
    if (elapsed >= nextCrate)
    {
        nextCrate += config.airDropInterval;
        Vec2 p = defenseMode() ? maze.randomOpen(rng, defense.core, 1600) : maze.randomOpen(rng);
        if (defenseMode() && maze.hasDeck())
        {
            std::vector<int> deckCells;
            for (int id = 0; id < static_cast<int>(maze.decks.size()); ++id)
                if (maze.decks[id])
                    deckCells.push_back(id);
            p = maze.center(deckCells[static_cast<size_t>(randomStep(rng) % deckCells.size())]);
        }
        crates.push_back({nextId++, 1 + static_cast<int>(randomStep(rng) % 5), p, 2, 35, true});
    }
    for (auto& c : crates)
        if (c.active)
        {
            c.dropTime = std::max(0., c.dropTime - dt);
            c.remaining -= dt;
            if (c.remaining <= 0)
                c.active = false;
            if (c.active && c.dropTime <= 0)
                for (auto& u : units)
                    if (u.alive && u.player() && (u.position - c.position).length() < 65 * perk(u, Attribute::Reach) &&
                        pickupWeapon(u.id, c.weapon))
                    {
                        c.active = false;
                        event(EffectStyle::PickupWeapon, c.position, u.position, u.team, c.weapon);
                        break;
                    }
        }
    for (auto& e : effects)
        e.remaining -= dt;
    for (auto& n : damageNumbers)
        n.remaining -= dt;
    damageNumbers.erase(
        std::remove_if(damageNumbers.begin(), damageNumbers.end(), [](const auto& n) { return n.remaining <= 0; }),
        damageNumbers.end());
    effects.erase(std::remove_if(effects.begin(), effects.end(), [](auto& e) { return e.remaining <= 0; }),
                  effects.end());
    projectiles.erase(std::remove_if(projectiles.begin(), projectiles.end(), [](auto& p) { return p.remaining <= 0; }),
                      projectiles.end());
    crates.erase(std::remove_if(crates.begin(), crates.end(), [](auto& c) { return !c.active; }), crates.end());
    // Defense ends on the core (lost) or the last wave (won), both decided in updateDefense; the hub never ends.
    if (!survivalMode())
        return;
    if (elapsed + 1e-8 >= config.duration)
    {
        finished = true;
        winner = 1;
    }
    // The run is lost once every human has fallen; AI teammates alone cannot carry it.
    if (humanCount() > 0)
    {
        bool living = false;
        for (auto& u : units)
            if (u.human && u.alive)
                living = true;
        if (!living)
        {
            finished = true;
            winner = 2;
        }
    }
}
} // namespace gwgame
