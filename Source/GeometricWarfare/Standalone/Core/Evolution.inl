#pragma once
namespace gwgame
{
inline void Battle::makeChoices(Unit& u)
{
    uint64_t r = rng ^ static_cast<uint64_t>(u.id * 31337 + u.level * 17);
    if (!r)
        r = 1;
    std::vector<int> available;
    for (int i = 0; i < 20; ++i)
    {
        const int core = skillPrerequisite(i);
        if (u.skills[i] < 5 && (core < 0 || u.skills[core] > 0))
            available.push_back(i);
    }
    for (size_t i = available.size(); i > 1; --i)
        std::swap(available[i - 1], available[randomStep(r) % i]);
    // Never repeat a card: once fewer than three skills remain, the spare slots offer recovery.
    for (int i = 0; i < 3; ++i)
        u.choices[i] = i < static_cast<int>(available.size()) ? available[i] : -1;
}
inline bool Battle::chooseUpgrade(int id, int choice)
{
    auto* u = find(id);
    if (!u || !u->player() || !u->alive || u->pendingUpgrades <= 0 || choice < 0 || choice >= 3 || finished)
        return false;
    int skill = u->choices[choice];
    bool capped = skill < 0 || u->skills[skill] >= 5;
    if (!capped)
        ++u->skills[skill];
    else
    {
        u->hp = std::min(u->maxHp, u->hp + u->maxHp * .25);
        u->armor += 50;
    }
    --u->pendingUpgrades;
    event(EffectStyle::Upgrade, u->position, u->position, u->team, capped ? -1 : skill, .6);
    if (skill == 16 && !capped)
        u->armor += 70;
    if (skill == 17 && !capped)
    {
        u->maxHp += 35;
        u->hp = std::min(u->maxHp, u->hp + 70);
    }
    makeChoices(*u);
    return true;
}
inline void Battle::addExperience(double amount)
{
    if (!std::isfinite(amount) || amount <= 0)
        return;
    sharedXP += amount;
    while (sharedXP >= sharedLevel * 100)
    {
        sharedXP -= sharedLevel * 100;
        ++sharedLevel;
        for (auto& u : units)
            if (u.player() && u.alive)
            {
                u.input = {};
                u.moveQueue.clear();
                cancelCharge(u.id);
                ++u.pendingUpgrades;
                u.level = sharedLevel;
                makeChoices(u);
                // Humans pause the world to choose; AI teammates decide on the spot.
                if (u.bot)
                    botChoose(u);
            }
    }
}
inline void Battle::spawnMonster(bool bossEncounter)
{
    int active = 0;
    for (auto& u : units)
        if (u.alive && u.kind == UnitKind::Monster)
            ++active;
    if (active >= config.maxMonsters && !bossEncounter)
        return;
    const Unit* p = nullptr;
    for (auto& u : units)
        if (u.player() && u.alive)
        {
            if (!p || random01(rng) < .3)
                p = &u;
        }
    if (!p)
        return;
    Unit monster;
    monster.id = nextId++;
    monster.team = 2;
    monster.kind = UnitKind::Monster;
    monster.shape = static_cast<Shape>(randomStep(rng) % 4);
    double angle = random01(rng) * 2 * Pi;
    Vec2 preferred = p->position + Vec2{std::cos(angle), std::sin(angle)} * 1100;
    monster.position = maze.walkable(preferred, 28) ? preferred : maze.randomOpen(rng, p->position, 1800);
    monster.hp = monster.maxHp = 75 + elapsed * .8;
    monster.radius = 27;
    monster.think = .05 * (monster.id % 12);
    if (bossEncounter)
    {
        monster.hp = monster.maxHp = 6000 + elapsed * 15;
        monster.radius = 110;
        monster.shape = Shape::Rectangle;
        monster.cooldown = 3;
    }
    else if (monster.id % 23 == 0)
    {
        monster.hp = monster.maxHp = monster.hp * 8;
        monster.radius = 55;
        monster.shape = Shape::Rectangle;
    }
    event(EffectStyle::SpawnEnemy, monster.position, monster.position, 2, monster.radius, .5);
    // Reuse dead monster slots after all projectiles from the last frame have expired ownership checks.
    for (auto& u : units)
        if (!u.alive && u.kind == UnitKind::Monster)
        {
            u = monster;
            return;
        }
    if (units.size() < 500)
        units.push_back(monster);
}
inline void Battle::updateEvolution(double dt)
{
    // Survival spawns allies, monster streams and elite encounters by the clock; Defense spawns by wave.
    if (survivalMode() && elapsed >= nextRecruit)
    {
        nextRecruit += config.recruitInterval;
        int idle = 0;
        for (auto& u : units)
            if (u.alive && u.kind == UnitKind::Companion && u.followerOf < 0)
                ++idle;
        if (idle < config.maxIdleCompanions && units.size() < 500 && humanCount() > 0)
        {
            Unit a;
            a.id = nextId++;
            a.kind = UnitKind::Companion;
            a.state = UnitState::Idle;
            a.sides = MinSides;
            a.shape = Shape::Triangle;
            a.position = maze.randomOpen(rng, {maze.sizeX() * .5, maze.sizeY() * .5}, 2500);
            a.maxHp = a.hp = 220;
            units.push_back(a);
            event(EffectStyle::SpawnAlly, a.position, a.position, 1, a.radius, .5);
        }
    }
    if (!survivalMode())
        nextWave = nextBoss = 1e9;
    if (config.enemiesEnabled && elapsed >= nextWave)
    {
        nextWave += std::max(.12, .8 - elapsed * .001);
        spawnMonster();
        if (elapsed > 120)
            spawnMonster();
    }
    if (config.enemiesEnabled && config.bossEnabled && elapsed >= nextBoss && humanCount() > 0)
    {
        spawnMonster(true);
        ++bossWave;
        nextBoss = bossWave < static_cast<int>(BossSchedule.size()) ? BossSchedule[bossWave] : 1e9;
    }
    for (auto& u : units)
        if (u.alive)
        {
            if (u.burnTime > 0)
            {
                u.burnTime -= dt;
                damageUnit(u.burnOwner, u.id, u.burnDamage * dt);
                if (!u.alive)
                {
                    auto* source = find(u.burnOwner);
                    if (source && source->skills[10] > 0)
                        blast(source->id, source->team, u.position, 180, u.burnDamage * 6 * source->skills[10], .35,
                              EffectStyle::Combustion);
                    if (source && source->skills[9] > 0)
                    {
                        int sparks = 0;
                        for (auto& other : units)
                            if (other.alive && other.team != source->team &&
                                (other.position - u.position).length() < 200)
                            {
                                other.burnTime = std::max(other.burnTime, 2.);
                                other.burnDamage = u.burnDamage;
                                other.burnOwner = source->id;
                                if (sparks++ < 6)
                                {
                                    Effect spread{u.position, other.position, 0, .3, 2, source->team};
                                    spread.style = EffectStyle::Spread;
                                    addEffect(spread);
                                }
                            }
                    }
                }
            }
            if (!u.alive)
                continue;
            if (u.player())
            {
                u.emergencyTime = std::max(0., u.emergencyTime - dt);
                if (u.skills[17] > 0)
                    u.hp = std::min(u.maxHp, u.hp + u.skills[17] * dt * .8);
                u.armor = std::min(100. + u.skills[16] * 40, u.armor + u.skills[18] * dt * 1.5);
                u.skillTime += dt;
                if (u.skillTime >= 2)
                {
                    u.skillTime = 0;
                    if (u.skills[6] > 0)
                    {
                        event(EffectStyle::Voltage, u.position, u.position, u.team, 210, .35);
                        for (auto& enemy : units)
                            if (enemy.alive && enemy.team != u.team && (enemy.position - u.position).length() < 210)
                            {
                                damageUnit(u.id, enemy.id, 8 * u.skills[6]);
                                Effect zap{u.position, enemy.position, 0, .2, 2, 1};
                                zap.style = EffectStyle::Voltage;
                                addEffect(zap);
                            }
                    }
                    if (u.hp < u.maxHp * .25 && u.skills[19] > 0 && u.emergencyTime <= 0)
                    {
                        u.armor += 40 * u.skills[19];
                        u.hp = std::min(u.maxHp, u.hp + u.maxHp * .15);
                        u.emergencyTime = 30;
                        event(EffectStyle::Emergency, u.position, u.position, u.team, 0, .6);
                    }
                }
            }
        }
}
} // namespace gwgame
