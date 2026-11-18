#pragma once
namespace gwgame
{
inline void Battle::fire(Unit& u)
{
    const bool onRun = expeditionMode() && u.kind == UnitKind::Fighter;
    // The guns in hand (2026-11-17: a long gun, or a pistol in either hand or both). Each fires with its own numbers,
    // trick and nib (Expedition.h gunVariant); two pistols fire side by side on every pull, a little wilder (design 4.2).
    struct HandGun
    {
        int variant = -1, nib = -1;
    };
    HandGun guns[2];
    int count = 1;
    if (onRun)
    {
        const HotSlot* hand[2];
        const int held = handGuns(u, hand);
        for (int g = 0; g < held; ++g)
            guns[g] = {hand[g]->variant >= 0 && validVariant(u.weapon, hand[g]->variant) ? hand[g]->variant : -1, hand[g]->nib};
        count = std::max(1, held);
    }
    const bool pair = count == 2;
    const bool ballpoint = onRun && u.kit.trait == static_cast<int>(PenBiome::Ballpoint);
    double interval = 0;
    for (int g = 0; g < count; ++g)
        interval = std::max(interval, gunStats(u.weapon, guns[g].variant).interval);
    u.cooldown = interval / (ballpoint ? 1.15 : 1);
    u.ammo = std::max(0, u.ammo - count);
    Vec2 direction = u.aim.normalized();
    double angle = std::atan2(direction.y, direction.x);
    double scale = u.kind == UnitKind::Companion ? sideDamage(u.sides) : perk(u, Attribute::Damage);
    if (onRun)
        scale *= lookPerks(u.kit).damage;
    // A crayon gun's rounds take turns: red, blue, yellow, green (the round's colour is its trick).
    const int turn = static_cast<int>(u.kit.shots % 4);
    auto shoot = [&](const HandGun& gun, double heading, Vec2 offset, double share)
    {
        if (projectiles.size() >= 768)
            return;
        const int variant = gun.variant, nib = gun.nib;
        const GunVariant& quality = gunVariant(variant);
        const auto stats = gunStats(u.weapon, variant);
        const int prism = quality.trick == GunTrick::Prism ? turn : -1;
        const Vec2 d{std::cos(heading), std::sin(heading)};
        Projectile p;
        p.id = nextProjectile++;
        p.owner = u.id;
        p.squad = squadHead(u);
        p.team = u.team;
        p.weapon = u.weapon;
        p.position = u.position + d * (u.radius + 9) + offset;
        p.deck = p.fromDeck = u.deck;
        p.velocity = d * stats.speed;
        p.damage = stats.damage * (1 + u.skills[2] * .15 + u.skills[8] * .1) * scale * share;
        p.remaining = stats.range / stats.speed;
        p.radius = 4;
        p.pierce = u.skills[0] + (u.weapon == 3 ? 1 : 0) + (onRun && u.kit.trait == static_cast<int>(PenBiome::Fountain) ? 1 : 0) +
                   (nib == static_cast<int>(PenBiome::Fountain) ? 1 : 0) +
                   (quality.trick == GunTrick::Pierce ? static_cast<int>(quality.power) : 0);
        p.nib = nib;
        if (nib == static_cast<int>(PenBiome::Brush))
            p.damage *= 1.1;
        if (variant >= 0)
        {
            p.variant = variant;
            p.trick = static_cast<int>(quality.trick);
            p.prism = prism;
            p.tint = prism >= 0 ? 8 + prism : quality.tint;
            if (quality.trick == GunTrick::Bounce)
                p.ricochet = static_cast<int>(quality.power);
            if (prism == 3)
                ++p.pierce;
        }
        projectiles.push_back(p);
    };
    const Vec2 side{-direction.y, direction.x};
    for (int g = 0; g < count; ++g)
    {
        const int pellets = gunStats(u.weapon, guns[g].variant).pellets;
        for (int pellet = 0; pellet < pellets; ++pellet)
        {
            double spread = pellets > 1 ? (pellet - (pellets - 1) * .5) * .075 : 0;
            if (pair)
                spread += (random01(rng) * 2 - 1) * DualSpread;
            shoot(guns[g], angle + spread, pair ? side * (g ? 11. : -11.) : Vec2{}, 1);
        }
    }
    if (!onRun)
        return;
    // Copy paper: every fifth shot a half-strength blue copy flies 8 degrees off; a ballpoint nib adds a copy every
    // fourth; the ballpoint's body sends a copy after every eighth at the nearest enemy in sight.
    ++u.kit.shots;
    if (carries(u, Gear::CarbonPaper) && u.kit.shots % 5 == 0)
        shoot(guns[0], angle + 8 * Pi / 180, {}, .5);
    for (int g = 0; g < count; ++g)
        if (guns[g].nib == static_cast<int>(PenBiome::Ballpoint) && u.kit.shots % 4 == 0)
        {
            shoot(guns[g], angle - 6 * Pi / 180, {}, .6);
            break;
        }
    if (ballpoint && u.kit.shots % 8 == 0)
    {
        const Unit* target = nullptr;
        double best = 900 * 900;
        for (const auto& v : units)
            if (enemy(v) && (v.position - u.position).lengthSquared() < best && maze.clear(u.position, v.position, 4))
            {
                best = (v.position - u.position).lengthSquared();
                target = &v;
            }
        const Vec2 to = target ? (target->position - u.position).normalized() : direction;
        shoot(guns[0], std::atan2(to.y, to.x), {}, .6);
    }
}
// The rocket launcher (design: 火箭筒): holding the trigger while it is loaded charges the shot for up to
// RocketChargeSeconds; letting go lobs a shell along the aim, as far as the charge says, over every wall. A gap
// in the player's command stream (fresh false) neither charges nor releases.
inline void Battle::rocketTrigger(Unit& u, double dt, bool fresh)
{
    const bool ready = u.cooldown <= 0 && u.reload <= 0 && u.ammo > 0;
    if (!ready)
    {
        u.rocketHeld = false;
        u.rocketCharge = 0;
        return;
    }
    if (!fresh)
        return;
    if (u.input.fire)
    {
        u.rocketHeld = true;
        u.rocketCharge = std::min(RocketChargeSeconds, u.rocketCharge + dt);
        return;
    }
    if (!u.rocketHeld)
        return;
    lob(u, u.rocketCharge / RocketChargeSeconds);
    u.rocketHeld = false;
    u.rocketCharge = 0;
}
inline void Battle::lob(Unit& u, double power)
{
    const bool onRun = expeditionMode() && u.kind == UnitKind::Fighter;
    const int variant = onRun ? handVariant(u) : -1;
    const auto stats = gunStats(RocketWeapon, variant);
    u.cooldown = stats.interval;
    --u.ammo;
    if (projectiles.size() >= 768)
        return;
    power = std::clamp(power, 0., 1.);
    const Vec2 target = rocketLanding(u.position, u.aim, power, maze.sizeX(), maze.sizeY());
    const double flight = rocketFlight(power);
    Projectile p;
    p.id = nextProjectile++;
    p.owner = u.id;
    p.squad = squadHead(u);
    p.team = u.team;
    p.weapon = LobWeapon;
    p.high = true;
    p.position = u.position;
    p.velocity = (target - u.position) * (1 / flight);
    p.remaining = flight;
    p.radius = 12;
    p.damage = stats.damage * (1 + u.skills[2] * .15 + u.skills[8] * .1) *
               (u.kind == UnitKind::Companion ? sideDamage(u.sides) : perk(u, Attribute::Damage));
    if (onRun)
        p.damage *= lookPerks(u.kit).damage;
    if (variant >= 0)
    {
        p.variant = variant;
        p.trick = static_cast<int>(gunVariant(variant).trick);
        p.tint = gunVariant(variant).tint;
    }
    projectiles.push_back(p);
    event(EffectStyle::RocketLob, u.position, target, u.team, power, .3);
}
// A shell bursts on whatever level it lands on: enemies take the full blast (half at its edge); the shooter's own
// team takes a share of it by the trolling level and is thrown about, which counts as a shove (design: 火箭筒
// 一不小心会炸到自己人).
inline void Battle::rocketBlast(int owner, int team, Vec2 at, double damage, double reach, double burn)
{
    // A wax-seal shell (Expedition.h GunTrick::Burn) reaches farther and leaves what it catches burning.
    const double span = std::max(1., reach);
    Effect ring{at, at, span, .45, 1, team};
    ring.style = EffectStyle::Rocket;
    addEffect(ring);
    const double share = rocketFriendlyShare(config.prank), push = prankPush(config.prank);
    Unit* shooter = find(owner);
    const bool shooterPlayer = shooter && shooter->player();
    for (auto& v : units)
    {
        if (!v.alive)
            continue;
        const double d = (v.position - at).length();
        if (d >= span + v.radius)
            continue;
        const double falloff = 1 - .5 * std::clamp(d / span, 0., 1.);
        const Vec2 away = d > 1e-6 ? (v.position - at) * (1 / d) : Vec2{1, 0};
        if (v.team != team)
        {
            damageUnit(owner, v.id, damage * falloff);
            if (!v.turret && !v.dummy && v.alive)
                v.impulse += away * 420;
            if (burn > 0 && v.alive && v.kind == UnitKind::Monster)
            {
                v.burnTime = std::max(v.burnTime, BurnSeconds);
                v.burnDamage = std::max(v.burnDamage, burn);
                v.burnOwner = owner;
            }
        }
        else if (share > 0 && v.kind == UnitKind::Fighter && v.climb < 0)
        {
            damageUnit(owner, v.id, damage * falloff * share, true);
            if (!v.alive)
                continue;
            v.impulse += away * 420 * push;
            const double cap = MaxFriendlyImpulse * std::max(1., push);
            if (const double length = v.impulse.length(); length > cap)
                v.impulse = v.impulse * (cap / length);
            if (shooterPlayer && v.id != owner && v.player())
                ++shooter->stats.pushes;
        }
    }
    // Survival's maze takes a bite; players never blast a defense page's own drawn walls apart.
    if (!(defenseMode() && team == 1) && !maze.damageDisc(at, 110, 45).empty())
        addEffect({at, at, 140, .8, 6, 0});
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
                u.ammo = clipSize(u);
                u.reload = reloadTime(u);
            }
            if (u.kind == UnitKind::Monster)
            {
                // Training targets only stand there and take it; a climber has its hands full; a stunned enemy too.
                if (u.dummy || u.climb >= 0 || u.stun > 0)
                    continue;
                if (u.radius >= 100 && !expeditionMode())
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
                            const double slam = defenseMode()      ? 90 * (1 + .06 * std::max(0, defense.wave - 1)) * enemyPower()
                                                : expeditionMode() ? 70 * expeditionPower(u)
                                                                   : 100 + elapsed * .08;
                            // The slam shakes the floor it lands on (the platform stands above the box's).
                            blast(u.id, 2, u.castTarget, 320, slam, .45, EffectStyle::BossSlam, u.deck ? 1 : 0);
                            if (defenseMode())
                                maze.damageDisc(u.castTarget, 320, 70);
                        }
                    }
                    else if (u.cooldown <= 0)
                    {
                        // The boss slams a teammate lying as bait first, then the nearest player on its level.
                        const Unit* target = baitFor(u, 900);
                        const bool lured = target != nullptr;
                        double nearest = 1e20;
                        for (auto& other : units)
                            if (!lured && other.player() && other.alive && other.deck == u.deck && other.climb < 0 &&
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
                    // Claws reach only what stands on the same level (a climber is out of reach for a moment).
                    for (auto& target : units)
                        if (target.alive && target.team != u.team && target.deck == u.deck && target.climb < 0 &&
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
            bool fresh = !u.human || u.freshInput;
            if (u.state == UnitState::Following && !u.human)
            {
                const auto* head = find(u.followerOf);
                u.input.fire = head && head->alive && head->input.fire;
                fresh = !head || !head->human || head->freshInput;
                if (head)
                    u.aim = head->aim;
            }
            // The expedition's hotbar decides what the trigger does (a gun, a pair, an item, bare hands).
            if (expeditionMode() && u.kind == UnitKind::Fighter && u.player())
            {
                expeditionHands(u, dt, fresh);
                continue;
            }
            if (u.state == UnitState::Launched || u.climb >= 0)
            {
                u.rocketHeld = false;
                u.rocketCharge = 0;
                continue;
            }
            if (u.weapon == RocketWeapon)
            {
                rocketTrigger(u, dt, fresh);
                continue;
            }
            if (u.input.fire && u.cooldown <= 0 && u.reload <= 0 && u.ammo > 0)
                fire(u);
        }
}
inline void Battle::blast(int owner, int team, Vec2 position, double radius, double damage, double lifetime,
                          EffectStyle style, int layer)
{
    Effect ring{position, position, radius, lifetime, 1, team};
    ring.style = style;
    addEffect(ring);
    for (auto& u : units)
        if (u.alive && u.team != team && (layer < 0 || u.deck == (layer == 1)) && (u.position - position).length() < radius + u.radius)
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
        // A lobbed shell flies over everything and bursts where it comes down (a mortar's on enemies only, in the box).
        if (p.weapon == LobWeapon || p.weapon == MortarWeapon)
        {
            p.position += p.velocity * std::min(dt, p.remaining);
            p.remaining -= dt;
            if (p.remaining <= 0)
            {
                if (p.weapon == LobWeapon)
                {
                    const bool wax = p.trick == static_cast<int>(GunTrick::Burn);
                    rocketBlast(p.owner, p.team, p.position, p.damage, RocketBlast * (wax ? BurnReach : 1), wax ? gunVariant(p.variant).power : 0);
                }
                else
                    blast(p.owner, p.team, p.position, MortarBlast, p.damage, .4, EffectStyle::Rocket, 0);
            }
            continue;
        }
        if (p.weapon == FoeLob)
        {
            p.position += p.velocity * std::min(dt, p.remaining);
            p.remaining -= dt;
            if (p.remaining <= 0)
                foeLobLanded(p);
            continue;
        }
        Vec2 from = p.position, to = from + p.velocity * dt;
        p.remaining -= dt;
        auto* source = find(p.owner);
        bool stopped = false;
        double fraction = 1;
        Vec2 normal{};
        int wallCell = -1;
        // A shot from the platform flies over it, then drops into the box, where the platform's wall stops it like rock
        // (and a shot fired in the box never gets up there); a tower's bolts fly over every wall.
        if (p.deck && !maze.touchesDeck(from, p.radius))
            p.deck = false;
        if (!p.high && maze.raycast(from, to, p.radius, fraction, normal, &wallCell, p.deck))
        {
            to = from + (to - from) * fraction;
            stopped = true;
            // A highlighter's glossy wall sends any shot (either side's) back off it, twice at most.
            int ricochets = std::max({source ? source->skills[1] : 0, maze.kind(wallCell) == WallKind::Bounce ? BounceRicochets : 0, p.ricochet});
            if (expeditionMode() && expedition.floorPrank == static_cast<int>(FloorPrank::Bouncy))
                ricochets = std::max(ricochets, 2);
            // A dashed pencil wall (the pencil biome's pillars) takes every player shot off its strength and breaks whole.
            if (expeditionMode() && p.team == 1 && wallCell >= 0 && !maze.isFixed(wallCell) && maze.walls[static_cast<size_t>(wallCell)] > 0)
            {
                double& strength = maze.walls[static_cast<size_t>(wallCell)];
                strength = std::max(0., strength - p.damage);
                if (strength <= 0)
                {
                    maze.setWall(wallCell, false);
                    event(EffectStyle::WallBreak, maze.center(wallCell), maze.center(wallCell), p.team, 0, .5);
                }
            }
            if (ricochets > p.bounces)
            {
                p.velocity = p.velocity - normal * (2 * p.velocity.dot(normal));
                to += p.velocity.normalized() * 2;
                ++p.bounces;
                stopped = false;
            }
        }
        // An enemy shot that meets a tower's post hits the tower.
        if (stopped && defenseMode() && p.team == 2 && wallCell >= 0)
            if (const int tower = towerAt(wallCell); tower >= 0)
                damageUnit(p.owner, tower, p.damage);
        if (expeditionMode() && p.team == 1)
            popSacs(p, from, to);
        if (defenseMode() && p.team == 2 && distanceToSegment(defense.core, from, to) < DefenseState::CoreRadius + p.radius)
        {
            hurtCore(p.damage, to);
            p.position = to;
            p.remaining = 0;
            continue;
        }
        for (auto& target : units)
        {
            if (!target.alive || target.climb >= 0 || (target.deck && !p.deck && !p.high) ||
                distanceToSegment(target.position, from, to) >= target.radius + p.radius ||
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
            // The plastic ruler held up in front turns an enemy shot back the way it came (design item 16).
            if (expeditionMode() && target.kind == UnitKind::Fighter && target.player())
            {
                const HotSlot& held = target.kit.slots[static_cast<size_t>(std::clamp(target.kit.selected, 0, HotbarSlots - 1))];
                if (held.kind == SlotKind::Item && held.what == static_cast<int>(Gear::Ruler) &&
                    target.aim.normalized().dot((p.velocity * -1.).normalized()) > std::cos(RulerArc * .5))
                {
                    p.velocity = p.velocity * -1.;
                    p.team = target.team;
                    p.owner = p.squad = target.id;
                    p.hit.clear();
                    p.hit.push_back(target.id);
                    event(EffectStyle::Shield, to, to, target.team, 70, .3);
                    to = from;
                    break;
                }
            }
            if (expeditionMode() && p.team == 1 && target.kind == UnitKind::Monster)
                if (const int deflected = foeDeflect(p, target, to))
                {
                    if (deflected == 1)
                        stopped = true;
                    else
                        to = from;
                    break;
                }
            p.hit.push_back(target.id);
            damageUnit(p.owner, target.id, p.damage);
            if (expeditionMode())
                expeditionShotHit(p, target);
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
